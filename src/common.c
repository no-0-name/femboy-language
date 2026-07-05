#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void femboy_error_clear(FemboyError *err) {
    err->status = FEMBOY_OK;
    err->message[0] = '\0';
    err->line = -1;
    err->column = -1;
}

FemboyStatus femboy_error_set(FemboyError *err, FemboyStatus status, int line, int column,
                             const char *fmt, ...) {
    err->status = status;
    err->line = line;
    err->column = column;

    va_list args;
    va_start(args, fmt);
    vsnprintf(err->message, FEMBOY_ERR_MSG_MAX, fmt, args);
    va_end(args);

    return status;
}

static const char *stage_label(const char *stage_name) {
    return stage_name ? stage_name : "error";
}

void femboy_error_print(const FemboyError *err, const char *stage_name) {
    if (err->status == FEMBOY_OK) return;

    fprintf(stderr, "[Femboy: %s] ", stage_label(stage_name));
    if (err->line >= 0) {
        fprintf(stderr, "line %d", err->line);
        if (err->column >= 0) fprintf(stderr, ", column %d", err->column);
        fprintf(stderr, ": ");
    }
    fprintf(stderr, "%s\n", err->message);
}

void femboy_fatal_oom(const char *what) {
    fprintf(stderr, "[Femboy: fatal error] failed to allocate memory (%s)\n", what ? what : "?");
    exit(2);
}

void *femboy_malloc(size_t size) {
    void *p = malloc(size);
    if (!p && size > 0) femboy_fatal_oom("malloc");
    return p;
}

void *femboy_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size > 0) femboy_fatal_oom("realloc");
    return p;
}

char *femboy_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *r = femboy_malloc(len);
    memcpy(r, s, len);
    return r;
}
