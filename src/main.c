#include <string.h>

#include "add.c"
#include "compile.c"
#include "config.c"
#include "dir/snapshot.c"
#include "help.c"
#include "init.c"
#include "store/store.c"
#include "tomlc17/src/tomlc17.c"
#include "tomlc17/src/tomlc17.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxHash/xxhash.h"

int main(int argc, char* argv[]) {
    if (argc == 1 || !strcmp("help", argv[1])) {
        help_show(HELP);
        return 0;
    }
    if (!strcmp("ex", argv[1])) {
        help_show(EX);
        return 0;
    }
    if (!strcmp("init", argv[1])) {
        char* init_dir = argc >= 3 ? argv[2] : ".";
        return init_project(init_dir);
    }

    if (!strcmp("remove", argv[1])) {
        if(argc < 3) {
            printf("Please provide a dependency identifier to remove from the global store.\n");
            return 1;
        }
        printf("Removing %s from the global dependency store\n", argv[2]);
        store_init();
        return store_remove_dependency(store_resolve_identifier(argv[2]));
    }

    // The next options all require parsing configuration
    toml_result_t config = parse_config();
    int exit_code = 0;

    if (!strcmp("compile", argv[1])) {
        char* target = argc >= 3 ? argv[2] : "dev";
        printf("Compiling target %s\n", target);
        exit_code = compile_target(target, config);
    }
    if (!strcmp("run", argv[1])) {
        char* target = "dev";
        char** run_argv = &argv[argc];

        if (argc >= 3) {
            target = argv[2];
            run_argv = argv + 3;
        }

        printf("Compiling and running target %s\n", target);

        exit_code = compile_target(target, config);
        if (!exit_code) exit_code = run_target(target, config, run_argv);
    }
    if (!strcmp("install", argv[1])) {
        store_init();
        exit_code = install_dependencies(config);
    }

    free_config(config);
    return exit_code;
}