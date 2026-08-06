#include <stdio.h>

#include "fmt/color.h"

typedef enum {
    INIT,
    HELP,
    COMPILE,
    RUN,
    INSTALL,
    ADD
} HelpCommand;

const char* HELP_MENU_PLAIN =
    "cpk: Simple dependency manager and project runner for C\n"
    "\n"
    "Getting started:\n"
    "\tcpk init [directory]\tInitialise a cpk project in directory, or the current one if not specified\n"
    "\tcpk help\t\tShow the help menu\n"
    "\tcpk ex\t\t\tShow a comprehensive example for cpk.toml\n"
    "\n"
    "Build your project:\n"
    "\tcpk compile target*\trun the compilation command for the targets to their output folders\n"
    "\tcpk run [target]\tcompile [target], then run its executable\n"
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

const char* HELP_MENU_COLORED =
    COLOR_BOLD("cpk")": Simple dependency manager and project runner for C\n"
    "\n"
    "Getting started:\n"
    "\tcpk "COLOR_BOLD("init")"    "COLOR_GRAY("[dir]")"\tInitialise a cpk project in directory, or the current one if not specified\n"
    "\tcpk "COLOR_BOLD("help")"\t\tShow the help menu\n"
    "\tcpk "COLOR_BOLD("ex")"\t\t\tShow a comprehensive example for cpk.toml\n"
    "\n"
    "Build your project:\n"
    "\tcpk "COLOR_BOLD("compile")" "COLOR_GRAY("target*")"\trun the compilation command for the targets to their output folders\n"
    "\tcpk "COLOR_BOLD("run")"     "COLOR_GRAY("[target]")"\tcompile [target], then run its executable\n"
    "\tcpk "COLOR_BOLD("targets")"\t\tlist all targets in cpk.toml\n"
    "\n"
    "Manage your project dependencies:\n"
    "\tcpk "COLOR_BOLD("install")"\t\tInstall and link all the dependencies specified in cpk.toml\n"
    "\tcpk "COLOR_BOLD("add")"     "COLOR_GRAY("<name=dep>+")"\tAdd one or more dependencies to cpk.toml and install them\n"
    "\tcpk "COLOR_BOLD("remove")"  "COLOR_GRAY("<name>")"\tRemove a dependency from cpk.toml and the project\n"
    "\n"
    "Maintain your local dependency store:\n"
    "\tcpk "COLOR_BOLD("update")"  "COLOR_GRAY("<dep>")"\tUpdate/reinstall a dependency in the global store\n"
    "\tcpk "COLOR_BOLD("delete")"  "COLOR_GRAY("<dep>")"\tRemove a dependency from the global store\n"
    "\n"
    "Dependency types:\n"
    "\tgit:<repo>[::<path>]\tGit repository to clone from repo, optionally checking out path "COLOR_GRAY("(requires git command)")"\n"
    "\tweb:<URL>\t\tSingle source file hosted at URL "COLOR_GRAY("(requires curl command)")"\n"
    "\tzip:<URL>\t\t.zip archive hosted at URL       "COLOR_GRAY("(requires curl command)")"\n"
    "\tfile:<path>\t\tLocal file/directory";

void help_show(HelpCommand cmd) {
    switch (cmd) {
        case HELP:
            printf("%s\n", use_colors ? HELP_MENU_COLORED : HELP_MENU_PLAIN);
            break;
        default:
            break;
    }
}
