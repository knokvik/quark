# Latency budget (MVP)

| Stage | Budget |
|-------|--------|
| Pool allocate | ~5–15 ns |
| ID index insert | ~20–40 ns |
| Level append | ~10–20 ns |
| Match (empty opposite) | branch only |
| Match (hit) | +fill bookkeeping |

End-to-end p50 target: &lt; 500 ns including validation.
