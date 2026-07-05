#ifndef FEMBOY_OBJECT_H
#define FEMBOY_OBJECT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_MAP,
} ObjType;

typedef struct FemboyObj {
    ObjType type;
    bool marked;
    struct FemboyObj *next;
} FemboyObj;

typedef struct {
    FemboyObj obj;
    int length;
    char *chars;
} ObjString;

typedef struct Value Value;

typedef struct {
    FemboyObj obj;
    Value *items;
    int count, cap;
} ObjArray;

typedef struct ObjMap ObjMap;

#endif
