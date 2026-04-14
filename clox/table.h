#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct ObjString ObjString;

typedef struct {
  Value key;
  Value value;
  uint8_t state; /* 0 = empty, 1 = occupied, 2 = tombstone */
} Entry;

typedef struct {
  int count;    /* number of occupied entries (state == 1) */
  int capacity;
  Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);

/* Generic key API: keys are arbitrary Values (string, number, bool, nil). */
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(Table* from, Table* to);

/* Helper that looks up a string by raw chars (used for interning).
   Returns an existing interned ObjString* or NULL. */
ObjString* tableFindString(Table* table, const char* chars,
                           int length, uint32_t hash);

#endif
