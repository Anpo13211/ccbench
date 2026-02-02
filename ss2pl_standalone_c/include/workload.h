#pragma once

#include <stdint.h>

typedef enum OpType {
  OP_READ = 0,
  OP_WRITE = 1,
  OP_RMW = 2
} OpType;

typedef struct Op {
  OpType type;
  uint64_t key;
} Op;
