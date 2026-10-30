/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer (tests/fake_fm1.h): the app (firmware/src/aurora.c) runs unchanged on it. platform.c
 * includes this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/aurora firmware/src/aurora.c
 *   build/host/aurora build/host/aurora              (-> build/host/aurora.wav, build/host/aurora-<ms>.ppm)
 *   build/host/aurora OUT script.txt [FPS]       (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT0 below to try your own app. After Dinghy's tests/host.c (zvenson). */
#include <stdio.h>
#include <stdlib.h>

static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }
static void midi_sysex(const uint8_t *d, uint32_t n) { uint32_t i; printf("%6u ms  SysEx out", fm1_ms); for (i = 0; i < n; i++) printf(" %02X", d[i]); printf("\n"); }

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, a MIDI message. A script file (the second
 * argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID", "T midi ST D1 D2", "T enc ID N",
 * "T end" */
typedef struct { uint32_t t, keys; int btn; int enc, n; uint8_t st, d1, d2; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {200, 1u << 7, -1, -1, 0, 0, 0, 0},                                /* C4 */
    {700, 1u << 7 | 1u << 11 | 1u << 14, -1, -1, 0, 0, 0, 0},          /* + E4 G4: a chord */
    {1500, 0, -1, -1, 0, 0, 0, 0},
    {1700, 0, -1, -1, 0, 0x90, 81, 120},                               /* MIDI in: A5 */
    {2300, 0, -1, -1, 0, 0x80, 81, 0},
    {2500, 0, BTN_OCT_UP, -1, 0, 0, 0, 0},
    {2700, 1u << 2 | 1u << 9, -1, -1, 0, 0, 0, 0},                     /* G4 + D5, an octave up */
    {3400, 0, -1, -1, 0, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 6500, frame_fps;
static const uint32_t SHOT[] = {1000, 3000};

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, -1, 0, 0, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "keys")) { sscanf(line, "%*u %*s %i", (int *)&e.keys); }
        else if (!strcmp(what, "btn")) { sscanf(line, "%*u %*s %d", &e.btn); }
        else if (!strcmp(what, "hold")) { sscanf(line, "%*u %*s %d", &e.btn); e.btn += 100; }   /* hold ID: held until "hold -1" */
        else if (!strcmp(what, "enc")) { sscanf(line, "%*u %*s %d %d", &e.enc, &e.n); }
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/aurora";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static uint32_t rv_hot_seen;
static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
    printf("meters: voices %d, after chorus %d, after delay %d, after reverb %d (of 32767); reverb tanks near the 16-bit edge: %u samples; delay writes clamped: %u\n",
           dbg_peak[0], dbg_peak[1], dbg_peak[2], dbg_peak[3], rv_hot, dly_hot);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame, saves, bsaves;
    fm1_ms++;
    while (step < script_n && script[step].t <= fm1_ms) {
        const host_ev_t *e = &script[step];
        if (e->st) fake_midi_put(e->st, e->d1, e->d2);
        else if (e->btn >= 100) fake_buttons(1u << (e->btn - 100));
        else if (e->btn == 99) fake_buttons(0);
        else if (e->btn >= 0) { fake_buttons(1u << e->btn); fake_buttons(0); }
        else if (e->enc >= 0) fake_enc[e->enc % 7] += e->n;
        else fake_keys(e->keys);
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        fake_render_half();
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < HALF_WORDS; i++)
            wav[2u * wav_n + i] = (int16_t)fake_blk[i];
        wav_n += HALF_FRAMES;
    }
    if (fake_saves != saves) { saves = fake_saves; printf("%6u ms  settings saved (%u B)\n", fm1_ms, fake_store[1] | fake_store[2] << 8); }
    if (fake_bank_saves != bsaves) { bsaves = fake_bank_saves; printf("%6u ms  user bank saved (slot magic %02X at 0, %02X at 128)\n", fm1_ms, fake_bank[0], fake_bank[128]); }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

/* HOST_STORE=file: the settings sector and the user bank kept in a file across runs, a power cycle between scripts */
static const char *store_file;
static void store_write(void)
{
    FILE *f = store_file ? fopen(store_file, "wb") : NULL;
    if (!f) return;
    fwrite(fake_store, 1, sizeof fake_store, f); fwrite(fake_bank, 1, sizeof fake_bank, f); fclose(f);
}
static void store_read(void)
{
    FILE *f = store_file ? fopen(store_file, "rb") : NULL;
    if (!f) return;
    if (fread(fake_store, 1, sizeof fake_store, f) == sizeof fake_store) (void)!fread(fake_bank, 1, sizeof fake_bank, f);
    fclose(f);
}
int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    store_file = getenv("HOST_STORE");
    store_read(); atexit(store_write);
    app_init();
    for (;;) {
        platform_poll();
        app_loop();
    }
}
