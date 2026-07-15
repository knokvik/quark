# Price Encoding

Prices are fixed-point integers: `price_ticks = dollars * 10_000`.

Example: `$100.25` → `1_002_500`.

Benefits: deterministic comparisons, no FPU rounding, integer-only hot path.
