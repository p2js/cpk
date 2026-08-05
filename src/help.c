#include <stdio.h>

typedef enum {
    INIT,
    HELP,
    COMPILE,
    RUN,
    INSTALL,
    ADD
} HelpCommand;

const char* HELP_MENU =
    "cpk: Simple dependency manager and project runner for C\n"
    "\n"
    "Getting started:\n"
    "\tcpk init [directory]\tInitialise a cpk project in [directory], or the current one if not specified\n"
    "\tcpk help\t\tShow the help menu\n"
    "\tcpk ex\t\t\tShow a comprehensive example for cpk.toml\n"
    "\n"
    "Build your project:\n"
    "\tcpk compile target*\trun the compilation command for the targets to their output folders\n"
    "\tcpk run [target]\tcpk compile [target], then run the executable\n"
    "\tcpk targets\t\tlist all targets in cpk.toml\n"
    "\n"
    "Manage your project dependencies:\n"
    "\tcpk install\t\tInstall and link all the dependencies specified in cpk.toml\n"
    "\tcpk add <name>=<dep>+\tAdd one or more dependencies to cpk.toml and install them\n"
    "\tcpk remove <name>\tRemove a dependency from cpk.toml and the project\n"
    "\n"
    "Maintain your local dependency store:\n"
    "\tcpk update <dep>\tUpdate/reinstall a dependency in the global store\n"
    "\tcpk delete <dep>\tRemove a dependency from the global store\n"
    "\n"
    "Dependency types:\n"
    "\tgit:<repo>[::<path>]\tGit repository to clone from repo, optionally checking out path (requires git command)\n"
    "\tweb:<URL>\t\tSingle source file hosted at URL (requires curl command)\n"
    "\tzip:<URL>\t\t.zip archive hosted at URL       (requires curl command)\n"
    "\tfile:<path>\t\tLocal file/directory";

void help_show(HelpCommand cmd) {
    switch (cmd) {
        case HELP:
            printf("%s\n", HELP_MENU);
            break;
        default:
            break;
    }
}
