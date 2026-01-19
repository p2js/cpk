#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dir/snapshot.h"
#include "tomlc17/src/tomlc17.h"

int compile_target(char* target, toml_result_t config) {
    // Check that the config defines the given target

    char target_table_name[4096];
    snprintf(target_table_name, 4096, "targets.%s", target);

    toml_datum_t toml_target = toml_seek(config.toptab, target_table_name);
    if (toml_target.type != TOML_TABLE) {
        fprintf(stderr, "Error: cpk.toml: target %s is not defined\n", target);
        return 1;
    }

    // Grab the target dir name, use current dire
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target_dir");
    if (toml_target_dir.type != TOML_STRING) {
        fprintf(stderr, "Info: cpk.toml: target_dir not defined, using current directory\n");
    }
    const char* target_dir = toml_target_dir.type == TOML_STRING 
        ? toml_target_dir.u.str.ptr
        : NULL;
    int target_dir_len = toml_target_dir.type == TOML_STRING ? toml_target_dir.u.str.len : 0;

    char dirpath[4096];
    bool move = true;

    if(target_dir) {
        // Make the build and target directories if they don't already exist
        if (mkdir(target_dir, 0700) && errno != EEXIST) {
            fprintf(stderr, "Error: Could not create targets directory %s: ", target_dir);
            perror("");
            return 1;
        }
        strcpy(dirpath, target_dir);
        dirpath[target_dir_len] = '/';
        strcpy(dirpath + target_dir_len + 1, target);
        if (mkdir(dirpath, 0700) && errno != EEXIST) {
            fprintf(stderr, "Could not create target directory %s: ", dirpath);
            perror("");
            return 1;
        }
    } else {
        move = false;
        dirpath[0] = '.';
        dirpath[1] = 0;
    }

    // Compile code using the build script
    toml_datum_t toml_target_build = toml_get(toml_target, "build");
    if (toml_target_build.type != TOML_STRING) {
        fprintf(stderr, "Error: cpk.toml: target %s does not provide a build command string", target);
        return 1;
    }
    const char* build_command = toml_target_build.u.str.ptr;

    // Inject dependencies via CFLAGS environment variable
    char* original_cflags = getenv("CFLAGS");
    if (!original_cflags) original_cflags = "";
    char* new_cflags = calloc(strlen(original_cflags) + 7, sizeof(char));
    strcpy(new_cflags, original_cflags);
    strcat(new_cflags, " -I.cpk");

    setenv("CFLAGS", new_cflags, true);

    // Capture project directory before build command
    dir_snapshot before = snapshot_directory(".");

    // Run build command
    printf("> %s\n", build_command);
    int build_result = system(build_command);
    if (build_result) {
        fprintf(stderr, "Error: build command returned non-zero exit code\n");
        snapshot_free(&before);
        return build_result;
    }
    // Capture and diff directory after build, move any new files to target
    dir_snapshot after = snapshot_directory(".");
    dir_snapshot diff = diff_snapshots(&before, &after);

    if (!diff.count && toml_target_dir.type == TOML_STRING) {
        fprintf(stderr, "Warning: build command did not produce any output files\n");
    }
    move_snapshot_diff_items(&diff, ".", dirpath);

    snapshot_free(&before);
    snapshot_free(&after);
    snapshot_free(&diff);

    // reset CFLAGS
    free(new_cflags);
    setenv("CFLAGS", original_cflags, true);

    return 0;
}

int run_target(char* target, toml_result_t config, char* argv[]) {
    // This runs after compile_target, so we are guaranteed that target_dir/target/ exists
    // and that both are validly defined in the toml
    char executable_path[4096];
    snprintf(executable_path, 4096, "targets.%s.ex", target);

    toml_datum_t target_ex = toml_seek(config.toptab, executable_path);
    if (target_ex.type != TOML_STRING) {
        printf("Error: cpk.toml: target %s does not provide an executable name\n", target);
        return 1;
    }
    const char* executable_name = target_ex.u.str.ptr;
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target_dir");
    if(toml_target_dir.type == TOML_STRING) {
        snprintf(executable_path, 4096, 
            "%s/%s/%s", 
            toml_target_dir.u.str.ptr, 
            target, 
            executable_name);
    } else {
        strcpy(executable_path, executable_name);
    }
    
    printf("> %s", executable_path);

    for (size_t i = 0; argv[i] != NULL; i++) {
        printf(" %s", argv[i]);
    }
    putchar('\n');

    int exit_code = execv(executable_path, argv);
    return exit_code;
}