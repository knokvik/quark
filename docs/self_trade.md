# Self-trade prevention (future)

STP modes (cancel newest / cancel oldest / decrement) are intentionally out of
MVP scope. Hook point: before emitting a fill, compare owner IDs if present.
