/*
 * env.h
 *
 * Function to read environment variables for alternative commands from
 * the environment, otherwise providing defaults. Provides the commands
 * as variables.
 */

/**
 * Shell command to use for git operations.
 * Set by the CPK_GIT_COMMAND environment variable, or `git` by default.
 */
char* env_git_command;
/**
 * Shell command to use for curl operations.
 * Set by the CPK_CURL_COMMAND environment variable, or `curl` by default.
 */
char* env_curl_command;

/**
 * Read, in order of priority:
 * - the currently set environment variables
 * - the `.env` file in the current directory
 * for the environment variables
 */
void env_read();