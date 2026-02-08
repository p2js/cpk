/*
 * store.c
 * Functions for interacting with the dependency store of a cpk installation.
 */
#include "store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../dir/snapshot.h"
#include "miniz/miniz.h"
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
 * Checks where dependencies should be stored and whether the store is
 * accessible, creating the directory if necessary and setting the directory
 * variable.
 */
int store_init() {
    char* xdg_cache_home = getenv("XDG_CACHE_HOME");

    if (xdg_cache_home) {
        if (strlen(xdg_cache_home) + 6 > 4096) {
            fprintf(stderr,
                    "Error: Could not initialise dependency store, "
                    "XDG_CACHE_HOME path is too long");
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
            fprintf(stderr,
                    "Error: Could not initialise dependency store, HOME path "
                    "is too long");
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

/**
 * Helper function to extract the filename from a source URL
 */
void store_identifier_extract_url_filename(const char* default_filename,
                                           const char* ident_string,
                                           store_dependency_identifier* ident) {
    if (strlen(ident_string) > 2048) {
        fprintf(stderr, "Error: %s: URL is too long", ident_string);
        ident->mode = DEPENDENCY_UNKNOWN;
    } else {
        strcpy(ident->URL, ident_string);
        const char* filename = strrchr(ident->URL, '/');
        if (filename) {
            filename++;  // Move past the '/'
        } else {
            filename = default_filename;
        }

        if (strlen(ident->path) + strlen(filename) + 2 > 4096) {  // +2 for '/' and 0
            fprintf(stderr, "Error: Resolved path for %s is too long\n", ident_string);
            ident->mode = DEPENDENCY_UNKNOWN;
        } else {
            strcat(ident->path, "/");
            strcat(ident->path, filename);
        }
    }
}

store_dependency_identifier store_resolve_identifier(const char* ident_string) {
    store_dependency_identifier ident = {0};

    XXH128_hash_t ident_string_hash = XXH3_128bits(ident_string, strlen(ident_string));
    sprintf(ident.path, "%s%016lx%016lx", store_dir, ident_string_hash.high64,
            ident_string_hash.low64);

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
        store_identifier_extract_url_filename("file", ident_string + 4, &ident);
    } else if (!strncmp("zip:", ident_string, 4)) {
        ident.mode = DEPENDENCY_ZIP;
        store_identifier_extract_url_filename("file.zip", ident_string + 4, &ident);
    } else if (!strncmp("tar:", ident_string, 4)) {
        ident.mode = DEPENDENCY_TAR;
        store_identifier_extract_url_filename("file.tar.gz", ident_string + 4, &ident);
    } else {
        fprintf(stderr, "Error: %s does not represent a valid dependency identifier\n",
                ident_string);
        ident.mode = DEPENDENCY_UNKNOWN;
    }

    return ident;
};

int store_curl_dependency(store_dependency_identifier dependency) {
    // No chdir here, curl will download directly to dependency.path
    char curl_command[2048 + 4096 + 20];  // URL + path + "curl -o " + " " + " -L"
    snprintf(curl_command, sizeof(curl_command), "curl -o \"%s\" %s -L", dependency.path,
             dependency.URL);
    printf("Downloading %s to %s\n", dependency.URL, dependency.path);
    if (system(curl_command)) {
        fprintf(stderr, "Error downloading dependency from %s\n", dependency.URL);
        return 1;
    }
    return 0;
}

int store_create_dependency_dir(char* dep_dir, store_dependency_identifier* dependency) {
    strcpy(dep_dir, dependency->path);
    char* last_slash = strrchr(dep_dir, '/');
    if (last_slash != NULL) {
        *last_slash = 0;  // Null-terminate to get the directory path
    }
    if (mkdir(dep_dir, 0700)) {
        if (errno == EEXIST) {
            // Dependency directory already exists, do not download, just update dep
            // path
            strcpy(dependency->path, dep_dir);
            return EEXIST;
        }
        fprintf(stderr, "Error creating dependency directory %s: ", dep_dir);
        perror("");
        return 1;
    }
    return 0;
}

int store_get_dependency(store_dependency_identifier* dependency) {
    if (dependency->mode == DEPENDENCY_FILE)
        return 0;  // Local dependencies do not need to be installed

    char cwd[4096];
    getcwd(cwd, 4096);

    int exit_code = 0;
    switch (dependency->mode) {
        case DEPENDENCY_GIT: {
            if (mkdir(dependency->path, 0700)) {
                if (errno == EEXIST) {
                    // Dependency directory already exist, do not download
                    return 0;
                }
                fprintf(stderr, "Error creating dependency directory %s: ", dependency->path);
                perror("");
                return 1;
            }
            chdir(dependency->path);
            char git_clone_command[2048 + 12];
            snprintf(git_clone_command, sizeof(git_clone_command), "git clone %s .",
                     dependency->URL);

            if (system(git_clone_command)) {
                fprintf(stderr, "Error cloning dependency from %s\n", dependency->URL);
                exit_code = 1;
            }
            if (dependency->git_path[0] != 0) {
                char git_checkout_command[256 + 13];
                snprintf(git_checkout_command, sizeof(git_checkout_command), "git checkout %s",
                         dependency->git_path);

                if (system(git_checkout_command)) {
                    fprintf(stderr, "Error checking out %s\n", dependency->git_path);
                    exit_code = 1;
                }
            }
            break;
        }
        case DEPENDENCY_WEB: {
            // Create the directory for the dependency and download it
            char dep_dir[4096];
            exit_code = store_create_dependency_dir(dep_dir, dependency);
            if (exit_code == EEXIST) {
                strcpy(dependency->path, dep_dir);
                return 0;
            }
            exit_code = store_curl_dependency(*dependency);
            if (!exit_code) strcpy(dependency->path, dep_dir);
            break;
        }
        case DEPENDENCY_ZIP: {
            // Create the directory for the dependency and download it
            char dep_dir[4096];
            exit_code = store_create_dependency_dir(dep_dir, dependency);
            if (exit_code == EEXIST) return 0;
            exit_code = exit_code || store_curl_dependency(*dependency);
            if (exit_code) break;

            // Unzip the downloaded archive
            chdir(dep_dir);
            mz_zip_archive zip_archive;
            memset(&zip_archive, 0, sizeof(zip_archive));

            if (!mz_zip_reader_init_file(&zip_archive, dependency->path, 0)) {
                fprintf(stderr, "Error: Could not open zip file %s\n", dependency->path);
                exit_code = 1;
                break;
            }

            mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
            for (mz_uint i = 0; i < num_files; i++) {
                mz_zip_archive_file_stat file_stat;
                if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
                    fprintf(stderr, "Error: Could not get file stat for file in zip\n");
                    exit_code = 1;
                    break;
                }

                // Create parent directories for the file
                char path_copy[4096];
                strcpy(path_copy, file_stat.m_filename);
                char* last_slash_in_path = strrchr(path_copy, '/');
                if (last_slash_in_path) {
                    *last_slash_in_path = '\0';  // Null-terminate to get the directory part
                    mk_subdirectories(path_copy);
                }

                if (!mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
                    if (!mz_zip_reader_extract_to_file(&zip_archive, i, file_stat.m_filename, 0)) {
                        fprintf(stderr, "Error: Could not extract file %s from zip\n",
                                file_stat.m_filename);
                        exit_code = 1;
                        break;
                    }
                }
            }
            mz_zip_reader_end(&zip_archive);
            if (exit_code) break;

            // If the contents were only 1 folder, move all its contents into the
            // dependency's directory then remove the original folder
            DIR* dir = opendir(".");
            if (!dir) {
                perror("Error: Could not open dependency directory");
                exit_code = 1;
                break;
            }

            struct dirent* entry;
            int entry_count = 0;
            char entry_name[1024];

            while ((entry = readdir(dir))) {
                if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
                entry_count++;
                strcpy(entry_name, entry->d_name);
            }
            closedir(dir);

            if (entry_count == 1) {
                struct stat path_stat;
                stat(entry_name, &path_stat);
                if (S_ISDIR(path_stat.st_mode)) {
                    // It's a directory, move its contents up
                    dir = opendir(entry_name);
                    if (!dir) {
                        perror("Error: Could not open subdirectory");
                        exit_code = 1;
                        break;
                    }
                    chdir(entry_name);
                    while ((entry = readdir(dir))) {
                        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

                        char new_path[4096];
                        snprintf(new_path, sizeof(new_path), "../%s", entry->d_name);
                        if (rename(entry->d_name, new_path)) {
                            perror("Error: Could not move file");
                            exit_code = 1;
                            break;
                        }
                    }
                    closedir(dir);
                    if (exit_code) break;
                    chdir("..");
                    if (rmdir(entry_name)) {
                        perror("Error: Could not remove empty directory");
                        exit_code = 1;
                        break;
                    }
                }
            }
            // Remove the original zip file
            if (remove(dependency->path)) {
                perror("Error: Could not remove zip file");
                exit_code = 1;
            }
            // Update the dependency path to the extracted directory
            strcpy(dependency->path, dep_dir);
            break;
        }
        case DEPENDENCY_TAR:
            break;
        // Unreachable
        case DEPENDENCY_UNKNOWN:
        case DEPENDENCY_FILE:
            break;
    }
    chdir(cwd);
    return exit_code;
}

int store_remove_dependency(store_dependency_identifier* dependency) {
    char rm_command[4096 + 27] = "rm -r --interactive=never ";
    strcat(rm_command, dependency->path);
    return system(rm_command);
}

int store_update_dependency(store_dependency_identifier* dependency) {
    switch (dependency->mode) {
        case DEPENDENCY_FILE:
            printf("Dependency is local and therefore cannot be updated\n");
            return 0;
        case DEPENDENCY_GIT: {
            if (chdir(dependency->path)) {
                perror("Error: Could not find dependency directory");
                return 1;
            }
            if (system("git pull")) {
                fprintf(stderr, "Error updating dependency from %s", dependency->git_path);
                return 1;
            }
            return 0;
        }
        default: {
            printf("Dependency is not a git repository, reinstalling manually\n");
            store_remove_dependency(dependency);
            return store_get_dependency(dependency);
        }
    }
}

int store_create_symlink(store_dependency_identifier* dependency, const char* local_name) {
    char local_dependency_path[4096];
    if (strlen(local_name) + 6 > 4096) {
        fprintf(stderr, "Could not link %s: Local name is too long", local_name);
        return 1;
    }
    strcpy(local_dependency_path, ".cpk/");
    strcat(local_dependency_path, local_name);

    if (symlink(dependency->path, local_dependency_path) && errno != EEXIST) {
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