#include <stdio.h>
#include <stdlib.h>

#include "tomlc17/src/tomlc17.h"

toml_result_t config_parse() {
    FILE* toml_cpk = fopen("cpk.toml", "r");
    if (!toml_cpk) {
        fprintf(
            stderr,
            "Could not open cpk.toml.\nYou may not have initialised the project (cpk init),\nor your config file was not found in the working directory.\n");
        exit(1);
    }
    toml_result_t config = toml_parse_file(toml_cpk);
    if (!config.ok) {
        fprintf(stderr, "Error parsing cpk.toml: %s\n", config.errmsg);
        fclose(toml_cpk);
        exit(1);
    }
    fclose(toml_cpk);
    return config;
}

void config_free(toml_result_t config) {
    toml_free(config);
}

char* toml_stringify_result(const toml_result_t* r);

int config_write_out(toml_result_t config) {
    char* txt = toml_stringify_result(&config);
    if (!txt) return 1;

    FILE* fp = fopen("cpk.toml", "w");
    if (!fp) {
        free(txt);
        perror("ERROR: could not open cpk.toml for writing");
        return 1;
    }

    fputs(txt, fp);
    fclose(fp);
    free(txt);
    return 0;
}