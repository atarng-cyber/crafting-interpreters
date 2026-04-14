#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

static uint32_t hashValue(Value key) {
  if (IS_OBJ(key)) {
    ObjString* string = AS_STRING(key);
    return string->hash;
  }

  switch (key.type) {
    case VAL_BOOL:
      return AS_BOOL(key) ? 3u : 5u;
    case VAL_NIL:
      return 7u;
    case VAL_NUMBER: {
      /* Hash the double's bit pattern. */
      union { uint64_t bits; double num; } u;
      u.num = AS_NUMBER(key);
      uint32_t high = (uint32_t)(u.bits >> 32);
      uint32_t low = (uint32_t)(u.bits & 0xFFFFFFFFu);
      return high ^ low;
    }
    default:
      return 0;
  }
}

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
  uint32_t index = hashValue(key) % (uint32_t)capacity;
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];
    if (entry->state == 0) {
      return tombstone != NULL ? tombstone : entry;
    } else if (entry->state == 2) {
      if (tombstone == NULL) tombstone = entry;
    } else if (valuesEqual(entry->key, key)) {
      return entry;
    }

    index = (index + 1) % capacity;
  }
}

static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].state = 0;
    entries[i].key = NIL_VAL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->state != 1) continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    dest->state = 1;
    table->count++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(Table* table, Value key, Value value) {
  if (table->count + 1 > (int)(table->capacity * TABLE_MAX_LOAD)) {
    int capacity = GROW_CAPACITY(table->capacity);
    if (capacity < 8) capacity = 8;
    adjustCapacity(table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = (entry->state == 0);
  if (isNewKey) table->count++;

  entry->key = key;
  entry->value = value;
  entry->state = 1;
  return isNewKey;
}

bool tableGet(Table* table, Value key, Value* value) {
  if (table->count == 0) return false;
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->state != 1) return false;
  *value = entry->value;
  return true;
}

bool tableDelete(Table* table, Value key) {
  if (table->count == 0) return false;
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->state != 1) return false;
  entry->state = 2; /* tombstone */
  entry->key = NIL_VAL;
  entry->value = BOOL_VAL(true);
  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->state == 1) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars,
                           int length, uint32_t hash) {
  if (table->count == 0) return NULL;
  uint32_t index = hash % (uint32_t)table->capacity;
  for (;;) {
    Entry* entry = &table->entries[index];
    if (entry->state == 0) return NULL;
    if (entry->state == 1 && IS_STRING(entry->key)) {
      ObjString* s = AS_STRING(entry->key);
      if (s->length == length && s->hash == hash &&
          memcmp(s->chars, chars, (size_t)length) == 0) {
        return s;
      }
    }
    index = (index + 1) % table->capacity;
  }
}
