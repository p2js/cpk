#include <string.h>

#include "compile.c"
#include "dependency.c"
#include "dir/snapshot.c"
#include "help.c"
#include "init.c"
#include "store/store.c"
#include "store/store.h"
#include "toml/config.c"
// Dependencies
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

    if (!strcmp("delete", argv[1])) {
        if (argc < 3) {
            printf("Please provide a dependency identifier to delete from the global store.\n");
            return 1;
        }
        printf("Deleting %s from the global dependency store\n", argv[2]);
        store_init();
        store_dependency_identifier ident = store_resolve_identifier(argv[2]);
        if (!ident.mode) {
            return 1;
        }
        return store_remove_dependency(ident);
    }

    if (!strcmp("update", argv[1])) {
        if (argc < 3) {
            printf("Please provide a dependency identifier to update in the global store.\n");
            return 1;
        }
        printf("Updating %s in the global dependency store\n", argv[2]);
        store_init();
        store_dependency_identifier ident = store_resolve_identifier(argv[2]);
        if (!ident.mode) {
            return 1;
        }
        return store_update_dependency(ident);
    }

    // The next options all require parsing configuration
    toml_result_t config = config_parse();
    int exit_code = -1;

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
    if (!strcmp("add", argv[1])) {
        store_init();
        exit_code = add_dependencies(argv + 2, config);
    }
    if (!strcmp("remove", argv[1])) {
        if (argc < 3) {
            printf("No dependency name was specified\n");
            exit_code = 1;
        } else {
            exit_code = remove_dependency(argv[2], config);
        }
    }

    if (exit_code == -1) {
        printf("Unknown command '%s'\nTo view a list of commands, use 'cpk help'\n", argv[1]);
        exit_code = 1;
    }

    config_free(config);
    return exit_code;
}