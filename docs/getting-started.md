# Getting started with cpk

Table of contents:
- [Initialising a new project](#initialising-a-new-project)
- [cpk.toml](#cpktoml)
- [Commands](#commands)

## Initialising a new project

To prepare a new directory or a current project for use with cpk, use `cpk init [directory]` to create all the files and directories you will need (no argument will initialise it in the current directory). This will create the following structure:

```
├─ .cpk/       Directory where dependencies are linked 
├─ build/      Default output directory for compilation
├─ src/
│  ├─ main.c
├─ .gitignore
├─ cpk.toml    Configuration file
```

`.gitignore` will be automatically set to ignore `.cpk` and `build`. If your project already has a `.gitignore` file, it will not be overwritten and this will instead be appended to the end of the file.

Similarly, if your project already has a `src` directory, it will not be touched.

## cpk.toml

The project will be initialised with the assumption that it will be using both aspects of cpk, but any parts of the directory and configuration file that are irrelevant to your project can be removed.

## Commands
