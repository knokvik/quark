#include "me/naive_book.hpp"
#include "me/order_book.hpp"
#include "me/robin_hood.hpp"
#include "me/spsc_queue.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace me;

static int g_failed = 0;
static int g_passed = 0;

#define EXPECT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++g_failed;                                                                            \
        } else {                                                                                   \
            ++g_passed;                                                                            \
        }                                                                                          \
    } while (0)

static BookConfig small_cfg() {
    BookConfig c;
    c.order_capacity = 4096;
    c.level_capacity = 1024;
    c.index_capacity = 8192;
    c.event_capacity = 4096;
    return c;
}

static void test_fifo_within_level() {
    OrderBook book(small_cfg());
    // 5 asks at same price, cancel 3rd → 4th becomes 3rd in queue
    const uint64_t px = 100'0000; // $100.00
    for (uint64_t i = 1; i <= 5; ++i) {
        auto r = book.insert(i, Side::Ask, OrderType::Limit, px, 10);
        EXPECT(r.ok);
    }
    EXPECT(book.level_count(Side::Ask, px) == 5);
    EXPECT(book.cancel(3).ok);
    EXPECT(book.level_count(Side::Ask, px) == 4);
    EXPECT(book.find(3) == nullptr);
    EXPECT(book.find(4) != nullptr);

    // Aggressive bid for 10 should take id=1 first (FIFO)
    auto r = book.insert(100, Side::Bid, OrderType::Limit, px, 10);
    EXPECT(r.ok);
    EXPECT(book.find(1) == nullptr); // fully filled
    EXPECT(book.find(2) != nullptr);
    EXPECT(book.level_count(Side::Ask, px) == 3);
}

static void test_price_time_priority() {
    OrderBook book(small_cfg());
    const uint64_t px = 100'5000;
    book.insert(1, Side::Ask, OrderType::Limit, px, 5);
    book.insert(2, Side::Ask, OrderType::Limit, px, 5);
    // Better price should match first
    book.insert(3, Side::Ask, OrderType::Limit, px - 100, 5); // $100.49 better for buy

    book.insert(10, Side::Bid, OrderType::Limit, px, 5);
    EXPECT(book.find(3) == nullptr); // better ask filled first
    EXPECT(book.find(1) != nullptr);
}

static void test_partial_fills() {
    OrderBook book(small_cfg());
    const uint64_t px = 100'0000;
    book.insert(1, Side::Bid, OrderType::Limit, px, 100);
    book.insert(2, Side::Ask, OrderType::Limit, px, 30);
    EXPECT(book.find(2) == nullptr);
    Order* bid = book.find(1);
    EXPECT(bid != nullptr);
    EXPECT(bid->remaining() == 70);
    EXPECT(bid->filled_qty == 30);
    EXPECT(book.level_qty(Side::Bid, px) == 70);
}

static void test_cross_prevention() {
    OrderBook book(small_cfg());
    book.insert(1, Side::Bid, OrderType::Limit, 100'5000, 10);
    book.insert(2, Side::Ask, OrderType::Limit, 100'7500, 10); // ask > bid, no cross
    EXPECT(book.find(1) != nullptr);
    EXPECT(book.find(2) != nullptr);
    EXPECT(book.best_bid() == 100'5000);
    EXPECT(book.best_ask() == 100'7500);
    EXPECT(book.fill_count() == 0);
}

static void test_cancel_nonexistent() {
    OrderBook book(small_cfg());
    auto r = book.cancel(999);
    EXPECT(!r.ok);
    EXPECT(r.reason == RejectReason::NotFound);
}

static void test_pool_exhaustion() {
    BookConfig c;
    c.order_capacity = 4;
    c.level_capacity = 4;
    c.index_capacity = 16;
    c.event_capacity = 32;
    OrderBook book(c);
    EXPECT(book.insert(1, Side::Bid, OrderType::Limit, 100'0000, 1).ok);
    EXPECT(book.insert(2, Side::Bid, OrderType::Limit, 100'0000, 1).ok);
    EXPECT(book.insert(3, Side::Bid, OrderType::Limit, 100'0000, 1).ok);
    EXPECT(book.insert(4, Side::Bid, OrderType::Limit, 100'0000, 1).ok);
    auto r = book.insert(5, Side::Bid, OrderType::Limit, 100'0000, 1);
    EXPECT(!r.ok);
    EXPECT(r.reason == RejectReason::PoolExhausted);
}

static void test_market_order() {
    OrderBook book(small_cfg());
    book.insert(1, Side::Ask, OrderType::Limit, 100'0000, 50);
    book.insert(2, Side::Ask, OrderType::Limit, 100'1000, 50);
    auto r = book.insert(10, Side::Bid, OrderType::Market, 0, 75);
    EXPECT(r.ok);
    EXPECT(book.find(1) == nullptr);
    Order* a2 = book.find(2);
    EXPECT(a2 != nullptr);
    EXPECT(a2->remaining() == 25);
    EXPECT(book.find(10) == nullptr); // market fully filled, not resting
}

static void test_duplicate_id() {
    OrderBook book(small_cfg());
    EXPECT(book.insert(1, Side::Bid, OrderType::Limit, 100'0000, 10).ok);
    auto r = book.insert(1, Side::Ask, OrderType::Limit, 101'0000, 10);
    EXPECT(!r.ok);
    EXPECT(r.reason == RejectReason::DuplicateId);
}

static void test_cancel_restores_best() {
    OrderBook book(small_cfg());
    book.insert(1, Side::Bid, OrderType::Limit, 100'0000, 10);
    book.insert(2, Side::Bid, OrderType::Limit, 101'0000, 10);
    EXPECT(book.best_bid() == 101'0000);
    EXPECT(book.cancel(2).ok);
    EXPECT(book.best_bid() == 100'0000);
}

static void test_robin_hood() {
    RobinHoodIndex idx(1024);
    Order orders[100];
    for (int i = 0; i < 100; ++i) {
        orders[i].id = static_cast<uint64_t>(i + 1) * 17;
        EXPECT(idx.insert(orders[i].id, &orders[i]));
    }
    EXPECT(idx.size() == 100);
    for (int i = 0; i < 100; ++i) {
        EXPECT(idx.find(orders[i].id) == &orders[i]);
    }
    EXPECT(idx.erase(orders[50].id));
    EXPECT(idx.find(orders[50].id) == nullptr);
    EXPECT(idx.size() == 99);
    // re-insert
    EXPECT(idx.insert(orders[50].id, &orders[50]));
    EXPECT(idx.find(orders[50].id) == &orders[50]);
}

static void test_spsc() {
    SpscQueue<int> q(8);
    EXPECT(q.try_push(1));
    EXPECT(q.try_push(2));
    int v = 0;
    EXPECT(q.try_pop(v) && v == 1);
    EXPECT(q.try_pop(v) && v == 2);
    EXPECT(!q.try_pop(v));
}

static void test_vs_naive_random() {
    BookConfig c;
    c.order_capacity = 100000;
    c.level_capacity = 10000;
    c.index_capacity = 200000;
    c.event_capacity = 100000;
    OrderBook fast(c);
    NaiveBook naive;

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 100);
    std::normal_distribution<double> px_dist(100.0, 0.5);

    uint64_t next_id = 1;
    std::vector<uint64_t> live;

    for (int step = 0; step < 5000; ++step) {
        const int op = op_dist(rng);
        if (op < 70 || live.empty()) {
            // insert limit
            uint64_t id = next_id++;
            Side side = (rng() & 1) ? Side::Bid : Side::Ask;
            double px_d = px_dist(rng);
            if (px_d < 1.0) px_d = 1.0;
            if (px_d > 999.0) px_d = 999.0;
            uint64_t px = static_cast<uint64_t>(px_d * kPriceScale);
            if (px < kMinPrice) px = kMinPrice;
            if (px > kMaxPrice) px = kMaxPrice;
            uint32_t qty = qty_dist(rng);

            fast.insert(id, side, OrderType::Limit, px, qty);
            naive.insert(id, side, OrderType::Limit, px, qty);

            if (fast.find(id)) live.push_back(id);
        } else if (op < 90) {
            // cancel
            size_t i = static_cast<size_t>(rng() % live.size());
            uint64_t id = live[i];
            bool f = fast.cancel(id).ok;
            bool n = naive.cancel(id);
            EXPECT(f == n);
            live[i] = live.back();
            live.pop_back();
        } else {
            // market
            uint64_t id = next_id++;
            Side side = (rng() & 1) ? Side::Bid : Side::Ask;
            uint32_t qty = qty_dist(rng);
            fast.insert(id, side, OrderType::Market, 0, qty);
            naive.insert(id, side, OrderType::Market, 0, qty);
        }

        EXPECT(fast.best_bid() == naive.best_bid());
        EXPECT(fast.best_ask() == naive.best_ask());
    }
}


static void test_multi_level_sweep() {
    OrderBook book(small_cfg());
    book.insert(1, Side::Ask, OrderType::Limit, 100'0000, 10);
    book.insert(2, Side::Ask, OrderType::Limit, 100'1000, 10);
    book.insert(3, Side::Ask, OrderType::Limit, 100'2000, 10);
    auto r = book.insert(9, Side::Bid, OrderType::Limit, 100'2000, 25);
    EXPECT(r.ok);
    EXPECT(book.find(1) == nullptr);
    EXPECT(book.find(2) == nullptr);
    Order* a3 = book.find(3);
    EXPECT(a3 != nullptr);
    EXPECT(a3->remaining() == 5);
    EXPECT(book.best_ask() == 100'2000);
}

static void test_events() {
    OrderBook book(small_cfg());
    book.insert(1, Side::Ask, OrderType::Limit, 100'0000, 10);
    book.insert(2, Side::Bid, OrderType::Limit, 100'0000, 10);
    EngineEvent buf[64];
    size_t n = book.poll_events(buf, 64);
    EXPECT(n >= 2);
    bool saw_fill = false;
    for (size_t i = 0; i < n; ++i) {
        if (buf[i].type == EventType::Fill) saw_fill = true;
    }
    EXPECT(saw_fill);
}

int main() {
    test_fifo_within_level();
    test_price_time_priority();
    test_partial_fills();
    test_cross_prevention();
    test_cancel_nonexistent();
    test_pool_exhaustion();
    test_market_order();
    test_duplicate_id();
    test_cancel_restores_best();
    test_robin_hood();
    test_spsc();
    test_events();
    test_multi_level_sweep();
    test_vs_naive_random();

    std::printf("Passed: %d  Failed: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
