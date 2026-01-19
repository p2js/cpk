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

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
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
        strcpy(store_dir, xdg_cache_home);
        strcat(store_dir, "/cpk/");
    } else {
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
            if (ident_string[i] == '-') {
                strcpy(ident.git_path, ident_string + i + 1);
                break;
            }
        }
        // Copy the remaining URL to the right field
        strncat(ident.URL, ident_string + 3, i - 3);
    } else if (!strncmp("git:", ident_string, 4)) {
        ident.mode = DEPENDENCY_GIT;
        // Determine potential git path at end then cut off string
        size_t a = strlen(ident_string);
        int i;
        for (i = 2; i < a; i++) {
            if (ident_string[i] == '-') {
                strcpy(ident.git_path, ident_string + i + 1);
                break;
            }
        }
        // Copy the remaining URL
        strncat(ident.URL, ident_string + 4, i - 4);
    } else if(!strncmp("file:", ident_string, 5)) {
        // Local file on disk
        ident.mode = DEPENDENCY_FILE;
        strcpy(ident.path, ident_string + 5);
        printf(".path = %s\n", ident.path);
    } else {
        ident.mode = DEPENDENCY_WEB;
        // Simple URL
        strcpy(ident.URL, ident_string);
    }

    return ident;
};

int store_get_dependency(store_dependency_identifier dependency) {
    if(dependency.mode == DEPENDENCY_FILE) return 0; // Local dependencies do not need to be installed
    if (dependency.mode == DEPENDENCY_WEB) {
        printf("Downloading non-git dependencies is currently unimplemented\n");
        return 1;
    }
    char cwd[4096];  // TODO: Move this outside to general commands to
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
    char rm_command[4096 + 7] = "rm -rf ";
    strcat(rm_command, dependency.path);
    if (system(rm_command)) {
        perror("Error removing dependency from global store");
        return 1;
    }
    return 0;
}

int store_update_dependency(store_dependency_identifier dependency) {
    switch(dependency.mode) {
        case DEPENDENCY_FILE:
            return 0;
        case DEPENDENCY_WEB:
            printf("Dependency is not a git repository, reinstalling manually\n");
            store_remove_dependency(dependency);
            return store_get_dependency(dependency);
        case DEPENDENCY_GIT:
            // TODO: use git pull
            return 0;
    }
}

int store_create_symlink(store_dependency_identifier dependency, const char* local_name) {
    // // Create .cpk folder if it doesn't already exist
    // if (mkdir(".cpk", 0700) && errno != EEXIST) {
    //     perror("Could not create .cpk dependency directory");
    //     return 1;
    // }

    char local_dependency_path[4096];
    strcpy(local_dependency_path, ".cpk/");
    strcat(local_dependency_path, local_name);

    if (symlink(dependency.path, local_dependency_path) && errno != EEXIST) {
        fprintf(stderr, "Could not install dependency %s in local project: ", local_name);
        perror("");
        return 1;
    }
    return 0;
}
