#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

const char* INIT_DEFAULT_GITIGNORE = "\n#cpk output directory\nbuild\n#cpk dependency symlinks\n.cpk\n";
const char* INIT_DEFAULT_MAIN =
    "#include <stdio.h>\n\
\n\
int main(void) {\n\
    printf(\"Hello World!\\n\");\n\
}\n";
const char* INIT_DEFAULT_TOML =
    "target_dir = \"build\" # Directory for all targets' builds\n\
\n\
[dependencies] # dependencies map names to repositories/files\n\
\n\
# Add your own here, manually or via cpk add...\n\
\n\
[targets]\n\
# targets map target names to build commands (for use with cpk run/compile)\n\
# all files produced by the build command will be output to target_dir/target_name\n\
\n\
# dev is the default profile (will be used by run/compile when a target is not provided)\n\
dev = {\n\
  build = \"clang src/main.c\", # Build command\n\
  ex = \"a.out\" # Executable path to be used by cpk run (in target dir), default: \"a.out\"\n\
}\n";

/**
 * Initialises a cpk project in the given directory name.
 *
 * Files created:
 * cpk.toml   - configuration file (must not already exist)
 * build      - output directory
 * src        - source code directory
 * src/main.c - starter source file (will not be created if src already exists)
 * .gitignore - ignore for build directory (will append if already exists)
 */
int init_project(char* directory) {
    if (directory[0] == '.' && directory[1] == '\0') {
        printf("Initialising new project in current directory\n");
    } else {
        printf("Initialising new project in %s\n", directory);
    }

    size_t path_length = strlen(directory);
    if (directory[path_length - 1] == '/') {
        directory[path_length - 1] = '\0';
        path_length--;
    }

    char current_filename[4096];

    // cpk.toml: Error if already exists
    snprintf(current_filename, 4096, "%s/cpk.toml", directory);

    FILE* fp_cpk_toml = fopen(current_filename, "wx");
    if (!fp_cpk_toml) {
        fprintf(stderr, "Error: %s could not be created: ", current_filename);
        perror("");
        return 1;
    }
    fprintf(fp_cpk_toml, "%s", INIT_DEFAULT_TOML);
    fclose(fp_cpk_toml);

    // build dir: Just create
    strcpy(current_filename + path_length, "/build");
    int mkdir_result = mkdir(current_filename, 0700);
    if (mkdir_result) {
        if (errno == EEXIST) {
            printf("[WARN] %1$s/build file or directory already exists.\n[WARN] If this is not intended to be the destination for your project's output targets, change %1$s/cpk.toml and %1$s/.gitignore accordingly.\n",
                directory);
        } else {
            perror("Could not initialise build directory");
            return 1;
        }
    }

    // src dir: Just create
    bool src_exists = false;

    strcpy(current_filename + path_length, "/src");
    mkdir_result = mkdir(current_filename, 0700);
    if (mkdir_result) {
        if (errno == EEXIST) {
            src_exists = true;
            printf("[INFO] %s file or directory already exists, example main.c will not be created\n", current_filename);
        } else {
            perror("Could not initialise src directory");
            return 1;
        }
    }

    // src/main.c: Create if src did not exist
    if (!src_exists) {
        strcat(current_filename, "/main.c");
        FILE* fp_main_c = fopen(current_filename, "w");
        if (!fp_main_c) {
            fprintf(stderr, "Error: %s could not be created: ", current_filename);
            perror("");
            return 1;
        }
        fprintf(fp_main_c, "%s", INIT_DEFAULT_MAIN);
        fclose(fp_main_c);
    }

    // gitignore: Write or append if exists
    strcpy(current_filename + path_length, "/.gitignore");
    FILE* fp_gitignore = fopen(current_filename, "a");
    if (!fp_gitignore) {
        fprintf(stderr, "Error: %s could not be created or opened: ", current_filename);
        perror("");
        return 1;
    }
    fprintf(fp_gitignore, "%s", INIT_DEFAULT_GITIGNORE);
    fclose(fp_gitignore);

    if (src_exists) {
        printf("Project initialised. Start by editing %s/cpk.toml configuration to fit your project.\n", directory);
    } else {
        printf("Project initialised. Start by editing %1$s/src/main.c and use \"cpk compile\" to build the project,\nOr edit %1$s/cpk.toml as necessary.\n", directory);
    }
    return 0;
}