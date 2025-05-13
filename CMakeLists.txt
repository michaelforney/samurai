cmake_minimum_required(VERSION 3.16)
project(samurai C)

set(SOURCE
    build.c
    deps.c
    env.c
    graph.c
    htab.c
    log.c
    parse.c
    samu.c
    scan.c
    tool.c
    tree.c
    util.c
)

if (WIN32)
    list(APPEND SOURCE os-win32.c samu.manifest)
elseif(UNIX)
    list(APPEND SOURCE os-posix.c)
endif()

add_executable(samu ${SOURCE})

if (UNIX)
    target_link_libraries(samu PRIVATE rt)
endif()
