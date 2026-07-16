# Quark

**84 ns p50 · 0.9 μs p99 · 8.9 M orders/sec · Zero allocations · 11,240 tests passing**

Single-threaded limit order book in **C++20**. Zero heap allocations and zero locks on the hot path. Sub-microsecond latency via cache-aligned arenas, intrusive FIFO price levels, and Robin Hood ID lookup.

---

## Performance

| Metric | Value |
|--------|-------|
| **p50 latency** | **84 ns** |
| **p99 latency** | **0.9 μs** |
| **p999 latency** | **~9 μs** |
| **Throughput** | **8.9 M orders/sec** (single core, Apple Silicon) |
| **Heap allocations (hot path)** | **0** |
| **Lock contention** | **None** |
| **Unit assertions** | **11,240** passing |

> Deep-book churn workload (few prices, long FIFO queues, heavy cancels). Release `-O3 -march=native`, batch throughput, events off. Re-run: `./build/me_bench`.

### Latency distribution

![Latency histogram](docs/latency_histogram.png)

![Latency CDF](docs/latency_cdf.png)

### Design-target compliance

![Compliance scorecard](docs/compliance_scorecard.png)

### Throughput vs symbol shards

![Throughput vs symbols](docs/throughput_vs_symbols.png)

| Series | Meaning |
|--------|---------|
| **Grey bars** | All N books on **one core** (serial) — aggregate Mops stays roughly flat |
| **Green bars** | **Ideal multi-core** — one book per core → linear scale from the 1-book baseline |

---

## vs. Baselines (same harness, same stream)

Deep FIFO queues expose O(n) cancel in textbook designs. Quark cancel is O(1) intrusive unlink.

| Metric | Textbook `map`+`vector` | STL `map`+`list` | **Quark** | Speedup vs textbook |
|--------|-------------------------|------------------|-----------|---------------------|
| **p50 latency** | 208 ns | 250 ns | **84 ns** | **~2.5×** |
| **p99 latency** | 11 μs | 4.4 μs | **0.9 μs** | **~12×** |
| **Throughput** | 0.53 Mops/s | 1.6 Mops/s | **8.9 Mops/s** | **~17×** |
| **Heap allocs (hot path)** | yes | yes | **0** | **∞** |
| **Cancel complexity** | O(n) scan/shift | O(1) list | **O(1)** intrusive | — |
| **Locks** | none | none | **none** | — |

![Measured comparison](docs/comparison_measured.png)

![Structural comparison](docs/comparison_structural.png)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Order Entry (Single Thread / Symbol)                        │
│  ├── Limit GTC · Market · Cancel                             │
│  └── No heap, no locks, no syscalls on hot path              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Robin Hood Index (OrderID → Order*)                         │
│  ├── Flat array, open addressing                             │
│  ├── O(1) expected lookup                                    │
│  └── Pre-sized (no rehash on hot path)                       │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
┌─────────────────────┐           ┌─────────────────────┐
│   BID levels        │           │   ASK levels        │
│  (sorted DESC head) │           │  (sorted ASC head)  │
│                     │           │                     │
│  $100.50 → [O1]→[O2]│           │  $100.75 → [O3]     │
│  $100.25 → [O4]     │           │  $101.00 → [O5]→[O6]│
│                     │           │                     │
│  Intrusive FIFO     │           │  Intrusive FIFO     │
│  (next/prev ptrs)   │           │  (next/prev ptrs)   │
└─────────────────────┘           └─────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Match Engine                                                │
│  ├── Cross best bid/ask · price–time priority                │
│  ├── Fills → lock-free SPSC ring                             │
│  └── O(1) best price · O(n) FIFO within level                │
└─────────────────────────────────────────────────────────────┘
```

### Key design decisions

| Component | Choice | Why |
|-----------|--------|-----|
| **Order storage** | Pre-allocated arena (1M × 64 B) | Zero `malloc` after init |
| **Price levels** | Intrusive doubly-linked FIFO | No node alloc; linear walk |
| **Best price** | Sorted level-list head | O(1) best bid/ask |
| **Price → level** | Dense flat window + overflow map | O(1) typical lookup |
| **ID index** | Robin Hood open addressing | O(1), no chaining |
| **Alignment** | `alignas(64)` on hot structs | Cache-line isolation |
| **Output** | Lock-free SPSC ring | Async log without blocking match |

---

## Profiling

### Hardware counters (Linux)

```bash
sudo perf stat -e cycles,instructions,cache-misses,branch-misses,page-faults \
  ./build/me_bench --no-csv
```

**What good looks like**

| Signal | Target |
|--------|--------|
| IPC (insn/cycle) | ≳ 2.5 |
| Cache-miss rate | ≪ 1% of refs |
| Page faults after warmup | ~0 |
| Time in `malloc` / `mutex` | 0% on hot path |

### Hot-path stack (illustrative)

![Flamegraph](docs/flamegraph.svg)

*SVG summary of expected exclusive time: `insert` / `match` / index — not `malloc`. On Linux, regenerate with `perf` + [FlameGraph](https://github.com/brendangregg/FlameGraph) or `inferno`.*

---

## Testing

- **11,240 assertions** — FIFO, price–time priority, partial fills, cancels, multi-level sweeps  
- **Differential testing** — same best bid/ask as naive `std::map` reference on random streams  
- **Pool exhaustion / duplicate ID / cancel-missing** — soft fail, no crash  
- **Stress-ready pools** — free-list accounting under high cancel ratios  

```bash
./build/me_tests
# Passed: 11240  Failed: 0
```

---

## Quick start

```bash
git clone https://github.com/knokvik/quark.git
cd quark

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/me_tests
./build/me_bench                  # writes build/latencies.csv + summary

# Charts for this README
python3 scripts/plot_latency.py
python3 scripts/plot_comparison.py
python3 scripts/plot_throughput.py
```

---

## Design notes

- [Intrusive lists vs `std::list`](docs/intrusive_lists.md)
- [Robin Hood ID index](docs/robin_hood.md)
- [Memory / arena strategy](docs/memory.md)
- [Architecture detail](docs/architecture.md)
- [Full design specification](docs/DESIGN.md)
- [Performance report](docs/PERFORMANCE.md)
- [Benchmark methodology](docs/benchmarking.md)

---

## Benchmark reproduction

| Setting | Value |
|---------|--------|
| Build | Release, `-O3 -march=native` |
| Workload | 60% limit / 20% cancel / 20% market |
| Warmup | 50K ops (untimed) |
| Sample | 450K timed ops → `latencies.csv` |
| Events | Off for core-path timing |
| Linux hygiene | `performance` governor · `taskset -c N` · quiet machine |

```bash
./scripts/run_release.sh
```

---

## License

[MIT](LICENSE)
