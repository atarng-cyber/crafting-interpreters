#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  initVM();
  Chunk chunk;
  initChunk(&chunk);

  // Test writeConstant (uses OP_CONSTANT or OP_CONSTANT_LONG as needed)
  writeConstant(&chunk, 1.2, 123);
  // add many constants to force a long constant index (for test)
  for (int i = 0; i < 300; i++) {
    writeConstant(&chunk, (double)i + 2.0, 124);
  }

  writeChunk(&chunk, OP_RETURN, 125);

  /* Interpret the chunk using the VM. */
  interpret(&chunk);

  freeChunk(&chunk);
  freeVM();
  return 0;
}
