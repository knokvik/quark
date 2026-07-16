# Documentation Index

Formal technical documentation for the **Mercury-style Limit Order Book Matching Engine**.

| Document | Description |
|----------|-------------|
| [DESIGN.md](DESIGN.md) | System design specification (architecture, algorithms, data structures) |
| [PERFORMANCE.md](PERFORMANCE.md) | Benchmark methodology, results, charts, and interpretation |
| [architecture.md](architecture.md) | Component diagram and data-flow overview |
| [memory.md](memory.md) | Arena allocation and zero-allocation strategy |
| [robin_hood.md](robin_hood.md) | Order-ID index design |
| [intrusive_lists.md](intrusive_lists.md) | FIFO price-level queues |
| [benchmarking.md](benchmarking.md) | How to reproduce measurements |
| [correctness.md](correctness.md) | Validation checklist |
| [api.md](api.md) | Public API sketch |
| [concurrency.md](concurrency.md) | Threading model |
| [event_model.md](event_model.md) | Async output events |
| [roadmap.md](roadmap.md) | Post-MVP extensions |
| [faq.md](faq.md) | Design FAQ |
| [interview.md](interview.md) | Concise talking points |

### Figures

All publication figures live under [`assets/`](assets/) (PNG + SVG).

Regenerate with:

```bash
python3 scripts/generate_charts.py
```
