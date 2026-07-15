# Interview Talking Points

- Arena + free list eliminates allocator jitter.
- Intrusive FIFO gives true price-time priority without `std::list` nodes.
- Robin Hood keeps cancel latency flat under load.
- Sorted level lists give O(1) best bid/ask without scanning a price array.
- Always validate against a slow reference implementation.
