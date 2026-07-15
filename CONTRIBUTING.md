# Contributing

1. Keep the hot path allocation-free.
2. No locks inside `OrderBook` methods.
3. Prefer fixed-point prices (`uint64_t`) over floating point.
4. Add a unit test for every matching rule change.
5. Run `./scripts/run_release.sh` before opening a PR.

Keep commit volume moderate per day for a readable history.
