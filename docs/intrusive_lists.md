# Why Intrusive Lists Beat `std::list`

## The problem with `std::list`

Every `std::list` node is a separately heap-allocated object:

```
[list node] → heap block A  (prev, next, Order payload)
[list node] → heap block B
[list node] → heap block C
```

Consequences on the matching hot path:

1. **Allocator traffic** — `new`/`delete` per order rest/cancel; locks + syscalls under load.
2. **Pointer chasing** — nodes are not contiguous; hardware prefetchers cannot stream the FIFO.
3. **Extra cache lines** — list node headers sit apart from order fields you actually need.

## Intrusive list

The `Order` struct *is* the node:

```cpp
struct alignas(64) Order {
    // ... payload ...
    Order* next;
    Order* prev;
    PriceLevel* level;
};
```

Orders live in a pre-allocated pool (`Order pool_[1 << 20]`). Resting an order is:

1. Pop from free-list / bump allocator — O(1), no syscall.
2. Splice into `PriceLevel` head/tail — O(1) pointer writes.
3. Matching walks `head → next → next` within the same arena.

## FIFO price-time priority

Within a price level, the head is always the oldest resting order. Matches consume from the head; cancels splice out via `prev`/`next` and the `level` back-pointer in O(1) without searching the queue.
