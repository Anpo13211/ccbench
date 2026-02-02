# SS2PL Standalone C (YCSB, no Masstree)

This is a C-language standalone SS2PL YCSB benchmark extracted and simplified from CCBench. It uses a fixed-size in-memory array as the storage backend (no Masstree access). The focus is correctness of the 2PL concept, not performance.

## Build
```bash
mkdir -p build
cd build
cmake ..
make ycsb_ss2pl_standalone_c
```

## Run
```bash
./ycsb_ss2pl_standalone_c \
  --thread_num=8 \
  --extime=3 \
  --ycsb_tuple_num=1000000 \
  --ycsb_max_ope=10 \
  --ycsb_rratio=50 \
  --ycsb_zipf_skew=0
```

## Notes
- Fixed-size table: key is mapped directly to array index.
- Strict 2PL: read/write locks are held until commit/abort.
- INSERT/DELETE are supported within the fixed-size table range using a presence flag.
