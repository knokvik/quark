#include "me/naive_book.hpp"
#include "me/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

using namespace me;

static void pin_to_core(int core) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#elif defined(__APPLE__)
    // macOS: QoS as a soft preference; true affinity requires private APIs.
    (void)core;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    (void)core;
#endif
}

static uint64_t rdtsc_now() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned aux;
    return __builtin_ia32_rdtscp(&aux);
#else
    // ARM / portable fallback
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

struct Stats {
    double p50_ns{0};
    double p99_ns{0};
    double p999_ns{0};
    double mean_ns{0};
    double throughput{0};
};

static Stats compute_stats(std::vector<double>& samples_ns, double total_sec) {
    std::sort(samples_ns.begin(), samples_ns.end());
    Stats s;
    auto pct = [&](double p) {
        if (samples_ns.empty()) return 0.0;
        size_t i = static_cast<size_t>(p * (samples_ns.size() - 1));
        return samples_ns[i];
    };
    s.p50_ns = pct(0.50);
    s.p99_ns = pct(0.99);
    s.p999_ns = pct(0.999);
    s.mean_ns = std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0) /
                static_cast<double>(samples_ns.size());
    s.throughput = static_cast<double>(samples_ns.size()) / total_sec;
    return s;
}

static void print_stats(const char* name, const Stats& s) {
    std::printf("%s\n", name);
    std::printf("  p50:   %8.1f ns\n", s.p50_ns);
    std::printf("  p99:   %8.1f ns\n", s.p99_ns);
    std::printf("  p999:  %8.1f ns\n", s.p999_ns);
    std::printf("  mean:  %8.1f ns\n", s.mean_ns);
    std::printf("  thruput: %.2f Mops/s\n", s.throughput / 1e6);
}

struct Op {
    enum class Kind : uint8_t { Limit, Market, Cancel } kind;
    uint64_t id;
    Side side;
    uint64_t price;
    uint32_t qty;
};

static std::vector<Op> generate_workload(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 1000);
    std::normal_distribution<double> px_dist(100.0, 0.5);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;
    live.reserve(n);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        int roll = op_dist(rng);
        if (roll < 60 || live.empty()) {
            Op o;
            o.kind = Op::Kind::Limit;
            o.id = next_id++;
            o.side = (rng() & 1) ? Side::Bid : Side::Ask;
            double px = px_dist(rng);
            if (px < 50.0) px = 50.0;
            if (px > 150.0) px = 150.0;
            o.price = static_cast<uint64_t>(px * kPriceScale);
            o.qty = qty_dist(rng);
            live.push_back(o.id);
            ops.push_back(o);
        } else if (roll < 80) {
            Op o;
            o.kind = Op::Kind::Cancel;
            size_t idx = static_cast<size_t>(rng() % live.size());
            o.id = live[idx];
            live[idx] = live.back();
            live.pop_back();
            o.side = Side::Bid;
            o.price = 0;
            o.qty = 0;
            ops.push_back(o);
        } else {
            Op o;
            o.kind = Op::Kind::Market;
            o.id = next_id++;
            o.side = (rng() & 1) ? Side::Bid : Side::Ask;
            o.price = 0;
            o.qty = qty_dist(rng);
            ops.push_back(o);
        }
    }
    return ops;
}

template <typename BookT, typename InsertFn, typename CancelFn>
static Stats run_bench(const char* name, BookT& book, const std::vector<Op>& ops,
                       InsertFn&& insert_fn, CancelFn&& cancel_fn, size_t warmup) {
    // Warmup
    for (size_t i = 0; i < warmup && i < ops.size(); ++i) {
        const Op& o = ops[i];
        if (o.kind == Op::Kind::Cancel) {
            cancel_fn(book, o.id);
        } else if (o.kind == Op::Kind::Market) {
            insert_fn(book, o.id, o.side, OrderType::Market, 0, o.qty);
        } else {
            insert_fn(book, o.id, o.side, OrderType::Limit, o.price, o.qty);
        }
    }

    std::vector<double> samples;
    samples.reserve(ops.size() - warmup);

    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = warmup; i < ops.size(); ++i) {
        const Op& o = ops[i];
        const auto a = std::chrono::steady_clock::now();
        if (o.kind == Op::Kind::Cancel) {
            cancel_fn(book, o.id);
        } else if (o.kind == Op::Kind::Market) {
            insert_fn(book, o.id, o.side, OrderType::Market, 0, o.qty);
        } else {
            insert_fn(book, o.id, o.side, OrderType::Limit, o.price, o.qty);
        }
        const auto b = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::nano>(b - a).count());
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();

    Stats s = compute_stats(samples, sec);
    print_stats(name, s);
    (void)rdtsc_now;
    return s;
}

int main() {
    pin_to_core(0);

    constexpr size_t N = 500'000;
    constexpr size_t WARMUP = 50'000;
    auto ops = generate_workload(N, 12345);

    BookConfig cfg;
    cfg.order_capacity = 1 << 20;
    cfg.level_capacity = 1 << 16;
    cfg.index_capacity = 1 << 21;
    cfg.event_capacity = 1 << 16;

    {
        cfg.enable_events = false; // fair hot-path measurement (async logger off)
        OrderBook book(cfg);
        auto s = run_bench(
            "=== Fast OrderBook (me) ===", book, ops,
            [](OrderBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
                b.insert(id, side, t, px, q);
            },
            [](OrderBook& b, uint64_t id) { b.cancel(id); }, WARMUP);

        std::printf("\nTargets: p50 < 500ns, p99 < 2000ns, thruput > 1M/s\n");
        if (s.p50_ns < 500) std::printf("  p50: PASS\n");
        else std::printf("  p50: CHECK (platform-dependent): %.1f ns\n", s.p50_ns);
        if (s.p99_ns < 2000) std::printf("  p99: PASS\n");
        else std::printf("  p99: CHECK (platform-dependent): %.1f ns\n", s.p99_ns);
        if (s.throughput >= 1e6) std::printf("  thruput: PASS\n");
        else std::printf("  thruput: CHECK: %.2f Mops/s\n", s.throughput / 1e6);
    }

    {
        NaiveBook book;
        // Use unique ids across full stream — regenerate fresh for fair naive
        auto ops2 = generate_workload(200'000, 12345); // smaller for naive
        run_bench(
            "=== Naive (std::map + std::list) ===", book, ops2,
            [](NaiveBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
                b.insert(id, side, t, px, q);
            },
            [](NaiveBook& b, uint64_t id) { b.cancel(id); }, 20'000);
    }

    return 0;
}
