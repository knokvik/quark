# Architecture

```
Order Entry (single-threaded per symbol)
        │
        ▼
 Robin Hood ID Index  ──► Order* in arena
        │
   ┌────┴────┐
   ▼         ▼
 BID levels  ASK levels
 (sorted DESC list + flat price window)
   │         │
   └────┬────┘
        ▼
   Match @ passive price (FIFO within level)
        │
        ▼
   SPSC event ring → async logger / gateway
```

## Concurrency model (MVP)

One thread owns one symbol book. No mutexes, no atomics inside the book.
Optional v2: SPSC ingress queues from network threads into the matching core.

## Matching rules

- **Price-time priority** — better price first; FIFO within a price level.
- **Passive pricing** — trade price is the resting order’s price.
- **GTC limits** rest residual quantity; **market** orders never rest (residual cancelled).
- **Cancel** is O(1) via ID index + intrusive list unlink.
