/**
 * env.c
 *
 * Function to read environment variables for alternative commands from
 * the environment, otherwise providing defaults. Provides the commands
 * as variables.
 */
#include <stdlib.h>

#include "dotenv-c/src/dotenv.h"

char* env_git_command = "git";
char* env_curl_command = "curl";

void env_read() {
    env_load(".env", false);

    char* getenv_git_command = getenv("CPK_GIT_COMMAND");
    if (getenv_git_command) env_git_command = getenv_git_command;

    char* getenv_curl_command = getenv("CPK_CURL_COMMAND");
    if (getenv_curl_command) env_curl_command = getenv_curl_command;
}