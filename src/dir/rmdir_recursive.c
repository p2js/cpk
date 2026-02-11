#include "rmdir_recursive.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int rmdir_r(const char* path, bool unlink_symlinks) {
    DIR* dir = opendir(path);
    int exit_code = -1;
    if (dir) {
        struct dirent* p;
        exit_code = 0;
        while (!exit_code && (p = readdir(dir))) {
            int r2 = -1;
            char buf[4096];
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

            struct stat statbuf;

            snprintf(buf, 4096, "%s/%s", path, p->d_name);
            if (!lstat(buf, &statbuf)) {
                bool unlink_symlink = unlink_symlinks && ((statbuf.st_mode & S_IFMT) == S_IFLNK);

                if (S_ISDIR(statbuf.st_mode) && !unlink_symlink) {
                    r2 = rmdir_r(buf, unlink_symlink);
                } else {
                    r2 = unlink(buf);
                }
            }

            exit_code = r2;
        }
        closedir(dir);
    }
    if (!exit_code) exit_code = rmdir(path);
    return exit_code;
}