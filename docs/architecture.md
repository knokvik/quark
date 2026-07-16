# Architecture Overview

## 1. High-level view

![System architecture](assets/architecture_overview.png)

The engine is a **single-writer** matching core for one instrument. All mutation of book state occurs on one thread. Output is decoupled through a lock-free SPSC ring so logging never stalls matching under normal load (events may drop if the ring is full — matching never blocks).

---

## 2. Component diagram

```mermaid
flowchart TB
    subgraph Client["Client / Gateway Adapter"]
        API["insert / cancel API"]
    end

    subgraph ME["me::OrderBook"]
        VAL[Validation]
        POOL[OrderPool + LevelPool]
        RH[RobinHoodIndex]
        FLAT[Flat price window]
        OV[Overflow PriceMap]
        BL[Bid level list head]
        AL[Ask level list head]
        M[match]
        EV[SpscQueue EngineEvent]
    end

    API --> VAL
    VAL --> POOL
    VAL --> RH
    POOL --> BL
    POOL --> AL
    FLAT --> BL
    FLAT --> AL
    OV --> BL
    OV --> AL
    BL --> M
    AL --> M
    M --> RH
    M --> EV
    VAL --> EV
```

---

## 3. Bid / ask geometry

```mermaid
flowchart LR
    subgraph Bids["Bids - price DESC"]
        B1["100.50 head = best"] --> B2["100.25"] --> B3["100.00"]
        B1 --- Q1["FIFO: o1 - o2 - o3"]
    end

    subgraph Asks["Asks - price ASC"]
        A1["100.75 head = best"] --> A2["101.00"] --> A3["101.25"]
        A1 --- Q2["FIFO: o9 - o8"]
    end
```

- **Best bid** = head of bid level list  
- **Best ask** = head of ask level list  
- Cross when `best_bid >= best_ask` (or market ignores limit check)

---

## 4. Memory and ownership

![Memory layout](assets/memory_layout.png)

| Object | Owner | Lifetime |
|--------|--------|----------|
| `Order` | `OrderPool` | allocate on insert; free on fill/cancel/reject |
| `PriceLevel` | `LevelPool` | create on first rest at price; free when empty |
| Index entry | `RobinHoodIndex` | insert with order; erase on destroy |
| Event | `SpscQueue` | produced by book; consumed by `poll_events` |

---

## 5. Control flow — insert limit

```mermaid
sequenceDiagram
    participant C as Client
    participant B as OrderBook
    participant P as OrderPool
    participant I as RobinHoodIndex
    participant L as PriceLevel
    participant E as Event Ring

    C->>B: insert(id, side, Limit, px, qty)
    B->>B: validate
    B->>P: allocate()
    B->>I: insert(id, Order*)
    B->>E: Ack
    alt crosses opposite best
        B->>B: match() loop
        B->>E: Fill(s)
        alt residual and GTC
            B->>L: append FIFO
        else fully filled
            B->>P: deallocate
            B->>I: erase
        end
    else no cross
        B->>L: append FIFO
    end
    B-->>C: InsertResult
```

---

## 6. Concurrency model (MVP)

One thread owns one symbol book. **No mutexes and no atomics inside the book.**

```text
[Thread 0] ---> Symbol A book
[Thread 1] ---> Symbol B book
[Thread 2] ---> Symbol C book
```

v2 ingress: multiple RX threads → per-symbol SPSC → matching thread (see [spsc_ingress.md](spsc_ingress.md)).

---

## 7. Matching rules (normative)

| Rule | Specification |
|------|----------------|
| Priority | Price, then time (FIFO within price) |
| Trade price | Passive order price |
| Limit residual | Rest as GTC |
| Market residual | Do not rest; cancel remainder |
| Cancel complexity | O(1) expected (hash) + O(1) list unlink |

Lifecycle figure: ![Order lifecycle](assets/order_lifecycle.png)

---

## 8. Related documents

- [DESIGN.md](DESIGN.md) — full design specification  
- [PERFORMANCE.md](PERFORMANCE.md) — measurements and charts  
- [memory.md](memory.md) — arenas and free lists  
- [robin_hood.md](robin_hood.md) — ID index  
