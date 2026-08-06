#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dir/snapshot.h"
#include "fmt/color.h"
#include "shell/shell.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Runs the build commands for a target in cpk.toml
 */
int compile_target(char* target, toml_result_t config) {
    // Check that the config defines the given target
    char target_table_name[4096];
    snprintf(target_table_name, 4096, "target.%s", target);

    toml_datum_t toml_target = toml_seek(config.toptab, target_table_name);
    if (toml_target.type != TOML_TABLE) {
        print_err(false, "cpk.toml: target %s is not defined", target);
        return 1;
    }

    // Grab the target dir name, or use current dir
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target-dir");
    if (toml_target_dir.type != TOML_STRING) {
        print_info("cpk.toml: target-dir not defined, using current directory");
    }
    const char* target_dir = toml_target_dir.type == TOML_STRING ? toml_target_dir.u.str.ptr : NULL;
    int target_dir_len = toml_target_dir.type == TOML_STRING ? toml_target_dir.u.str.len : 0;

    char dirpath[4096];
    bool move = true;

    if (target_dir_len + strlen(target) + 2 > 4096) {
        print_err(false, "target directory path is too long");
        return 1;
    }

    if (target_dir) {
        // Make the build and target directories if they don't already exist
        if (mkdir(target_dir, 0700) && errno != EEXIST) {
            print_err(true, "could not create targets directory %s", target_dir);
            return 1;
        }
        strcpy(dirpath, target_dir);
        dirpath[target_dir_len] = '/';
        strcpy(dirpath + target_dir_len + 1, target);
        if (mkdir(dirpath, 0700) && errno != EEXIST) {
            print_err(true, "could not create target directory %s", dirpath);
            return 1;
        }
    } else {
        move = false;
        dirpath[0] = '.';
        dirpath[1] = 0;
    }

    // Compile code using the build script
    toml_datum_t toml_target_build = toml_get(toml_target, "build");
    if (toml_target_build.type != TOML_STRING && toml_target_build.type != TOML_ARRAY) {
        print_err(false, "cpk.toml: target %s does not provide one or more build commands", target);
        return 1;
    }
    // If given an array of commands, validate that each one is a string
    if (toml_target_build.type == TOML_ARRAY) {
        for (int32_t i = 0; i < toml_target_build.u.arr.size; i++) {
            if (toml_target_build.u.arr.elem[i].type != TOML_STRING) {
                print_err(false, "cpk.toml: build command %d of target %s is not a string", i + 1,
                          target);
                return 1;
            }
        }
    }

    // Inject dependencies via CFLAGS environment variable
    char* original_cflags = getenv("CFLAGS");
    size_t cflags_len = original_cflags ? strlen(original_cflags) : 0;

    char* new_cflags = calloc(cflags_len + 8, sizeof(char));
    strcpy(new_cflags, original_cflags ? original_cflags : "");
    strcat(new_cflags, " -I.cpk");

    setenv("CFLAGS", new_cflags, true);

    // Capture project directory before build command
    dir_snapshot before, after, diff;
    if (move) before = snapshot_directory(".");

    toml_datum_t build_commands =
        toml_target_build.type == TOML_ARRAY
            ? toml_target_build
            : (toml_datum_t){.type = TOML_ARRAY, .u.arr = {.size = 1, .elem = &toml_target_build}};

    // Run build command(s)
    Shell shell;
    if (shell_init(&shell) != 0) {
        print_err(false, "failed to start shell for build commands");
        snapshot_free(&before);
        free(new_cflags);
        return 1;
    }

    for (int32_t i = 0; i < build_commands.u.arr.size; i++) {
        toml_datum_t command_datum = build_commands.u.arr.elem[i];
        const char* build_command = command_datum.u.str.ptr;
        printf("> %s\n", build_command);
        int command_result = shell_exec(&shell, build_command);
        if (command_result) break;
    }

    int build_result = shell_kill(&shell);
    if (build_result) {
        print_err(false, "build command returned non-zero exit code");
        snapshot_free(&before);
        free(new_cflags);
        return build_result;
    }

    // Capture and diff directory after build, move any new files to target
    if (move) {
        after = snapshot_directory(".");
        diff = diff_snapshots(&before, &after);
        if (!diff.count) {
            print_warn("build command did not produce any output files");
        } else {
            move_snapshot_diff_items(&diff, ".", dirpath);
        }
        snapshot_free(&before);
        snapshot_free(&after);
        snapshot_free(&diff);
    }

    // reset CFLAGS
    free(new_cflags);
    if (original_cflags) {
        setenv("CFLAGS", original_cflags, true);
    } else {
        unsetenv("CFLAGS");
    }

    return 0;
}

/**
 * Runs the executable defined for a target in cpk.toml
 *
 * This function assumes compile_target already ran, so target-dir/target/
 * is guaranteed to exist and be valid in cpk.toml
 */
int run_target(char* target, toml_result_t config, char* argv[]) {
    // This runs after compile_target, so we are guaranteed that target-dir/target/ exists
    // and that both are validly defined in the toml
    char executable_path[4096];
    snprintf(executable_path, 4096, "target.%s.exec", target);

    toml_datum_t target_ex = toml_seek(config.toptab, executable_path);
    if (target_ex.type != TOML_STRING) {
        print_err(false, "cpk.toml: target %s does not provide an executable name", target);
        return 1;
    }
    const char* executable_name = target_ex.u.str.ptr;
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target-dir");
    if (toml_target_dir.type == TOML_STRING) {
        snprintf(executable_path, 4096, "%s/%s/%s", toml_target_dir.u.str.ptr, target,
                 executable_name);
    } else {
        strcpy(executable_path, executable_name);
    }

    argv[0] = executable_path;
    putchar('>');
    for (size_t i = 0; argv[i] != NULL; i++) {
        printf(" %s", argv[i]);
    }
    putchar('\n');

    int exit_code = execv(executable_path, argv);
    return exit_code;
}