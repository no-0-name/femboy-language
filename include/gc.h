#ifndef FEMBOY_GC_H
#define FEMBOY_GC_H

#include <stddef.h>
#include "object.h"
#include "value.h"

struct VM;

ObjString *gc_new_string(struct VM *vm, const char *chars, int length);

ObjString *gc_take_string(struct VM *vm, char *chars, int length);

ObjArray *gc_new_array(struct VM *vm, int cap_hint);

void gc_array_push(struct VM *vm, ObjArray *arr, Value elem);

ObjMap *gc_new_map(struct VM *vm, int cap_hint);

void gc_map_grow(struct VM *vm, ObjMap *m);

MapEntry *gc_map_find_slot(MapEntry *entries, int cap, const char *key, size_t len);

bool gc_map_key_is_tombstone(const char *key);

char *gc_map_tombstone(void);

void gc_account_bytes(struct VM *vm, long delta);

void gc_collect(struct VM *vm);

void gc_free_all(struct VM *vm);

#endif
