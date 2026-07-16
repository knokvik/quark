#include "me/order_book.hpp"

#include <algorithm>

namespace me {

OrderBook::OrderBook(BookConfig cfg)
    : orders_(cfg.order_capacity)
    , levels_(cfg.level_capacity)
    , index_(cfg.index_capacity)
    , events_(cfg.event_capacity)
    , bid_map_(cfg.price_map_capacity)
    , ask_map_(cfg.price_map_capacity)
    , events_enabled_(cfg.enable_events) {
    bid_active_.reserve(cfg.level_capacity);
    ask_active_.reserve(cfg.level_capacity);
}

void OrderBook::emit(EventType type, RejectReason reason, uint64_t order_id, uint64_t match_id,
                     uint64_t price, uint32_t qty, Side side) noexcept {
    if (!events_enabled_) return;
    EngineEvent ev{};
    ev.type = type;
    ev.reason = reason;
    ev.side = side;
    ev.quantity = qty;
    ev.order_id = order_id;
    ev.match_id = match_id;
    ev.price = price;
    ev.seq = ++event_seq_;
    (void)events_.try_push(ev);
}

size_t OrderBook::poll_events(EngineEvent* out, size_t max) noexcept {
    size_t n = 0;
    while (n < max && events_.try_pop(out[n])) {
        ++n;
    }
    return n;
}

PriceLevel* OrderBook::lookup_level(Side side, uint64_t price) const noexcept {
    const PriceMap& map = (side == Side::Bid) ? bid_map_ : ask_map_;
    return map.find(price);
}

void OrderBook::store_level(Side side, uint64_t price, PriceLevel* lvl) noexcept {
    PriceMap& map = (side == Side::Bid) ? bid_map_ : ask_map_;
    map.insert(price, lvl);
}

void OrderBook::erase_level_key(Side side, uint64_t price) noexcept {
    PriceMap& map = (side == Side::Bid) ? bid_map_ : ask_map_;
    map.erase(price);
}

uint32_t OrderBook::level_qty(Side side, uint64_t price) const noexcept {
    if (!valid_price(price)) return 0;
    PriceLevel* lvl = lookup_level(side, price);
    return lvl ? lvl->total_qty : 0;
}

uint32_t OrderBook::level_count(Side side, uint64_t price) const noexcept {
    if (!valid_price(price)) return 0;
    PriceLevel* lvl = lookup_level(side, price);
    return lvl ? lvl->order_count : 0;
}

void OrderBook::append_to_level(PriceLevel* lvl, Order* o) noexcept {
    o->level = lvl;
    o->prev = lvl->tail;
    o->next = nullptr;
    if (lvl->tail) {
        lvl->tail->next = o;
    } else {
        lvl->head = o;
    }
    lvl->tail = o;
    lvl->total_qty += o->remaining();
    ++lvl->order_count;
}

void OrderBook::detach_from_level(Order* o) noexcept {
    PriceLevel* lvl = o->level;
    if (!lvl) return;

    const uint32_t rem = o->remaining();
    if (o->prev) {
        o->prev->next = o->next;
    } else {
        lvl->head = o->next;
    }
    if (o->next) {
        o->next->prev = o->prev;
    } else {
        lvl->tail = o->prev;
    }
    o->next = o->prev = nullptr;
    o->level = nullptr;

    if (lvl->total_qty >= rem) {
        lvl->total_qty -= rem;
    } else {
        lvl->total_qty = 0;
    }
    if (lvl->order_count > 0) {
        --lvl->order_count;
    }
}

void OrderBook::register_level(Side side, PriceLevel* lvl) noexcept {
    lvl->left = lvl->right = nullptr;
    if (side == Side::Bid) {
        bid_active_.push_back(lvl);
        // stash index in height field for O(1) unregister
        lvl->height = static_cast<uint32_t>(bid_active_.size() - 1);
        if (!bid_head_ || lvl->price > bid_head_->price) bid_head_ = lvl;
    } else {
        ask_active_.push_back(lvl);
        lvl->height = static_cast<uint32_t>(ask_active_.size() - 1);
        if (!ask_head_ || lvl->price < ask_head_->price) ask_head_ = lvl;
    }
}

void OrderBook::unregister_level(Side side, PriceLevel* lvl) noexcept {
    auto& active = (side == Side::Bid) ? bid_active_ : ask_active_;
    const uint32_t idx = lvl->height;
    if (idx < active.size() && active[idx] == lvl) {
        PriceLevel* last = active.back();
        active[idx] = last;
        last->height = idx;
        active.pop_back();
    }
    lvl->height = 0;
    const bool was_best = (side == Side::Bid) ? (lvl == bid_head_) : (lvl == ask_head_);
    if (was_best) {
        if (side == Side::Bid) recompute_best_bid();
        else recompute_best_ask();
    }
}

void OrderBook::recompute_best_bid() noexcept {
    bid_head_ = nullptr;
    for (PriceLevel* p : bid_active_) {
        if (!bid_head_ || p->price > bid_head_->price) bid_head_ = p;
    }
}

void OrderBook::recompute_best_ask() noexcept {
    ask_head_ = nullptr;
    for (PriceLevel* p : ask_active_) {
        if (!ask_head_ || p->price < ask_head_->price) ask_head_ = p;
    }
}

PriceLevel* OrderBook::get_or_create_level(Side side, uint64_t price) noexcept {
    if (PriceLevel* existing = lookup_level(side, price)) return existing;

    PriceLevel* lvl = levels_.allocate();
    if (!lvl) [[unlikely]]
        return nullptr;
    lvl->reset(price);
    store_level(side, price, lvl);
    register_level(side, lvl);
    return lvl;
}

void OrderBook::unlink_empty_level(Side side, PriceLevel* lvl) noexcept {
    if (!lvl || !lvl->empty()) return;
    erase_level_key(side, lvl->price);
    unregister_level(side, lvl);
    levels_.deallocate(lvl);
}

void OrderBook::destroy_order(Order* o) noexcept {
    if (!o) return;
    index_.erase(o->id);
    orders_.deallocate(o);
}

void OrderBook::remove_order_from_book(Order* o) noexcept {
    if (!o || !o->level) return;
    const Side side = o->side;
    PriceLevel* lvl = o->level;
    detach_from_level(o);
    unlink_empty_level(side, lvl);
}

void OrderBook::rest_order(Order* o) noexcept {
    PriceLevel* lvl = get_or_create_level(o->side, o->price);
    if (!lvl) [[unlikely]] {
        o->status = OrderStatus::Rejected;
        emit(EventType::Reject, RejectReason::PoolExhausted, o->id, 0, o->price, o->remaining(),
             o->side);
        destroy_order(o);
        return;
    }
    append_to_level(lvl, o);
    o->status = (o->filled_qty > 0) ? OrderStatus::Partial : OrderStatus::New;
}

static void unlink_filled_passive(PriceLevel* lvl, Order* passive) noexcept {
    if (passive->prev) {
        passive->prev->next = passive->next;
    } else {
        lvl->head = passive->next;
    }
    if (passive->next) {
        passive->next->prev = passive->prev;
    } else {
        lvl->tail = passive->prev;
    }
    passive->next = passive->prev = nullptr;
    passive->level = nullptr;
    if (lvl->order_count > 0) --lvl->order_count;
}

uint32_t OrderBook::match(Order* incoming) noexcept {
    if (incoming->side == Side::Bid) {
        while (incoming->remaining() > 0 && ask_head_ &&
               (incoming->type == OrderType::Market || incoming->price >= ask_head_->price)) {
            PriceLevel* lvl = ask_head_;
            Order* passive = lvl->head;
#if defined(__GNUC__) || defined(__clang__)
            if (passive->next) __builtin_prefetch(passive->next, 0, 3);
#endif
            const uint32_t qty = std::min(incoming->remaining(), passive->remaining());
            const uint64_t trade_px = passive->price;

            incoming->filled_qty += qty;
            passive->filled_qty += qty;
            lvl->total_qty -= qty;
            ++fill_count_;

            emit(EventType::Fill, RejectReason::None, incoming->id, passive->id, trade_px, qty,
                 incoming->side);

            if (passive->fully_filled()) {
                unlink_filled_passive(lvl, passive);
                passive->status = OrderStatus::Filled;
                unlink_empty_level(Side::Ask, lvl);
                destroy_order(passive);
            } else {
                passive->status = OrderStatus::Partial;
            }
        }
    } else {
        while (incoming->remaining() > 0 && bid_head_ &&
               (incoming->type == OrderType::Market || incoming->price <= bid_head_->price)) {
            PriceLevel* lvl = bid_head_;
            Order* passive = lvl->head;
#if defined(__GNUC__) || defined(__clang__)
            if (passive->next) __builtin_prefetch(passive->next, 0, 3);
#endif
            const uint32_t qty = std::min(incoming->remaining(), passive->remaining());
            const uint64_t trade_px = passive->price;

            incoming->filled_qty += qty;
            passive->filled_qty += qty;
            lvl->total_qty -= qty;
            ++fill_count_;

            emit(EventType::Fill, RejectReason::None, incoming->id, passive->id, trade_px, qty,
                 incoming->side);

            if (passive->fully_filled()) {
                unlink_filled_passive(lvl, passive);
                passive->status = OrderStatus::Filled;
                unlink_empty_level(Side::Bid, lvl);
                destroy_order(passive);
            } else {
                passive->status = OrderStatus::Partial;
            }
        }
    }

    const uint32_t filled = incoming->filled_qty;

    if (incoming->fully_filled()) {
        incoming->status = OrderStatus::Filled;
        destroy_order(incoming);
    } else if (incoming->type == OrderType::Limit) {
        rest_order(incoming);
    } else {
        incoming->status =
            (incoming->filled_qty > 0) ? OrderStatus::Partial : OrderStatus::Cancelled;
        emit(EventType::CancelAck, RejectReason::BookEmpty, incoming->id, 0, 0,
             incoming->remaining(), incoming->side);
        destroy_order(incoming);
    }
    return filled;
}

InsertResult OrderBook::insert(uint64_t id, Side side, OrderType type, uint64_t price,
                               uint32_t quantity) noexcept {
    InsertResult r{};

    if (quantity == 0) [[unlikely]] {
        r.reason = RejectReason::InvalidQuantity;
        emit(EventType::Reject, r.reason, id, 0, price, quantity, side);
        return r;
    }
    if (type == OrderType::Limit && !valid_price(price)) [[unlikely]] {
        r.reason = RejectReason::InvalidPrice;
        emit(EventType::Reject, r.reason, id, 0, price, quantity, side);
        return r;
    }
    if (type == OrderType::Market) price = 0;

    Order* o = orders_.allocate();
    if (!o) [[unlikely]] {
        r.reason = RejectReason::PoolExhausted;
        emit(EventType::Reject, r.reason, id, 0, price, quantity, side);
        return r;
    }

    o->id = id;
    o->price = price;
    o->quantity = quantity;
    o->filled_qty = 0;
    o->side = side;
    o->type = type;
    o->status = OrderStatus::New;
    o->next = o->prev = nullptr;
    o->level = nullptr;

    if (!index_.insert(id, o)) [[unlikely]] {
        r.reason = RejectReason::DuplicateId;
        orders_.deallocate(o);
        emit(EventType::Reject, r.reason, id, 0, price, quantity, side);
        return r;
    }

    emit(EventType::Ack, RejectReason::None, id, 0, price, quantity, side);

    const bool can_cross =
        (type == OrderType::Market) ||
        (side == Side::Bid && ask_head_ && price >= ask_head_->price) ||
        (side == Side::Ask && bid_head_ && price <= bid_head_->price);

    if (can_cross) {
        const uint32_t filled = match(o);
        r.ok = true;
        r.filled_qty = filled;
        Order* still = index_.find(id);
        if (still) {
            r.status = still->status;
            r.filled_qty = still->filled_qty;
        } else if (type == OrderType::Market && filled < quantity) {
            r.status = (filled > 0) ? OrderStatus::Partial : OrderStatus::Cancelled;
        } else if (filled >= quantity) {
            r.status = OrderStatus::Filled;
        } else {
            r.status = OrderStatus::Cancelled;
        }
        return r;
    }

    if (type == OrderType::Limit) {
        rest_order(o);
        Order* still = index_.find(id);
        r.ok = still != nullptr;
        if (still) {
            r.status = still->status;
            r.filled_qty = 0;
        } else {
            r.reason = RejectReason::PoolExhausted;
            r.status = OrderStatus::Rejected;
        }
        return r;
    }

    r.ok = true;
    r.status = OrderStatus::Cancelled;
    r.filled_qty = 0;
    emit(EventType::CancelAck, RejectReason::BookEmpty, id, 0, 0, quantity, side);
    destroy_order(o);
    return r;
}

CancelResult OrderBook::cancel(uint64_t id) noexcept {
    CancelResult r{};
    Order* o = index_.find(id);
    if (!o) {
        r.reason = RejectReason::NotFound;
        emit(EventType::Reject, r.reason, id, 0, 0, 0, Side::Bid);
        return r;
    }
    r.remaining_qty = o->remaining();
    const uint64_t px = o->price;
    const Side side = o->side;
    remove_order_from_book(o);
    o->status = OrderStatus::Cancelled;
    emit(EventType::CancelAck, RejectReason::None, id, 0, px, r.remaining_qty, side);
    destroy_order(o);
    r.ok = true;
    return r;
}

void OrderBook::clear() noexcept {
    bid_head_ = nullptr;
    ask_head_ = nullptr;
    fill_count_ = 0;
    bid_active_.clear();
    ask_active_.clear();
    bid_map_.clear();
    ask_map_.clear();
    index_.clear();
    orders_.reset();
    levels_.reset();
    EngineEvent dump;
    while (events_.try_pop(dump)) {
    }
}

} // namespace me
