/*
 * store.c
 * Functions for managing the dependency store of a cpk installation (install, remove).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xxhash/xxhash.h"

const char* GITHUB_URL = "https://github.com/";

/*
 * Should have:
 * - A function for parsing/resolving dependency identifiers
 * - A function for downloading a dependency
 * - A function for removing a dependency
 * - A function for updating a dependency
 */

typedef struct {
    /**
     * Hash string (folder name in store).
     * 32 is the number of characters needed to represent a 128-bit hash as hex.
     */
    char hash_string[33];
    /**
     * Source URL.
     * 2000 is the generally agreed-upon convention for a maximum URL length (rounds up to 2048).
     */
    char URL[2048];
    /**
     * Boolean to indicate whether this is a git repository or source file.
     */
    bool git;
    /**
     * Path to check out in git.
     * "" for unspecified path (latest commit of main branch)
     * 255 is the max branch name length allowed by git.
     */
    char git_path[256];
} store_dependency_identifier;

store_dependency_identifier store_resolve_identifier(char* ident_string) {
    store_dependency_identifier ident = {0};

    XXH128_hash_t ident_string_hash = XXH3_128bits(ident_string, strlen(ident_string));
    sprintf(ident.hash_string, "%016lx%016lx", ident_string_hash.high64, ident_string_hash.low64);

    if (!strncmp("gh:", ident_string, 3)) {
        ident.git = true;
        strcpy(ident.URL, GITHUB_URL);
        // Determine potential git path at end then cut off string
        size_t a = strlen(ident_string);
        int i;
        for (i = 3; i < a; i++) {
            if (ident_string[i] == '>') {
                strcpy(ident.git_path, ident_string + i + 1);
                break;
            }
        }
        ident_string[i] = 0;
        // Copy the remaining URL to the right field
        strcat(ident.URL, ident_string + 3);
    } else if (!strncmp("git:", ident_string, 4)) {
        ident.git = true;
        // Determine potential git path at end then cut off string
        size_t a = strlen(ident_string);
        int i;
        for (i = 2; i < a; i++) {
            if (ident_string[i] == '>') {
                strcpy(ident.git_path, ident_string + i + 1);
                break;
            }
        }
        ident_string[i] = 0;
        // Copy the remaining URL
        strcat(ident.URL, ident_string + 4);
    } else {
        // Simple URL
        strcpy(ident.URL, ident_string);
    }

    return ident;
};

/**
 * Checks where dependencies should be stored and whether the store is accessible,
 * creating the directory if necessary and returning the path.
 */
char* store_init() {
    static char store_dir[4096];

    char* xdg_cache_home = getenv("XDG_CACHE_HOME");
    snprintf(store_dir, 4096, "%s/cpk/", xdg_cache_home != NULL ? xdg_cache_home : "~/.cache");

    int mkdir_result = mkdir(store_dir, 0700);

    if (mkdir_result && mkdir_result != EEXIST) {
        perror("FATAL: cpk store directory could not be created");
        exit(1);
    }

    return store_dir;
}

int store_get_dependency(store_dependency_identifier dependency, char* store_directory) {
    if (!dependency.git) {
        printf("Downloading non-git dependencies is currently unimplemented\n");
        return 1;
    }
    char cwd[4096];  // TODO: Move this outside to general commands to
    getcwd(cwd, 4096);
    char dependency_dir[4096];
    strcpy(dependency_dir, store_directory);
    strcat(dependency_dir, dependency.hash_string);

    int mkdir_result = mkdir(dependency_dir, 0700);
    if (mkdir_result && mkdir_result != EEXIST) {
        printf("Error creating dependency directory for %s: ", dependency.URL);
        perror("");
        return 1;
    }

    chdir(dependency_dir);

    if (dependency.git) {
        char git_clone_command[2048 + 10] = "git clone ";
        strcat(git_clone_command, dependency.URL);

        int git_clone_result = system(git_clone_command);
        if (git_clone_result) {
            printf("Error cloning dependency from %s\n", dependency.URL);
            return git_clone_result;
        }
        if (dependency.git_path[0] != 0) {
            char git_checkout_command[256 + 13] = "git checkout ";
            strcat(git_checkout_command, dependency.git_path);
            int git_checkout_result = system(git_checkout_command);
            if (git_checkout_result) {
                printf("Error checking out %s\n", dependency.git_path);
                return git_checkout_result;
            }
        }
    } else {
        // TODO
    }

    chdir(cwd);
    return 0;
}

int store_remove_dependency(store_dependency_identifier dependency, char* store_directory) {
    char dependency_dir[4096];
    strcpy(dependency_dir, store_directory);
    strcat(dependency_dir, dependency.hash_string);
    int rmdir_result = rmdir(dependency_dir);
    if (rmdir_result) {
        perror("Error removing dependency from global store");
    }
    return rmdir_result;
}

int store_update_dependency(store_dependency_identifier dependency, char* store_directory) {
    if (!dependency.git) {
        store_remove_dependency(dependency, store_directory);
        return store_get_dependency(dependency, store_directory);
    }
    // TODO: use git pull
}

int store_create_symlink(store_dependency_identifier dependency, char* store_directory);
