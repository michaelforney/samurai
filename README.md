# samurai

[![Build Status](https://travis-ci.org/michaelforney/samurai.svg?branch=master)](https://travis-ci.org/michaelforney/samurai)

samurai is a ninja-compatible build tool written in C.

It is mostly feature-complete, and is able to build large projects like
`chromium` and [`oasis`](https://github.com/michaelforney/oasis).

## Requirements
samurai should run on any POSIX system. It currently also requires `err.h`, but
this may change in the future.

## TODO
samurai does not yet parse or update `.ninja_deps` or gcc's `-MD` output. This
is planned.
