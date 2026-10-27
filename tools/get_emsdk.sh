#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Emscripten for the browser emulator, into ~/.emsdk (prebuilt binaries: no Xcode, no Homebrew needed).
set -e
[ -d "$HOME/.emsdk" ] || git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$HOME/.emsdk"
cd "$HOME/.emsdk"
./emsdk install latest
./emsdk activate latest
echo "emsdk: ready; web/emu/build.sh finds it in ~/.emsdk"
