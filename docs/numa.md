# NUMA (Linux production)

Allocate the order arena on the same NUMA node as the pinned matching core.
Use `numactl --membind` / `numa_alloc_onnode` when deploying multi-socket hosts.
