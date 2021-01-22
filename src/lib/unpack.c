/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <swamp-runtime/allocator.h>
#include <swamp-runtime/log.h>
#include <swamp-runtime/print.h>
#include <swamp-runtime/ref_count.h>
#include <swamp-runtime/types.h>
#include <swamp-typeinfo/deserialize.h>
#include <swamp-typeinfo/typeinfo.h>
#include <swamp-unpack/swamp_unpack.h>

#include <clog/clog.h>
#include <flood/out_stream.h>
#include <raff/raff.h>
#include <raff/tag.h>

#include <string.h> // strcmp

void unpack_constants_init(unpack_constants* self)
{
    self->index = 0;
}

void octet_stream_init(octet_stream* self, const uint8_t* octets, size_t octet_count)
{
    self->octets = octets;
    self->octet_count = octet_count;
    self->position = 0;
}

static inline uint8_t read_uint8(octet_stream* s)
{
    if (s->position >= s->octet_count) {
        char* p = 0;
        *p = -1;
    }

    return s->octets[s->position++];
}

static inline int32_t read_int32(octet_stream* s)
{
    if (s->position + 4 >= s->octet_count) {
        char* p = 0;
        *p = -1;
    }
    uint32_t h0 = read_uint8(s);
    uint32_t h1 = read_uint8(s);
    uint32_t h2 = read_uint8(s);
    uint32_t h3 = read_uint8(s);
    uint32_t t = (h0 << 24) | (h1 << 16) | (h2 << 8) | h3;
    return t;
}

static inline int32_t read_uint32(octet_stream* s)
{
    if (s->position + 4 >= s->octet_count) {
        char* p = 0;
        *p = -1;
    }
    uint32_t h0 = read_uint8(s);
    uint32_t h1 = read_uint8(s);
    uint32_t h2 = read_uint8(s);
    uint32_t h3 = read_uint8(s);
    uint32_t t = (h0 << 24) | (h1 << 16) | (h2 << 8) | h3;
    return t;
}

static inline int32_t read_uint16(octet_stream* s)
{
    if (s->position + 4 >= s->octet_count) {
        char* p = 0;
        *p = -1;
    }
    uint8_t h2 = read_uint8(s);
    uint8_t h3 = read_uint8(s);
    uint32_t t = (h2 << 8) | h3;
    return t;
}

static inline void read_string(octet_stream* s, char* buf)
{
    uint8_t len = read_uint8(s);
    if (s->position + len >= s->octet_count) {
        char* p = 0;
        *p = -1;
    }

    const char* raw = (const char*) &s->octets[s->position];
    for (int i = 0; i < len; i++) {
        buf[i] = raw[i];
    }
    s->position += len;
    buf[len] = 0;
}

static inline uint8_t read_count(octet_stream* s)
{
    return read_uint8(s);
}

static inline uint32_t read_dword_count(octet_stream* s)
{
    return read_uint32(s);
}

static void read_booleans(octet_stream* s, swamp_allocator* allocator, unpack_constants* repo, int verboseFlag)
{
    uint8_t count = read_count(s);

    if (verboseFlag) {
        SWAMP_LOG_INFO("=== read booleans %d ===", count);
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t b = read_uint8(s);
        if (verboseFlag) {
            SWAMP_LOG_DEBUG("%d: read boolean %d", repo->index, b);
        }
        repo->table[repo->index++] = swamp_allocator_alloc_boolean(allocator, b ? 1 : 0);
    }
}

static void read_integers(octet_stream* s, swamp_allocator* allocator, unpack_constants* repo, int verboseFlag)
{
    uint8_t count = read_count(s);

    if (verboseFlag) {
        SWAMP_LOG_INFO("=== read integers %d ===", count);
    }

    for (uint8_t i = 0; i < count; ++i) {
        int32_t b = read_int32(s);
        if (verboseFlag) {
            SWAMP_LOG_DEBUG(" %d: read int %d", repo->index, b);
        }
        repo->table[repo->index++] = swamp_allocator_alloc_integer(allocator, b);
    }
}

static void read_strings(octet_stream* s, swamp_allocator* allocator, unpack_constants* repo, int verboseFlag)
{
    uint8_t count = read_count(s);
    if (verboseFlag) {
        SWAMP_LOG_INFO("=== read strings %d ===", count);
    }

    for (uint8_t i = 0; i < count; ++i) {
        char buf[512];
        read_string(s, buf);
        if (verboseFlag) {
            SWAMP_LOG_DEBUG(" %d: read string '%s'", repo->index, buf);
        }
        repo->table[repo->index++] = swamp_allocator_alloc_string(allocator, buf);
    }
}

static void read_resource_names(octet_stream* s, swamp_allocator* allocator, unpack_constants* repo, int verboseFlag)
{
    uint8_t count = read_count(s);
    if (verboseFlag) {
        SWAMP_LOG_INFO("=== read resource names %d ===", count);
    }

    for (uint8_t i = 0; i < count; ++i) {
        char buf[512];
        read_string(s, buf);
        if (verboseFlag) {
            SWAMP_LOG_DEBUG(" %d: read resource name '%s'", repo->index, buf);
        }
        repo->resource_names[i] = tc_str_dup(buf);
        repo->resource_name_index = i + 1;
        repo->table[repo->index++] = swamp_allocator_alloc_integer(allocator, i);
    }
}

static char* duplicate_string(const char* source)
{
    int source_size;
    char* target;
    char* target_ptr;

    source_size = strlen(source);
    target = (char*) malloc(sizeof(char) * source_size + 1);
    if (target == 0) {
        return 0;
    }

    target_ptr = target;
    while (*source) {
        *target_ptr++ = *source++;
    }
    *target_ptr = '\0';

    return target;
}

static void read_functions(swamp_unpack* self, octet_stream* s, swamp_allocator* allocator, unpack_constants* repo)
{
    uint32_t count = read_dword_count(s);
    if (count != self->function_declaration_count) {
        SWAMP_LOG_DEBUG("wrong function count %d vs %d", self->function_declaration_count, count);
        return;
    }

    if (self->verbose_flag) {
        SWAMP_LOG_DEBUG("=== functions (%d) ===", count);
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t param_count = read_uint8(s);
        uint8_t variable_count = read_uint8(s);
        uint8_t temp_count = read_uint8(s);
        uint8_t constant_count = read_uint8(s);
        uint16_t declarationRef = self->offset_function_declarations + i;
        swamp_func* previously_allocated_function = (swamp_func*) repo->table[declarationRef];

        if (self->verbose_flag) {
            SWAMP_LOG_DEBUG("%d: name: '%s' functionRef:%d param_count:%d "
                            "var_count:%d temp_count:%d constant_count:%d",
                            i, previously_allocated_function->debug_name, declarationRef, param_count, variable_count,
                            temp_count, constant_count);
        }

        const swamp_value* constants[512];

        for (uint8_t j = 0; j < constant_count; ++j) {
            uint8_t index = read_uint8(s);
            constants[j] = repo->table[index];
            if (self->verbose_flag) {
                SWAMP_LOG_DEBUG(" -- %d: constant: type: %d", index, constants[j]->internal.type);
                swamp_value_print(constants[j], "_constant");
            }
        }

        if (self->verbose_flag && constant_count > 0) {
            SWAMP_LOG_DEBUG("\n\n");
        }

        uint16_t opcode_count = read_uint16(s);
        const uint8_t* opcodes = &s->octets[s->position];
        s->position += opcode_count;
        const size_t constant_parameter_count = 0;
        swamp_allocator_set_function(previously_allocated_function, opcodes, opcode_count, constant_parameter_count,
                                     param_count, variable_count, constants, constant_count,
                                     previously_allocated_function->debug_name);
    }
}

static void read_type_ref(octet_stream* s, uint8_t* typeRef)
{
    *typeRef = read_uint8(s);
}

static void read_external_functions(swamp_unpack* self, octet_stream* s, swamp_allocator* allocator,
                                    unpack_constants* repo)
{
    uint8_t count = read_count(s);
    if (self->verbose_flag) {
        SWAMP_LOG_DEBUG("=== external functions (%d) ===", count);
    }

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t param_count = read_uint8(s);

        char name[512];
        read_string(s, name);

        uint8_t typeRef;
        read_type_ref(s, &typeRef);

        if (self->verbose_flag) {
            SWAMP_LOG_DEBUG("%d (%d): name:%s typeIndex:%d param_count:%d", repo->index, i, name, typeRef, param_count);
        }

        swamp_external_fn external_function = self->bind_fn(name);
        if (external_function == 0 && !self->ignore_external_function_bind_errors) {
            SWAMP_ERROR("external function returned null %s", name);
        }

        const swamp_value* external_func = swamp_allocator_alloc_external_function(allocator, external_function,
                                                                                   param_count, name);

        repo->table[repo->index++] = external_func;
    }
}

static void read_function_declarations(swamp_unpack* self, octet_stream* s, swamp_allocator* allocator,
                                       unpack_constants* repo)
{
    uint32_t count = read_dword_count(s);

    if (self->verbose_flag) {
        SWAMP_LOG_DEBUG("=== function declarations (%d) ===", count);
    }

    self->offset_function_declarations = repo->index;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t param_count = read_uint8(s);

        char name[512];
        read_string(s, name);

        uint8_t typeRef;
        read_type_ref(s, &typeRef);

        if (self->verbose_flag) {
            const struct SwtiType* foundType = swtiChunkTypeFromIndex(&self->typeInfoChunk, typeRef);
            const char* x;
            FldOutStream outStream;
            uint8_t temp[1024];
            fldOutStreamInit(&outStream, temp, 1024);

            if (foundType == 0) {
                fldOutStreamWritef(&outStream, "unknown");
            } else {
                swtiDebugOutput(&outStream, foundType);
            }
            fldOutStreamWriteUInt8(&outStream, 0);
            const char* typeString = (const char*) outStream.octets;
            SWAMP_LOG_DEBUG("%d (%d): '%s' type: '%s' (%d) param_count:%d", repo->index, i, name, typeString, typeRef,
                            param_count);
        }

        swamp_func* function_declaration = calloc(1, sizeof(swamp_func));
        function_declaration->internal.type = swamp_type_function;

        function_declaration->debug_name = duplicate_string(name);
        function_declaration->typeIndex = typeRef;

        if (strcmp(name, "main") == 0) {
            self->entry = function_declaration;
            INC_REF(self->entry);
        }

        repo->table[repo->index++] = (swamp_value*) function_declaration;
    }
    self->function_declaration_count = count;
}

int readAndVerifyRaffHeader(octet_stream* s)
{
    const uint8_t* p = &s->octets[s->position];

    int count = raffReadAndVerifyHeader(p, s->octet_count - s->position);
    if (count < 9) {
        return -1;
    }

    s->position += count;

    return 0;
}

int readAndVerifyRaffChunkHeader(octet_stream* s, RaffTag icon, RaffTag name)
{
    const uint8_t* p = &s->octets[s->position];

    RaffTag foundIcon, foundName;

    uint32_t chunkSize;

    int count = raffReadChunkHeader(p, s->octet_count - s->position, foundIcon, foundName, &chunkSize);
    if (count < 0) {
        return count;
    }

    if (!raffTagEqual(icon, foundIcon)) {
        return -2;
    }

    if (!raffTagEqual(name, foundName)) {
        return -3;
    }

    s->position += count;

    return chunkSize;
}

int readRaffMarker(octet_stream* s, RaffTag tag, int verboseLevel)
{
    int count = raffReadMarker(&s->octets[s->position], s->octet_count - s->position, tag);
    if (count < 0) {
        return count;
    }

    if (SWAMP_LOG_SHOULD_LOG(verboseLevel)) {
        SWAMP_LOG_DEBUG("");
        char temp[64];
        SWAMP_LOG_DEBUG("tag: %s", raffTagToString(temp, 64, tag));
    }

    s->position += count;

    return count;
}

int verifyMarker(octet_stream* s, RaffTag expectedMarker, int verboseFlag)
{
    RaffTag marker;

    readRaffMarker(s, marker, verboseFlag);

    if (!raffTagEqual(marker, expectedMarker)) {
        return -1;
    }

    return 0;
}

int readTypeInformation(swamp_unpack* self, octet_stream* s)
{
    RaffTag expectedPacketName = {'s', 't', 'i', '0'};
    RaffTag expectedPacketIcon = {0xF0, 0x9F, 0x93, 0x9C};

    int upcomingOctetsInChunk = readAndVerifyRaffChunkHeader(s, expectedPacketIcon, expectedPacketName);
    if (upcomingOctetsInChunk <= 0) {
        return upcomingOctetsInChunk;
    }

    int errorCode = swtiDeserialize(&s->octets[s->position], upcomingOctetsInChunk, &self->typeInfoChunk);
    if (errorCode < 0) {
        CLOG_SOFT_ERROR("swtiDeserialize: error %d", errorCode);
        return errorCode;
    }

    s->position += upcomingOctetsInChunk;

    return 0;
}

int readCode(swamp_unpack* self, octet_stream* s, int verboseFlag)
{
    int errorCode;

    RaffTag expectedPacketName = {'s', 'c', 'd', '0'};
    RaffTag expectedPacketIcon = {0xF0, 0x9F, 0x92, 0xBB};

    int upcomingOctetsInChunk = readAndVerifyRaffChunkHeader(s, expectedPacketIcon, expectedPacketName);
    if (upcomingOctetsInChunk <= 0) {
        return upcomingOctetsInChunk;
    }

    RaffTag externalMarker = {0xF0, 0x9F, 0x91, 0xBE};
    if ((errorCode = verifyMarker(s, externalMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_external_functions(self, s, self->allocator, self->table);

    RaffTag functionDeclarationMarker = {0xF0, 0x9F, 0x9B, 0x82};
    if ((errorCode = verifyMarker(s, functionDeclarationMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_function_declarations(self, s, self->allocator, self->table);

    RaffTag booleanMarker = {0xF0, 0x9F, 0x90, 0x9C};
    if ((errorCode = verifyMarker(s, booleanMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_booleans(s, self->allocator, self->table, self->verbose_flag);

    RaffTag integerMarker = {0xF0, 0x9F, 0x94, 0xA2};
    if ((errorCode = verifyMarker(s, integerMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_integers(s, self->allocator, self->table, self->verbose_flag);

    RaffTag stringMarker = {0xF0, 0x9F, 0x8E, 0xBB};
    if ((errorCode = verifyMarker(s, stringMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_strings(s, self->allocator, self->table, self->verbose_flag);

    RaffTag resourceNameMarker = {0xF0, 0x9F, 0x8C, 0xB3};
    if ((errorCode = verifyMarker(s, resourceNameMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_resource_names(s, self->allocator, self->table, self->verbose_flag);


    RaffTag functionMarker = {0xF0, 0x9F, 0x90, 0x8A};
    if ((errorCode = verifyMarker(s, functionMarker, verboseFlag)) != 0) {
        return errorCode;
    }
    read_functions(self, s, self->allocator, self->table);

    if (self->verbose_flag) {
        SWAMP_LOG_INFO("read functions");
        SWAMP_LOG_INFO("done!\n");
    }

    return 0;
}

int swamp_unpack_octet_stream(swamp_unpack* self, octet_stream* s, int verboseFlag)
{
    int errorCode = readAndVerifyRaffHeader(s);
    if (errorCode != 0) {
        return errorCode;
    }

    RaffTag expectedPacketName = {'s', 'p', 'k', '3'};
    RaffTag expectedPacketIcon = {0xF0, 0x9F, 0x93, 0xA6};

    int upcomingOctetsInChunk = readAndVerifyRaffChunkHeader(s, expectedPacketIcon, expectedPacketName);
    if (upcomingOctetsInChunk < 0) {
        return upcomingOctetsInChunk;
    }

    if ((errorCode = readTypeInformation(self, s)) < 0) {
        SWAMP_LOG_SOFT_ERROR("problem with type information chunk");
        return errorCode;
    }

    if ((errorCode = readCode(self, s, verboseFlag)) != 0) {
        SWAMP_LOG_SOFT_ERROR("problem with code chunk");
        return errorCode;
    }

    return 0;
}

static void read_whole_file(const char* filename, octet_stream* stream)
{
    uint8_t* source = 0;
    FILE* fp = fopen(filename, "rb");
    if (fp == 0) {
        SWAMP_LOG_INFO("errror:%s\n", filename);
        return;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        SWAMP_LOG_INFO("seek err:\n");
        return;
    }

    long bufsize = ftell(fp);
    if (bufsize == -1) {
        SWAMP_LOG_INFO("bufsize error\n");
        return;
    }

    source = malloc(sizeof(uint8_t) * (bufsize));

    if (fseek(fp, 0L, SEEK_SET) != 0) {
        SWAMP_LOG_INFO("seek error\n");
        return;
    }

    size_t new_len = fread(source, sizeof(uint8_t), bufsize, fp);
    stream->octets = source;
    stream->octet_count = new_len;
    stream->position = 0;

    fclose(fp);
}

void swamp_unpack_init(swamp_unpack* self, swamp_allocator* allocator, unpack_constants* table, unpack_bind_fn bind_fn,
                       int verbose_flag)
{
    self->allocator = allocator;
    self->table = table;
    self->entry = 0;
    self->bind_fn = bind_fn;
    self->verbose_flag = verbose_flag;
    self->ignore_external_function_bind_errors = 0;
}

int swamp_unpack_filename(swamp_unpack* self, const char* pack_filename, int verboseFlag)
{
    octet_stream stream;
    octet_stream* s = &stream;
    read_whole_file(pack_filename, s);
    int result = swamp_unpack_octet_stream(self, s, verboseFlag);
    free((void*) s->octets);
    return result;
}

struct swamp_func* swamp_unpack_entry_point(swamp_unpack* self)
{
    return self->entry;
}
