/* SPDX-License-Identifier: GPL-3.0-only */
/* AURORA in the browser: the firmware's app against the fake FM-1 (tests/fake_fm1.h) and the exports the
 * AudioWorklet calls (web_fm1.h), compiled to WebAssembly by web/emu/build.sh. Nothing of the firmware changes. */
#define DINGHY_WEB 1
#include "../../firmware/src/aurora.c"

/* the sound now, for the page's "Copy sound": the patch's bytes and their names */
EXPORT uint8_t *web_patch(void) { return (uint8_t *)&patch; }
EXPORT uint32_t web_patch_size(void) { return sizeof patch; }
EXPORT const char *web_patch_fields(void) { return PATCH_FIELDS; }
