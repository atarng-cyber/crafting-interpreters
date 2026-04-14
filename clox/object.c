#include <string.h>
#include <stdio.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  vm.objects = object;
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
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
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
    tableSet(&vm.strings, OBJ_VAL(string), NIL_VAL);
  return string;
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}
