# Recommended flags

```
-O3 -march=native -DNDEBUG
```

Optional: `-fno-exceptions -fno-rtti` if integrating into freestanding hot paths.
LTO via `ME_ENABLE_LTO=ON` when the toolchain supports IPO.
