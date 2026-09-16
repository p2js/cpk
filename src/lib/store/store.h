/*
 * store.h
 * Functions for interacting with the dependency store of a cpk installation.
 */
#ifndef _CPK_STORE_H
#define _CPK_STORE_H

typedef struct {
    /**
     * Path of the dependency's store folder.
     */
    char path[4096];
    /**
     * Source URL.
     * 2000 is the generally agreed-upon convention for a maximum URL length (rounds up to 2048).
     */
    char URL[2048];
    /**
     * Boolean to indicate whether this is a git repository or source file.
     */
    enum {
        DEPENDENCY_UNKNOWN = 0,
        DEPENDENCY_FILE,
        DEPENDENCY_GIT,
        DEPENDENCY_WEB,
        DEPENDENCY_ZIP,
        DEPENDENCY_TAR
    } mode;
    /**
     * Path to check out in git.
     * "" for unspecified path (latest commit of main branch)
     * 255 is the max branch name length allowed by git.
     */
    char git_path[256];
} store_dependency_identifier;

store_dependency_identifier store_resolve_identifier(const char* ident_string);
int store_get_dependency(store_dependency_identifier* dependency);
int store_remove_dependency(store_dependency_identifier* dependency);
int store_update_dependency(store_dependency_identifier* dependency);
int store_create_symlink(store_dependency_identifier* dependency, const char* local_name);
int store_remove_symlink(const char* local_name);

#endif