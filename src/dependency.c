#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "store/store.h"
#include "toml/config.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Installs all the dependencies from the parsed cpk.toml
 */
int install_dependencies(toml_result_t config) {
    // Remove and recreate .cpk directory
    if (system("rm -r .cpk")) {
        perror(
            "Warning: Could not remove .cpk directory, will install and link dependencies anyway");
    }
    if (mkdir(".cpk", 0700) && errno != EEXIST) {
        perror("Error: Could not create cpk directory");
        return 1;
    }

    toml_datum_t dependencies_table = toml_get(config.toptab, "dependencies");
    if (dependencies_table.type != TOML_TABLE) {
        fprintf(stderr, "Error: cpk.toml: dependencies table not found");
        return 1;
    }
    int exit_code = 0;
    for (int i = 0; i < dependencies_table.u.tab.size; i++) {
        const char* dependency_key = dependencies_table.u.tab.key[i];
        toml_datum_t dependency_value = dependencies_table.u.tab.value[i];
        if (dependency_value.type != TOML_STRING) {
            fprintf(stderr, "ERROR: cpk.toml: dependency path for %s is not a string\n",
                    dependency_key);
            exit_code = 1;
            continue;
        }
        printf("\e[1m(%d/%d)\e[0m Installing and linking %s from %s...\n", i + 1,
               dependencies_table.u.tab.size, dependency_key, dependency_value.u.str.ptr);
        store_dependency_identifier identifier =
            store_resolve_identifier(dependency_value.u.str.ptr);
        if (!identifier.mode || store_get_dependency(&identifier) ||
            store_create_symlink(&identifier, dependency_key)) {
            exit_code = 1;
            continue;
        }
    }
    return exit_code;
}

/**
 * Growable string struct used to rebuild dependency tables to rewrite to cpk.toml.
 */
typedef struct {
    char* ptr;
    size_t len;
    size_t cap;
} dep_string;

dep_string dep_string_new(size_t starting_cap) {
    char* ptr = malloc(starting_cap);
    if (!ptr) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    dep_string out = {
        .ptr = malloc(starting_cap),
        .len = 16,
        .cap = starting_cap,
    };
    memcpy(out.ptr, "[dependencies]\n\n", 16);
    return out;
}

void dep_string_ensure_space(dep_string string, size_t size_needed) {
    if (string.len + size_needed >= string.cap) {
        string.cap *= 2;
        string.ptr = realloc(string.ptr, string.cap);
        if (!string.ptr) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
}

int dep_string_write(dep_string string) {
    string.ptr[string.len] = 0;
    int exit_code = config_write_dependencies(string.ptr);
    free(string.ptr);
    return exit_code;
}

/**
 * Installs all dependencies provided and adds them to cpk.toml
 * Expects a null-terminated array of dependency types as described by the help menu.
 */
int add_dependencies(char* new_dependencies[], toml_result_t config) {
    // get previous dependencies in cpk.toml (to re-stringify)
    toml_datum_t deps = toml_get(config.toptab, "dependencies");

    if (deps.type != TOML_TABLE && deps.type != TOML_UNKNOWN) {
        fprintf(stderr, "Error: cpk.toml: dependencies is not defined as a valid table");
        return 1;
    }

    // build string dynamically
    dep_string dep_string = dep_string_new(4096);

    // re-add existing dependencies if present
    if (deps.type == TOML_TABLE) {
        for (int i = 0; i < deps.u.tab.size; i++) {
            const char* dependency_key = deps.u.tab.key[i];
            size_t dependency_key_len = deps.u.tab.len[i];
            toml_datum_t dependency_val = deps.u.tab.value[i];
            if (dependency_val.type != TOML_STRING) {
                fprintf(stderr,
                        "Warning: dependency '%s' is not a string, removing from cpk.toml\n",
                        dependency_key);
                continue;
            }

            size_t space_needed =
                dependency_key_len + dependency_val.u.str.len + 6;  // key = "val"\n
            dep_string_ensure_space(dep_string, space_needed);

            dep_string.len += sprintf(dep_string.ptr + dep_string.len, "%s = \"%s\"\n",
                                      dependency_key, dependency_val.u.str.ptr);
        }
    }

    // add new dependencies from cli
    for (size_t i = 0; new_dependencies[i] != NULL; i++) {
        char* dep_equals = strchr(new_dependencies[i], '=');
        if (!dep_equals) {
            fprintf(
                stderr,
                "Error parsing dependency \"%s\": No equals sign separating local name from dependency value\nThis dependency will be skipped.\n",
                new_dependencies[i]);
            continue;
        }

        // split into key=value
        size_t key_len = dep_equals - new_dependencies[i];
        char dependency_key[256];
        if (key_len == 0) {
            fprintf(
                stderr,
                "Error: Dependency name not provided in \"%s\".\nThis dependency will be skipped.\n",
                new_dependencies[i]);
            continue;
        }
        if (key_len >= sizeof(dependency_key)) {
            fprintf(
                stderr,
                "Error: dependency name \"%s\"is too long (max 256 characters)\nThis dependency will be skipped.\n",
                new_dependencies[i]);
            continue;
        }
        memcpy(dependency_key, new_dependencies[i], key_len);
        dependency_key[key_len] = 0;
        const char* dependency_value = dep_equals + 1;
        if (dependency_value[0] == 0) {
            fprintf(
                stderr,
                "Error, Dependency value not provided in \"%s\".\nThis dependency will be skipped.\n",
                new_dependencies[i]);
            continue;
        }

        // check that the key was not already defined in previous toml or arg dependencies
        bool duplicate = false;
        const char* p = dep_string.ptr;
        while (*p) {
            const char* line_start = p;
            const char* newline = strchr(p, '\n');
            size_t len = newline ? (size_t)(newline - p) : strlen(p);
            // check if line starts with 'key ='
            if (len > key_len + 3 && strncmp(line_start, dependency_key, key_len) == 0 &&
                line_start[key_len] == ' ' && line_start[key_len + 1] == '=') {
                duplicate = true;
                break;
            }
            // move to next line
            p = newline ? newline + 1 : p + len;
        }

        if (duplicate) {
            fprintf(
                stderr,
                "Error adding dependency \"%1$s\": Dependency name \"%2$s\" was already defined in cpk.toml or another argument.\nIf you intended to replace it, please run \"cpk remove %2$s\" first.\nThis dependency will be skipped.\n",
                new_dependencies[i], dependency_key);
            continue;
        }

        // Install and link the dependency
        printf("Installing and linking %s from %s...\n", dependency_key, dependency_value);
        store_dependency_identifier identifier = store_resolve_identifier(dependency_value);
        if (!identifier.mode) {
            fprintf(stderr, "This dependency will be skipped.\n");
            continue;
        }
        store_get_dependency(&identifier) || store_create_symlink(&identifier, dependency_key);
        size_t space_needed =
            key_len + strlen(dependency_value) +
            7;  // key = "val"\n, plus an extra space for a null-terminator if at end

        dep_string_ensure_space(dep_string, space_needed);
        dep_string.len += sprintf(dep_string.ptr + dep_string.len, "%s = \"%s\"\n", dependency_key,
                                  dependency_value);
    }
    // Write the updated dependencies into cpk.toml
    return dep_string_write(dep_string);
}

int remove_dependency(char* dependency, toml_result_t config) {
    bool removed = false;

    // get previous dependencies in cpk.toml (to re-stringify)
    toml_datum_t deps = toml_get(config.toptab, "dependencies");

    if (deps.type != TOML_TABLE) {
        fprintf(stderr, "Error: cpk.toml: dependencies is not defined as a valid table");
        return 1;
    }
    dep_string dep_string = dep_string_new(4096);

    // re-add all dependencies except for the one to be removed
    if (deps.type == TOML_TABLE) {
        for (int i = 0; i < deps.u.tab.size; i++) {
            const char* dependency_key = deps.u.tab.key[i];
            size_t dependency_key_len = deps.u.tab.len[i];
            toml_datum_t dependency_val = deps.u.tab.value[i];
            if (dependency_val.type != TOML_STRING) {
                fprintf(stderr,
                        "Warning: dependency '%s' is not a string, removing from cpk.toml\n",
                        dependency_key);
                continue;
            }
            if (!strcmp(dependency_key, dependency)) {
                removed = true;
                continue;
            }
            size_t space_needed =
                dependency_key_len + dependency_val.u.str.len + 6;  // key = "val"\n
            dep_string_ensure_space(dep_string, space_needed);
            dep_string.len += sprintf(dep_string.ptr + dep_string.len, "%s = \"%s\"\n",
                                      dependency_key, dependency_val.u.str.ptr);
        }
    }
    if (!removed) {
        fprintf(stderr, "Error: Could not remove dependency \"%s\": Not found in cpk.toml\n",
                dependency);
        return 1;
    }
    // Otherwise unlink the dependency and rewrite the new dependency string
    printf("Unlinking and removing %s from the project\n", dependency);
    return store_remove_symlink(dependency) || dep_string_write(dep_string);
}