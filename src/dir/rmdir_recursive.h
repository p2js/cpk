/*
 * rmdir_recursive.c
 * Implementation of "rm -r".
 */
#ifndef _CPK_RMDIR_H
#define _CPK_RMDIR_H

#include <stdbool.h>

/**
 * Remove a non-empty directory by recursively unlinking its contents.
 * @param path path of directory to remove
 * @param unlink_symlinks whether to treat symlinks of directories as symlinks to unlink or
 * directories to recursively remove
 */
int rmdir_r(const char* path, bool unlink_symlinks);

#endif