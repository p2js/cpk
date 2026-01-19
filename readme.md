# cpk

WIP Simple dependency manager and project runner for C, designed around unity builds.

## Installation

TODO

## Usage

cpk can be used in two independent ways: as a dependency manager and simple build/run manager.

> For detailed info on commands, use `cpk help`. To see a comprehensive example of everything in the configuration file, use `cpk ex`. Alternatively, you can use this repository as an example of usage, since it uses itself for dependency and build management.

### Initialising a project

To prepare a new directory or a current project for use with cpk, use `cpk init [directory]` to create all the files and directories you will need (no argument will initialise it in the current directory). This amounts to:

- `cpk.toml`, the main configuration file.
- `.cpk`, the folder where dependencies will be symlinked.
- `build`, the default output directory for compilation targets.
- `src`, a directory for your source code, with a starter `main.c` file.
    - Note that if your project already has an `src` directory, it will **NOT** be overwritten and `src/main.c` will not be created.
- `.gitignore`, set to ignore the dependency symlink directory and build directory.
    - Note that if your project already has a `.gitignore` file, it will **NOT** be overwritten and the `cpk`-specific files will be appended to the end of the file.

The project will be initialised with the assumption that it will be using both aspects of cpk, but any parts of the directory and configuration file that are irrelevant to your project can be removed.

### cpk as a dependency manager

The primary purpose of this project is to enable easy installation and management of dependencies of C projects that use unity builds (also known as one-file builds), in order to avoid the hassle of the usual dependency management.

#### Adding dependencies to your project

Dependencies can be added by using `cpk add (name=dep)`, with `name` being the local name to reference the dependency in the project and `dep` being one of the dependency types listed below. You can also add these manually to the `dependencies` table in `cpk.toml`.

If you need to install dependencies from a downloaded project, ones manually added to `cpk.toml` or you are looking to fix the symlinks, use `cpk install`.

The same dependency can be used in different projects (even under different names) without multiple installations, as dependencies are stored globally and symlinked per-project.

#### Dependency types

There are 4 types of dependencies, each handled differently by cpk:
- `git:(URL)[::path]` represents a git repository hosted at URL, optionally checking out `path`.
- `gh:(repo)[::path]` represents a GitHub repository, optionally checking out `path`.
- `file:(path)` represents a local directory or file. These dependencies will be linked directly rather than copied to the global dependency store.
- `(URL)` represents a single sourcefile, zip or tarball hosted at URL.

> `git:` and `gh:` dependency types will require that cpk have access to the `git` shell command.

#### Referencing dependencies

After installing and linking dependencies, you can simply include their source files as necessary to your code using `#include` directives, and compile using either `$CFLAGS` or `-I.cpk` to add them to the compiler's search paths.

#### Managing the global dependency store

to reinstall or delete a dependency from cpk's global store, use `cpk update (dep)` and `cpk delete (dep)` repsectively, using the dependency values outlined above.

### cpk as a project runner

cpk also doubles as a simple multi-target project runner that can integrate with your current build system.

New targets can be defined in the `[targets]` table of `cpk.toml`. Each target has a name, a build command and an executable file name.

Running `cpk compile [name]` will invoke your build command, and `cpk run [name] [...args]` will both invoke the build command and execute the file defined, forwarding any arguments given after the target name. All files produced by your build command will be moved to `target_dir/name`, where `target_dir` is the main output directory defined at the top of `cpk.toml`.

Using either command with no arguments will use the `dev` target. If `target_dir` is not defined, the output files will not be moved.