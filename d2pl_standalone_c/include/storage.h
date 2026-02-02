#pragma once

#include <stdatomic.h>
#include <stdint.h>

#include "lock.h"

#ifndef VAL_SIZE
#define VAL_SIZE 4
#endif

typedef struct Tuple {
  RWLock lock;
  atomic_bool present;
  uint64_t id;
  char val[VAL_SIZE];
} Tuple;

extern Tuple *Table;
