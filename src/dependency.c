#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dir/rmdir_recursive.h"
#include "store/store.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Installs all the dependencies from the parsed cpk.toml
 */
int install_dependencies(toml_result_t config) {
    // Remove and recreate .cpk directory
    if (rmdir_r(".cpk", true) && errno != ENOENT) {
        perror(
            "WARNING: Could not remove .cpk directory, will install and link dependencies anyway");
    }
    if (mkdir(".cpk", 0700) && errno != EEXIST) {
        perror("ERROR: Could not create cpk directory");
        return 1;
    }

    toml_datum_t dependencies_table = toml_get(config.toptab, "dependencies");
    if (dependencies_table.type != TOML_TABLE) {
        fprintf(stderr, "ERROR: cpk.toml: dependencies table not found");
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
        printf("\e[1m(%d/%d)\e[0m Installing and linking %s from \"%s\"\n", i + 1,
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

// add_dependencies and remove_dependency need access to some tomlc17 internal functions
typedef struct pool_t pool_t;
char* pool_alloc(pool_t* pool, int n);
char* cell_realloc(char* p, int size);
void datum_free(toml_datum_t* datum);

/**
 * Installs all dependencies provided and adds them to cpk.toml
 * Expects a null-terminated array of dependency types as described by the help menu.
 */
int add_dependencies(char* new_dependencies[], toml_result_t config) {
    toml_datum_t* deps = NULL;

    for (int i = 0; i < config.toptab.u.tab.size; i++) {
        if (!strcmp(config.toptab.u.tab.key[i], "dependencies")) {
            deps = &config.toptab.u.tab.value[i];
            break;
        }
    }

    if (!deps || deps->type != TOML_TABLE) {
        fprintf(stderr, "ERROR: cpk.toml: dependencies is not defined as a valid table\n");
        return 1;
    }

    for (size_t i = 0; new_dependencies[i] != NULL; i++) {
        char* dep_equals = strchr(new_dependencies[i], '=');

        if (!dep_equals) {
            fprintf(
                stderr,
                "Error parsing dependency \"%s\": No equals sign separating local name from dependency value\n"
                "This dependency will be skipped.\n",
                new_dependencies[i]);
            continue;
        }

        size_t key_len = dep_equals - new_dependencies[i];

        if (key_len == 0) {
            fprintf(stderr,
                    "ERROR: Dependency name not provided in \"%s\".\n"
                    "This dependency will be skipped.\n",
                    new_dependencies[i]);
            continue;
        }

        char dependency_key[256];

        if (key_len >= sizeof(dependency_key)) {
            fprintf(stderr,
                    "ERROR: dependency name \"%s\" is too long\n"
                    "This dependency will be skipped.\n",
                    new_dependencies[i]);
            continue;
        }

        memcpy(dependency_key, new_dependencies[i], key_len);
        dependency_key[key_len] = '\0';

        const char* dependency_value = dep_equals + 1;

        if (dependency_value[0] == '\0') {
            fprintf(stderr,
                    "ERROR: dependency value not provided in \"%s\".\n"
                    "This dependency will be skipped.\n",
                    new_dependencies[i]);
            continue;
        }

        bool duplicate = false;

        for (int j = 0; j < deps->u.tab.size; j++) {
            if (deps->u.tab.value[j].type != TOML_STRING) continue;

            if (!strcmp(deps->u.tab.key[j], dependency_key)) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            fprintf(
                stderr,
                "Error adding dependency \"%s\": Dependency name \"%s\" was already defined in cpk.toml.\n"
                "If you intended to replace it, please remove it first.\n"
                "This dependency will be skipped.\n",
                new_dependencies[i], dependency_key);
            continue;
        }

        printf("Installing and linking %s from \"%s\"\n", dependency_key, dependency_value);
        store_dependency_identifier identifier = store_resolve_identifier(dependency_value);
        if (!identifier.mode) {
            fprintf(stderr, "This dependency will be skipped.\n");
            continue;
        }
        if (!store_get_dependency(&identifier)) {
            store_create_symlink(&identifier, dependency_key);
        }

        int index = deps->u.tab.size;
        int new_size = index + 1;

        deps->u.tab.key =
            (const char**)cell_realloc((char*)deps->u.tab.key, sizeof(*deps->u.tab.key) * new_size);

        deps->u.tab.len =
            (int*)cell_realloc((char*)deps->u.tab.len, sizeof(*deps->u.tab.len) * new_size);

        deps->u.tab.value = (toml_datum_t*)cell_realloc((char*)deps->u.tab.value,
                                                        sizeof(*deps->u.tab.value) * new_size);

        if (!deps->u.tab.key || !deps->u.tab.len || !deps->u.tab.value) {
            fprintf(stderr, "ERROR: Failed reallocating dependencies table\n");
            return 1;
        }

        char* key_copy = pool_alloc(config.__internal, key_len + 1);
        if (!key_copy) {
            fprintf(stderr, "ERROR: Failed allocating dependency key\n");
            return 1;
        }

        memcpy(key_copy, dependency_key, key_len + 1);

        size_t value_len = strlen(dependency_value);

        char* value_copy = pool_alloc(config.__internal, value_len + 1);
        if (!value_copy) {
            fprintf(stderr, "ERROR: Failed allocating dependency value\n");
            return 1;
        }

        memcpy(value_copy, dependency_value, value_len + 1);

        deps->u.tab.key[index] = key_copy;
        deps->u.tab.len[index] = key_len;

        deps->u.tab.value[index] = (toml_datum_t){
            .type = TOML_STRING,
            .u.str =
                {
                    .ptr = value_copy,
                    .len = value_len,
                },
        };

        deps->u.tab.size = new_size;
    }

    return 0;
}

int remove_dependency(char* dependency, toml_result_t config) {
    bool removed = false;

    toml_datum_t deps = toml_get(config.toptab, "dependencies");
    if (deps.type != TOML_TABLE) {
        fprintf(stderr, "ERROR: cpk.toml: dependencies is not defined as a valid table");
        return 1;
    }

    for (int i = 0; i < deps.u.tab.size; i++) {
        const char* dependency_key = deps.u.tab.key[i];
        toml_datum_t* dependency_val = &deps.u.tab.value[i];

        if (!strcmp(dependency_key, dependency)) {
            datum_free(dependency_val);
            dependency_val->type = TOML_UNKNOWN;
            removed = true;
            break;
        }
    }

    if (!removed) {
        fprintf(stderr, "ERROR: Could not remove dependency \"%s\": Not found in cpk.toml\n",
                dependency);
        return 1;
    }

    printf("Unlinking and removing %s from the project\n", dependency);
    return store_remove_symlink(dependency);
}
