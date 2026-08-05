# cpk as a dependency manager

cpk's primary purpose is to enable easy installation and management of dependencies of C projects that use unity builds (also known as one-file builds), in order to avoid the hassle of the usual dependency management.

#### Adding dependencies to your project

Dependencies can be added by using `cpk add (name=dep)`, with `name` being the local name to reference the dependency in the project and `dep` being one of the dependency types listed below. You can also add these manually to the `dependencies` table in `cpk.toml`.

If you need to install dependencies from a downloaded project, ones manually added to `cpk.toml` or you are looking to fix the symlinks, use `cpk install`.

The same dependency can be used in different projects (even under different names) without multiple installations, as dependencies are stored globally and symlinked per-project.

#### Dependency types

There are 5 types of dependencies, each handled differently:
- `git:<repo>[::<path>]` represents a git repository to be cloned from `repo` (can be a URL or local link), optionally checking out `path`.
- `web:<URL>` represents a single source file hosted at `URL`.
- `zip:<URL>` represents a `.zip` archive hosted at `URL`.
- `file:<path>` represents a local directory or file. These dependencies will be linked directly rather than copied to the global dependency store.

> `git:` dependencies will require that cpk have access to the `git` shell command.
> `web:` and `zip:` dependencies will require that cpk have access to the `curl` shell command.

#### Referencing dependencies

After installing dependencies, you can simply include their source files as necessary in your code. 

In C, this would look like `#include "dep/file.c"`. To reference dependencies directly like this, you can pass the `-I.cpk` flag to your build command which will add the dependency directory to the compiler's search path. `cpk` will also inject that flag into the `$CFLAGS` environment variable when compiling or running a target.

#### Managing the global dependency store

to reinstall or delete a dependency in cpk's global store, use `cpk update (dep)` and `cpk delete (dep)` respectively, using the dependency values outlined above.
