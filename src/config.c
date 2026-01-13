#include <stdio.h>
#include <stdlib.h>

#include "tomlc17/tomlc17.h"

toml_result_t parse_config() {
    FILE* toml_cpk_fp = fopen("cpk.toml", "r");
    if (!toml_cpk_fp) {
        fprintf(stderr, "Could not open cpk.toml.\nYou may not have initialised the project (cpk init),\nor your config file was not found in the working directory.\n");
        exit(1);
    }
    toml_result_t config = toml_parse_file(toml_cpk_fp);
    if (!config.ok) {
        fprintf(stderr, "Error parsing cpk.toml: %s\n", config.errmsg);
        fclose(toml_cpk_fp);
        exit(1);
    }
    fclose(toml_cpk_fp);
    return config;
}

void free_config(toml_result_t config) {
    toml_free(config);
}