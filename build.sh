#!/usr/bin/env bash
set -e

BUILD_TYPE=${BUILD_TYPE:-Debug}
BUILD_DIR=${BUILD_DIR:-build}

cmake -S . -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$(pwd)/$BUILD_DIR/bin" \
    -DHLASM_DEV_GUESS_BIN_SUBDIR=ON \
    -DBUILD_VSIX=OFF \
    -DDISCOVER_TESTS=OFF

cmake --build "$BUILD_DIR" "${@}"
