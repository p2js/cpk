#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dir/rmdir_recursive.h"
#include "fmt/color.h"
#include "store/store.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Installs all the dependencies from the parsed cpk.toml
 */
int install_dependencies(toml_result_t config) {
    // Remove and recreate .cpk directory
    if (rmdir_r(".cpk", true) && errno != ENOENT) {
        print_err(true, "could not remove the .cpk directory");
        return 1;
    }
    if (mkdir(".cpk", 0700) && errno != EEXIST) {
        print_err(true, "could not create the .cpk directory");
        return 1;
    }

    toml_datum_t dependencies_table = toml_get(config.toptab, "dependencies");
    if (dependencies_table.type != TOML_TABLE) {
        print_err(false, "cpk.toml: dependencies table not found");
        return 1;
    }

    int exit_code = 0;
    for (int i = 0; i < dependencies_table.u.tab.size; i++) {
        const char* dependency_key = dependencies_table.u.tab.key[i];
        toml_datum_t dependency_value = dependencies_table.u.tab.value[i];
        if (dependency_value.type != TOML_STRING) {
            print_err(false, "cpk.toml: dependency path for %s is not a string", dependency_key);
            exit_code = 1;
            continue;
        }
        printf(use_colors() ? COLOR_BOLD("(%d/%d)") : "(%d/%d)", i + 1,
               dependencies_table.u.tab.size);
        printf(" Installing and linking %s from \"%s\"\n", dependency_key,
               dependency_value.u.str.ptr);

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
        print_err(false, "cpk.toml: dependencies is not defined as a valid table");
        return 1;
    }

    for (size_t i = 0; new_dependencies[i] != NULL; i++) {
        char* dep_equals = strchr(new_dependencies[i], '=');

        if (!dep_equals) {
            print_err(
                false,
                "parsing dependency \"%s\" failed: no equals sign ('=') separating local name from dependency value\n"
                "      this dependency will be skipped",
                new_dependencies[i]);
            continue;
        }

        size_t key_len = dep_equals - new_dependencies[i];

        if (key_len == 0) {
            print_err(false,
                      "parsing dependency \"%s\" failed: no name provided\n"
                      "      this dependency will be skipped",
                      new_dependencies[i]);
            continue;
        }

        char dependency_key[256];

        if (key_len >= sizeof(dependency_key)) {
            print_err(false,
                      "parsing dependency\"%s\" failed: name is too long\n"
                      "      this dependency will be skipped",
                      new_dependencies[i]);
            continue;
        }

        memcpy(dependency_key, new_dependencies[i], key_len);
        dependency_key[key_len] = '\0';

        const char* dependency_value = dep_equals + 1;

        if (dependency_value[0] == '\0') {
            print_err(false,
                      "parsing dependency \"%s\" failed: no value provided\n"
                      "      this dependency will be skipped",
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
            print_err(
                false,
                "adding dependency \"%s\" failed: dependency name \"%s\" was already defined in cpk.toml\n"
                "      (if you intended to replace it, please remove it first)\n"
                "      this dependency will be skipped",
                new_dependencies[i], dependency_key);
            continue;
        }

        print_info("Installing and linking %s from \"%s\"", dependency_key, dependency_value);
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
            print_err(true, "failed reallocating memory for dependencies table");
            return 1;
        }

        char* key_copy = pool_alloc(config.__internal, key_len + 1);
        if (!key_copy) {
            print_err(true, "failed reallocating memory for dependencies table");
            return 1;
        }

        memcpy(key_copy, dependency_key, key_len + 1);

        size_t value_len = strlen(dependency_value);

        char* value_copy = pool_alloc(config.__internal, value_len + 1);
        if (!value_copy) {
            print_err(true, "failed reallocating memory for dependencies table");
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
        print_err(false, "cpk.toml: dependencies is not defined as a valid table");
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
        print_err(false, "removing dependency \"%s\" failed: Not found in cpk.toml", dependency);
        return 1;
    }

    print_info("unlinking and removing %s from the project", dependency);
    return store_remove_symlink(dependency);
}
