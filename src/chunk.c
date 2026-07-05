#include "chunk.h"

#include <stdlib.h>

void chunk_init(Chunk *c) {
    c->code = NULL; c->count = 0; c->cap = 0;
    c->numbers = NULL; c->nnum = 0; c->capnum = 0;
    c->strings = NULL; c->nstr = 0; c->capstr = 0;
}

void chunk_free(Chunk *c) {
    free(c->code);
    free(c->numbers);
    for (int i = 0; i < c->nstr; i++) free(c->strings[i]);
    free(c->strings);
    chunk_init(c);
}

void chunk_emit_byte(Chunk *c, uint8_t b) {
    if (c->count >= c->cap) {
        c->cap = c->cap ? c->cap * 2 : 64;
        c->code = femboy_realloc(c->code, c->cap);
    }
    c->code[c->count++] = b;
}

void chunk_emit_u16(Chunk *c, uint16_t v) {
    chunk_emit_byte(c, v & 0xFF);
    chunk_emit_byte(c, (v >> 8) & 0xFF);
}

int chunk_add_const_num(Chunk *c, double v) {
    if (c->nnum >= c->capnum) {
        c->capnum = c->capnum ? c->capnum * 2 : 16;
        c->numbers = femboy_realloc(c->numbers, sizeof(double) * c->capnum);
    }
    c->numbers[c->nnum] = v;
    return c->nnum++;
}

int chunk_add_const_str(Chunk *c, const char *s) {
    if (c->nstr >= c->capstr) {
        c->capstr = c->capstr ? c->capstr * 2 : 16;
        c->strings = femboy_realloc(c->strings, sizeof(char *) * c->capstr);
    }
    c->strings[c->nstr] = femboy_strdup(s);
    return c->nstr++;
}

uint16_t chunk_read_u16(const uint8_t *code, int pos) {
    return (uint16_t)(code[pos] | (code[pos + 1] << 8));
}
