# cpk as a build system

On top of dependency management, cpk also doubles as a simple multi-target build system project runner that can integrate with and extend your current build system.

## EDITME BELOW NEEDS TO BE ITS OWN TABLE
New targets can be defined in the `[targets]` table of `cpk.toml`. Each target has a name, one or more build commands, and an executable file name.

Running `cpk compile [name]` will invoke your build commands, and `cpk run [name] [...args]` will both invoke the build commands and execute the file defined, forwarding any arguments given after the target name. All files produced by your build command will be moved to `target_dir/name`, where `target_dir` is the main output directory defined at the top of `cpk.toml`.

Using either command with no arguments will use the `dev` target. If `target_dir` is not defined, the output files will not be moved.

> Tip: Use `cpk targets | xargs cpk compile` to compile all targets with one command.