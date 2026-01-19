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
    if (system("rm -rf .cpk")) {
        perror("Warning: Could not remove .cpk directory, will install and link dependencies anyway");
    } else {
        if (mkdir(".cpk", 0700)) {
            perror("Error: Could not create cpk directory");
            return 1;
        }
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
            fprintf(stderr, "ERROR: cpk.toml: dependency path for %s is not a string\n", dependency_key);
            exit_code = 1;
            continue;
        }
        printf("Installing and linking %s from %s...\n", dependency_key, dependency_value.u.str.ptr);
        store_dependency_identifier identifier = store_resolve_identifier(dependency_value.u.str.ptr);
        if (store_get_dependency(identifier)) {
            exit_code = 1;
            continue;
        };
        if (store_create_symlink(identifier, dependency_key)) {
            exit_code = 1;
            continue;
        }
    }
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
    size_t dep_cap = 4096;
    size_t dep_len = 16;
    char* dep_str = malloc(dep_cap);
    if (!dep_str) {
        fprintf(stderr, "Error: Out of memory\n");
        return 1;
    }
    memcpy(dep_str, "[dependencies]\n\n", 16);

    // re-add existing dependencies if present
    if (deps.type == TOML_TABLE) {
        for (int i = 0; i < deps.u.tab.size; i++) {
            const char* dependency_key = deps.u.tab.key[i];
            size_t dependency_key_len = deps.u.tab.len[i];
            toml_datum_t dependency_val = deps.u.tab.value[i];
            if (dependency_val.type != TOML_STRING) {
                fprintf(stderr, "Warning: dependency '%s' is not a string, skipping\n", dependency_key);
                continue;
            }

            size_t space_needed = dependency_key_len + dependency_val.u.str.len + 6;  // key = "val"\n
            if (dep_len + space_needed >= dep_cap) {
                dep_cap *= 2;
                dep_str = realloc(dep_str, dep_cap);
                if (!dep_str) {
                    fprintf(stderr, "Out of memory\n");
                    return 1;
                }
            }

            dep_len += sprintf(dep_str + dep_len,
                "%s = \"%s\"\n",
                dependency_key,
                dependency_val.u.str.ptr);
        }
    }

    // add new dependencies from cli
    for (size_t i = 0; new_dependencies[i] != NULL; i++) {
        char* dep_equals = strchr(new_dependencies[i], '=');
        if (!dep_equals) {
            fprintf(stderr, "Error parsing dependency \"%s\": No equals sign separating local name from dependency value\n", new_dependencies[i]);
            continue;
        }

        // Split into key=value
        size_t key_len = dep_equals - new_dependencies[i];
        char dependency_key[256];
        if (key_len >= sizeof(dependency_key)) {
            fprintf(stderr, "Error: dependency key too long (max 256 characters): %s\n", new_dependencies[i]);
            continue;
        }
        memcpy(dependency_key, new_dependencies[i], key_len);
        dependency_key[key_len] = 0;
        const char* dependency_value = dep_equals + 1;

        // Install and link the dependency
        printf("Installing and linking %s from %s...\n", dependency_key, dependency_value);
        store_dependency_identifier identifier = store_resolve_identifier(dependency_value);
        store_get_dependency(identifier) || store_create_symlink(identifier, dependency_key);
        size_t space_needed = key_len + strlen(dependency_value) + 7;  // key = "val"\n, plus an extra space for a null-terminator if at end
        if (dep_len + space_needed >= dep_cap) {
            dep_cap *= 2;
            dep_str = realloc(dep_str, dep_cap);
            if (!dep_str) {
                fprintf(stderr, "Out of memory\n");
                return 1;
            }
        }

        dep_len += sprintf(dep_str + dep_len,
            "%s = \"%s\"\n",
            dependency_key,
            dependency_value);
    }

    // Write the updated dependencies into cpk.toml
    dep_str[dep_len] = 0;
    int exit_code = config_add_dependencies(dep_str);

    free(dep_str);
    return exit_code;
}