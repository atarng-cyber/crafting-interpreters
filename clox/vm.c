#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "vm.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include <string.h>
#include <stdarg.h>
#include <time.h>

VM vm;

static void runtimeError(const char* format, ...);

static CallFrame* currentFrame() {
  if (vm.frameCount == 0) return NULL;
  return &vm.frames[vm.frameCount - 1];
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prev = NULL;
  ObjUpvalue* cur = vm.openUpvalues;
  /* Keep list sorted by address (ascending). If an upvalue already exists for
     this slot, return it. Otherwise insert a new upvalue in the right place. */
  while (cur != NULL && cur->location > local) {
    prev = cur;
    cur = cur->next;
  }
  if (cur != NULL && cur->location == local) return cur;

  ObjUpvalue* upvalue = newUpvalue(local);
  upvalue->next = cur;
  if (prev == NULL) {
    vm.openUpvalues = upvalue;
  } else {
    prev->next = upvalue;
  }
  return upvalue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static bool call(ObjClosure* closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = &vm.stack[vm.stackTop - argCount - 1];
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, &vm.stack[vm.stackTop - argCount]);
        vm.stackTop = vm.stackTop - argCount - 1;
        push(result);
        return true;
      }
      default:
        break;
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}

static void resetStack() {
  vm.stackTop = 0;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = getLine(vm.chunk, (int)instruction);
  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
}

static Value peek(int distance) {
  return vm.stack[vm.stackTop - 1 - distance];
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  // Keep operands on the stack while we allocate so the GC can find them.
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  pop();
  pop();
  push(OBJ_VAL(result));
}

/* Native: clock() -> number of seconds since epoch (double) */
static Value native_clock(int argCount, Value* args) {
  (void)argCount; (void)args;
  double secs = (double)time(NULL);
  return NUMBER_VAL(secs);
}

void initVM() {
  resetStack();
  /* initVM: stack tracing removed for normal operation. */
  vm.objects = NULL;
  vm.chunk = NULL;
  vm.openUpvalues = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  vm.markValue = true;
  initTable(&vm.globals);
  initTable(&vm.strings);
  /* Register native functions */
  ObjString* name = copyString("clock", 5);
  /* Protect the new string and the native value while we register it. The
    allocation of the native or table resizing may trigger GC, and if the
    native hasn't been inserted into the globals table yet it would be
    unreachable. Push both so the collector treats them as roots. */
  push(OBJ_VAL(name));
  ObjNative* native = newNative(native_clock);
  push(OBJ_VAL(native));
  tableSet(&vm.globals, OBJ_VAL(name), OBJ_VAL(native));
  pop(); /* pop native */
  pop(); /* pop name */
}

void freeVM() {
  freeTable(&vm.globals);
  freeTable(&vm.strings);
  freeObjects();
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
  CallFrame* frame = NULL;
#define READ_BYTE() (frame ? *frame->ip++ : *vm.ip++)

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

#define READ_CONSTANT() (frame ? frame->closure->function->chunk.constants.values[READ_BYTE()] : vm.chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_SHORT() \
  (frame ? (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1])) : (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1])))
  /* Helper macro for binary ops that produce a Value via a wrapper macro. */
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  /* Refresh the active frame pointer each loop, if any. */
  frame = vm.frameCount > 0 ? &vm.frames[vm.frameCount - 1] : NULL;
  uint8_t instruction;
  switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
  Value constant = READ_CONSTANT();
  push(constant);
  break;
}
      case OP_CONSTANT_LONG: {
  uint32_t b1 = READ_BYTE();
  uint32_t b2 = READ_BYTE();
  uint32_t b3 = READ_BYTE();
  uint32_t constantIndex = b1 | (b2 << 8) | (b3 << 16);

  if (constantIndex >= (uint32_t)vm.chunk->constants.count) {
    runtimeError("Invalid long constant index %u.", constantIndex);
    return INTERPRET_RUNTIME_ERROR;
  }

  push(vm.chunk->constants.values[constantIndex]);
  break;
}
      case OP_NIL: push(NIL_VAL); break;
  case OP_TRUE: push(BOOL_VAL(true)); break;
  case OP_FALSE: push(BOOL_VAL(false)); break;
  case OP_POP: pop(); break;
      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }
      case OP_ADD: {
        if (IS_OBJ(peek(0)) && IS_OBJ(peek(1)) && IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          BINARY_OP(NUMBER_VAL, +);
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: {
        BINARY_OP(NUMBER_VAL, -);
        break;
      }
      case OP_MULTIPLY: {
        BINARY_OP(NUMBER_VAL, *);
        break;
      }
      case OP_DIVIDE: {
        BINARY_OP(NUMBER_VAL, /);
        break;
      }
      case OP_NEGATE: {
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_NOT: {
        push(BOOL_VAL(isFalsey(pop())));
        break;
      }
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER: {
        BINARY_OP(BOOL_VAL, >);
        break;
      }
      case OP_LESS: {
        BINARY_OP(BOOL_VAL, <);
        break;
      }
      case OP_PRINT: {
        Value value = pop();
        printValue(value);
        printf("\n");
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }
      case OP_DEFINE_GLOBAL_LONG: {
        uint32_t b1 = READ_BYTE();
        uint32_t b2 = READ_BYTE();
        uint32_t b3 = READ_BYTE();
        uint32_t index = b1 | (b2 << 8) | (b3 << 16);
        if (index >= (uint32_t)vm.chunk->constants.count) {
          runtimeError("Invalid global constant index %u.", index);
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* name = AS_STRING(vm.chunk->constants.values[index]);
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) vm.ip += offset;
        break;
      }
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        vm.ip += offset;
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_GET_GLOBAL_LONG: {
        uint32_t b1 = READ_BYTE();
        uint32_t b2 = READ_BYTE();
        uint32_t b3 = READ_BYTE();
        uint32_t index = b1 | (b2 << 8) | (b3 << 16);
        if (index >= (uint32_t)vm.chunk->constants.count) {
          runtimeError("Invalid global constant index %u.", index);
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* name = AS_STRING(vm.chunk->constants.values[index]);
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_GLOBAL_LONG: {
        uint32_t b1 = READ_BYTE();
        uint32_t b2 = READ_BYTE();
        uint32_t b3 = READ_BYTE();
        uint32_t index = b1 | (b2 << 8) | (b3 << 16);
        if (index >= (uint32_t)vm.chunk->constants.count) {
          runtimeError("Invalid global constant index %u.", index);
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjString* name = AS_STRING(vm.chunk->constants.values[index]);
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        break;
      }
      case OP_GET_LOCAL_LONG: {
        uint32_t b1 = READ_BYTE();
        uint32_t b2 = READ_BYTE();
        uint32_t b3 = READ_BYTE();
        uint32_t slot = b1 | (b2 << 8) | (b3 << 16);
        push(vm.stack[slot]);
        break;
      }
      case OP_SET_LOCAL_LONG: {
        uint32_t b1 = READ_BYTE();
        uint32_t b2 = READ_BYTE();
        uint32_t b3 = READ_BYTE();
        uint32_t slot = b1 | (b2 << 8) | (b3 << 16);
        vm.stack[slot] = peek(0);
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        if (frame) frame->ip -= offset; else vm.ip -= offset;
        break;
      }
      case OP_CALL: {
        uint8_t argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_CLOSURE: {
        Value constant = READ_CONSTANT();
        if (!IS_FUNCTION(constant)) {
          runtimeError("OP_CLOSURE expected function constant");
          return INTERPRET_RUNTIME_ERROR;
        }
        ObjFunction* function = AS_FUNCTION(constant);
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        /* Read upvalue descriptors emitted by the compiler and capture them. */
        for (int i = 0; i < function->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal) {
            /* Capture a local from the current frame's slots. */
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            /* Reference an upvalue from the current frame's closure. */
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        break;
      }
      case OP_GET_UPVALUE: {
        uint8_t index = READ_BYTE();
        ObjUpvalue* upvalue = frame->closure->upvalues[index];
        push(*upvalue->location);
        break;
      }
      case OP_SET_UPVALUE: {
        uint8_t index = READ_BYTE();
        ObjUpvalue* upvalue = frame->closure->upvalues[index];
        *upvalue->location = peek(0);
        break;
      }
      case OP_CLOSE_UPVALUE: {
        closeUpvalues(vm.stack + vm.stackTop - 1);
        pop();
        break;
      }
      case OP_RETURN: {
        Value result = pop();
        /* Close any upvalues referencing the frame's slots. */
        if (frame != NULL) closeUpvalues(frame->slots);

        vm.frameCount--;
        if (vm.frameCount == 0) {
          /* Top-level return — end interpretation. */
          pop(); /* remove the function/closure receiver */
          push(result);
          return INTERPRET_OK;
        }

        /* Restore caller stack top and push the result. */
        vm.stackTop = (int)(frame->slots - vm.stack);
        push(result);
        frame = vm.frameCount > 0 ? &vm.frames[vm.frameCount - 1] : NULL;
        break;
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

  // Clear vm.chunk before freeing it so the GC won't mark a freed array.
  vm.chunk = NULL;
  freeChunk(&chunk);
  return result;
}
