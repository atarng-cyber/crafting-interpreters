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

static ObjString* allocateStringInternal(int length) {
  /* Allocate a single block big enough for the ObjString and its chars. */
  size_t size = sizeof(ObjString) + (size_t)length + 1;
  ObjString* string = (ObjString*)allocateObject(size, OBJ_STRING);
  string->length = length;
  string->ownsChars = false; /* chars are embedded in same allocation */
  string->chars = (char*)(string + 1);
  return string;
}

ObjString* copyString(const char* chars, int length) {
  ObjString* string = allocateStringInternal(length);
  memcpy(string->chars, chars, length);
  string->chars[length] = '\0';
  return string;
}

ObjString* takeString(char* chars, int length) {
  /* Claim ownership of an externally allocated char array. We allocate a
   * minimal ObjString header (no embedded chars) and point into the
   * provided buffer. freeObject will free the chars separately. */
  ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
  string->length = length;
  string->ownsChars = true;
  string->chars = chars;
  return string;
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
  }
}
