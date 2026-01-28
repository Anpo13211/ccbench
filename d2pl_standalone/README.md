# D2PL Standalone (YCSB, no Masstree)

This is a standalone D2PL YCSB benchmark extracted from CCBench. It uses an in-memory array as the storage backend (no Masstree access). All required source files live under this directory.

## Build
```bash
mkdir -p build
cd build
cmake ..
make ycsb_d2pl_standalone.exe
```

## Standalone Build (d2pl_standalone only)
```bash
mkdir -p build-standalone
cd build-standalone
cmake ../d2pl_standalone
make ycsb_d2pl_standalone.exe
```

## Root Build (optional, if you keep the repo root)
```bash
mkdir -p build
cd build
cmake .. -DBUILD_ONLY_D2PL_STANDALONE=ON
make ycsb_d2pl_standalone.exe
```

## Run
```bash
./ycsb_d2pl_standalone.exe \
  --thread_num=8 \
  --extime=3 \
  --ycsb_tuple_num=1000000 \
  --ycsb_max_ope=10 \
  --ycsb_rratio=50 \
  --ycsb_zipf_skew=0
```

## Notes
- The table is preallocated as a contiguous array of `Tuple` and accessed via key → index.
- `FLAGS_tuple_num/rratio/max_ope/rmw/zipf_skew` are synchronized to the corresponding `FLAGS_ycsb_*` values at runtime for logging compatibility.
- INSERT/DELETE are supported within the fixed-size table range using a per-tuple presence flag.
