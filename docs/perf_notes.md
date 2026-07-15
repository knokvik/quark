# Performance Notes

- Prefer resting non-crossing limits when measuring pure insert cost.
- Event emission is optional (`BookConfig::enable_events`) for fair core timing.
- p999 spikes often come from OS noise on shared laptops; use isolated cores for claims.
- Dense flat price window avoids TLB blow-ups from multi-hundred-MB pointer arrays.
