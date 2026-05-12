#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

/* Call frame limits and stack sizing */
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots; /* pointer into the VM stack where this frame's slots start */
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Chunk* chunk; /* current executing chunk (kept for script-level code) */
  uint8_t* ip;  /* instruction pointer into current chunk */

  Value stack[STACK_MAX];
  int stackTop; /* index of next free slot (0 when empty) */

  Obj* objects;
  Table globals;
  Table strings;

  ObjUpvalue* openUpvalues; /* linked list of open upvalues */
  size_t bytesAllocated;
  size_t nextGC;

  int grayCount;
  int grayCapacity;
  Obj** grayStack;
} VM;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

/* Stack operations */
void push(Value value);
Value pop();

#endif
