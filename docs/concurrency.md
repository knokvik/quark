# Concurrency Model

MVP: one matching thread per symbol. Ingress may be multi-threaded only if
orders are funneled through a per-symbol SPSC queue (v2).

Do not share an `OrderBook` across threads without external serialization.
