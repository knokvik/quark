# Benchmarking Methodology

## 1. Objectives

Measure **per-operation latency** (p50 / p99 / p999) and **sustained throughput** for a mixed order stream on a single core, under Release compiler settings.

## 2. Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target me_bench -j
```

Recommended flags (applied in CMake for Release): `-O3 -march=native -DNDEBUG`.

## 3. Harness behaviour (`me_bench`)

| Parameter | Default |
|-----------|---------|
| Total operations | 500 000 |
| Warmup | 50 000 (untimed) |
| Workload mix | 60% limit, 20% cancel, 20% market |
| Price model | Normal(μ=$100, σ=$0.50), clipped |
| Quantity | Uniform 1…1000 |
| Events | Off during timed run (`enable_events = false`) |
| Clock | `std::chrono::steady_clock` |

A smaller stream is used for the naive STL baseline so wall time remains practical.

## 4. Platform hygiene

| Platform | Recommendation |
|----------|----------------|
| Linux | `taskset -c N`, `performance` governor, quiet machine |
| macOS | Limited affinity; treat p99/p999 as noisier |

Disable frequency scaling and background load when publishing numbers.

## 5. Reporting checklist

- [ ] CPU model and OS  
- [ ] Compiler version and flags  
- [ ] p50, p99, p999, mean, throughput  
- [ ] Workload definition  
- [ ] Whether events were enabled  

## 6. Figures

Charts in `docs/assets/` are generated from representative measurements:

```bash
python3 scripts/generate_charts.py
```

See [PERFORMANCE.md](PERFORMANCE.md) for the full report.
