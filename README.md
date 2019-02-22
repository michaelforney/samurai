# samurai

[![Build Status](https://travis-ci.org/michaelforney/samurai.svg?branch=master)](https://travis-ci.org/michaelforney/samurai)
[![builds.sr.ht status](https://builds.sr.ht/~mcf/samurai.svg)](https://builds.sr.ht/~mcf/samurai)

samurai is a ninja-compatible build tool written in C99 with a focus on
simplicity, speed, and portability.

## Status

samurai implements the ninja build language through version 1.9.0 except
for MSVC dependency handling (`deps = msvc`). It uses the same format
for `.ninja_log` and `.ninja_deps` as ninja, currently version 5 and 3
respectively.

It is largely feature-complete and supports most of the same options as ninja.

## Requirements

samurai requires various POSIX.1-2008 interfaces.
