#include <stdlib.h>
#include <stdio.h>

#include "memory.h"
#include "vm.h"
#include "object.h"
#include "compiler.h"
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

/* Forward declaration of helper to free a single object. */
static void freeObject(Obj* object);

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;

#ifdef DEBUG_STRESS_GC
  if (newSize > oldSize) collectGarbage();
#endif

  if (newSize > oldSize && vm.bytesAllocated > vm.nextGC) {
    collectGarbage();
  }

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

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  free(vm.grayStack);
}

static void freeObject(Obj* object) {
  (void)object;
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      if (string->ownsChars) {
        FREE_ARRAY(char, string->chars, string->length + 1);
      }
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
  }
}

/* MARK PHASE */
void markObject(Obj* object) {
  if (object == NULL) return;
  if (object->isMarked == vm.markValue) return;
#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  object->isMarked = vm.markValue;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
    if (vm.grayStack == NULL) exit(1);
  }
  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) markValue(array->values[i]);
}

static void markRoots() {
  for (Value* slot = vm.stack; slot < vm.stack + vm.stackTop; slot++) {
    markValue(*slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }

  for (ObjUpvalue* up = vm.openUpvalues; up != NULL; up = up->next) {
    markObject((Obj*)up);
  }

  // Script-level code runs out of vm.chunk (not wrapped in an ObjFunction here),
  // so its constants aren't reachable via any closure on the frame stack.
  if (vm.chunk != NULL) markArray(&vm.chunk->constants);

  markTable(&vm.globals);
  markCompilerRoots();
}

/* TABLE helpers */
void markTable(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->state == 1) {
      markValue(entry->key);
      markValue(entry->value);
    }
  }
}

/* TRACE phase */
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) markObject((Obj*)closure->upvalues[i]);
      break;
    }
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

/* SWEEP phase */
void tableRemoveWhite(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->state == 1 && IS_STRING(entry->key) &&
        AS_STRING(entry->key)->obj.isMarked != vm.markValue) {
      tableDelete(table, entry->key);
    }
  }
}

static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while (object != NULL) {
    if (object->isMarked == vm.markValue) {
      // Alive — leave its mark alone. After this cycle we'll flip vm.markValue
      // so this object's mark automatically becomes "stale" next cycle.
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }
      freeObject(unreached);
    }
  }
}

/* Collect garbage: mark, trace, remove white strings, sweep, and update nextGC */
void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif

  size_t before = vm.bytesAllocated;

  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();

  // Flip the mark sentinel: every surviving object's mark now means "stale"
  // for the next cycle, so we never had to walk the heap to clear marks.
  vm.markValue = !vm.markValue;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
}
