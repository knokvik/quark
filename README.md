# Mercury-Style Matching Engine

Sub-microsecond limit order book matching engine in modern C++20.

Built for HFT-style constraints: **zero allocations** and **zero locks** on the hot path, cache-line-aligned orders, Robin Hood ID lookup, intrusive FIFO price levels, and a lock-free SPSC event ring.

## Performance

Measured on Apple Silicon (Release, `-O3 -march=native`), mixed workload
(60% limit / 20% cancel / 20% market, 500K ops, 50K warmup, events off):

| Metric | This engine | Target |
|--------|-------------|--------|
| **p50 latency** | ~125 ns | &lt; 500 ns |
| **p99 latency** | ~1.7 μs | &lt; 2 μs |
| **Throughput** | ~3.7 M ops/s | ≥ 1 M ops/s |
| **Hot-path allocs** | 0 | 0 |
| **Hot-path locks** | 0 | 0 |

Numbers vary by CPU, governor, and thermal state. Run `./build/me_bench` locally.

## Architecture

- **Order pool** — pre-allocated arena + free list (`alignas(64)` orders)
- **Price levels** — intrusive doubly-linked FIFO queues per price
- **Best price** — intrusive sorted level lists (O(1) best bid/ask)
- **Price lookup** — dense flat window (O(1)) + overflow Robin Hood map
- **ID index** — Robin Hood open-addressing hash (no rehash on hot path)
- **Output** — lock-free SPSC ring of `EngineEvent`s

See [docs/architecture.md](docs/architecture.md).

## Quick start

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/me_tests
./build/me_bench
```

## Project layout

```
matching-engine/
├── include/me/          # headers (order, pool, robin_hood, order_book, ...)
├── src/order_book.cpp   # matching core
├── tests/unit_tests.cpp # correctness + vs naive reference
├── bench/benchmark.cpp  # latency / throughput harness
├── docs/                # design notes
└── CMakeLists.txt
```

## Testing

- FIFO within price level, price-time priority, partial fills
- Cross prevention, cancel missing, pool exhaustion, market orders
- Randomized stream checked against a naive `std::map` + `std::list` book
- 10K+ assertions in the default unit run

```bash
./build/me_tests
# Passed: N  Failed: 0
```

## Design notes

- [Intrusive lists vs `std::list`](docs/intrusive_lists.md)
- [Robin Hood ID index](docs/robin_hood.md)
- [Memory / arena strategy](docs/memory.md)
- [Architecture overview](docs/architecture.md)

## Interview sound-bite

> Single-threaded C++20 LOB: arena-allocated orders, intrusive FIFO levels,
> Robin Hood ID map, O(1) best price via sorted level lists. p50 ~100–300 ns
> on Apple Silicon with multi-million ops/s throughput and zero hot-path heap.

## License

MIT

## Build tips

Use Ninja + Release for representative numbers.
