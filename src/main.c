#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "preprocessor.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"

#define EXIT_USAGE   1
#define EXIT_IO      3
#define EXIT_LEX     4
#define EXIT_PARSE   5
#define EXIT_COMPILE 6
#define EXIT_RUNTIME 7

#ifdef _WIN32

extern int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);
extern int __stdcall SetConsoleCP(unsigned int wCodePageID);
#define FEMBOY_CP_UTF8 65001u
#endif

static void setup_console_encoding(void) {
#ifdef _WIN32

    SetConsoleOutputCP(FEMBOY_CP_UTF8);
    SetConsoleCP(FEMBOY_CP_UTF8);
#endif
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <file.fmb> [--dump]\n", argv0);
}

int main(int argc, char **argv) {
    setup_console_encoding();

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_USAGE;
    }
    bool dump = (argc >= 3 && !strcmp(argv[2], "--dump"));

    FemboyError err;
    femboy_error_clear(&err);

    char *src = NULL;
    if (femboy_preprocess_file(argv[1], &src, &err) != FEMBOY_OK) {
        femboy_error_print(&err, "io");
        return EXIT_IO;
    }

    TokenArray tokens = {0};
    if (femboy_tokenize(src, &tokens, &err) != FEMBOY_OK) {
        femboy_error_print(&err, "lexer");
        free(src);
        return EXIT_LEX;
    }
    free(src);

    Node *program = NULL;
    if (femboy_parse(tokens.items, tokens.count, &program, &err) != FEMBOY_OK) {
        femboy_error_print(&err, "parser");
        token_array_free(&tokens);
        return EXIT_PARSE;
    }
    token_array_free(&tokens);

    Chunk chunk;
    chunk_init(&chunk);
    if (femboy_compile(program, &chunk, &err) != FEMBOY_OK) {
        femboy_error_print(&err, "compiler");
        ast_free(program);
        chunk_free(&chunk);
        return EXIT_COMPILE;
    }
    ast_free(program);

    if (dump) debug_dump_chunk(&chunk);

    VM vm;
    vm_init(&vm);
    FemboyStatus run_status = femboy_vm_run(&vm, &chunk, &err);
    vm_free(&vm);
    chunk_free(&chunk);

    if (run_status != FEMBOY_OK) {
        femboy_error_print(&err, "runtime");
        return EXIT_RUNTIME;
    }

    return 0;
}
