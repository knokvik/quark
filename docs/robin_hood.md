# Robin Hood Hashing for O(1) Order ID Lookup

Cancels and amends need **OrderID → Order\*** in expected O(1) with tight tail latency.

## Why not `std::unordered_map`?

- Bucket arrays + node allocations.
- Rehash spikes when load grows.
- Poor cache locality under open load.

## Robin Hood open addressing

A flat array of entries (power-of-two size):

```cpp
struct RobinHoodEntry {
    uint64_t key;   // 0 = empty
    uint64_t dist;  // probe sequence length
    Order*   value;
};
```

On collision, the entry with the **longer probe distance keeps the slot** (the “rich get richer” inverted — poor entries rob rich ones). This keeps probe lengths short and variance low.

## Deletion

Backward-shift deletion (no tombstones): when an entry is removed, subsequent cluster entries slide back so find() can still terminate on an empty slot / distance violation.

## Operational rules

- Load factor kept **&lt; 0.7**.
- Capacity fixed at construction — **never rehash on the hot path**.
- Keys are never zero (order IDs start at 1).
