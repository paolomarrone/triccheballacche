#!/bin/sh
# Build the LV2 metadata library and its dependencies in a private prefix.
set -eu
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
build() {
    name=$1 version=$2 url=$3
    shift 3
    source="$prefix/src/$name-$version"
    if test ! -f "$source/meson.build"; then
        mkdir -p "$prefix/src"
        curl -fL --retry 2 "$url/$name-$version.tar.xz" -o "$source.tar.xz"
        tar -xf "$source.tar.xz" -C "$prefix/src"
    fi
    meson setup --reconfigure "$source/build" "$source" --prefix="$prefix" --libdir=lib \
        --buildtype=release --default-library=static -Ddocs=disabled -Dtests=disabled "$@"
    meson compile -C "$source/build"
    meson install -C "$source/build"
}
build lv2 1.18.10 https://lv2plug.in/spec -Dplugins=disabled
build zix 0.8.2 https://download.drobilla.net -Dbenchmarks=disabled
build serd 0.32.10 https://download.drobilla.net -Dtools=disabled
build sord 0.16.22 https://download.drobilla.net -Dtools=disabled
build sratom 0.6.22 https://download.drobilla.net
build lilv 0.28.0 https://download.drobilla.net -Dtools=disabled -Dbindings_py=disabled -Dbindings_cpp=disabled
touch "$prefix/.ready"
