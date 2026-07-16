#pragma once

// Reference implementation using STL containers — for correctness comparison only.
// NOT used on the production hot path.

#include "me/types.hpp"

#include <algorithm>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace me {

struct NaiveOrder {
    uint64_t id{0};
    uint64_t price{0};
    uint32_t quantity{0};
    uint32_t filled_qty{0};
    Side side{Side::Bid};
    OrderType type{OrderType::Limit};

    [[nodiscard]] uint32_t remaining() const noexcept { return quantity - filled_qty; }
    [[nodiscard]] bool fully_filled() const noexcept { return filled_qty >= quantity; }
};

struct NaiveFill {
    uint64_t taker_id{0};
    uint64_t maker_id{0};
    uint64_t price{0};
    uint32_t qty{0};
};

class NaiveBook {
public:
    bool insert(uint64_t id, Side side, OrderType type, uint64_t price, uint32_t quantity) {
        if (quantity == 0 || id_index_.count(id)) return false;
        if (type == OrderType::Limit && !valid_price(price)) return false;

        NaiveOrder o;
        o.id = id;
        o.price = (type == OrderType::Market) ? 0 : price;
        o.quantity = quantity;
        o.filled_qty = 0;
        o.side = side;
        o.type = type;

        match(o);

        if (!o.fully_filled() && type == OrderType::Limit) {
            rest(o);
        }
        return true;
    }

    bool cancel(uint64_t id) {
        auto it = id_index_.find(id);
        if (it == id_index_.end()) return false;
        const Loc loc = it->second;
        if (loc.side == Side::Bid) {
            auto pit = bids_.find(loc.price);
            if (pit == bids_.end()) {
                id_index_.erase(it);
                return false;
            }
            pit->second.erase(loc.lit);
            if (pit->second.empty()) bids_.erase(pit);
        } else {
            auto pit = asks_.find(loc.price);
            if (pit == asks_.end()) {
                id_index_.erase(it);
                return false;
            }
            pit->second.erase(loc.lit);
            if (pit->second.empty()) asks_.erase(pit);
        }
        id_index_.erase(it);
        return true;
    }

    [[nodiscard]] uint64_t best_bid() const {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }
    [[nodiscard]] uint64_t best_ask() const {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }

    [[nodiscard]] const std::vector<NaiveFill>& fills() const { return fills_; }
    void clear_fills() { fills_.clear(); }

    void clear() {
        bids_.clear();
        asks_.clear();
        id_index_.clear();
        fills_.clear();
    }

private:
    // Bid: price DESC, Ask: price ASC
    struct BidCmp {
        bool operator()(uint64_t a, uint64_t b) const { return a > b; }
    };
    struct AskCmp {
        bool operator()(uint64_t a, uint64_t b) const { return a < b; }
    };

    using BidBook = std::map<uint64_t, std::list<NaiveOrder>, BidCmp>;
    using AskBook = std::map<uint64_t, std::list<NaiveOrder>, AskCmp>;

    // id → (side, price, list iterator)
    struct Loc {
        Side side;
        uint64_t price;
        std::list<NaiveOrder>::iterator lit;
    };

    void rest(NaiveOrder& o) {
        if (o.side == Side::Bid) {
            auto& lst = bids_[o.price];
            lst.push_back(o);
            auto lit = std::prev(lst.end());
            id_index_[o.id] = Loc{o.side, o.price, lit};
        } else {
            auto& lst = asks_[o.price];
            lst.push_back(o);
            auto lit = std::prev(lst.end());
            id_index_[o.id] = Loc{o.side, o.price, lit};
        }
    }

    void match(NaiveOrder& incoming) {
        if (incoming.side == Side::Bid) {
            while (incoming.remaining() > 0 && !asks_.empty()) {
                auto pit = asks_.begin();
                if (incoming.type == OrderType::Limit && incoming.price < pit->first) break;
                auto& lst = pit->second;
                auto& passive = lst.front();
                const uint32_t qty = std::min(incoming.remaining(), passive.remaining());
                const uint64_t px = passive.price;
                incoming.filled_qty += qty;
                passive.filled_qty += qty;
                fills_.push_back({incoming.id, passive.id, px, qty});
                if (passive.fully_filled()) {
                    id_index_.erase(passive.id);
                    lst.pop_front();
                    if (lst.empty()) asks_.erase(pit);
                }
            }
        } else {
            while (incoming.remaining() > 0 && !bids_.empty()) {
                auto pit = bids_.begin();
                if (incoming.type == OrderType::Limit && incoming.price > pit->first) break;
                auto& lst = pit->second;
                auto& passive = lst.front();
                const uint32_t qty = std::min(incoming.remaining(), passive.remaining());
                const uint64_t px = passive.price;
                incoming.filled_qty += qty;
                passive.filled_qty += qty;
                fills_.push_back({incoming.id, passive.id, px, qty});
                if (passive.fully_filled()) {
                    id_index_.erase(passive.id);
                    lst.pop_front();
                    if (lst.empty()) bids_.erase(pit);
                }
            }
        }
    }

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<uint64_t, Loc> id_index_;
    std::vector<NaiveFill> fills_;
};

// Textbook baseline: map of price → vector, cancel scans the level (O(n)).
// Typical first-pass interview solution — allocates and scans on cancel.
class TextbookBook {
public:
    bool insert(uint64_t id, Side side, OrderType type, uint64_t price, uint32_t quantity) {
        if (quantity == 0 || index_.count(id)) return false;
        if (type == OrderType::Limit && !valid_price(price)) return false;

        NaiveOrder o;
        o.id = id;
        o.price = (type == OrderType::Market) ? 0 : price;
        o.quantity = quantity;
        o.filled_qty = 0;
        o.side = side;
        o.type = type;

        match(o);
        if (!o.fully_filled() && type == OrderType::Limit) {
            if (side == Side::Bid) {
                bids_[o.price].push_back(o);
            } else {
                asks_[o.price].push_back(o);
            }
            index_[o.id] = {side, o.price};
        }
        return true;
    }

    bool cancel(uint64_t id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        const Side side = it->second.first;
        const uint64_t price = it->second.second;
        if (side == Side::Bid) {
            auto pit = bids_.find(price);
            if (pit == bids_.end()) {
                index_.erase(it);
                return false;
            }
            auto& vec = pit->second;
            for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
                if (vit->id == id) {
                    vec.erase(vit); // O(n) element shift
                    if (vec.empty()) bids_.erase(pit);
                    index_.erase(it);
                    return true;
                }
            }
        } else {
            auto pit = asks_.find(price);
            if (pit == asks_.end()) {
                index_.erase(it);
                return false;
            }
            auto& vec = pit->second;
            for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
                if (vit->id == id) {
                    vec.erase(vit);
                    if (vec.empty()) asks_.erase(pit);
                    index_.erase(it);
                    return true;
                }
            }
        }
        index_.erase(it);
        return false;
    }

    [[nodiscard]] uint64_t best_bid() const {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }
    [[nodiscard]] uint64_t best_ask() const {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }

private:
    struct BidCmp {
        bool operator()(uint64_t a, uint64_t b) const { return a > b; }
    };
    struct AskCmp {
        bool operator()(uint64_t a, uint64_t b) const { return a < b; }
    };

    void match(NaiveOrder& incoming) {
        if (incoming.side == Side::Bid) {
            while (incoming.remaining() > 0 && !asks_.empty()) {
                auto pit = asks_.begin();
                if (incoming.type == OrderType::Limit && incoming.price < pit->first) break;
                auto& vec = pit->second;
                if (vec.empty()) {
                    asks_.erase(pit);
                    continue;
                }
                auto& passive = vec.front();
                const uint32_t qty = std::min(incoming.remaining(), passive.remaining());
                incoming.filled_qty += qty;
                passive.filled_qty += qty;
                if (passive.fully_filled()) {
                    index_.erase(passive.id);
                    vec.erase(vec.begin());
                    if (vec.empty()) asks_.erase(pit);
                }
            }
        } else {
            while (incoming.remaining() > 0 && !bids_.empty()) {
                auto pit = bids_.begin();
                if (incoming.type == OrderType::Limit && incoming.price > pit->first) break;
                auto& vec = pit->second;
                if (vec.empty()) {
                    bids_.erase(pit);
                    continue;
                }
                auto& passive = vec.front();
                const uint32_t qty = std::min(incoming.remaining(), passive.remaining());
                incoming.filled_qty += qty;
                passive.filled_qty += qty;
                if (passive.fully_filled()) {
                    index_.erase(passive.id);
                    vec.erase(vec.begin());
                    if (vec.empty()) bids_.erase(pit);
                }
            }
        }
    }

    std::map<uint64_t, std::vector<NaiveOrder>, BidCmp> bids_;
    std::map<uint64_t, std::vector<NaiveOrder>, AskCmp> asks_;
    std::unordered_map<uint64_t, std::pair<Side, uint64_t>> index_;
};

} // namespace me
