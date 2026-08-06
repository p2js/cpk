## FEATURE CHANGES

- DONE (BREAKING) remove gh: in favor of just using git:
- DONE (BREAKING) change file: dependencies to not be relative from .cpk (add a ../ if relative path, not if absolute ie starts with /)
- DONE (BREAKING) Style change to cpk.toml as above, which will allow proper stringification. (breaking change: targets -> target, ex -> exec)
- add cpk build command as alias to cpk compile
- DONE accept multiple targets for cpk compile
- DONE add cpk targets command that will print all targets space separated (this and above for building all as "cpk targets | xargs cpk compile")
- DONE Introduce arrays to build for multiple commands (stop on any nonzero exit code)

TODO:

- colored help menu (see cargo)
- Use custom wget: 
- Add header comments
- Documentation
- bootstrap script
- aarch64
- efficient git dependencies

## DOCUMENTATION CHANGES

- Add example tomls
- change git: to say path rather than URL

After installing dependencies, you can simply include their source files as necessary in your code. 

In C, this would look like `#include "dep/file.c"`. To reference dependencies directly like this, you can pass the `-I.cpk` flag to your build command which will add the dependency directory to the compiler's search path. `cpk` will also inject that flag into the `$CFLAGS` environment variable when compiling or running a target.

> Tip: Use `cpk targets | xargs cpk compile` to compile all targets at once.