/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#ifndef swamp_unpack_h
#define swamp_unpack_h

#include <swamp-runtime/types.h>
#include <swamp-typeinfo/chunk.h>

struct unpack_constants;
struct swamp_allocator;
struct swamp_value;
struct swamp_func;
typedef struct octet_stream {
    const uint8_t* octets;
    size_t octet_count;
    size_t position;
} octet_stream;

void octet_stream_init(octet_stream* self, const uint8_t* octets, size_t octet_count);

typedef swamp_external_fn (*unpack_bind_fn)(const char* function_name);

typedef struct swamp_unpack {
    struct swamp_allocator* allocator;
    struct unpack_constants* table;
    struct swamp_func* entry;
    unpack_bind_fn bind_fn;
    int verbose_flag;
    int ignore_external_function_bind_errors;
    int offset_function_declarations;
    uint32_t function_declaration_count;
    SwtiChunk typeInfoChunk;
} swamp_unpack;

typedef struct unpack_constants {
    const struct swamp_value* table[512];
    int index;
    const char* resource_names[512];
    int resource_name_index;
} unpack_constants;

void unpack_constants_init(unpack_constants* self);

void swamp_unpack_init(swamp_unpack* self, struct swamp_allocator* allocator, struct unpack_constants* table,
                       unpack_bind_fn bind_fn, int verbose_flag);
int swamp_unpack_filename(swamp_unpack* self, const char* pack_filename, int verboseFlag);

int swamp_unpack_octet_stream(swamp_unpack* self, octet_stream* s, int verboseFlag);
struct swamp_func* swamp_unpack_entry_point(swamp_unpack* self);

#endif
