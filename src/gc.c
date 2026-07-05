#include "gc.h"
#include "vm.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GC_INITIAL_THRESHOLD (1024 * 64)
#define GC_GROWTH_FACTOR 2

static bool gc_logging_enabled(void) {
    static int cached = -1;
    if (cached == -1) {
        const char *v = getenv("FEMBOY_GC_LOG");
        cached = (v && v[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

static void register_object(VM *vm, FemboyObj *obj, size_t size) {
    obj->next = vm->objects;
    vm->objects = obj;
    vm->bytes_allocated += size;
}

static void maybe_collect(VM *vm) {
    if (vm->bytes_allocated > vm->gc_threshold) {
        gc_collect(vm);

        vm->gc_threshold = vm->bytes_allocated > 0
            ? vm->bytes_allocated * GC_GROWTH_FACTOR
            : GC_INITIAL_THRESHOLD;
        if (vm->gc_threshold < GC_INITIAL_THRESHOLD) vm->gc_threshold = GC_INITIAL_THRESHOLD;
    }
}

ObjString *gc_new_string(VM *vm, const char *chars, int length) {
    maybe_collect(vm);

    ObjString *s = femboy_malloc(sizeof(ObjString));
    s->obj.type = OBJ_STRING;
    s->obj.marked = false;
    s->length = length;
    s->chars = femboy_malloc((size_t)length + 1);
    memcpy(s->chars, chars, (size_t)length);
    s->chars[length] = '\0';

    register_object(vm, (FemboyObj *)s, sizeof(ObjString) + (size_t)length + 1);
    return s;
}

ObjString *gc_take_string(VM *vm, char *chars, int length) {
    maybe_collect(vm);

    ObjString *s = femboy_malloc(sizeof(ObjString));
    s->obj.type = OBJ_STRING;
    s->obj.marked = false;
    s->length = length;
    s->chars = chars;
    s->chars[length] = '\0';

    register_object(vm, (FemboyObj *)s, sizeof(ObjString) + (size_t)length + 1);
    return s;
}

ObjArray *gc_new_array(VM *vm, int cap_hint) {
    maybe_collect(vm);

    ObjArray *a = femboy_malloc(sizeof(ObjArray));
    a->obj.type = OBJ_ARRAY;
    a->obj.marked = false;
    a->count = 0;
    a->cap = cap_hint > 0 ? cap_hint : 4;
    a->items = femboy_malloc(sizeof(Value) * (size_t)a->cap);

    register_object(vm, (FemboyObj *)a, sizeof(ObjArray) + sizeof(Value) * (size_t)a->cap);
    return a;
}

void gc_array_push(VM *vm, ObjArray *arr, Value elem) {
    if (arr->count >= arr->cap) {
        size_t old_bytes = sizeof(Value) * (size_t)arr->cap;
        arr->cap = arr->cap ? arr->cap * 2 : 4;
        arr->items = femboy_realloc(arr->items, sizeof(Value) * (size_t)arr->cap);
        vm->bytes_allocated += sizeof(Value) * (size_t)arr->cap - old_bytes;
    }
    arr->items[arr->count++] = elem;
}

ObjMap *gc_new_map(VM *vm, int cap_hint) {
    maybe_collect(vm);

    ObjMap *m = femboy_malloc(sizeof(ObjMap));
    m->obj.type = OBJ_MAP;
    m->obj.marked = false;
    m->count = 0;
    m->cap = 0;
    m->entries = NULL;

    (void)cap_hint;

    register_object(vm, (FemboyObj *)m, sizeof(ObjMap));
    return m;
}

static char tombstone_marker;
#define TOMBSTONE ((char *)&tombstone_marker)

bool gc_map_key_is_tombstone(const char *key) {
    return key == TOMBSTONE;
}

char *gc_map_tombstone(void) {
    return TOMBSTONE;
}

void gc_account_bytes(VM *vm, long delta) {
    if (delta >= 0) {
        vm->bytes_allocated += (size_t)delta;
    } else {
        size_t dec = (size_t)(-delta);
        vm->bytes_allocated = vm->bytes_allocated > dec ? vm->bytes_allocated - dec : 0;
    }
}

#define MAP_MIN_CAP 8
#define MAP_MAX_LOAD 0.75

static uint32_t hash_string(const char *s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

MapEntry *gc_map_find_slot(MapEntry *entries, int cap, const char *key, size_t len) {
    if (cap == 0) return NULL;
    uint32_t h = hash_string(key, len);
    uint32_t mask = (uint32_t)cap - 1;
    uint32_t idx = h & mask;
    MapEntry *first_tombstone = NULL;

    for (;;) {
        MapEntry *e = &entries[idx];
        if (e->key == NULL) {
            return first_tombstone ? first_tombstone : e;
        }
        if (e->key == TOMBSTONE) {
            if (!first_tombstone) first_tombstone = e;
        } else if (strlen(e->key) == len && memcmp(e->key, key, len) == 0) {
            return e;
        }
        idx = (idx + 1) & mask;

    }
}

void gc_map_grow(VM *vm, ObjMap *m) {
    int old_cap = m->cap;
    MapEntry *old_entries = m->entries;

    int new_cap = old_cap == 0 ? MAP_MIN_CAP : old_cap * 2;
    MapEntry *new_entries = femboy_malloc(sizeof(MapEntry) * (size_t)new_cap);
    for (int i = 0; i < new_cap; i++) { new_entries[i].key = NULL; new_entries[i].value = value_nil(); }

    int live_count = 0;
    for (int i = 0; i < old_cap; i++) {
        if (old_entries[i].key != NULL && old_entries[i].key != TOMBSTONE) {
            size_t len = strlen(old_entries[i].key);
            MapEntry *slot = gc_map_find_slot(new_entries, new_cap, old_entries[i].key, len);
            slot->key = old_entries[i].key;
            slot->value = old_entries[i].value;
            live_count++;
        }
    }

    free(old_entries);
    m->entries = new_entries;
    m->cap = new_cap;
    m->count = live_count;

    vm->bytes_allocated += sizeof(MapEntry) * (size_t)new_cap;
    if (old_cap > 0) vm->bytes_allocated -= sizeof(MapEntry) * (size_t)old_cap;
}

static void mark_object(FemboyObj *obj);

static void mark_value(Value v) {
    if (v.type == V_STR || v.type == V_ARRAY || v.type == V_MAP) mark_object(v.as.obj);
}

static void mark_object(FemboyObj *obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;

    if (obj->type == OBJ_ARRAY) {
        ObjArray *a = (ObjArray *)obj;
        for (int i = 0; i < a->count; i++) mark_value(a->items[i]);
    } else if (obj->type == OBJ_MAP) {
        ObjMap *m = (ObjMap *)obj;
        for (int i = 0; i < m->cap; i++) {

            if (m->entries[i].key != NULL && !gc_map_key_is_tombstone(m->entries[i].key)) {
                mark_value(m->entries[i].value);
            }
        }
    }

}

static void mark_roots(VM *vm) {
    for (int i = 0; i < vm->sp; i++) mark_value(vm->stack[i]);
    for (int i = 0; i < vm->nglobals; i++) mark_value(vm->globals[i].value);
}

static size_t object_size(FemboyObj *obj) {
    if (obj->type == OBJ_STRING) {
        ObjString *s = (ObjString *)obj;
        return sizeof(ObjString) + (size_t)s->length + 1;
    } else if (obj->type == OBJ_ARRAY) {
        ObjArray *a = (ObjArray *)obj;
        return sizeof(ObjArray) + sizeof(Value) * (size_t)a->cap;
    } else {
        ObjMap *m = (ObjMap *)obj;
        return sizeof(ObjMap) + (size_t)m->cap * sizeof(MapEntry);
    }
}

static void free_object(FemboyObj *obj) {
    if (obj->type == OBJ_STRING) {
        ObjString *s = (ObjString *)obj;
        free(s->chars);
        free(s);
    } else if (obj->type == OBJ_ARRAY) {
        ObjArray *a = (ObjArray *)obj;
        free(a->items);
        free(a);
    } else {
        ObjMap *m = (ObjMap *)obj;

        for (int i = 0; i < m->cap; i++) {
            if (m->entries[i].key != NULL && !gc_map_key_is_tombstone(m->entries[i].key)) {
                free(m->entries[i].key);
            }
        }
        free(m->entries);
        free(m);
    }
}

static void sweep(VM *vm) {
    FemboyObj **link = &vm->objects;
    while (*link) {
        FemboyObj *obj = *link;
        if (obj->marked) {
            obj->marked = false;
            link = &obj->next;
        } else {
            *link = obj->next;
            vm->bytes_allocated -= object_size(obj);
            free_object(obj);
        }
    }
}

void gc_collect(VM *vm) {
    bool log = gc_logging_enabled();
    size_t before = vm->bytes_allocated;

    mark_roots(vm);
    sweep(vm);

    if (log) {
        fprintf(stderr, "[Femboy GC] collected: %zu -> %zu bytes (threshold was %zu)\n",
                before, vm->bytes_allocated, vm->gc_threshold);
    }
}

void gc_free_all(VM *vm) {
    FemboyObj *obj = vm->objects;
    while (obj) {
        FemboyObj *next = obj->next;
        free_object(obj);
        obj = next;
    }
    vm->objects = NULL;
    vm->bytes_allocated = 0;
}
