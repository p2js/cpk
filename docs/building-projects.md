# cpk as a build system

On top of dependency management, cpk also doubles as a simple multi-target build system project runner that can integrate with and extend your current build system.

## Defining compilation targets
New targets can be defined as a `[target.<name>]` table in `cpk.toml`. Each target may define one or more build commands, an executable file name for 

```toml
[targets.dev]
build = "clang src/main.c -I.cpk"
exec = "a.out"

# Multiple commands + copying example
[targets.release]
build = [
    "echo Building for release mode",
    "clang src/main.c -I.cpk -O3"
]
copy = [
    "LICENSE"
]
exec = "a.out"
```

The `build` command(s) get executed when using `cpk compile` and `cpk run`, and the `exec` file gets ran when using `cpk run`.

`target.dev` is considered the default target; it will be used when calling `cpk compile` or `cpk run` without supplying any further arguments.

> Tip: Use `cpk targets | xargs cpk compile` to compile all targets with one command.

## Compiling targets {MENTION THE MOVING TO DEDICATED TARGET FOLDER}

Running `cpk compile [name]` will invoke your build commands, and `cpk run [name] [...args]` will both invoke the build commands and execute the file defined, forwarding any arguments given after the target name. All files produced by your build command will be moved to `target-dir/name`, where `target-dir` is the main output directory defined at the top of `cpk.toml`.

Using either command with no arguments will use the `dev` target. If `target-dir` is not defined, the output files will not be moved.
