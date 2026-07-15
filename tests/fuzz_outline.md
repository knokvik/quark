# Fuzz testing outline

Future `libFuzzer` target should:

1. Decode a byte stream into New/Cancel/Market ops.
2. Apply ops to both `OrderBook` and `NaiveBook`.
3. Assert equal best bid/ask and fill aggregates.
4. Run under ASAN/UBSAN in CI weekly jobs.
