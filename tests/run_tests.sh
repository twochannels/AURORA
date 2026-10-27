#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Everything that runs without the FM-1: the host build (WAV, screens, the worker tests), the site build
# (web/site/build.sh: the pages, the generated reference and the browser build) and the browser build's Node test
# (web/emu/emu_test.mjs). Needs cc, Python 3 with Pillow, Emscripten (tools/get_emsdk.sh) and Node.
set -e
cd "$(dirname "$0")/.."
# the generated headers must be what the generators make now
GEN_WAS=$(mktemp -d); cp firmware/gen/aurora_*.h "$GEN_WAS"/
python3 tools/gen_tables.py > /dev/null
diff -q "$GEN_WAS"/aurora_tables.h firmware/gen/aurora_tables.h > /dev/null || { echo 'firmware/gen/ was not what tools/gen_*.py make (now regenerated: review and commit it)'; exit 1; }
rm -rf "$GEN_WAS"
tests/run_host.sh
web/site/build.sh
node web/emu/emu_test.mjs
