install_man('samu.1')

install_data('bash-completions',
    rename: meson.project_name(),
    install_dir: get_option('datadir') / 'bash-completion' / 'completions',
)

install_data('zsh-completions',
    rename: '_' + meson.project_name(),
    install_dir: get_option('datadir') / 'zsh' / 'site-functions',
)
