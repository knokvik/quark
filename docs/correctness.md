# Correctness Checklist

- [x] FIFO within price level
- [x] Price-time priority across levels
- [x] Partial fills leave residual quantity
- [x] Non-crossing book stays crossed-free (bid < ask when both present)
- [x] Cancel missing ID is a soft error
- [x] Pool exhaustion rejects without crash
- [x] Randomized stream matches naive reference best bid/ask
