#pragma once

#include "me/order.hpp"
#include "me/pool.hpp"
#include "me/price_level.hpp"
#include "me/price_map.hpp"
#include "me/robin_hood.hpp"
#include "me/spsc_queue.hpp"
#include "me/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace me {

struct BookConfig {
    size_t order_capacity = kDefaultOrderCapacity;
    size_t level_capacity = kDefaultLevelCapacity;
    size_t index_capacity = kDefaultIndexCapacity;
    size_t event_capacity = kDefaultEventCapacity;
    size_t price_map_capacity = 1 << 15; // 32K price levels / side
    bool enable_events = true;
};

struct InsertResult {
    bool ok{false};
    RejectReason reason{RejectReason::None};
    OrderStatus status{OrderStatus::Rejected};
    uint32_t filled_qty{0};
};

struct CancelResult {
    bool ok{false};
    RejectReason reason{RejectReason::None};
    uint32_t remaining_qty{0};
};

// Single-threaded-per-symbol matching engine.
// Zero allocations / zero locks on the hot path after construction.
class OrderBook {
public:
    explicit OrderBook(BookConfig cfg = {});
    ~OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    InsertResult insert(uint64_t id, Side side, OrderType type, uint64_t price,
                        uint32_t quantity) noexcept;

    CancelResult cancel(uint64_t id) noexcept;

    [[nodiscard]] Order* find(uint64_t id) const noexcept { return index_.find(id); }

    [[nodiscard]] uint64_t best_bid() const noexcept {
        return bid_head_ ? bid_head_->price : 0;
    }
    [[nodiscard]] uint64_t best_ask() const noexcept {
        return ask_head_ ? ask_head_->price : 0;
    }
    [[nodiscard]] bool has_best_bid() const noexcept { return bid_head_ != nullptr; }
    [[nodiscard]] bool has_best_ask() const noexcept { return ask_head_ != nullptr; }

    [[nodiscard]] uint32_t level_qty(Side side, uint64_t price) const noexcept;
    [[nodiscard]] uint32_t level_count(Side side, uint64_t price) const noexcept;

    [[nodiscard]] uint32_t orders_live() const noexcept { return orders_.live(); }
    [[nodiscard]] uint32_t levels_live() const noexcept { return levels_.live(); }
    [[nodiscard]] size_t index_size() const noexcept { return index_.size(); }
    [[nodiscard]] uint64_t fill_count() const noexcept { return fill_count_; }
    [[nodiscard]] uint64_t event_seq() const noexcept { return event_seq_; }

    size_t poll_events(EngineEvent* out, size_t max) noexcept;

    void set_events_enabled(bool on) noexcept { events_enabled_ = on; }
    void clear() noexcept;

private:
    uint32_t match(Order* incoming) noexcept;
    void rest_order(Order* o) noexcept;
    void remove_order_from_book(Order* o) noexcept;
    void destroy_order(Order* o) noexcept;

    [[nodiscard]] PriceLevel* lookup_level(Side side, uint64_t price) const noexcept;
    void store_level(Side side, uint64_t price, PriceLevel* lvl) noexcept;
    void erase_level_key(Side side, uint64_t price) noexcept;

    PriceLevel* get_or_create_level(Side side, uint64_t price) noexcept;
    void unlink_empty_level(Side side, PriceLevel* lvl) noexcept;
    void append_to_level(PriceLevel* lvl, Order* o) noexcept;
    void detach_from_level(Order* o) noexcept;

    void register_level(Side side, PriceLevel* lvl) noexcept;
    void unregister_level(Side side, PriceLevel* lvl) noexcept;
    void recompute_best_bid() noexcept;
    void recompute_best_ask() noexcept;

    void emit(EventType type, RejectReason reason, uint64_t order_id, uint64_t match_id,
              uint64_t price, uint32_t qty, Side side) noexcept;

    OrderPool orders_;
    LevelPool levels_;
    RobinHoodIndex index_;
    SpscQueue<EngineEvent> events_;

    // Dense open-addressing price → level
    PriceMap bid_map_;
    PriceMap ask_map_;

    // Active levels for rare best-recompute (only when best level empties)
    std::vector<PriceLevel*> bid_active_;
    std::vector<PriceLevel*> ask_active_;

    // Best price heads (O(1) match path)
    PriceLevel* bid_head_{nullptr};
    PriceLevel* ask_head_{nullptr};

    uint64_t event_seq_{0};
    uint64_t fill_count_{0};
    bool events_enabled_{true};
};

} // namespace me
