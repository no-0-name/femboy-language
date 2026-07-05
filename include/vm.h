#ifndef FEMBOY_VM_H
#define FEMBOY_VM_H

#include <stddef.h>
#include "common.h"
#include "chunk.h"
#include "value.h"
#include "object.h"

#define FEMBOY_STACK_MAX 65536
#define FEMBOY_FRAMES_MAX 4096
#define FEMBOY_GLOBALS_MAX 1024
#define FEMBOY_HANDLERS_MAX 256

typedef struct {
    int return_addr;
    int base_slot;
    int handler_depth;

} Frame;

typedef struct {
    char *name;
    Value value;
} Global;

typedef struct {
    int catch_addr;
    int saved_sp;
    int saved_fp;
} TryHandler;

typedef struct VM {
    Chunk *chunk;
    Value stack[FEMBOY_STACK_MAX];
    int sp;
    Frame frames[FEMBOY_FRAMES_MAX];
    int fp;
    Global globals[FEMBOY_GLOBALS_MAX];
    int nglobals;
    TryHandler handlers[FEMBOY_HANDLERS_MAX];
    int nhandlers;

    FemboyObj *objects;
    size_t bytes_allocated;
    size_t gc_threshold;
} VM;

void vm_init(VM *vm);

void vm_free(VM *vm);

FemboyStatus femboy_vm_run(VM *vm, Chunk *chunk, FemboyError *err);

#endif
