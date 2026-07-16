#include "me/naive_book.hpp"
#include "me/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#if defined(__APPLE__)
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
    (void)core;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#else
    (void)core;
#endif
}

struct Stats {
    double p50_ns{0};
    double p99_ns{0};
    double p999_ns{0};
    double mean_ns{0};
    double throughput{0};
    size_t n{0};
};

static Stats compute_stats(std::vector<double>& samples_ns, double total_sec) {
    std::sort(samples_ns.begin(), samples_ns.end());
    Stats s;
    s.n = samples_ns.size();
    auto pct = [&](double p) {
        if (samples_ns.empty()) return 0.0;
        size_t i = static_cast<size_t>(p * (samples_ns.size() - 1));
        return samples_ns[i];
    };
    s.p50_ns = pct(0.50);
    s.p99_ns = pct(0.99);
    s.p999_ns = pct(0.999);
    s.mean_ns = std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0) /
                static_cast<double>(std::max<size_t>(samples_ns.size(), 1));
    s.throughput = total_sec > 0 ? static_cast<double>(samples_ns.size()) / total_sec : 0;
    return s;
}

static void print_stats(const char* name, const Stats& s) {
    std::printf("%s\n", name);
    std::printf("  n:     %zu\n", s.n);
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
                       InsertFn&& insert_fn, CancelFn&& cancel_fn, size_t warmup,
                       std::vector<double>* out_samples = nullptr) {
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
    samples.reserve(ops.size() > warmup ? ops.size() - warmup : 0);

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
        samples.push_back(std::chrono::duration<double, std::nano>(b - a).count());
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();

    Stats s = compute_stats(samples, sec);
    print_stats(name, s);
    if (out_samples) *out_samples = std::move(samples);
    return s;
}

static void write_csv(const char* path, const std::vector<double>& samples) {
    std::ofstream out(path);
    for (double v : samples) {
        out << static_cast<long long>(v) << '\n';
    }
}

static void write_summary(const char* path, const Stats& engine, const Stats& naive) {
    std::ofstream out(path);
    out << "engine_p50_ns=" << engine.p50_ns << '\n';
    out << "engine_p99_ns=" << engine.p99_ns << '\n';
    out << "engine_p999_ns=" << engine.p999_ns << '\n';
    out << "engine_mean_ns=" << engine.mean_ns << '\n';
    out << "engine_mops=" << (engine.throughput / 1e6) << '\n';
    out << "naive_p50_ns=" << naive.p50_ns << '\n';
    out << "naive_p99_ns=" << naive.p99_ns << '\n';
    out << "naive_p999_ns=" << naive.p999_ns << '\n';
    out << "naive_mean_ns=" << naive.mean_ns << '\n';
    out << "naive_mops=" << (naive.throughput / 1e6) << '\n';
}

// Throughput vs symbol count: measure one core, project ideal multi-core sharding.
static void bench_symbol_scaling(const char* csv_path, double single_core_mops) {
    std::ofstream out(csv_path);
    out << "symbols,mops\n";
    for (int symbols : {1, 2, 4, 8, 16}) {
        const double scaled = single_core_mops * symbols;
        out << symbols << ',' << scaled << '\n';
        std::printf("symbols=%d  ~%.2f Mops/s (ideal multi-core estimate)\n", symbols, scaled);
    }
}

int main(int argc, char** argv) {
    pin_to_core(0);

    bool csv = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-csv") == 0) csv = false;
    }

    constexpr size_t N = 500'000;
    constexpr size_t WARMUP = 50'000;
    auto ops = generate_workload(N, 12345);

    BookConfig cfg;
    cfg.order_capacity = 1 << 20;
    cfg.level_capacity = 1 << 16;
    cfg.index_capacity = 1 << 21;
    cfg.event_capacity = 1 << 16;
    cfg.enable_events = false;

    std::vector<double> engine_samples;
    Stats engine_stats;
    {
        OrderBook book(cfg);
        engine_stats = run_bench(
            "=== Quark OrderBook ===", book, ops,
            [](OrderBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
                b.insert(id, side, t, px, q);
            },
            [](OrderBook& b, uint64_t id) { b.cancel(id); }, WARMUP, &engine_samples);

        std::printf("\nTargets: p50 < 500ns, p99 < 2000ns, thruput > 1M/s\n");
        if (engine_stats.p50_ns < 500) std::printf("  p50: PASS\n");
        else std::printf("  p50: CHECK: %.1f ns\n", engine_stats.p50_ns);
        if (engine_stats.p99_ns < 2000) std::printf("  p99: PASS\n");
        else std::printf("  p99: CHECK: %.1f ns\n", engine_stats.p99_ns);
        if (engine_stats.throughput >= 1e6) std::printf("  thruput: PASS\n");
        else std::printf("  thruput: CHECK: %.2f Mops/s\n", engine_stats.throughput / 1e6);
    }

    Stats naive_stats;
    {
        NaiveBook book;
        auto ops2 = generate_workload(N, 12345);
        naive_stats = run_bench(
            "=== Naive (std::map + std::list) ===", book, ops2,
            [](NaiveBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
                b.insert(id, side, t, px, q);
            },
            [](NaiveBook& b, uint64_t id) { b.cancel(id); }, WARMUP, nullptr);
    }

    if (csv) {
        write_csv("latencies.csv", engine_samples);
        write_summary("bench_summary.txt", engine_stats, naive_stats);
        std::printf("\nWrote latencies.csv (%zu samples) and bench_summary.txt\n",
                    engine_samples.size());
        std::printf("\n=== Symbol scaling (ideal multi-core) ===\n");
        bench_symbol_scaling("throughput_vs_symbols.csv", engine_stats.throughput / 1e6);
    }

    return 0;
}
