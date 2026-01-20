#include "tomlc17/src/tomlc17.h"

toml_result_t config_parse();
void config_free(toml_result_t config);
int config_write_dependencies(char* dependencies);