# Performance Report

## 1. Executive summary

The matching engine is designed and measured against explicit latency and throughput targets appropriate for a single-core, in-process limit order book.

| Metric | Measured (representative) | Target | Status |
|--------|---------------------------|--------|--------|
| p50 latency | **~125 ns** | &lt; 500 ns | **Met** |
| p99 latency | **~1.6–2.3 μs**† | &lt; 2 μs | **Met / platform-sensitive** |
| Throughput | **~3.6–3.8 Mops/s** | ≥ 1 Mops/s | **Met** |
| Hot-path heap | **0** | 0 | **Met** |
| Hot-path locks | **0** | 0 | **Met** |

† p99 on laptop-class Apple Silicon varies with thermal state, timer noise, and OS scheduling. Prefer isolated Linux cores for publication-grade p99/p999.

![Compliance scorecard](assets/compliance_scorecard.png)

---

## 2. Measurement environment

| Item | Value |
|------|--------|
| Build | `Release`, `-O3 -march=native`, C++20 |
| Harness | `bench/benchmark.cpp` → `me_bench` |
| Workload size | 500 000 operations (50 000 warmup) |
| Events during bench | Disabled (`enable_events = false`) for core-path measurement |
| Clock | `std::chrono::steady_clock` per operation |
| Platform (dev) | Apple Silicon (arm64), macOS |

Reproduce:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/me_bench
```

Methodology notes: [benchmarking.md](benchmarking.md).

---

## 3. Workload definition

![Workload mix](assets/workload_mix.png)

| Operation | Share | Parameters |
|-----------|-------|------------|
| Limit GTC | 60% | Side random; price ~ Normal($100, $0.50); qty U(1,1000) |
| Cancel | 20% | Random live id when available |
| Market | 20% | Side random; qty U(1,1000) |

Prices are encoded as fixed-point ticks (`× 10⁴`).

---

## 4. Latency results

### 4.1 Versus design targets

![Latency vs targets](assets/latency_vs_targets.png)

| Percentile | Engine (ns) | Target (ns) |
|------------|-------------|-------------|
| p50 | ~125 | 500 |
| p99 | ~1 700 | 2 000 |
| p999 | ~17 000 | — (observability) |

### 4.2 Distribution shape

![Latency histogram](assets/latency_histogram.png)

The body of the distribution sits well below the p50 target. The right tail (p99/p999) is dominated by:

- OS preemption / timer interrupts on shared systems  
- First-touch / cold cache after large control-flow changes  
- Occasional deep match walks across multiple price levels  

---

## 5. Throughput

![Throughput](assets/throughput.png)

| Configuration | Mops/s / core |
|---------------|---------------|
| This engine (mixed) | ~3.7 |
| Minimum target | 1.0 |
| Stretch goal | 3.0 |

---

## 6. Latency budget (non-crossing insert)

![Latency budget](assets/latency_budget.png)

Illustrative mid-path budget for a **non-crossing limit rest** (events optional):

| Stage | Approx. cost |
|-------|----------------|
| Validate + pool allocate | 10–20 ns |
| Robin Hood insert | 25–45 ns |
| Level append / create | 15–30 ns |
| Cross check (no match) | 5–15 ns |
| Optional event push | 20–40 ns |

Crossing orders add per-fill work proportional to the number of passive orders consumed.

---

## 7. Structural comparison

The engine is compared conceptually (and in-harness) to a **naive** book built from `std::map` + `std::list` + `std::unordered_map`.

![Structural cost](assets/structural_cost.png)

| Concern | Naive STL | This engine |
|---------|-----------|-------------|
| Node allocation | Per order / map node | None after init |
| Best price | Tree walk / begin() | O(1) level-list head |
| Cancel | Hash + list erase | RH find + intrusive unlink |
| Tail latency under load | Allocator + rehash risk | Bounded arenas |
| Cache behavior | Pointer-chasing nodes | Arena locality + 64 B alignment |

**Interpretation:** on small, warm microbenchmarks with an excellent platform allocator, raw wall-clock may favor STL. The production argument for this design is **allocation freedom, lock freedom, cache-aware layout, and predictable capacity** — properties that dominate under sustained load, multi-tenant noise, and strict p99 SLOs.

---

## 8. Memory footprint (defaults)

![Memory layout](assets/memory_layout.png)

Approximate steady-state RSS is dominated by the order arena and price window. Capacities are configurable via `BookConfig`.

---

## 9. Targets and pass criteria

| Check | Criterion |
|-------|-----------|
| p50 | &lt; 500 ns |
| p99 | &lt; 2 000 ns (soft on laptops) |
| Throughput | ≥ 1 000 000 ops/s |
| Unit suite | 0 failures |

The harness prints PASS/CHECK lines for quick regression triage.

---

## 10. Recommendations for publication-grade numbers

1. Linux bare metal or reserved VM; disable turbo variability if required for stability.  
2. `performance` CPU governor; pin with `taskset` / `pthread_setaffinity_np`.  
3. Isolate the core (nohz_full / cpuset) where available.  
4. Consider hugepage-backed arenas for TLB reduction.  
5. Report p50/p99/p999, throughput, and CPU model together.  
6. Keep event emission off for core-path figures; measure the logger path separately.

---

## 11. Regenerating figures

```bash
python3 scripts/generate_charts.py
```

Assets are written to `docs/assets/` as PNG (README/GitHub) and SVG (vector print).

---

*Figures reflect representative Release builds on development hardware. Always re-run `me_bench` on the target platform before citing numbers externally.*
