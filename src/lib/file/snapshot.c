/*
 * snapshot.c
 * Utilities for creating and diffing recursive snapshots of directories.
 */
#include "snapshot.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/stat.h>

#define SNAPSHOT_STARTING_CAPACITY 64

void snapshot_init(dir_snapshot* snapshot) {
    snapshot->paths = NULL;
    snapshot->count = 0;
    snapshot->capacity = 0;
}
void snapshot_add(dir_snapshot* snapshot, const char* path) {
    if (snapshot->count == snapshot->capacity) {
        snapshot->capacity =
            snapshot->capacity ? snapshot->capacity * 2 : SNAPSHOT_STARTING_CAPACITY;
        snapshot->paths = realloc(snapshot->paths, snapshot->capacity * sizeof(char*));
    }
    snapshot->paths[snapshot->count++] = strdup(path);
}
void snapshot_dir_recursive(dir_snapshot* snapshot, const char* base, const char* path) {
    char full_dirname[4096];
    snprintf(full_dirname, sizeof(full_dirname), "%s/%s", base, path);
    DIR* current_directory = opendir(full_dirname);
    if (!current_directory) return;
    struct dirent* entry;
    while ((entry = readdir(current_directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char relative_pathname[4096];
        snprintf(relative_pathname, sizeof(relative_pathname), "%s/%s", path, entry->d_name);
        snapshot_add(snapshot, relative_pathname);
        // Recurse into directories
        if (entry->d_type == DT_DIR) {
            snapshot_dir_recursive(snapshot, base, relative_pathname);
        }
    }
    closedir(current_directory);
}

dir_snapshot snapshot_directory(const char* root) {
    dir_snapshot snapshot;
    snapshot_init(&snapshot);
    snapshot_dir_recursive(&snapshot, root, "");
    return snapshot;
}

int snapshot_contains(dir_snapshot* snapshot, const char* path) {
    for (size_t i = 0; i < snapshot->count; i++) {
        if (strcmp(snapshot->paths[i], path) == 0) return 1;
    }
    return 0;
}

dir_snapshot diff_snapshots(dir_snapshot* before, dir_snapshot* after) {
    dir_snapshot diff;
    snapshot_init(&diff);
    for (size_t i = 0; i < after->count; i++) {
        if (!snapshot_contains(before, after->paths[i])) {
            snapshot_add(&diff, after->paths[i]);
        }
    }
    return diff;
}

void create_parent_directories(const char* path) {
    char temp[4096];
    strcpy(temp, path);

    // Truncate to parent directory
    char* last = strrchr(temp, '/');
    if (!last) return;
    *last = '\0';

    for (char* ptr = temp + 1; *ptr; ptr++) {
        if (*ptr == '/') {
            *ptr = '\0';
            if (mkdir(temp, 0700) && errno != EEXIST) return;
            *ptr = '/';
        }
    }
    if (mkdir(temp, 0700) && errno != EEXIST) return;
}

void move_snapshot_diff_items(dir_snapshot* diff, const char* root, const char* target) {
    for (size_t i = 0; i < diff->count; i++) {
        char src[4096];
        char dst[4096];
        snprintf(src, sizeof(src), "%s/%s", root, diff->paths[i]);
        snprintf(dst, sizeof(dst), "%s/%s", target, diff->paths[i]);
        create_parent_directories(dst);
        rename(src, dst);
    }
}

void snapshot_free(dir_snapshot* snapshot) {
    for (size_t i = 0; i < snapshot->count; i++) {
        free(snapshot->paths[i]);
    }
    free(snapshot->paths);
}