# SPSC ingress (v2.0)

Network threads push `OrderMessage` into a per-symbol SPSC queue.
The matching thread drains in a tight loop. Queue is the only synchronization
boundary — book internals stay single-threaded.
