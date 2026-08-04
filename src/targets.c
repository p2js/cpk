#include <stdio.h>

#include "tomlc17/src/tomlc17.h"

int list_targets(toml_result_t config) {
    toml_datum_t dependencies_table = toml_get(config.toptab, "target");
    if (dependencies_table.type != TOML_TABLE) {
        fprintf(stderr, "ERROR: cpk.toml: target table not found");
        return 1;
    }

    for (int i = 0; i < dependencies_table.u.tab.size; i++) {
        const char* dependency_key = dependencies_table.u.tab.key[i];
        printf("%s ", dependency_key);
    }

    putchar('\n');

    return 0;
}