#ifndef FEMBOY_DEBUG_H
#define FEMBOY_DEBUG_H

#include "chunk.h"

const char *debug_op_name(uint8_t op);
void debug_dump_chunk(const Chunk *c);

#endif
