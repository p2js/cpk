/*
 * snapshot.h
 * Utilities for creating and diffing recursive snapshots of directories.
 */

#ifndef _CPK_SNAPSHOT_H
#define _CPK_SNAPSHOT_H

#include <stdio.h>

/**
 *  A snapshot of a directory as a resizable array of entry paths.
 */
typedef struct {
    char** paths;
    size_t count;
    size_t capacity;
} dir_snapshot;

/**
 * Create a recursive snapshot of a directory and all its entries.
 * @param root starting directory
 */
dir_snapshot snapshot_directory(const char* root);

/**
 * Create a diff between the files in two snapshots.
 */
dir_snapshot diff_snapshots(dir_snapshot* before, dir_snapshot* after);

/**
 * Move all items from a diff snapshot from the source directory to a target directory.
 * Will automatically create intermediate directories.
 */
void move_snapshot_diff_items(dir_snapshot* diff, const char* root, const char* target);

/**
 * Free a snapshot object.
 */
void snapshot_free(dir_snapshot* snapshot);

void create_parent_directories(const char* path);

#endif