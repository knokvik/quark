# Atomic replace (v1.3)

Replace = cancel + new with the same ID reuse policy or new ID.
Must not release the matching thread between cancel and insert when atomicity
is required for risk checks.
