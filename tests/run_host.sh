#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/aurora.c on this computer (tests/host.c): build/host/aurora.wav and the screens as PNG.
# Also the audio worker's unit tests (the second core's hand-off, on a host thread).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
CC="${CC:-cc}"
$CC -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-$(cat VERSION 2>/dev/null || echo dev)}\"" -Ifirmware/src -Ifirmware/gen -o build/host/aurora firmware/src/aurora.c
$CC -O2 -Wall -o build/host/audio_worker_test tests/audio_worker_test.c
build/host/audio_worker_test
$CC -O2 -Wall -pthread -o build/host/audio_worker_thread_test tests/audio_worker_thread_test.c
build/host/audio_worker_thread_test
$CC -O2 -Wall -Wno-unused-function -o build/host/sense_test tests/sense_test.c
build/host/sense_test
build/host/aurora build/host/aurora
python3 - <<'PYEOF'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("aurora-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PYEOF
$CC -O1 -Wno-unused-function -DDINGHY_HOST -Ifirmware/src -o build/host/ota_test tests/ota_test.c
build/host/ota_test web/site/firmware/aurora.fwsc web/site/firmware/aurora.fwsc 0 > /dev/null && echo "ota: our package installs over our head"
