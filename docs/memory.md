# Memory Architecture

## 1. Goal

**Zero heap allocations on the hot path** after `OrderBook` construction. Resource exhaustion is reported as a reject, never as unbounded growth.

![Memory layout](assets/memory_layout.png)

## 2. Arenas

| Pool | Element | Default capacity | Role |
|------|---------|------------------|------|
| Orders | `alignas(64) Order` (64 B) | 2²⁰ | Resting and in-flight orders |
| Levels | `alignas(64) PriceLevel` (64 B) | 2¹⁶ | One node per active price per side |
| ID index | Robin Hood entries | 2²¹ slots | Cancel / lookup |
| Events | `EngineEvent` (64 B) | 2¹⁶ | SPSC output |
| Price flat window | `PriceLevel*` | ~2M ticks × 2 | O(1) level pointer |

## 3. Allocation policy

```text
allocate():
  if free_list non-empty → pop free_list
  else if bump_index < capacity → return &pool[bump_index++]
  else → nullptr   // circuit breaker
```

Cancelled and fully filled orders return to an **intrusive free list** (reuses the first pointer-sized word of the object).

## 4. Alignment and false sharing

Hot structs use `alignas(64)` so adjacent objects do not share a cache line. This matters when counters or multi-threaded adapters are introduced later; for the single-threaded MVP it still yields predictable object stride for the prefetcher.

## 5. Production note (Linux)

Backing the order arena with hugepages (`mmap` + `MAP_HUGETLB` or transparent huge pages) reduces TLB pressure for multi-tens-of-MB arenas. The portable default uses over-aligned `operator new[]` and zeroes pages at init.

## 6. Related

- [DESIGN.md](DESIGN.md) §6  
- [intrusive_lists.md](intrusive_lists.md)  
