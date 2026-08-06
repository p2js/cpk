# cpk

A simple dependency manager and build system, designed for C unity builds and inspired by Rust's [cargo](https://github.com/rust-lang/cargo).

## Installation

cpk is currently for Linux only.

Simply download a release of cpk from the releases tab according to your needs, then install it to be able to use it from anywhere:

```sh
unzip cpk.zip # or cpk-static-musl.zip
chmod +x cpk
sudo mv cpk /usr/local/bin/cpk
```

> Most users on most standard Linux distributions (Ubuntu, Debian, Fedora, Arch and any other glibc-based distro) can use the regular `cpk.zip` release. However, use `cpk-static-musl.zip` if you need a statically compiled executable that will work anywhere. 

## Usage

cpk can be used for two different purposes: as a dependency manager and simple build/run manager.

For detailed documentation see:
- [Getting started with cpk](docs/getting-started.md) for an introduction to the directory structure and configuration file, with examples
- [cpk as a dependency manager](docs/dependency-management.md) and [cpk as a build system](docs/building-projects.md) for more detailed information on the primary use cases

Alternatively, you can take this repository as an example of usage, since it uses itself for dependency and build management.

## Why unity builds?

See: [one file is better than cmake](https://www.youtube.com/watch?v=j3BvierE2hM)

Not convinced? See: [one file is still better than cmake](https://www.youtube.com/watch?v=Cgc4LKmjm_c)

Unity builds allow for faster compilation and a much simpler project structure, which is the reason cpk can even work with this level of simplicity in the first place: No extra specification of build commands/processes, no including each library separately in your build command.

Should you still not be convinced, there exist other dependency management alternatives for you (See [meson](https://mesonbuild.com/)).