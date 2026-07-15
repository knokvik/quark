# Stress scenarios

1. 10M mixed ops with 50% cancels — free-list accounting must balance.
2. Burst 1M inserts against warm book — watch p99/p999.
3. Pool capacity 1024 — verify reject path, no crash.
