#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

toml_result_t config_parse() {
    FILE* toml_cpk = fopen("cpk.toml", "r");
    if (!toml_cpk) {
        fprintf(stderr, "Could not open cpk.toml.\nYou may not have initialised the project (cpk init),\nor your config file was not found in the working directory.\n");
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

int config_write_dependencies(char* dependencies_block) {
    // read entire cpk.toml
    FILE* f = fopen("cpk.toml", "r");
    if (!f) {
        perror("Error: Could not open cpk.toml for reading");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* file = malloc(size + 1);
    if (!file) {
        fclose(f);
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    fread(file, 1, size, f);
    file[size] = 0;
    fclose(f);

    // locate the [dependencies] header
    char* p = file;
    char* deps_start = NULL;

    while (*p) {
        char* line_start = p;
        // move to end of line
        while (*p && *p != '\n') p++;
        char* line_end = p;
        // skip newline
        if (*p == '\n') p++;
        // trim leading whitespace
        char* trimmed = line_start;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (strncmp(trimmed, "[dependencies]", 14) == 0) {
            deps_start = line_start;
            break;
        }
    }
    if (!deps_start) {
        // not found, append at end
        FILE* out = fopen("cpk.toml", "a");
        if (!out) {
            perror("Error: Could not open cpk.toml for appending");
            free(file);
            return 1;
        }
        // print newline to separate table
        if (size > 0 && file[size - 1] != '\n') fprintf(out, "\n");
        fprintf(out, "%s\n", dependencies_block);
        fclose(out);
        free(file);
        return 0;
    }
    // otherwise, find where the table ends
    char* deps_end = p;
    while (*deps_end) {
        char* line_start = deps_end;
        while (*deps_end && *deps_end != '\n') deps_end++;
        if (*deps_end == '\n') deps_end++;
        char* trimmed = line_start;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        if (*trimmed == '[') {
            deps_end = line_start;
            break;
        }
    }
    // Build new file contents
    size_t prefix_len = deps_start - file;
    size_t suffix_len = strlen(deps_end);
    size_t new_block_len = strlen(dependencies_block);

    size_t new_size = prefix_len + new_block_len + suffix_len + 2;
    char* new_file = malloc(new_size);
    if (!new_file) {
        fprintf(stderr, "Out of memory\n");
        free(file);
        return 1;
    }

    // copy prefix, insert new dependency block, add newline, then suffix
    memcpy(new_file, file, prefix_len);
    memcpy(new_file + prefix_len, dependencies_block, new_block_len);
    new_file[prefix_len + new_block_len] = '\n';
    memcpy(new_file + prefix_len + new_block_len + 1, deps_end, suffix_len + 1);
    // write back to file
    FILE* out = fopen("cpk.toml", "w");
    if (!out) {
        perror("Error: Could not open cpk.toml for writing");
        free(file);
        free(new_file);
        return 1;
    }
    fwrite(new_file, 1, strlen(new_file), out);
    fclose(out);

    free(file);
    free(new_file);
    return 0;
}
