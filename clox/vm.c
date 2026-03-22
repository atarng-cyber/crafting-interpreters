#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "vm.h"
#include "debug.h"

VM vm;

static void resetStack() {
  vm.stackTop = 0;
}

void initVM() {
  resetStack();
  /* initVM: stack tracing removed for normal operation. */
}

void freeVM() {
  /* Nothing to free currently. */
}

void push(Value value) {
  if (vm.stackTop < 0 || vm.stackTop >= STACK_MAX) {
    fprintf(stderr, "Invalid stackTop index %d (0..%d)\n", vm.stackTop, STACK_MAX - 1);
    exit(1);
  }
  vm.stack[vm.stackTop] = value;
  vm.stackTop++;
}

Value pop() {
  if (vm.stackTop <= 0) {
    fprintf(stderr, "Stack underflow.\n");
    exit(1);
  }

  vm.stackTop--;
  return vm.stack[vm.stackTop];
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)

#ifdef DEBUG_TRACE_EXECUTION
  /* When tracing, print the current stack contents first. */
  printf("TRACE\n");
#endif

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (int i = 0; i < vm.stackTop; i++) {
      printf("[ ");
      printValue(vm.stack[i]);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
  uint8_t index = READ_BYTE();
  if (index >= vm.chunk->constants.count) {
    fprintf(stderr, "Invalid constant index %u.\n", index);
    return INTERPRET_RUNTIME_ERROR;
  }
  push(vm.chunk->constants.values[index]);
  break;
}
      case OP_CONSTANT_LONG: {
  uint32_t b1 = READ_BYTE();
  uint32_t b2 = READ_BYTE();
  uint32_t b3 = READ_BYTE();
  uint32_t constantIndex = b1 | (b2 << 8) | (b3 << 16);

  if (constantIndex >= (uint32_t)vm.chunk->constants.count) {
    fprintf(stderr, "Invalid long constant index %u.\n", constantIndex);
    return INTERPRET_RUNTIME_ERROR;
  }

  push(vm.chunk->constants.values[constantIndex]);
  break;
}
      case OP_ADD: {
        double b = pop();
        double a = pop();
        push(a + b);
        break;
      }
      case OP_SUBTRACT: {
        double b = pop();
        double a = pop();
        push(a - b);
        break;
      }
      case OP_MULTIPLY: {
        double b = pop();
        double a = pop();
        push(a * b);
        break;
      }
      case OP_DIVIDE: {
        double b = pop();
        double a = pop();
        push(a / b);
        break;
      }
      case OP_NEGATE: {
        push(-pop());
        break;
      }
      case OP_PRINT: {
        Value value = pop();
        printValue(value);
        printf("\n");
        break;
      }
      case OP_RETURN: {
        return INTERPRET_OK;
      }
      default: {
        printf("Unknown opcode %d\n", instruction);
        return INTERPRET_RUNTIME_ERROR;
      }
    }
  }

#undef READ_BYTE
}

/* At this stage of the book we accept source code and hand off to the
 * compiler/scanner pipeline. The compiler will (eventually) emit bytecode
 * into the current chunk and drive the VM. For now we just invoke the
 * compiler entry point.
 */
#include "compiler.h"

InterpretResult interpret(const char* source) {
  Chunk chunk;
  initChunk(&chunk);

  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;

  InterpretResult result = run();

  freeChunk(&chunk);
  return result;
}
