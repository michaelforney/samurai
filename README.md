# samurai

[![Build Status](https://travis-ci.org/michaelforney/samurai.svg?branch=master)](https://travis-ci.org/michaelforney/samurai)

samurai is a ninja-compatible build tool written in C99 with a focus on
simplicity, speed, and portability.

## Status

samurai implements the ninja build language through version 1.8.2 except
for MSVC dependency handling (`deps = msvc`). It uses the same format
for `.ninja_log` and `.ninja_deps` as ninja, currently version 5 and 3
respectively.

It is largely feature-complete and supports most of the same options as ninja.

## Requirements

samurai requires various POSIX.1-2008 interfaces.

On macOS, you will need to add `-D st_mtim=st_mtimespec` to `CFLAGS` since
it does not define the `st_mtim` member of `struct stat`.
