#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lineStarts = NULL;
  chunk->lineCounts = NULL;
  chunk->linesCount = 0;
  initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lineStarts, chunk->capacity);
  FREE_ARRAY(int, chunk->lineCounts, chunk->capacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code,
        oldCapacity, chunk->capacity);
    chunk->lineStarts = GROW_ARRAY(int, chunk->lineStarts,
        oldCapacity, chunk->capacity);
    chunk->lineCounts = GROW_ARRAY(int, chunk->lineCounts,
        oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  // Append/extend run-length entries for lines
  if (chunk->linesCount == 0) {
    // first entry
    chunk->lineStarts[0] = line;
    chunk->lineCounts[0] = 1;
    chunk->linesCount = 1;
  } else {
    int last = chunk->linesCount - 1;
    if (chunk->lineStarts[last] == line) {
      chunk->lineCounts[last]++;
    } else {
      chunk->lineStarts[chunk->linesCount] = line;
      chunk->lineCounts[chunk->linesCount] = 1;
      chunk->linesCount++;
    }
  }
  chunk->count++;
  /* Debug logging removed. */
}

int addConstant(Chunk* chunk, Value value) {
  /* Ensure the value is on the stack so a GC triggered while growing the
     constants array won't collect the new object. */
  push(value);
  writeValueArray(&chunk->constants, value);
  pop();
  return chunk->constants.count - 1;
}

int getLine(Chunk* chunk, int instructionIndex) {
  int acc = 0;
  for (int i = 0; i < chunk->linesCount; i++) {
    int run = chunk->lineCounts[i];
    if (instructionIndex < acc + run) return chunk->lineStarts[i];
    acc += run;
  }
  return -1; // out of range
}

void writeConstant(Chunk* chunk, Value value, int line) {
  int index = addConstant(chunk, value);
  if (index <= 0xFF) {
    writeChunk(chunk, OP_CONSTANT, line);
    writeChunk(chunk, (uint8_t)index, line);
  } else {
    writeChunk(chunk, OP_CONSTANT_LONG, line);
    // write 3 bytes (24-bit) little-endian
    writeChunk(chunk, (uint8_t)(index & 0xFF), line);
    writeChunk(chunk, (uint8_t)((index >> 8) & 0xFF), line);
    writeChunk(chunk, (uint8_t)((index >> 16) & 0xFF), line);
  }
}
