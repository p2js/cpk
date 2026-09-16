/*
 * copy.h
 * Function to copy a file to a destination path,
 * overwriting any previous contents.
 */

/**
 * Copy a file from `src_path` to `dst_path`.
 * `dst_path` will be overwritten if it exists.
 * File permissions and timestamps will be preserved.
 */
int copy_file(const char* src_path, const char* dst_path);