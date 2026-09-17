#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../lib/file/copy.h"
#include "../lib/file/snapshot.h"
#include "../lib/fmt/color.h"
#include "../lib/shell/shell.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Helper that normalises a string datum into an array,
 * leaving other types (array, unknown) unaltered
 */
toml_datum_t normalise_datum_as_array(toml_datum_t datum) {
    if (datum.type != TOML_STRING) return datum;
    return (toml_datum_t){.type = TOML_ARRAY, .u.arr = {.size = 1, .elem = &datum}};
}

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
    bool move = target_dir != NULL;

    if (target_dir_len + strlen(target) + 2 > 4096) {
        print_err(false, "target directory path is too long");
        return 1;
    }

    // Validate the build script(s)
    toml_datum_t toml_target_build = toml_get(toml_target, "build");
    if (toml_target_build.type != TOML_STRING && toml_target_build.type != TOML_ARRAY) {
        print_err(false, "cpk.toml: target %s does not provide one or more build commands", target);
        return 1;
    }

    if (toml_target_build.type == TOML_ARRAY) {
        for (int32_t i = 0; i < toml_target_build.u.arr.size; i++) {
            if (toml_target_build.u.arr.elem[i].type != TOML_STRING) {
                print_err(false, "cpk.toml: build command %d of target %s is not a string", i + 1,
                          target);
                return 1;
            }
        }
    }

    // Validate the copy list if given
    toml_datum_t toml_target_copy = toml_get(toml_target, "copy");
    if (toml_target_copy.type != TOML_UNKNOWN && toml_target_copy.type != TOML_ARRAY &&
        toml_target_copy.type != TOML_STRING) {
        print_err(false, "cpk.toml: target %s defines invalid file path(s) for copying", target);
        return 1;
    }
    if (toml_target_copy.type == TOML_ARRAY) {
        for (int32_t i = 0; i < toml_target_copy.u.arr.size; i++) {
            if (toml_target_copy.u.arr.elem[i].type != TOML_STRING) {
                print_err(false, "cpk.toml: copy file path %d of target %s is not a string", i + 1,
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

    // Run build command(s)
    Shell shell;
    if (shell_init(&shell) != 0) {
        print_err(false, "failed to start shell for build commands");
        snapshot_free(&before);
        free(new_cflags);
        return 1;
    }

    toml_datum_t build_commands = normalise_datum_as_array(toml_target_build);

    for (int32_t i = 0; i < build_commands.u.arr.size; i++) {
        toml_datum_t command_datum = build_commands.u.arr.elem[i];
        const char* build_command = command_datum.u.str.ptr;
        printf(use_colors ? COLOR_GRAY("> %s\n") : "> %s\n", build_command);
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

    // Capture and diff directory after build, move any new files to target dir
    if (move) {
        after = snapshot_directory(".");
        diff = diff_snapshots(&before, &after);
        if (!diff.count) {
            print_warn("build command did not produce any output files");
        } else {
            // Create dir if it doesn't already exist
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

    toml_datum_t copy_paths = normalise_datum_as_array(toml_target_copy);
    if (copy_paths.type != TOML_UNKNOWN) {
        for (int i = 0; i < copy_paths.u.arr.size; i++) {
            toml_datum_t element = copy_paths.u.arr.elem[i];
            const char* src_path = element.u.str.ptr;
            if (strlen(dirpath) + strlen(src_path) + 1 > 4096) {
                // path too long
                print_err(false, "Destination path for copying %s is too long", src_path);
                continue;
            }

            char dst_path[4096];
            snprintf(dst_path, 4096, "%s/%s", dirpath, element.u.str.ptr);

            printf("Copying file %s\n", element.u.str.ptr);

            if (copy_file(src_path, dst_path)) return 1;
        }
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
        if (executable_name[0] == '/') {
            snprintf(executable_path, 4096, "%s", executable_name);
        } else {
            snprintf(executable_path, 4096, "%s/%s/%s", toml_target_dir.u.str.ptr, target,
                     executable_name);
        }
    } else {
        strcpy(executable_path, executable_name);
    }

    argv[0] = executable_path;
    printf(use_colors ? COLOR_GRAY(">") : ">");
    for (size_t i = 0; argv[i] != NULL; i++) {
        printf(use_colors ? COLOR_GRAY(" %s") : " %s", argv[i]);
    }
    putchar('\n');

    int exit_code = execv(executable_path, argv);
    // -1 is the bash code for unknown command, but
    // cpk treats -1 as exit code unset
    if (exit_code == -1) exit_code = 1;
    return exit_code;
}