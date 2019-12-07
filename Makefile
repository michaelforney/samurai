default: build

_build:
	meson _build

build: _build
	ninja -C _build

install: _build
	ninja -C _build install
