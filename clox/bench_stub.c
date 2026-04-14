#include <stdlib.h>
#include <stdio.h>
#include "vm.h"
#include "memory.h"

VM vm; /* simple global used by object allocation in tests */

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }
  void* result = realloc(pointer, newSize);
  if (result == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  return result;
}
