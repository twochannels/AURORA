#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The site: web/site's pages, the reference generated from the firmware (tools/gen_docs.py), the screenshots, and the
# emulator (web/emu/build.sh) under emu/.   web/site/build.sh  ->  build/site/
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/site/emu
cp web/site/*.html build/site/
cp web/site.css build/site/
cp web/site/fm1ota.js build/site/
mkdir -p build/site/firmware && cp web/site/firmware/aurora.fwsc build/site/firmware/
[ -d web/site/img ] && cp -R web/site/img build/site/
[ -f tools/gen_docs.py ] && python3 tools/gen_docs.py > build/site/reference.html || true   # (the reference comes with the manual)
web/emu/build.sh
cp build/emu/index.html build/emu/worklet.js build/emu/aurora.wasm build/site/emu/
echo "site: build/site ($(ls build/site | tr '\n' ' '))"
