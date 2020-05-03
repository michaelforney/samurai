
project('samurai',
    'c',
    version: '1.9.0',
    meson_version: '>=0.49.0',
    default_options: [
        'c_std=c11',
        'warning_level=3',
    ],
)

config = configuration_data()

###############################################################################
# Project configuration

config.set_quoted('VERSION', meson.project_version())


###############################################################################
# Executable

configure_file(
    output: 'config.h',
    configuration: config,
)

samu_sources = [
    'build.c',
    'deps.c',
    'env.c',
    'graph.c',
    'htab.c',
    'log.c',
    'parse.c',
    'samu.c',
    'scan.c',
    'system.c',
    'tool.c',
    'tree.c',
    'util.c',
]

executable('samu',
    samu_sources,
    install: true,
)


###############################################################################
# Data

subdir('data')

install_data('LICENSE',
    install_dir: get_option('datadir') / 'licenses' / meson.project_name()
)
