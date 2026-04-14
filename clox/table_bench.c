#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "table.h"
#include "value.h"
#include "vm.h"
#include "object.h"

static double now_seconds() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void bench_numbers(int N) {
  Table table;
  initTable(&table);

  double t0 = now_seconds();
  for (int i = 0; i < N; i++) {
    Value key = NUMBER_VAL((double)i);
    Value val = NUMBER_VAL((double)i * 2.0);
    tableSet(&table, key, val);
  }
  double t1 = now_seconds();

  // lookups
  Value tmp;
  double t2 = now_seconds();
  for (int i = 0; i < N; i++) {
    Value key = NUMBER_VAL((double)i);
    if (!tableGet(&table, key, &tmp)) {
      printf("lookup failed for %d\n", i);
      break;
    }
  }
  double t3 = now_seconds();

  printf("numbers N=%d insert=%.6fs lookup=%.6fs\n", N, t1 - t0, t3 - t2);
  freeTable(&table);
}

static ObjString* make_raw_string(const char* base, int n) {
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "%s-%d", base, n);
  uint32_t hash = 2166136261u;
  for (int i = 0; i < len; i++) {
    hash ^= (uint8_t)buf[i];
    hash *= 16777619;
  }
  size_t size = sizeof(ObjString) + (size_t)len + 1;
  ObjString* s = (ObjString*)malloc(size);
  s->obj.type = OBJ_STRING;
  s->obj.next = NULL;
  s->length = len;
  s->ownsChars = false;
  s->hash = hash;
  s->chars = (char*)(s + 1);
  memcpy(s->chars, buf, len);
  s->chars[len] = '\0';
  return s;
}

static void bench_strings(int N) {
  Table table;
  initTable(&table);
  ObjString** keys = (ObjString**)malloc(sizeof(ObjString*) * N);

  double t0 = now_seconds();
  for (int i = 0; i < N; i++) {
    ObjString* s = make_raw_string("key", i);
    keys[i] = s;
    tableSet(&table, OBJ_VAL(s), NUMBER_VAL((double)i));
  }
  double t1 = now_seconds();

  Value tmp;
  double t2 = now_seconds();
  for (int i = 0; i < N; i++) {
    if (!tableGet(&table, OBJ_VAL(keys[i]), &tmp)) {
      printf("string lookup failed %d\n", i);
      break;
    }
  }
  double t3 = now_seconds();

  printf("strings N=%d insert=%.6fs lookup=%.6fs\n", N, t1 - t0, t3 - t2);

  for (int i = 0; i < N; i++) free(keys[i]);
  free(keys);
  freeTable(&table);
}

static void bench_mixed(int N) {
  Table table;
  initTable(&table);
  int stringCapacity = N / 4 + 1;
  ObjString** str_keys = (ObjString**)malloc(sizeof(ObjString*) * stringCapacity);
  int str_count = 0;

  double t0 = now_seconds();
  for (int i = 0; i < N; i++) {
    Value key;
    switch (i % 4) {
      case 0: key = NUMBER_VAL((double)i); break;
      case 1: key = BOOL_VAL(i % 2 == 0); break;
      case 2: key = NIL_VAL; break;
      default: {
        ObjString* s = make_raw_string("k", i);
        str_keys[str_count++] = s;
        key = OBJ_VAL(s);
        break;
      }
    }
    tableSet(&table, key, NUMBER_VAL((double)i));
  }
  double t1 = now_seconds();

  Value tmp;
  double t2 = now_seconds();
  int sidx = 0;
  for (int i = 0; i < N; i++) {
    Value key;
    switch (i % 4) {
      case 0: key = NUMBER_VAL((double)i); break;
      case 1: key = BOOL_VAL(i % 2 == 0); break;
      case 2: key = NIL_VAL; break;
      default: {
        key = OBJ_VAL(str_keys[sidx++]);
        break;
      }
    }
    if (!tableGet(&table, key, &tmp)) {
      printf("mixed lookup failed %d\n", i);
      break;
    }
  }
  double t3 = now_seconds();

  printf("mixed N=%d insert=%.6fs lookup=%.6fs\n", N, t1 - t0, t3 - t2);

  for (int i = 0; i < str_count; i++) free(str_keys[i]);
  free(str_keys);
  freeTable(&table);
}

int main(int argc, char** argv) {
  int N = 200000;
  if (argc > 1) N = atoi(argv[1]);

  printf("Table benchmark (N=%d)\n", N);
  bench_numbers(N);
  bench_strings(N/10); // fewer strings to save time
  bench_mixed(N);
  return 0;
}
