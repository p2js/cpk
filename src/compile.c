#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dir/snapshot.h"
#include "tomlc17/tomlc17.h"

int compile_target(char* target, toml_result_t config) {
    // Check that the config defines the given target

    char target_table_name[4096];
    snprintf(target_table_name, 4096, "targets.%s", target);

    toml_datum_t toml_target = toml_seek(config.toptab, target_table_name);
    if (toml_target.type != TOML_TABLE) {
        fprintf(stderr, "Error: cpk.toml: target %s is not defined\n", target_table_name);
        return 1;
    }

    // Grab the target dir name
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target_dir");
    if (toml_target_dir.type != TOML_STRING) {
        fprintf(stderr, "Error: cpk.toml: target_dir is not a valid string\n");
        return 1;
    }
    const char* target_dir = toml_target_dir.u.str.ptr;
    int target_dir_len = toml_target_dir.u.str.len;

    // Make the build and target directories if it doesn't already exist
    int mkdir_result = mkdir(target_dir, 0700);
    if (mkdir_result && errno != EEXIST) {
        fprintf(stderr, "Could not create targets directory %s: ", target_dir);
        perror("");
        return 1;
    }

    char dirpath[4096];

    strcpy(dirpath, target_dir);
    dirpath[target_dir_len] = '/';
    strcpy(dirpath + target_dir_len + 1, target);

    mkdir_result = mkdir(dirpath, 0700);

    if (mkdir_result && errno != EEXIST) {
        fprintf(stderr, "Could not create target directory %s: ", dirpath);
        perror("");
        return 1;
    }

    // Compile code using the build script
    toml_datum_t toml_target_build = toml_get(toml_target, "build");
    if (toml_target_build.type != TOML_STRING) {
        fprintf(stderr, "Error: cpk.toml: target %s does not provide a build command string", target);
        return 1;
    }

    // Capture project directory before build command
    dir_snapshot before = snapshot_directory(".");

    // Run build command
    int build_result = system(toml_target_build.u.str.ptr);
    if (build_result != 0) {
        fprintf(stderr, "Error: build command returned non-zero exit code\n");
        snapshot_free(&before);
        return 1;
    }
    // Capture and diff directory after build, move any new files to target
    dir_snapshot after = snapshot_directory(".");
    dir_snapshot diff = diff_snapshots(&before, &after);
    if (!diff.count) {
        fprintf(stderr, "Warning: build command did not produce any output files\n");
    }
    move_snapshot_diff_items(&diff, ".", dirpath);

    snapshot_free(&before);
    snapshot_free(&after);
    snapshot_free(&diff);

    return 0;
}

int run_target(char* target, toml_result_t config) {
    // This runs after compile_target, so we are guaranteed that target_dir/target/ exists
    return 0;
}