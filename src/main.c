#include <string.h>

#include "compile.c"
#include "config.c"
#include "dir/snapshot.c"
#include "help.c"
#include "init.c"
#include "tomlc17/tomlc17.c"
#include "tomlc17/tomlc17.h"

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
    // The next options all require parsing configuration
    toml_result_t config = parse_config();
    int exit_code = 0;

    if (!strcmp("compile", argv[1])) {
        char* target = argc >= 3 ? argv[2] : "dev";
        exit_code = compile_target(target, config);
    } else if (!strcmp("run", argv[1])) {
        char* target = argc >= 3 ? argv[2] : "dev";
        exit_code = compile_target(target, config);
        if (!exit_code) exit_code = run_target(target, config);
    }

    free_config(config);

    return exit_code;
}