#include <stdlib.h>
#include <string.h>

#include "tomlc17/tomlc17.h"

int compile_target(char* target, toml_result_t config) {
    // Check that the config defines the given target
    char* target_table = malloc((strlen(target) + 9) * sizeof(char));  // 8 == strlen("targets.") + 1
    strcpy(target_table, "targets.");
    strcat(target_table, target);

    toml_datum_t toml_target = toml_seek(config.toptab, target_table);
    if (toml_target.type != TOML_TABLE) {
        fprintf(stderr, "Error: cpk.toml: target %s is not defined\n", target_table);
        free(target_table);
        return 1;
    }
    free(target_table);

    // Grab the target dir name
    toml_datum_t toml_target_dir = toml_get(config.toptab, "target_dir");
    if (toml_target_dir.type != TOML_STRING) {
        fprintf(stderr, "Error: cpk.toml: target_dir is not a valid string\n");
        return 1;
    }
    const char* target_dir = toml_target_dir.u.str.ptr;
    int target_dir_len = toml_target_dir.u.str.len;

    return 0;
}

int run_target(char* target, toml_result_t config) {
    return 0;
}