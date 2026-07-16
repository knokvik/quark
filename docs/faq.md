# FAQ

**Why single-threaded?**  
Lowest latency; shard by symbol instead of locking one book.

**Why not `double` prices?**  
Non-determinism and slower compares; fixed-point is standard in HFT.

**Why events can drop?**  
Matching never blocks on a full ring; size the ring for peak fill rate.
