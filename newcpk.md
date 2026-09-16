## FEATURE CHANGES

- DONE (BREAKING) remove gh: in favor of just using git:
- DONE (BREAKING) change file: dependencies to not be relative from .cpk (add a ../ if relative path, not if absolute ie starts with /)
- DONE (BREAKING) Style change to cpk.toml as above, which will allow proper stringification. (breaking change: targets -> target, ex -> exec)
- add cpk build command as alias to cpk compile
- DONE accept multiple targets for cpk compile
- DONE add cpk targets command that will print all targets space separated (this and above for building all as "cpk targets | xargs cpk compile")
- DONE Introduce arrays to build for multiple commands (stop on any nonzero exit code)
- DONE change `cpk run` to not add build dir to absolute paths
- DONE `target.x.copy` to specify files to copy over to the build folder

TODO:

- Documentation
   - add examples for toml
- efficient git dependencies
- docker and target triple definitions in cpk.toml

```toml
[target.release-aarch64-static-musl]
arch = "aarch64"
dockerfile = "dockerfiles/aarch64-static-musl.Dockerfile"
build = "..."
exec = "cpk"
```
cpk compile will check your arch for you and use dockerfile if they don't match

to get uname (arch is buffer.machine, x86_64):
```c
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/utsname.h>

int main(void) {

   struct utsname buffer;

   errno = 0;
   if (uname(&buffer) < 0) {
      perror("uname");
      exit(EXIT_FAILURE);
   }

   printf("system name = %s\n", buffer.sysname);
   printf("node name   = %s\n", buffer.nodename);
   printf("release     = %s\n", buffer.release);
   printf("version     = %s\n", buffer.version);
   printf("machine     = %s\n", buffer.machine);

   #ifdef _GNU_SOURCE
      printf("domain name = %s\n", buffer.domainname);
   #endif

   return EXIT_SUCCESS;
}
```