# D2PL Standalone C (YCSB, no Masstree)

This is a C-language standalone D2PL YCSB benchmark extracted and simplified from CCBench. It uses a fixed-size in-memory array as the storage backend (no Masstree access). The focus is correctness of the 2PL concept, not performance.

## Build
```bash
mkdir -p build
cd build
cmake ..
make ycsb_d2pl_standalone_c
```

## Run
```bash
./ycsb_d2pl_standalone_c \
  --thread_num=8 \
  --extime=3 \
  --ycsb_tuple_num=1000000 \
  --ycsb_max_ope=10 \
  --ycsb_rratio=50 \
  --ycsb_zipf_skew=0
```

## Notes
- Fixed-size table: key is mapped directly to array index.
- D2PL style: all required locks are acquired up-front in key order.
- INSERT/DELETE are supported within the fixed-size table range using a presence flag.
