/*
 * rmdir_recursive.c
 * Implementation of "rm -r".
 */
#include "rmdir_recursive.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int rmdir_r(const char* path, bool unlink_symlinks) {
    DIR* directory = opendir(path);
    int exit_code = -1;
    if (directory) {
        struct dirent* directory_info;
        exit_code = 0;
        while (!exit_code && (directory_info = readdir(directory))) {
            int internal_exit_code = -1;
            char buf[4096];
            if (!strcmp(directory_info->d_name, ".") || !strcmp(directory_info->d_name, "..")) {
                continue;
            }

            struct stat statbuf;

            snprintf(buf, 4096, "%s/%s", path, directory_info->d_name);
            if (!lstat(buf, &statbuf)) {
                bool unlink_symlink = unlink_symlinks && ((statbuf.st_mode & S_IFMT) == S_IFLNK);

                if (S_ISDIR(statbuf.st_mode) && !unlink_symlink) {
                    internal_exit_code = rmdir_r(buf, unlink_symlink);
                } else {
                    internal_exit_code = unlink(buf);
                }
            }

            exit_code = internal_exit_code;
        }
        closedir(directory);
    }
    if (!exit_code) exit_code = rmdir(path);
    return exit_code;
}