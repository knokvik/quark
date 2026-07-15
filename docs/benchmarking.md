# Benchmarking Notes

1. Build Release with `-O3 -march=native`.
2. Pin the process to an isolated core when possible (Linux `taskset`).
3. Disable frequency scaling for stable p99 (`performance` governor).
4. Warm up ≥50K operations before measuring.
5. Compare against the naive `std::map` book in the same harness.

On macOS, true CPU affinity is limited; treat p99 as noisier than on bare-metal Linux.
