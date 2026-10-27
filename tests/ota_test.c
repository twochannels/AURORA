/* SPDX-License-Identifier: GPL-3.0-only */
/* The update entry (firmware/src/ota.c) on a computer, with the installer's side simulated: the device reads parts
 * of a package over SysEx, checks them, stages the package's loader and asks the host to confirm.
 *   cc -O1 -Wall -DDINGHY_HOST -Ifirmware/src -o build/host/ota_test tests/ota_test.c
 *   build/host/ota_test PACKAGE.fwsc [DEVICE.fwsc] [EXPECT]
 * PACKAGE is what the installer serves; the unit's flash head (its first 16 KiB) is taken from DEVICE's package
 * (default: PACKAGE itself). Prints the session's result; with EXPECT (an rc, 0 = committed) exits 1 on a mismatch.
 * A .fwsc wraps the logical image: one marker byte after each of the first twenty 0x2F-byte blocks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "FM-1_950"
static uint8_t *pkg;
static uint32_t pkg_n;
static uint8_t dev_head[0x4000], stage[0x5000];
static uint8_t frame[1024];
static uint32_t frame_n, frame_ready, now_ms, reads;
static int committed;

static int ota_wire_send(const uint8_t *p, uint32_t n);
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!frame_ready) return 0;
    *p = frame + 1; *n = frame_n - 2u;              /* the 7-bit bytes between F0 and F7 */
    return 1;
}
static void ota_frame_done(void) { frame_ready = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { memset(stage + (off - 0xE0000u), 0xFF, 0x1000); return 0; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { memcpy(stage + (off - 0xE0000u), p, n); return 0; }
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    if (off + n <= 0x4000u) memcpy(p, dev_head + off, n);
    else if (off >= 0xE0000u && off + n <= 0xE5000u) memcpy(p, stage + (off - 0xE0000u), n);
    else memset(p, 0xFF, n);
    return 0;
}
static void ota_show(uint32_t step, int32_t code) { printf("  screen: step %u code %d\n", step, code); }
static void ota_commit(const uint8_t *parm) { committed = 1; printf("  commit: the loader's identity '%.12s'\n", parm + 8); }
#include "ota.c"

/* the installer: a read request for the logical image (or "success" at 0xE0000000) answered with one frame */
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t m[64], r[600];
    uint32_t d = ota_unpack7(p + 1, n - 2u, m, sizeof m), addr, len, i, s = 0;
    if (d < 15u || m[2] != 0x30u) return 0;         /* the identity reply and the like: nothing to answer */
    addr = ota_rd32(m + 7); len = ota_rd16(m + 11);
    if (len > 512u) return 0;
    reads++;
    r[0] = 0; r[1] = 0x59; r[2] = 0x30; r[3] = (uint8_t)(len + 8u); r[4] = (uint8_t)((len + 8u) >> 8); r[5] = 0; r[6] = 0;
    ota_wr32(r + 7, addr); r[11] = (uint8_t)len; r[12] = (uint8_t)(len >> 8); r[13] = 0;
    for (i = 0; i < len; i++)
        r[14 + i] = addr == 0xE0000000u ? (uint8_t)"success\0"[i & 7u] : addr + i < pkg_n ? pkg[addr + i] : 0xFFu;
    for (i = 6; i < 14u + len; i++) s += r[i];
    r[14 + len] = (uint8_t)~s;
    frame[0] = 0xF0;
    frame_n = 1u + ota_pack7(r, 15u + len, frame + 1);
    frame[frame_n++] = 0xF7;
    frame_ready = 1;
    return 0;
}

static uint8_t *load_logical(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw, *out;
    uint32_t i, k;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); *n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    raw = malloc(*n); fread(raw, 1, *n, f); fclose(f);
    out = malloc(*n);
    for (i = 0, k = 0; i < 20u; i++) { memcpy(out + k, raw + i * 0x30u, 0x2F); k += 0x2F; }   /* the markers dropped */
    memcpy(out + k, raw + 20u * 0x30u, *n - 20u * 0x30u); k += *n - 20u * 0x30u;
    free(raw); *n = k;
    return out;
}
static void head_of(const uint8_t *img, uint8_t *head)   /* the package's flash.bin head: its UFW entry of type 0 */
{
    uint8_t hdr[0x400];
    uint32_t i, nent;
    memcpy(hdr, img, 0x400); ota_jl_enc(hdr, 0x40); nent = ota_rd16(hdr + 8);
    for (i = 0; i < nent && i < 11u; i++) {
        uint8_t *e = hdr + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) { memcpy(head, img + ota_rd32(e + 8), 0x4000); return; }
    }
    fprintf(stderr, "no flash.bin in the device package\n"); exit(2);
}

int main(int argc, char **argv)
{
    uint8_t *dev;
    uint32_t dn;
    int rc;
    if (argc < 2) { fprintf(stderr, "ota_test PACKAGE.fwsc [DEVICE.fwsc] [EXPECT]\n"); return 2; }
    pkg = load_logical(argv[1], &pkg_n);
    dev = load_logical(argc > 2 ? argv[2] : argv[1], &dn);
    head_of(dev, dev_head);
    memset(stage, 0xFF, sizeof stage);
    printf("%s on a unit with %s's head:\n", argv[1], argc > 2 ? argv[2] : "its own");
    rc = ota_session();
    printf("  result %d (%s) after %u reads\n", rc, committed ? "committed" : rc == -5 ? "head / chip key" : rc == -7 ? "loader" : "refused", reads);
    if (argc > 3 && rc != atoi(argv[3])) { printf("  EXPECTED %s\n", argv[3]); return 1; }
    return 0;
}
