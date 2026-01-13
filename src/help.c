#include <stdio.h>

typedef enum {
    INIT,
    HELP,
    EX,
    COMPILE,
    RUN,
    INSTALL,
    ADD
} HelpCommand;

const char* HELP_MENU =
    "cpk: Simple dependency manager and project runner for C\n\
\n\
Commands for new projects:\n\
\tcpk init [directory]\tInitialise a cpk project in [directory], or the current one if not specified\n\
\tcpk help\t\tShow the help menu\n\
\tcpk ex\t\t\tShow a comprehensive example for cpk.toml\n\
Commands for compilation:\n\
\tcpk compile [target]\trun the compilation command for the target to its output folder (target/[target])\n\
\tcpk run [target]\tcpk compile [target], then run the executable\n\
Commands for dependency management:\n\
\tcpk install\t\tInstall all the dependencies specified in cpk.toml\n\
\tcpk add (name=dep)+\tAdd one or more dependencies to cpk.toml and install them\n\
Dependency types:\n\
\tgh:(repo)\t\tGitHub repository\n\
\tgit:(URL)\t\tGit repository at URL\n\
\t(URL)\t\t\tSingle source file or directory of source files";

const char* TOML_EXAMPLE =
    "Example cpk.toml:\n\
\n\
target_dir = \"build\" # Directory for all targets' builds, default: \"build\"\n\
\n\
[dependencies] # dependencies map names to repositories/files\n\
CLArgs = \"gh:p2js/clargs\"\n\
\n\
[targets]\n\
# targets map target names to build commands (for use with cpk run/compile)\n\
# all files produced by the build command will be output to target_dir/target_name\n\
\n\
# dev is the default profile (will be used by run/compile when a target is not provided)\n\
dev = {\n\
  build = \"clang src/main.c src/debug.c\", # Build command\n\
  ex = \"a.out\" # Executable path to be used by cpk run (in target dir), default: \"a.out\"\n\
}";

void help_show(HelpCommand cmd) {
    switch (cmd) {
        case HELP:
            printf("%s\n", HELP_MENU);
            break;
        case EX:
            printf("%s\n", TOML_EXAMPLE);
            break;
        default:
            printf("Unimplemented\n");
    }
}