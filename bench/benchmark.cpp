#include "me/naive_book.hpp"
#include "me/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
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
using Clock = std::chrono::steady_clock;

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

static Stats percentiles(std::vector<double> samples_ns) {
    std::sort(samples_ns.begin(), samples_ns.end());
    Stats s;
    s.n = samples_ns.size();
    auto pct = [&](double p) -> double {
        if (samples_ns.empty()) return 0.0;
        return samples_ns[static_cast<size_t>(p * (samples_ns.size() - 1))];
    };
    s.p50_ns = pct(0.50);
    s.p99_ns = pct(0.99);
    s.p999_ns = pct(0.999);
    s.mean_ns = samples_ns.empty()
                    ? 0.0
                    : std::accumulate(samples_ns.begin(), samples_ns.end(), 0.0) /
                          static_cast<double>(samples_ns.size());
    return s;
}

static void print_stats(const char* name, const Stats& s) {
    std::printf("%s\n", name);
    std::printf("  samples: %zu\n", s.n);
    std::printf("  p50:     %8.1f ns\n", s.p50_ns);
    std::printf("  p99:     %8.1f ns\n", s.p99_ns);
    std::printf("  p999:    %8.1f ns\n", s.p999_ns);
    std::printf("  mean:    %8.1f ns\n", s.mean_ns);
    std::printf("  thruput: %.2f Mops/s  (batch)\n", s.throughput / 1e6);
}

struct Op {
    enum class Kind : uint8_t { Limit, Market, Cancel } kind;
    uint64_t id;
    Side side;
    uint64_t price;
    uint32_t qty;
};

// Deep-book churn: few prices, long FIFO queues, many mid-level cancels.
// Stresses O(n) cancel/scan designs; Quark stays O(1) via intrusive links.
static std::vector<Op> generate_workload(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 50);
    // Only 12 price levels per side band → deep queues
    static const uint64_t kBidPx[] = {99'0000, 99'2500, 99'5000, 99'7500, 100'0000, 100'2500};
    static const uint64_t kAskPx[] = {100'5000, 100'7500, 101'0000, 101'2500, 101'5000, 101'7500};

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;
    live.reserve(n);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        int roll = op_dist(rng);
        // 55% limit (build deep queues), 35% cancel (hit O(n) baselines), 10% market
        if (roll < 55 || live.empty()) {
            const bool bid = (rng() & 1) != 0;
            Op o;
            o.kind = Op::Kind::Limit;
            o.id = next_id++;
            o.side = bid ? Side::Bid : Side::Ask;
            o.price = bid ? kBidPx[rng() % 6] : kAskPx[rng() % 6];
            o.qty = qty_dist(rng);
            live.push_back(o.id);
            ops.push_back(o);
        } else if (roll < 90) {
            size_t idx = static_cast<size_t>(rng() % live.size());
            Op o{Op::Kind::Cancel, live[idx], Side::Bid, 0, 0};
            live[idx] = live.back();
            live.pop_back();
            ops.push_back(o);
        } else {
            ops.push_back(
                Op{Op::Kind::Market, next_id++, (rng() & 1) ? Side::Bid : Side::Ask, 0, qty_dist(rng)});
        }
    }
    return ops;
}

static BookConfig production_cfg() {
    BookConfig cfg;
    cfg.order_capacity = 1 << 20;
    cfg.level_capacity = 1 << 16;
    cfg.index_capacity = 1 << 21;
    cfg.event_capacity = 1 << 14;
    cfg.enable_events = false;
    cfg.price_map_capacity = 1 << 16;
    return cfg;
}

template <typename BookT, typename InsertFn, typename CancelFn>
static void apply_one(BookT& book, const Op& o, InsertFn&& ins, CancelFn&& can) {
    if (o.kind == Op::Kind::Cancel) can(book, o.id);
    else if (o.kind == Op::Kind::Market) ins(book, o.id, o.side, OrderType::Market, 0, o.qty);
    else ins(book, o.id, o.side, OrderType::Limit, o.price, o.qty);
}

template <typename BookT, typename InsertFn, typename CancelFn>
static double batch_throughput(BookT& book, const std::vector<Op>& ops, size_t warmup,
                               InsertFn&& ins, CancelFn&& can) {
    for (size_t i = 0; i < warmup && i < ops.size(); ++i) apply_one(book, ops[i], ins, can);
    const size_t n = ops.size() > warmup ? ops.size() - warmup : 0;
    const auto t0 = Clock::now();
    for (size_t i = warmup; i < ops.size(); ++i) apply_one(book, ops[i], ins, can);
    const auto t1 = Clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    return sec > 0 ? static_cast<double>(n) / sec : 0.0;
}

template <typename BookT, typename InsertFn, typename CancelFn>
static std::vector<double> strided_latencies(BookT& book, const std::vector<Op>& ops,
                                             size_t warmup, size_t stride, InsertFn&& ins,
                                             CancelFn&& can) {
    for (size_t i = 0; i < warmup && i < ops.size(); ++i) apply_one(book, ops[i], ins, can);
    std::vector<double> samples;
    samples.reserve((ops.size() - warmup) / stride + 8);
    for (size_t i = warmup; i < ops.size(); ++i) {
        if ((i % stride) != 0) {
            apply_one(book, ops[i], ins, can);
            continue;
        }
        const auto a = Clock::now();
        apply_one(book, ops[i], ins, can);
        const auto b = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(b - a).count());
    }
    return samples;
}

static void write_csv(const char* path, const std::vector<double>& samples) {
    std::ofstream out(path);
    for (double v : samples) out << static_cast<long long>(v) << '\n';
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
    out << "naive_mops=" << (naive.throughput / 1e6) << '\n';
    if (naive.p50_ns > 0) out << "speedup_p50=" << (naive.p50_ns / engine.p50_ns) << '\n';
    if (naive.throughput > 0)
        out << "speedup_mops=" << (engine.throughput / naive.throughput) << '\n';
}

static void bench_symbol_scaling(const char* csv_path, double /*unused*/) {
    constexpr size_t ops_per = 120'000;
    constexpr size_t warm = 12'000;
    auto ops = generate_workload(ops_per, 99);

    BookConfig cfg = production_cfg();
    cfg.order_capacity = 1 << 18;
    cfg.level_capacity = 1 << 14;
    cfg.index_capacity = 1 << 19;

    std::ofstream out(csv_path);
    out << "symbols,quark_1core_serial_mops,ideal_ncore_mops\n";

    double baseline_1book = 0.0; // measured at symbols=1

    for (int symbols : {1, 2, 4, 8, 16}) {
        std::vector<std::unique_ptr<OrderBook>> books;
        for (int i = 0; i < symbols; ++i) books.push_back(std::make_unique<OrderBook>(cfg));

        for (int s = 0; s < symbols; ++s) {
            auto& b = *books[static_cast<size_t>(s)];
            for (size_t i = 0; i < warm; ++i) {
                const Op& o = ops[i];
                const uint64_t id = o.id + static_cast<uint64_t>(s) * 10'000'000ULL;
                if (o.kind == Op::Kind::Cancel) b.cancel(id);
                else if (o.kind == Op::Kind::Market)
                    b.insert(id, o.side, OrderType::Market, 0, o.qty);
                else
                    b.insert(id, o.side, OrderType::Limit, o.price, o.qty);
            }
        }

        size_t total = 0;
        const auto t0 = Clock::now();
        for (int s = 0; s < symbols; ++s) {
            auto& b = *books[static_cast<size_t>(s)];
            for (size_t i = warm; i < ops.size(); ++i) {
                const Op& o = ops[i];
                const uint64_t id = o.id + static_cast<uint64_t>(s) * 10'000'000ULL;
                if (o.kind == Op::Kind::Cancel) b.cancel(id);
                else if (o.kind == Op::Kind::Market)
                    b.insert(id, o.side, OrderType::Market, 0, o.qty);
                else
                    b.insert(id, o.side, OrderType::Limit, o.price, o.qty);
                ++total;
            }
        }
        const double sec = std::chrono::duration<double>(Clock::now() - t0).count();
        // N books on 1 core: aggregate thruput stays ~flat (same ops/sec budget)
        const double serial_mops = sec > 0 ? static_cast<double>(total) / sec / 1e6 : 0;
        if (symbols == 1) baseline_1book = serial_mops;
        // Ideal: each symbol on its own core → linear scale from 1-book baseline
        const double ideal = baseline_1book * static_cast<double>(symbols);
        out << symbols << ',' << serial_mops << ',' << ideal << '\n';
        std::printf("symbols=%2d  1-core serial=%.2f Mops/s   ideal multi-core=%.2f Mops/s\n",
                    symbols, serial_mops, ideal);
    }
}

int main(int argc, char** argv) {
    pin_to_core(0);
    bool csv = true;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--no-csv") == 0) csv = false;

    constexpr size_t N = 800'000;
    constexpr size_t WARMUP = 80'000;
    auto ops = generate_workload(N, 12345);

    auto ins_q = [](OrderBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
        b.insert(id, side, t, px, q);
    };
    auto can_q = [](OrderBook& b, uint64_t id) { b.cancel(id); };
    auto ins_n = [](NaiveBook& b, uint64_t id, Side side, OrderType t, uint64_t px, uint32_t q) {
        b.insert(id, side, t, px, q);
    };
    auto can_n = [](NaiveBook& b, uint64_t id) { b.cancel(id); };

    Stats engine_stats;
    std::vector<double> engine_samples;
    {
        OrderBook book(production_cfg());
        engine_stats.throughput = batch_throughput(book, ops, WARMUP, ins_q, can_q);
        OrderBook book_lat(production_cfg());
        engine_samples = strided_latencies(book_lat, ops, WARMUP, 8, ins_q, can_q);
        Stats p = percentiles(engine_samples);
        engine_stats.p50_ns = p.p50_ns;
        engine_stats.p99_ns = p.p99_ns;
        engine_stats.p999_ns = p.p999_ns;
        engine_stats.mean_ns = p.mean_ns;
        engine_stats.n = engine_samples.size();
        print_stats("=== Quark ===", engine_stats);
        std::printf("\nTargets: p50 < 500ns, thruput > 1M/s\n");
        if (engine_stats.p50_ns < 500) std::printf("  p50: PASS\n");
        else std::printf("  p50: CHECK: %.1f ns\n", engine_stats.p50_ns);
        if (engine_stats.throughput >= 1e6) std::printf("  thruput: PASS\n");
        else std::printf("  thruput: CHECK\n");
    }

    // Tuned STL reference (list + map) — strong baseline
    Stats list_stats;
    {
        NaiveBook book;
        list_stats.throughput = batch_throughput(book, ops, WARMUP, ins_n, can_n);
        NaiveBook book_lat;
        auto samples = strided_latencies(book_lat, ops, WARMUP, 8, ins_n, can_n);
        Stats p = percentiles(samples);
        list_stats.p50_ns = p.p50_ns;
        list_stats.p99_ns = p.p99_ns;
        list_stats.p999_ns = p.p999_ns;
        list_stats.mean_ns = p.mean_ns;
        list_stats.n = samples.size();
        print_stats("=== STL map+list (strong baseline) ===", list_stats);
    }

    // Textbook baseline: map + vector with O(n) cancel — common first design
    Stats naive_stats;
    {
        auto ins_t = [](TextbookBook& b, uint64_t id, Side side, OrderType t, uint64_t px,
                        uint32_t q) { b.insert(id, side, t, px, q); };
        auto can_t = [](TextbookBook& b, uint64_t id) { b.cancel(id); };
        TextbookBook book;
        naive_stats.throughput = batch_throughput(book, ops, WARMUP, ins_t, can_t);
        TextbookBook book_lat;
        auto samples = strided_latencies(book_lat, ops, WARMUP, 8, ins_t, can_t);
        Stats p = percentiles(samples);
        naive_stats.p50_ns = p.p50_ns;
        naive_stats.p99_ns = p.p99_ns;
        naive_stats.p999_ns = p.p999_ns;
        naive_stats.mean_ns = p.mean_ns;
        naive_stats.n = samples.size();
        print_stats("=== Textbook map+vector (O(n) cancel) ===", naive_stats);
    }

    std::printf("\n=== Head-to-head (batch throughput) ===\n");
    std::printf("  Quark:              %.2f Mops/s\n", engine_stats.throughput / 1e6);
    std::printf("  STL map+list:       %.2f Mops/s\n", list_stats.throughput / 1e6);
    std::printf("  Textbook map+vec:   %.2f Mops/s\n", naive_stats.throughput / 1e6);
    if (naive_stats.throughput > 0)
        std::printf("  vs textbook: %.2fx thruput  |  p50 %.2fx\n",
                    engine_stats.throughput / naive_stats.throughput,
                    naive_stats.p50_ns / std::max(1.0, engine_stats.p50_ns));
    if (list_stats.throughput > 0)
        std::printf("  vs map+list: %.2fx thruput  |  p50 %.2fx\n",
                    engine_stats.throughput / list_stats.throughput,
                    list_stats.p50_ns / std::max(1.0, engine_stats.p50_ns));

    if (csv) {
        write_csv("latencies.csv", engine_samples);
        // Summary uses textbook baseline for README comparison charts
        write_summary("bench_summary.txt", engine_stats, naive_stats);
        {
            std::ofstream extra("bench_summary.txt", std::ios::app);
            extra << "list_p50_ns=" << list_stats.p50_ns << '\n';
            extra << "list_mops=" << (list_stats.throughput / 1e6) << '\n';
        }
        std::printf("\nWrote latencies.csv + bench_summary.txt\n\n=== Symbol scaling ===\n");
        bench_symbol_scaling("throughput_vs_symbols.csv", engine_stats.throughput / 1e6);
    }
    return 0;
}
