#include <stdlib.h>
#include <stdio.h>

#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  /* Diagnostic logging removed for production. */

  void* result = realloc(pointer, newSize);
  if (result == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }
  return result;
}
