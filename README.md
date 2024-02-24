# samurai

[![builds.sr.ht status](https://builds.sr.ht/~mcf/samurai.svg)](https://builds.sr.ht/~mcf/samurai)
[![GitHub build status](https://github.com/michaelforney/samurai/workflows/build/badge.svg)](https://github.com/michaelforney/samurai/actions)

samurai is a ninja-compatible build tool written in C99 with a focus on
simplicity, speed, and portability.

## Status

samurai implements the ninja build language through version 1.9.0 except
for MSVC dependency handling (`deps = msvc`). It uses the same format
for `.ninja_log` and `.ninja_deps` as ninja, currently version 5 and 4
respectively.

It is feature-complete and supports most of the same options as ninja.

## Requirements

samurai requires various POSIX.1-2008 interfaces.

Scheduling jobs based on load average requires the non-standard
`getloadavg` function. This feature can be enabled by defining
`HAVE_GETLOADAVG` in your `CFLAGS`, along with any other necessary
definitions for your platform.

## Compilation on Windows

The Windows port is a WIP. Development is done with MSYS2 + mingw-w64
but compilation with Visual Studio is regularly checked.

### MSYS2

Steps are:
 * install MSYS2, and the following mingw x86_64 packages:
  * `make`
  * `gcc toolchain`

Launch MSYS2 Migw64 terminal and enter `samurai` subdirectory. Then run

```
make CC=gcc OS=windows
```

### Visual Studio

 * open the x64 Native Tools Command Prompt from the start menu.
 * cl.exe /D_CRT_SECURE_NO_WARNINGS=1 /W3 build.c deps.c env.c graph.c htab.c log.c parse.c samu.c scan.c tool.c tree.c util.c windows.c /link /out:samu.exe

## Differences from ninja

samurai tries to match ninja behavior as much as possible, but there
are several cases where it is slightly different:

- samurai uses the [variable lookup order] documented in the ninja manual,
  while ninja has a quirk ([ninja-build/ninja#1516]) that if the build
  edge has no variable bindings, the variable is looked up in file scope
  *before* the rule-level variables.
- samurai schedules jobs using a stack, so the last scheduled job is
  the first to execute, while ninja schedules jobs based on the pointer
  value of the edge structure (they are stored in a `std::set<Edge*>`),
  so the first to execute depends on the address returned by `malloc`.
  This may result in build failures due to insufficiently specified
  dependencies in the project's build system.
- samurai does not post-process the job output in any way, so if it
  includes escape sequences they will be preserved, while ninja strips
  escape sequences if standard output is not a terminal. Some build
  systems, like meson, force color output from gcc by default using
  `-fdiagnostics-color=always`, so if you plan to save the output to a
  log, you should pass `-Db_colorout=auto` to meson.
- samurai follows the [POSIX Utility Syntax Guidelines], in particular
  guideline 9, so it requires that any command-line options precede
  the operands. It does not do GNU-style argument permutation.

[ninja-build/ninja#1516]: https://github.com/ninja-build/ninja/issues/1516
[variable lookup order]: https://ninja-build.org/manual.html#ref_scope
[POSIX Utility Syntax Guidelines]: https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap12.html#tag_12_02
