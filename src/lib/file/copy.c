/*
 * copy.c
 * Function to copy a file to a destination path,
 * overwriting any previous contents.
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int copy_file(const char* src_path, const char* dest_path) {
    int src = open(src_path, O_RDONLY);
    if (src < 0) return 1;

    struct stat src_stat;
    if (fstat(src, &src_stat) != 0) {
        close(src);
        return 1;
    }

    int dst = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode & 0777);
    if (dst < 0) {
        close(src);
        return 1;
    }

    off_t offset = 0;
    while (offset < src_stat.st_size) {
        ssize_t sent = sendfile(dst, src, &offset, src_stat.st_size - offset);
        if (sent < 0) {
            if (errno == EINTR) continue;
            close(src);
            close(dst);
            return 1;
        }
        if (sent == 0) break;
    }

    struct timespec times[2];
    times[0] = src_stat.st_atim;
    times[1] = src_stat.st_mtim;
    futimens(dst, times);

    close(src);
    close(dst);
    return 0;
}
