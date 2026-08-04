#include <string.h>

// External dependencies
#include "miniz/miniz.c"
#include "tomlc17/src/tomlc17.c"
#include "tomlc17/src/tomlc17.h"

// Internal implementations
#include "compile.c"
#include "dependency.c"
#include "dir/rmdir_recursive.c"
#include "dir/snapshot.c"
#include "help.c"
#include "init.c"
#include "store/store.c"
#include "store/store.h"
#include "targets.c"
#include "toml/config.c"
#include "toml/write.c"

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxHash/xxhash.h"

int main(int argc, char* argv[]) {
    // cpk help
    if (argc == 1 || !strcmp("help", argv[1])) {
        help_show(HELP);
        return 0;
    }
    // cpk init [dir]
    if (!strcmp("init", argv[1])) {
        char* init_dir = argc >= 3 ? argv[2] : ".";
        return init_project(init_dir);
    }
    // cpk delete (dep)
    if (!strcmp("delete", argv[1])) {
        if (argc < 3) {
            printf("Please provide a dependency identifier to delete from the global store.\n");
            return 1;
        }
        printf("Deleting %s from the global dependency store\n", argv[2]);
        store_init();
        store_dependency_identifier ident = store_resolve_identifier(argv[2]);
        if (!ident.mode) return 1;
        return store_remove_dependency(&ident);
    }
    // cpk update (dep)
    if (!strcmp("update", argv[1])) {
        if (argc < 3) {
            printf("Please provide a dependency identifier to update in the global store.\n");
            return 1;
        }
        printf("Updating %s in the global dependency store\n", argv[2]);
        store_init();
        store_dependency_identifier ident = store_resolve_identifier(argv[2]);
        if (!ident.mode) return 1;
        return store_update_dependency(&ident);
    }
    // The next options all require parsing configuration
    toml_result_t config = config_parse();
    int exit_code = -1;
    // cpk compile target*
    if (!strcmp("compile", argv[1])) {
        char* default_target[] = {"dev", NULL};
        char** targets = argc >= 3 ? argv + 2 : default_target;
        for (int i = 0; targets[i] != NULL; i++) {
            printf("Compiling target %s\n", targets[i]);
            int compile_exit_code = compile_target(targets[i], config);
            if (compile_exit_code) exit_code = 1;
        }
        if (exit_code == -1) exit_code = 0;
    }
    // cpk run [target]
    if (!strcmp("run", argv[1])) {
        char* target = "dev";
        char** run_argv = &argv[argc - 1];

        if (argc >= 2) {
            target = argv[2];
            run_argv = argv + 2;
        }

        printf("Compiling and running target %s\n", target);

        exit_code = compile_target(target, config);
        if (!exit_code) exit_code = run_target(target, config, run_argv);
    }
    // cpk install
    if (!strcmp("install", argv[1])) {
        store_init();
        exit_code = install_dependencies(config);
    }
    // cpk add (name=dep)+
    if (!strcmp("add", argv[1])) {
        store_init();
        exit_code = add_dependencies(argv + 2, config);
        exit_code = config_write_out(config) | exit_code;
    }
    // cpk remove (name)
    if (!strcmp("remove", argv[1])) {
        if (argc < 3) {
            printf("No dependency name was specified\n");
            exit_code = 1;
        } else {
            exit_code = remove_dependency(argv[2], config);
            exit_code = config_write_out(config) | exit_code;
        }
    }
    // cpk targets
    if (!strcmp("targets", argv[1])) {
        exit_code = list_targets(config);
    }

    if (exit_code == -1) {
        printf("Unknown command '%s'\nTo view a list of commands, use 'cpk help'\n", argv[1]);
        exit_code = 1;
    }

    config_free(config);
    return exit_code;
}