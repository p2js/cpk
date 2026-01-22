/*
 * store.c
 * Functions for interacting with the dependency store of a cpk installation.
 */
#include "store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xxHash/xxhash.h"

const char* GITHUB_URL = "https://github.com/";

/*
 * Should have:
 * - A function for parsing/resolving dependency identifiers
 * - A function for downloading a dependency
 * - A function for removing a dependency
 * - A function for updating a dependency
 */

static char store_dir[4096];

/**
 * Checks where dependencies should be stored and whether the store is accessible,
 * creating the directory if necessary and setting the directory variable.
 */
int store_init() {
    char* xdg_cache_home = getenv("XDG_CACHE_HOME");

    if (xdg_cache_home) {
        if (strlen(xdg_cache_home) + 6 > 4096) {
            fprintf(stderr, "Error: Could not initialise dependency store, XDG_CACHE_HOME path is too long");
        }
        strcpy(store_dir, xdg_cache_home);
        strcat(store_dir, "/cpk/");
    } else {
        char* home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "Error: Could not initialise dependency store, HOME is not set");
            return 1;
        }
        if (strlen(home) + 13 > 4096) {
            fprintf(stderr, "Error: Could not initialise dependency store, HOME path is too long");
        }
        strcpy(store_dir, getenv("HOME"));
        strcat(store_dir, "/.cache/cpk/");
    }

    if (mkdir(store_dir, 0700) && errno != EEXIST) {
        perror("FATAL: cpk store directory could not be created");
        return 1;
    }
    return 0;
}

store_dependency_identifier store_resolve_identifier(const char* ident_string) {
    store_dependency_identifier ident = {0};

    XXH128_hash_t ident_string_hash = XXH3_128bits(ident_string, strlen(ident_string));
    sprintf(ident.path, "%s%016lx%016lx", store_dir, ident_string_hash.high64, ident_string_hash.low64);

    if (!strncmp("gh:", ident_string, 3)) {
        ident.mode = DEPENDENCY_GIT;
        strcpy(ident.URL, GITHUB_URL);
        // Determine potential git path at end then cut off string
        size_t a = strlen(ident_string);
        int i;
        for (i = 3; i < a; i++) {
            if (ident_string[i] == ':' && ident_string[i + 1] == ':') {
                strncpy(ident.git_path, ident_string + i + 2, 256);
                break;
            }
        }
        // Copy the remaining URL to the right field
        if (i >= 2048) {
            fprintf(stderr, "Error: %s: URL is too long", ident_string);
            ident.mode = DEPENDENCY_UNKNOWN;
        } else {
            strncat(ident.URL, ident_string + 3, i - 3);
        }
    } else if (!strncmp("git:", ident_string, 4)) {
        ident.mode = DEPENDENCY_GIT;
        // Determine potential git path at end then cut off string
        size_t a = strlen(ident_string);
        int i;
        for (i = 2; i < a; i++) {
            if (ident_string[i] == ':' && ident_string[i + 1] == ':') {
                strcpy(ident.git_path, ident_string + i + 2);
                break;
            }
        }
        // Copy the remaining URL
        strncat(ident.URL, ident_string + 4, i - 4);
    } else if (!strncmp("file:", ident_string, 5)) {
        // Local file on disk
        ident.mode = DEPENDENCY_FILE;
        strcpy(ident.path, ident_string + 5);
    } else if (!strncmp("web:", ident_string, 4)) {
        ident.mode = DEPENDENCY_WEB;
        // Simple URL
        if (strlen(ident_string + 4) > 2048) {
            fprintf(stderr, "Errpr: %s: URL is too long", ident_string);
        } else {
            strcpy(ident.URL, ident_string + 4);
        }
    } else if (!strncmp("zip:", ident_string, 4)) {
        ident.mode = DEPENDENCY_ZIP;
        // Simple URL
        if (strlen(ident_string + 4) > 2048) {
            fprintf(stderr, "Errpr: %s: URL is too long", ident_string);
        } else {
            strcpy(ident.URL, ident_string + 4);
        }
    } else if (!strncmp("tar:", ident_string, 4)) {
        ident.mode = DEPENDENCY_TAR;
        // Simple URL
        if (strlen(ident_string + 4) > 2048) {
            fprintf(stderr, "Errpr: %s: URL is too long", ident_string);
        } else {
            strcpy(ident.URL, ident_string + 4);
        }
    } else {
        fprintf(stderr, "Error: %s does not represent a valid dependency identifier\n", ident_string);
        ident.mode = DEPENDENCY_UNKNOWN;
    }

    return ident;
};

int store_get_dependency(store_dependency_identifier dependency) {
    if (dependency.mode == DEPENDENCY_FILE) return 0;  // Local dependencies do not need to be installed
    if (dependency.mode != DEPENDENCY_GIT) {
        printf("Downloading non-git dependencies is currently unimplemented\n");
        return 1;
    }
    char cwd[4096];
    getcwd(cwd, 4096);

    if (mkdir(dependency.path, 0700)) {
        if (errno == EEXIST) return 0;  // Dependency already in store
        fprintf(stderr, "Error creating dependency directory for %s: ", dependency.URL);
        perror("");
        return 1;
    }

    chdir(dependency.path);

    if (dependency.mode == DEPENDENCY_GIT) {
        char git_clone_command[2048 + 12];
        snprintf(git_clone_command, 2048 + 12, "git clone %s .", dependency.URL);

        if (system(git_clone_command)) {
            printf("Error cloning dependency from %s\n", dependency.URL);
            return 1;
        }
        if (dependency.git_path[0] != 0) {
            char git_checkout_command[256 + 13] = "git checkout ";
            strcat(git_checkout_command, dependency.git_path);

            if (system(git_checkout_command)) {
                printf("Error checking out %s\n", dependency.git_path);
                return 1;
            }
        }
    } else {
        // TODO
    }

    chdir(cwd);
    return 0;
}

int store_remove_dependency(store_dependency_identifier dependency) {
    char rm_command[4096 + 27] = "rm -r --interactive=never ";
    strcat(rm_command, dependency.path);
    return system(rm_command);
}

int store_update_dependency(store_dependency_identifier dependency) {
    switch (dependency.mode) {
        case DEPENDENCY_FILE:
            printf("Dependency is local and therefore cannot be updated\n");
            return 0;
        case DEPENDENCY_GIT:
            if (chdir(dependency.path)) {
                perror("Error: Could not find dependency directory");
                return 1;
            }
            if (system("git pull")) {
                fprintf(stderr, "Error updating dependency from %s", dependency.git_path);
                return 1;
            }
            return 0;
        default:
            printf("Dependency is not a git repository, reinstalling manually\n");
            store_remove_dependency(dependency);
            return store_get_dependency(dependency);
    }
}

int store_create_symlink(store_dependency_identifier dependency, const char* local_name) {
    char local_dependency_path[4096];
    if (strlen(local_name) + 6 > 4096) {
        fprintf(stderr, "Could not link %s: Local name is too long", local_name);
        return 1;
    }
    strcpy(local_dependency_path, ".cpk/");
    strcat(local_dependency_path, local_name);

    if (symlink(dependency.path, local_dependency_path) && errno != EEXIST) {
        fprintf(stderr, "Could not install dependency %s in local project: ", local_name);
        perror("");
        return 1;
    }
    return 0;
}

int store_remove_symlink(const char* local_name) {
    char local_dependency_path[4096];
    if (strlen(local_name) + 6 > 4096) {
        fprintf(stderr, "Could not unlink %s: Local name is too long", local_name);
        return 1;
    }
    strcpy(local_dependency_path, ".cpk/");
    strcat(local_dependency_path, local_name);

    if (unlink(local_dependency_path)) {
        perror("Could not unlink dependency");
        return 1;
    }

    return 0;
}