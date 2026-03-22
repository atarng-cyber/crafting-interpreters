#include <stdlib.h>
#include <stdio.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  /* Diagnostic: log realloc calls to track possible corruption. */
  fprintf(stderr, "reallocate: ptr=%p oldSize=%zu newSize=%zu\n",
          pointer, oldSize, newSize);

  void* result = realloc(pointer, newSize);
  if (result == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  return result;
}
