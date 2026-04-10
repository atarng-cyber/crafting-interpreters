#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_MAX 4096

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value stack[STACK_MAX];
  int stackTop; /* index of next free slot (0 when empty) */
  struct Obj* objects;
} VM;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

/* Stack operations */
void push(Value value);
Value pop();

#endif
