# Memory Strategy

## Goal

**Zero heap allocations on the hot path** after `OrderBook` construction.

## Arenas

| Pool | Element | Default capacity | Notes |
|------|---------|------------------|-------|
| Orders | `alignas(64) Order` (64 B) | 1M | Bump + free-list |
| Levels | `alignas(64) PriceLevel` (64 B) | 65K | Bump + free-list |
| ID index | Robin Hood entries | 2M slots | Flat array |
| Events | `EngineEvent` (64 B) | 64K | SPSC ring |
| Price flat window | `PriceLevel*` | ~2M ticks | O(1) lookup ≤ $200 |

## Free list

Cancelled / fully-filled orders return to an intrusive free list (reuses the first pointer-sized word of the object). Next allocate prefers free-list over bump index.

## Hugepages (Linux)

Production deployments should back the order arena with `mmap(..., MAP_HUGETLB)` (2 MB pages) to cut TLB misses. macOS lacks transparent hugepage control; the portable path uses over-aligned `operator new[]` and page touch at init.

## Circuit breaker

If the order pool is exhausted, `insert` returns `RejectReason::PoolExhausted` instead of growing — predictable failure over unbounded latency.
