#include <stdbool.h>
#include <string.h>

// External dependencies
#include "dotenv-c/src/dotenv.c"
#include "miniz/miniz.c"
#include "tomlc17/src/tomlc17.c"
#include "tomlc17/src/tomlc17.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxHash/xxhash.h"
// Internal dependencies
#include "lib/file/copy.c"
#include "lib/file/rmdir_recursive.c"
#include "lib/file/snapshot.c"
#include "lib/fmt/color.c"
#include "lib/fmt/color.h"
#include "lib/shell/env.c"
#include "lib/shell/env.h"
#include "lib/shell/shell.c"
#include "lib/store/store.c"
#include "lib/store/store.h"
#include "lib/toml/config.c"
#include "lib/toml/write.c"
// Command implementations
#include "commands/compile.c"
#include "commands/dependency.c"
#include "commands/help.c"
#include "commands/init.c"
#include "commands/targets.c"

int main(int argc, char* argv[]) {
    set_use_colors();

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
            print_err(false, "no dependency identifier provided");
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
            print_err(false, "no dependency identifier provided");
            return 1;
        }
        printf("Updating %s in the global dependency store\n", argv[2]);
        store_init();
        store_dependency_identifier ident = store_resolve_identifier(argv[2]);
        if (!ident.mode) return 1;
        return store_update_dependency(&ident);
    }
    // The next options all require parsing configuration
    toml_result_t config;
    int exit_code = -1;
    // cpk compile target*
    if (!strcmp("compile", argv[1])) {
        config = config_parse();
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
        config = config_parse();
        char* target = "dev";
        char** run_argv = &argv[argc - 1];
        if (argc > 2) {
            target = argv[2];
            run_argv = argv + 2;
        }
        printf("Compiling and running target %s\n", target);
        exit_code = compile_target(target, config);
        if (!exit_code) exit_code = run_target(target, config, run_argv);
    }
    // cpk install
    if (!strcmp("install", argv[1])) {
        config = config_parse();
        env_read();
        store_init();
        exit_code = install_dependencies(config);
    }
    // cpk add (name=dep)+
    if (!strcmp("add", argv[1])) {
        config = config_parse();
        env_read();
        store_init();
        exit_code = add_dependencies(argv + 2, config);
        exit_code = config_write_out(config) | exit_code;
    }
    // cpk remove (name)
    if (!strcmp("remove", argv[1])) {
        config = config_parse();
        if (argc < 3) {
            print_err(false, "no dependency name was specified");
            exit_code = 1;
        } else {
            exit_code = remove_dependency(argv[2], config);
            exit_code = config_write_out(config) | exit_code;
        }
    }
    // cpk targets
    if (!strcmp("targets", argv[1])) {
        config = config_parse();
        exit_code = list_targets(config);
    }

    if (exit_code == -1) {
        print_err(false, "unknown command '%s'\n      to view a list of commands, use 'cpk help'",
                  argv[1]);
        exit_code = 1;
    } else {
        config_free(config);
    }
    return exit_code;
}