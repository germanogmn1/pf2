#!/bin/bash

set -ex

cd "$(dirname $0)"

mkdir -p vendor/include
curl -L -o vendor/include/stb_image.h https://raw.githubusercontent.com/nothings/stb/5c205738c191bcb0abc65c4febfa9bd25ff35234/stb_image.h

if [[ ! -d vendor/SDL ]]; then
    mkdir vendor/SDL
    curl -L -o /tmp/SDL3-3.2.0.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-3.2.0/SDL3-3.2.0.tar.gz
    tar xf /tmp/SDL3-3.2.0.tar.gz -C vendor/SDL --strip-components 1
fi

cd vendor/SDL
cmake -S . -B build
cmake --build build --config RelWithDebInfo
