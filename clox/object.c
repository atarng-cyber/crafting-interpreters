#include <string.h>
#include <stdio.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->isMarked = false;
  object->next = vm.objects;
  vm.objects = object;
#ifdef DEBUG_LOG_GC
  printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
  return object;
}

static ObjString* allocateStringInternal(int length, uint32_t hash) {
  /* Allocate a single block big enough for the ObjString and its chars. */
  size_t size = sizeof(ObjString) + (size_t)length + 1;
  ObjString* string = (ObjString*)allocateObject(size, OBJ_STRING);
  string->length = length;
  string->ownsChars = false; /* chars are embedded in same allocation */
  string->hash = hash;
  string->chars = (char*)(string + 1);
  return string;
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)chars[i];
    hash *= 16777619;
  }
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) return interned;

  ObjString* string = allocateStringInternal(length, hash);
  memcpy(string->chars, chars, length);
  string->chars[length] = '\0';
    push(OBJ_VAL(string));
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
    pop();
  return string;
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)chars[i];
    hash *= 16777619;
  }
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }

  /* Claim ownership of the provided buffer. */
  ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
  string->length = length;
  string->ownsChars = true;
  string->hash = hash;
  string->chars = chars;
    push(OBJ_VAL(string));
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
    pop();
  return string;
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_FUNCTION: {
      ObjFunction* function = AS_FUNCTION(value);
      if (function->name == NULL) {
        printf("<script>");
      } else {
        printf("<fn %s>", function->name->chars);
      }
      break;
    }
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
    case OBJ_CLOSURE: {
      ObjClosure* closure = AS_CLOSURE(value);
      if (closure->function->name == NULL) {
        printf("<script>");
      } else {
        printf("<fn %s>", closure->function->name->chars);
      }
      break;
    }
    case OBJ_UPVALUE:
      printf("<upvalue>");
      break;
  }
}

ObjFunction* newFunction() {
  ObjFunction* function = (ObjFunction*)allocateObject(sizeof(ObjFunction), OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(NativeFn function) {
  ObjNative* native = (ObjNative*)allocateObject(sizeof(ObjNative), OBJ_NATIVE);
  native->function = function;
  return native;
}

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = (ObjUpvalue*)allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) upvalues[i] = NULL;
  ObjClosure* closure = (ObjClosure*)allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}
