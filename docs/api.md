# Public API sketch

```cpp
OrderBook book({.order_capacity = 1<<20, .enable_events = true});
auto r = book.insert(id, Side::Bid, OrderType::Limit, price_ticks, qty);
auto c = book.cancel(id);
book.poll_events(buf, n);
```

All hot-path methods are `noexcept`.
