#include "store/store.h"
#include "tomlc17/src/tomlc17.h"

/**
 * Installs all the dependencies from the parsed cpk.toml
 */
int install_dependencies(toml_result_t config) {
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
            fprintf(stderr, "Error: cpk.toml: dependency path for %s is not a string", dependency_key);
            exit_code = 1;
            continue;
        }
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