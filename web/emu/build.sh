#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# AURORA in the browser: web/emu/aurora_web.c (the firmware against a fake FM-1) compiled to WebAssembly with
# Emscripten, plus the page.  web/emu/build.sh  ->  build/emu/{index.html, worklet.js, aurora.wasm}
# After Felucca's web/emu/build.sh (Leo Kuroshita, Hügelton Instruments, GPL-3.0).
# EMU_OPT replaces -O2, EMU_CFLAGS adds flags. The toolchain: emcc on the PATH, or ~/.emsdk (tools/get_emsdk.sh).
set -e
cd "$(dirname "$0")/../.."
if ! command -v emcc >/dev/null 2>&1 && [ -f "$HOME/.emsdk/emsdk_env.sh" ]; then
    EMSDK_QUIET=1 . "$HOME/.emsdk/emsdk_env.sh"
fi
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten): tools/get_emsdk.sh"; exit 1; }
VERSION="${APP_VER:-$(cat VERSION 2>/dev/null || echo dev)}"
mkdir -p build/emu
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function ${EMU_CFLAGS:-} \
    "-DAPP_VER=\"$VERSION\"" -Ifirmware/src -Ifirmware/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=16777216 -sFILESYSTEM=0 \
    -o build/emu/aurora.wasm web/emu/aurora_web.c
sed -e "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/aurora.wasm | tr -d ' ') B wasm, $VERSION)"
