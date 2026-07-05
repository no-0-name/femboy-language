#ifndef FEMBOY_COMMON_H
#define FEMBOY_COMMON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FEMBOY_OK = 0,
    FEMBOY_ERR_IO,
    FEMBOY_ERR_LEX,
    FEMBOY_ERR_PARSE,
    FEMBOY_ERR_COMPILE,
    FEMBOY_ERR_RUNTIME,
    FEMBOY_ERR_USAGE,
} FemboyStatus;

#define FEMBOY_ERR_MSG_MAX 256

typedef struct {
    FemboyStatus status;
    char message[FEMBOY_ERR_MSG_MAX];
    int line;
    int column;
} FemboyError;

void femboy_error_clear(FemboyError *err);

FemboyStatus femboy_error_set(FemboyError *err, FemboyStatus status, int line, int column,
                             const char *fmt, ...);

void femboy_error_print(const FemboyError *err, const char *stage_name);

void femboy_fatal_oom(const char *what) ;

void *femboy_malloc(size_t size);
void *femboy_realloc(void *ptr, size_t size);
char *femboy_strdup(const char *s);

#endif
