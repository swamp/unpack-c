/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Peter Bjorklund. All rights reserved.
 *  Licensed under the MIT License. See LICENSE in the project root for license information.
 *--------------------------------------------------------------------------------------------*/
#include <clog/clog.h>
#include <swamp-runtime/allocator.h>
#include <swamp-runtime/log.h>
#include <swamp-runtime/print.h>
#include <swamp-runtime/swamp.h>
#include <swamp-unpack/swamp_unpack.h>

#include <getopt.h>

clog_config g_clog;

static void tyran_log_implementation(enum clog_type type, const char* string)
{
    (void) type;
    fprintf(stderr, "%s\n", string);
}

typedef struct options {
    int is_verbose;
    int is_list;
    const char* pack_filename;
} options;

static void parse(options* flags, int argc, char* argv[])
{
    int opt;
    flags->is_verbose = 0;
    flags->is_list = 0;
    while ((opt = getopt(argc, argv, "vl")) != -1) {
        switch (opt) {
            case 'v':
                flags->is_verbose = 1;
                break;
            case 'l':
                flags->is_list = 1;
                break;
            default:
                SWAMP_LOG_INFO("Usage: %s file -v", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    flags->pack_filename = argv[optind];
}

int main(int argc, char* argv[])
{
    g_clog.log = tyran_log_implementation;

    options flags;
    SWAMP_LOG_INFO("swamp-run 0.1.2");
    parse(&flags, argc, argv);

    swamp_allocator allocator;
    swamp_allocator_init(&allocator);
    unpack_constants constants;
    constants.index = 0;

    swamp_unpack unpacker;

    swamp_unpack_init(&unpacker, &allocator, &constants, swamp_core_find_function, flags.is_verbose);
    // HACK!
    unpacker.ignore_external_function_bind_errors = flags.is_list;
    int err = swamp_unpack_filename(&unpacker, flags.pack_filename, flags.is_verbose);
    if (err != 0) {
        SWAMP_ERROR("problem:%d", err);
    }
    const swamp_func* main_func = swamp_unpack_entry_point(&unpacker);
    if (main_func) {
        if (flags.is_verbose) {
            swamp_value_print(main_func, "mainFunc");
        }
    } else {
        SWAMP_LOG_INFO("warning: couldn't find any entry-point");
    }

    SWAMP_LOG_INFO("done.");

    return 0;
}
