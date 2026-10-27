/* SPDX-License-Identifier: GPL-3.0-only */
/* AURORA: an ambient texture instrument for the M-VAVE FM-1, a sound module for a dawless rig. Big, wide sounds
 * that keep moving on their own and bend to MIDI. On the Dinghy platform (platform.c) with FM-106's dual-core
 * worker, as CS-1 was.
 *
 *   the pad      eight voices of twelve sine partials: TILT (how fast the partials fall off), SPREAD (odd against
 *                even partials: hollow to full), BRIGHT (a ceiling the partials fade under), an attack and a
 *                release; BLOOM staggers the partials' entry so a chord opens like a flower
 *   movement     every voice drifts on its own: a random walk per partial (DRIFT) and a slow wander of the tilt
 *                (MOVE, at RATE); more layers of movement to come
 *   width        odd partials lean left, even right, by WIDTH: the pad is as wide as you like, mono-safe
 *   space        the plate and its shimmer, the tape delay and the phaser from CS-1
 *   MIDI         notes on the MIDI page's channel; CC 80..87 are the eight macros, in the Circuit Tracks' order:
 *                MOVE SPACE AIR TILT WIDTH DRIFT FREEZE BLOOM; channel pressure or the expression CC swells
 *                (brightness and level); sustain (CC 64); pitch bend; MIDI clock for the delay
 *   keys         play; OCT- OCT+ the octave; PLAY latches (hold a drone with no hands)
 *   pages        HOME (the macros, a weather strip), EDIT: PAD, PAD 2; LFO: MOVE; FX: SPACE, DELAY, PHASER;
 *                GLO: MIDI. KNOB 1..4 the page's parameters. The panel is kept 2 s after the last change.
 * Rules of the road (no FPU, no libgcc): integers only, no float, no 64-bit division. tests/host.c runs this file
 * on a computer; web/emu in the browser. */
#define APP_NAME "AURORA"
#include "platform.c"
#include <stddef.h>
#include "aurora_dsp.h"

#define CTL 32u                                    /* frames a control tick */
#define NTICK (HALF_FRAMES / CTL)
#define NV 8u                                      /* voices */
#define NP 12u                                     /* partials a voice */

/* ---- the sound: everything a knob or a macro sets, kept with the settings */
typedef struct {
    uint8_t tilt, spread, bright, attack, release, bloom;        /* the pad */
    uint8_t move, rate, drift, width, air;                       /* movement, width, the air (noise: to come) */
    uint8_t swell;                                               /* how much pressure / expression opens the pad */
    uint8_t dly_time, dly_fb, dly_mix, dly_ping, dly_damp, dly_sync, dly_tape;   /* the delay (aurora_fx.h) */
    uint8_t rv_mix, rv_size, rv_damp, rv_shim, rv_shift;         /* the plate and its shimmer */
    uint8_t ph_rate, ph_depth, ph_fb, ph_mix;                    /* the phaser */
    uint8_t volume;
} patch_t;
static patch_t patch;
static const patch_t INIT_PATCH = {
    .tilt = 70, .spread = 64, .bright = 90, .attack = 60, .release = 80, .bloom = 40,
    .move = 40, .rate = 30, .drift = 30, .width = 70, .air = 0, .swell = 64,
    .dly_time = 90, .dly_fb = 50, .dly_mix = 0, .dly_ping = 1, .dly_damp = 50, .dly_sync = 0, .dly_tape = 40,
    .rv_mix = 60, .rv_size = 100, .rv_damp = 50, .rv_shim = 0, .rv_shift = 12,
    .ph_rate = 20, .ph_depth = 64, .ph_fb = 30, .ph_mix = 0,
    .volume = 100,
};
static volatile uint32_t sync_dly;                 /* the delay's time from the MIDI clock, frames (the main loop's) */
#include "aurora_fx.h"

/* ---- the voices (the render's, but for the main loop's note, vel, gate, trig, at) */
typedef struct {
    volatile uint8_t note, vel, gate, at;
    volatile uint32_t trig;
    uint32_t seen, age;
    volatile uint8_t active;
    uint32_t ph[NP];                               /* each partial's phase */
    int32_t amp[NP];                               /* each partial's level now (Q15), ramped a block */
    int32_t walk[NP];                              /* each partial's random walk (Q15 around 0) */
    int32_t wander;                                /* the voice's slow wander of the tilt (Q15) */
    int32_t env;                                   /* the envelope, Q24 */
    uint8_t stage;                                 /* 0 off 1 attack 2 held 3 release */
    uint32_t t;                                    /* blocks since the note began (BLOOM) */
    uint32_t rng;
} voice_t;
static voice_t voices[NV];
static volatile int32_t bend256;                   /* pitch bend, 1/256 semitone */
static volatile uint8_t press;                     /* channel pressure / expression, 0..127 (the main loop's) */
static int32_t g_l[2][HALF_FRAMES], g_r[2][HALF_FRAMES];   /* each core's voices, left and right */
static int32_t g_st[2u * HALF_FRAMES];
static int32_t rv_src[2u * HALF_FRAMES], rv_wet[2u * HALF_FRAMES];
static uint32_t audio_cycles;
static volatile int32_t out_peak;

/* ---- one voice, one block */
static inline uint32_t partial_inc(uint32_t note, int32_t bend, uint32_t k)   /* partial k + 1 of the note's pitch */
{
    uint32_t inc = pitch_inc256((int32_t)note * 256 + bend);
    return inc * (k + 1u);
}
static void voice_block(voice_t *w, int32_t *outl, int32_t *outr, uint32_t core)
{
    uint32_t k, i, trig = w->trig;
    int32_t target[NP], from[NP], env_q15, pr = press * 258;
    int32_t tilt16 = (int32_t)patch.tilt * 16, spread = (int32_t)patch.spread - 64, bright = patch.bright;
    int32_t swell = mulq15(pr, (int32_t)patch.swell * 258);        /* 0..32767: pressure opening the pad */
    int32_t wl, wr, width = (int32_t)patch.width * 258;
    (void)core;
    if (trig != w->seen) {                         /* a new note */
        w->seen = trig;
        w->stage = 1;
        w->t = 0;
        if (!w->rng) w->rng = 0x9E3779B9u * (uint32_t)(w - voices + 1);
    }
    if (!w->gate && w->stage && w->stage < 3) w->stage = 3;
    /* the envelope, a block at a time: linear attack, exponential release */
    if (w->stage == 1) {
        w->env += (1 << 24) / (int32_t)(4u + ((uint32_t)patch.attack * patch.attack >> 4));   /* 4 .. 1000 blocks */
        if (w->env >= (1 << 24)) { w->env = 1 << 24; w->stage = 2; }
    } else if (w->stage == 3) {                    /* exponential: 60 dB in 0.1 s (0) .. ~30 s (127), log-spaced */
        uint32_t sh = (uint32_t)patch.release * 9u / 127u, fr = (uint32_t)patch.release * 9u % 127u;
        int32_t k = (22000 >> sh) - (int32_t)(((22000u >> sh) - (22000u >> (sh + 1u))) * fr / 127u);
        w->env -= mulq15(w->env >> 9, k) << 9;
        if (w->env < 2048) { w->env = 0; w->stage = 0; }
    }
    w->active = w->stage != 0;
    if (!w->active) return;
    w->t++;
    /* the slow wander (MOVE at RATE) and the partials' random walks (DRIFT) */
    {
        int32_t step = 1 + ((int32_t)patch.rate * patch.rate >> 6);          /* 1 .. 252 a block */
        int32_t r = (int32_t)(noise32(&w->rng) >> 20) - 2048;               /* +-2048 */
        w->wander += (r * step) >> 6;
        w->wander = clamp(w->wander, -32767, 32767);
        w->wander -= w->wander >> 9;                                         /* and a leak back to the centre */
        for (k = 0; k < NP; k++) {
            int32_t d = (int32_t)(noise32(&w->rng) >> 22) - 512;
            w->walk[k] += (d * (int32_t)patch.drift) >> 5;
            w->walk[k] = clamp(w->walk[k], -32767, 32767);
            w->walk[k] -= w->walk[k] >> 8;
        }
    }
    env_q15 = w->env >> 9;
    /* each partial's level: 1 / (k + 1) ^ tilt, the odd / even balance, the brightness ceiling, the walk, the bloom */
    for (k = 0; k < NP; k++) {
        int32_t lv = 32767, n = (int32_t)k, j;
        int32_t t = tilt16 - (mulq15(patch.move * 258, w->wander) >> 4);     /* MOVE: the wander bends the tilt */
        for (j = 0; j < n; j++) lv -= (lv * t) >> 11;                        /* geometric fall: tilt 127 = x0.0 a step */
        if (k & 1u) lv = mulq15(lv, 32767 - spread * 256); else lv = mulq15(lv, 32767 + spread * 256);   /* SPREAD: odd (k even) against even */
        if ((int32_t)k * 10 > bright) lv = mulq15(lv, (int32_t)clamp(32767 - ((int32_t)k * 10 - bright) * 400, 0, 32767));   /* BRIGHT: a ceiling */
        lv = mulq15(lv, 32767 + (mulq15(w->walk[k], 16000)));               /* the walk: +-half */
        if (k && swell) lv += mulq15(32767 - lv, mulq15(swell, (int32_t)k * 2700));   /* pressure opens the upper partials */
        if (patch.bloom && w->t * 4u < (uint32_t)patch.bloom * k) lv = 0;   /* BLOOM: partial k enters bloom x k / 4 blocks in */
        target[k] = mulq15(lv, env_q15) / (int32_t)(2 + (k >> 1));           /* the sum of twelve kept under full scale */
        from[k] = w->amp[k];
        w->amp[k] = target[k];
    }
    wl = 32767; wr = 32767 - mulq15(width, 28000);                           /* odd partials: left and a little right ... */
    for (i = 0; i < HALF_FRAMES; i++) {
        int32_t odd = 0, even = 0, f = (int32_t)i;
        for (k = 0; k < NP; k++) {
            int32_t a = from[k] + (((target[k] - from[k]) * f) >> 8);
            int32_t s = mulq15(sine_i(w->ph[k]), a);
            if (k & 1u) even += s; else odd += s;
            w->ph[k] += partial_inc(w->note, bend256, k);
        }
        outl[i] += mulq15(odd, wl) + mulq15(even, wr);                       /* ... even partials: right and a little left */
        outr[i] += mulq15(even, wl) + mulq15(odd, wr);
    }
}
static void voices_render(uint32_t first, uint32_t step, int32_t *l, int32_t *r)
{
    uint32_t v, i;
    for (i = 0; i < HALF_FRAMES; i++) l[i] = r[i] = 0;
    for (v = first; v < NV; v += step) voice_block(&voices[v], l, r, first);
}
static void worker_render(void *ctx) { (void)ctx; voices_render(1, 2, g_l[1], g_r[1]); fx_reverb(rv_src, rv_wet, HALF_FRAMES); }
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, t0 = fm1_ticks();
    int32_t vol = patch.volume * 258;
    int submitted;
    (void)n;
    submitted = audio_worker_submit(worker_render, 0);
    voices_render(0, submitted ? 2u : 1u, g_l[0], g_r[0]);
    if (submitted) audio_worker_join();
    for (i = 0; i < HALF_FRAMES; i++) {
        int32_t l = g_l[0][i] + (submitted ? g_l[1][i] : 0), r = g_r[0][i] + (submitted ? g_r[1][i] : 0);
        g_st[2u * i] = mulq15(soft_knee(l, 20000), vol);
        g_st[2u * i + 1u] = mulq15(soft_knee(r, 20000), vol);
    }
    fx_delay(g_st, HALF_FRAMES);
    fx_phaser(g_st, HALF_FRAMES);
    if (submitted) {
        for (i = 0; i < 2u * HALF_FRAMES; i++) { rv_src[i] = g_st[i]; g_st[i] += rv_wet[i]; }
    } else {
        fx_reverb(g_st, rv_wet, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) g_st[i] += rv_wet[i];
    }
    {
        int32_t pk = 0;
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t o = soft_knee(g_st[i], 16000), a = o < 0 ? -o : o;
            out[i] += o;
            if (a > pk) pk = a;
        }
        out_peak = pk;
    }
    audio_cycles = fm1_ticks() - t0;
}

/* ---- the main loop's side: notes */
static uint32_t voice_ages;
static void voice_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u) return;
    for (v = 0; v < NV; v++) {
        voice_t *w = &voices[v];
        uint32_t score = w->note == note && w->gate ? 0u : w->gate ? 0xC0000000u + w->age : w->active ? 0x80000000u + w->age : w->age;
        if (score < best) { best = score; pick = v; }
    }
    voices[pick].note = (uint8_t)note;
    voices[pick].vel = (uint8_t)(vel > 127u ? 127u : vel);
    voices[pick].age = ++voice_ages;
    voices[pick].gate = 1;
    voices[pick].trig++;
}
static void voice_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voices[v].note == note && voices[v].gate) voices[v].gate = 0;
}
static uint32_t voices_sounding(void)
{
    uint32_t v, m = 0;
    for (v = 0; v < NV; v++) if (voices[v].active) m |= 1u << v;
    return m;
}

/* ---- what survives a power-off */
typedef struct {
    uint8_t magic, oct_plus2, midi_chan, expr, latch;
    patch_t patch;
} settings_t;
static settings_t set;
#define SET_MAGIC 0xA1u

/* ---- the pages */
enum { K_NUM = 0, K_ONOFF, K_MCHAN, K_EXPR, K_DIV };
enum { CW = 0, CG, CR, CK, CY, CS };                     /* a cap: white, green, red, black, yellow, silver */
typedef struct { const char *name; uint8_t off, max, kind, global, color; const char *hi, *lo; } pdesc_t;
#define PA(f, name, max, kind, c, hi, lo) {name, (uint8_t)offsetof(patch_t, f), max, kind, 0, c, hi, lo}
#define ST(f, name, max, kind, c, hi, lo) {name, (uint8_t)offsetof(settings_t, f), max, kind, 1, c, hi, lo}
#define NONE {0, 0, 0, 0, 0, 0, 0, 0}
typedef struct { const char *title; pdesc_t p[4]; } page_t;
enum { PG_HOME = 0, PG_PAD, PG_PAD2, PG_MOVE, PG_SPACE, PG_DELAY, PG_PHASER, PG_MIDI, NPAGE };
static const page_t PAGES[NPAGE] = {
    {"AURORA", {NONE, NONE, NONE, NONE}},
    {"PAD",    {PA(tilt, "TILT", 127, K_NUM, CW, "DARK", "FULL"), PA(spread, "SPREAD", 127, K_NUM, CW, "EVEN", "ODD"),
                PA(bright, "BRIGHT", 127, K_NUM, CG, "OPEN", "LOW"), PA(bloom, "BLOOM", 127, K_NUM, CY, "SLOW", "AT ONCE")}},
    {"PAD 2",  {PA(attack, "ATTACK", 127, K_NUM, CK, "LONG", "SHORT"), PA(release, "RELEASE", 127, K_NUM, CK, "LONG", "SHORT"),
                PA(swell, "SWELL", 127, K_NUM, CG, "10", "0"), PA(volume, "VOLUME", 127, K_NUM, CK, "10", "0")}},
    {"MOVE",   {PA(move, "MOVE", 127, K_NUM, CW, "10", "0"), PA(rate, "RATE", 127, K_NUM, CW, "FAST", "SLOW"),
                PA(drift, "DRIFT", 127, K_NUM, CW, "10", "0"), PA(width, "WIDTH", 127, K_NUM, CS, "WIDE", "MONO")}},
    {"SPACE",  {PA(rv_mix, "MIX", 127, K_NUM, CS, "10", "0"), PA(rv_size, "SIZE", 127, K_NUM, CW, "LONG", "SHORT"),
                PA(rv_damp, "DAMP", 127, K_NUM, CG, "10", "0"), PA(rv_shim, "SHIMMR", 127, K_NUM, CY, "10", "0")}},
    {"DELAY",  {PA(dly_time, "TIME", 127, K_NUM, CW, "LONG", "SHORT"), PA(dly_fb, "FEEDBK", 127, K_NUM, CW, "10", "0"),
                PA(dly_mix, "MIX", 127, K_NUM, CS, "10", "0"), PA(dly_tape, "TAPE", 127, K_NUM, CY, "10", "0")}},
    {"PHASER", {PA(ph_rate, "RATE", 127, K_NUM, CW, "FAST", "SLOW"), PA(ph_depth, "DEPTH", 127, K_NUM, CG, "10", "0"),
                PA(ph_fb, "FEEDBK", 127, K_NUM, CR, "10", "0"), PA(ph_mix, "MIX", 127, K_NUM, CS, "10", "0")}},
    {"MIDI",   {ST(midi_chan, "CHAN", 16, K_MCHAN, CW, "16", "OMNI"), ST(expr, "EXPR", 127, K_EXPR, CW, "CC", "AT"), NONE, NONE}},
};
static const uint8_t PAGE_GROUP[NPAGE][2] = {{PG_HOME, 1}, {PG_PAD, 2}, {PG_PAD, 2}, {PG_MOVE, 1}, {PG_SPACE, 3}, {PG_SPACE, 3}, {PG_SPACE, 3}, {PG_MIDI, 1}};
static const char *const N_ONOFF[2] = {"OFF", "ON"};
/* the macros, CC 80..87 in the Circuit Tracks' order, each a patch field */
static const struct { const char *name; uint8_t off; } MACRO[8] = {
    {"MOVE", offsetof(patch_t, move)}, {"SPACE", offsetof(patch_t, rv_mix)}, {"AIR", offsetof(patch_t, air)}, {"TILT", offsetof(patch_t, tilt)},
    {"WIDTH", offsetof(patch_t, width)}, {"DRIFT", offsetof(patch_t, drift)}, {"FREEZE", offsetof(patch_t, dly_fb)}, {"BLOOM", offsetof(patch_t, bloom)}};

/* ---- the screen */
#define C_PANEL RGB(14, 16, 24)                           /* a night sky */
#define C_SILK RGB(214, 222, 232)
#define C_LEG RGB(104, 116, 136)
#define C_RULE RGB(60, 70, 90)
#define C_ACC RGB(120, 230, 190)                          /* the aurora's green */
#define C_ACC2 RGB(180, 120, 230)
#define C_SLOT RGB(6, 7, 12)
#define KEY0 53u
static const uint16_t CAP[6] = {RGB(236, 234, 226), RGB(0, 150, 92), RGB(214, 40, 56), RGB(62, 62, 68), RGB(236, 200, 40), RGB(160, 160, 168)};
static uint32_t page = PG_HOME;
static uint8_t *param_ptr(const pdesc_t *d) { return d->global ? (uint8_t *)&set + d->off : (uint8_t *)&patch + d->off; }
static void text_box(uint32_t x, uint32_t y, uint32_t w, const char *str, uint16_t c, int align)
{
    int32_t tw = text_w(&FONT_S, str), tx = align == 1 ? ((int32_t)w - tw) / 2 : align == 2 ? (int32_t)w - tw : 0;
    cv_begin(w, FONT_S.h, C_PANEL);
    cv_text(tx, 0, &FONT_S, str, c);
    cv_blit(x, y);
}
static void fmt_value(const pdesc_t *d, uint32_t v, char *s)
{
    const char *src = 0;
    int32_t n = (int32_t)v, i = 0;
    switch (d->kind) {
    case K_ONOFF: src = N_ONOFF[v & 1u]; break;
    case K_MCHAN: if (!v) src = "OMNI"; break;
    case K_EXPR: if (!v) src = "AT"; break;
    default: break;
    }
    if (src) { while (*src) s[i++] = *src++; s[i] = 0; return; }
    if (n >= 100) s[i++] = (char)('0' + n / 100);
    if (n >= 10) s[i++] = (char)('0' + n / 10 % 10);
    s[i++] = (char)('0' + n % 10);
    s[i] = 0;
}
static void draw_header(void)
{
    const page_t *pg = &PAGES[page];
    char s[8];
    int32_t w = text_w(&FONT_S, pg->title), x0 = (240 - w) / 2;
    int32_t rl = PAGE_GROUP[page][1] > 1u ? 32 : 8, rr = page == PG_MIDI ? 232 - text_w(&FONT_S, "V" APP_VER) : 240 - 14;
    cv_begin(240, 24, C_PANEL);
    cv_rect(rl, 12, x0 - 6 - rl, 1, C_RULE);
    cv_rect(x0 + w + 6, 12, rr - (x0 + w + 6), 1, C_RULE);
    cv_text(x0, 4, &FONT_S, pg->title, page == PG_HOME ? C_ACC : C_SILK);
    if (page == PG_MIDI) cv_text(238 - text_w(&FONT_S, "V" APP_VER), 4, &FONT_S, "V" APP_VER, C_LEG);
    if (PAGE_GROUP[page][1] > 1u) {
        s[0] = (char)('1' + page - PAGE_GROUP[page][0]); s[1] = '/'; s[2] = (char)('0' + PAGE_GROUP[page][1]); s[3] = 0;
        cv_text(2, 4, &FONT_S, s, C_LEG);
    }
    cv_blit(0, 0);
}
static void draw_param(uint32_t k)
{
    const pdesc_t *d = &PAGES[page].p[k];
    uint32_t v, i;
    int32_t y, x = 60 * (int32_t)k;
    char s[8];
    cv_begin(60, 136, C_PANEL);
    if (!d->name) { cv_blit(x, 26); return; }
    v = *param_ptr(d);
    if (d->hi) cv_text(19 - (text_w(&FONT_S, d->hi) >> 1), 0, &FONT_S, d->hi, C_LEG);
    cv_rect(16, 22, 7, 80, C_SLOT);
    for (i = 0; i < 11u; i++) { cv_rect(9, 26 + (int32_t)i * 7, 5, 1, C_LEG); cv_rect(25, 26 + (int32_t)i * 7, 5, 1, C_LEG); }
    y = 98 - (int32_t)(v * 72u / (d->max ? d->max : 1u));
    cv_rect(8, y - 4, 23, 9, CAP[d->color]);
    cv_rect(8, y - 4, 23, 1, RGB(255, 255, 255));
    fmt_value(d, v, s);
    cv_text(33, y - 8, &FONT_S, s, C_SILK);
    if (d->lo) cv_text(19 - (text_w(&FONT_S, d->lo) >> 1), 106, &FONT_S, d->lo, C_LEG);
    { int32_t w = text_w(&FONT_S, d->name), tx = 19 - (w >> 1); cv_text(tx < 0 ? 0 : tx + w > 60 ? 60 - w : tx, 122, &FONT_S, d->name, C_SILK); }
    cv_blit(x, 26);
}
/* HOME: the eight macros as bars, in the Circuit Tracks' order */
static void draw_home(void)
{
    uint32_t k;
    char s[8];
    for (k = 0; k < 8u; k++) {
        uint32_t v = *((uint8_t *)&patch + MACRO[k].off);
        int32_t y = 30 + (int32_t)k * 17, w = (int32_t)(v * 120u / 127u);
        cv_begin(240, 17, C_PANEL);
        cv_text(0, 0, &FONT_S, MACRO[k].name, C_LEG);
        cv_rect(64, 4, 122, 9, C_SLOT);
        cv_rect(65, 5, w, 7, k & 1u ? C_ACC2 : C_ACC);
        fmt_value(&PAGES[PG_PAD].p[0], v, s);
        cv_text(196, 0, &FONT_S, s, C_SILK);
        cv_blit(0, y);
    }
}
static void draw_page(void)
{
    uint32_t k;
    lcd_fill(0, 0, 240, 240, C_PANEL);
    draw_header();
    if (page == PG_HOME) { draw_home(); return; }
    for (k = 0; k < 4u; k++) draw_param(k);
}
/* the strip at 166..196: the output level, the pressure, the voices (the weather graph to come) */
static int32_t meter_hold;
static void draw_monitor(uint32_t sounding)
{
    uint32_t v;
    int32_t w;
    cv_begin(240, 28, C_PANEL);
    cv_text(0, 0, &FONT_S, "OUT", C_LEG);
    cv_rect(30, 3, 122, 9, C_SLOT);
    w = meter_hold * 120 / 32767;
    cv_rect(31, 4, w > 120 ? 120 : w, 7, w > 100 ? CAP[CY] : CAP[CG]);
    cv_text(0, 15, &FONT_S, "AT", C_LEG);
    cv_rect(30, 18, 122, 9, C_SLOT);
    cv_rect(31, 19, (int32_t)press * 120 / 127, 7, CAP[CW]);
    for (v = 0; v < NV; v++) cv_rect(164 + (int32_t)v * 9, 8, 6, 14, sounding >> v & 1u ? C_ACC : C_SLOT);
    cv_blit(0, 166);
}
static uint32_t load_now, wload_now;
static void draw_status(int32_t oct, uint32_t nvoices)
{
    char t[28] = "OCT +0   0 VOICES";
    t[4] = oct < 0 ? '-' : '+';
    t[5] = (char)('0' + (oct < 0 ? -oct : oct));
    t[9] = (char)('0' + nvoices);
    if (load_now) {
        uint32_t l = load_now > 99u ? 99u : load_now, w = wload_now > 99u ? 99u : wload_now;
        t[16] = ' '; t[17] = l >= 10u ? (char)('0' + l / 10u) : ' '; t[18] = (char)('0' + l % 10u); t[19] = '/';
        t[20] = w >= 10u ? (char)('0' + w / 10u) : ' '; t[21] = (char)('0' + w % 10u); t[22] = '%'; t[23] = 0;
    }
    if (set.latch) { t[0] = 'L'; t[1] = 'A'; t[2] = 'T'; }
    text_box(0, 224, 240, t, C_LEG, 1);
}

/* ---- MIDI clock: the delay's SYNC follows it */
static uint32_t clk_n, clk_t0, clk_last, beat_ms = 500;
static void clock_tick(void)
{
    uint32_t now = fm1_ms;
    if (now - clk_last > 2000u) { clk_n = 0; clk_t0 = now; }
    clk_last = now;
    if (clk_n++ == 24u) {
        uint32_t b = now - clk_t0;
        clk_t0 = now; clk_n = 1;
        if (b > 100u && b < 4000u) { uint32_t d = b > beat_ms ? b - beat_ms : beat_ms - b; beat_ms = d > beat_ms / 10u ? b : (beat_ms + b) / 2u; }
    }
}

/* ---- the main loop */
static uint32_t prev_keys, t_change, dirty, page_drawn = ~0u, status_drawn = ~0u, strip_ms, last_midi_ms, loud_ms, midi_redraw;
static uint8_t held[27], sustain, sus_held[128 / 8];
static int32_t oct(void) { return (int32_t)set.oct_plus2 - 2; }
static void changed(void) { dirty = 1; t_change = fm1_ms; }
static void note_on(uint32_t note, uint32_t vel) { voice_on(note, vel); sus_held[note >> 3] &= (uint8_t)~(1u << (note & 7u)); }
static void note_off(uint32_t note)
{
    if (sustain || set.latch) sus_held[note >> 3] |= (uint8_t)(1u << (note & 7u));
    else voice_off(note);
}
static void release_all(void)
{
    uint32_t n;
    for (n = 0; n < 128u; n++)
        if (sus_held[n >> 3] >> (n & 7u) & 1u) { sus_held[n >> 3] &= (uint8_t)~(1u << (n & 7u)); voice_off(n); }
}
static void app_init(void)
{
    if (settings_load(&set, sizeof set) || set.magic != SET_MAGIC || set.oct_plus2 > 4u) {
        set.magic = SET_MAGIC; set.oct_plus2 = 2; set.midi_chan = 0; set.expr = 1; set.latch = 0;
        set.patch = INIT_PATCH;
    }
    patch = set.patch;
    lcd_fill(0, 0, 240, 240, C_PANEL);
}
static void app_loop(void)
{
    uint32_t keys, down, up, b, k, lit = 0, sounding, nv = 0;
    uint32_t base = (uint32_t)(KEY0 + 12 * oct());
    uint8_t st, d1, d2;
    int32_t n;
    if (platform_screen_take()) page_drawn = ~0u;
    keys = fm1_in.notes; down = keys & ~prev_keys; up = prev_keys & ~keys; prev_keys = keys;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u) { held[k] = (uint8_t)(base + k); note_on(held[k], 100); midi_send(0x90, held[k], 100); }
        if (up >> k & 1u) { note_off(held[k]); midi_send(0x80, held[k], 0); }
    }
    b = fm1_input_edges(&k);
    if (b & (1u << BTN_OCT_DOWN) && set.oct_plus2 > 0u) { set.oct_plus2--; changed(); }
    if (b & (1u << BTN_OCT_UP) && set.oct_plus2 < 4u) { set.oct_plus2++; changed(); }
    if (b & (1u << BTN_HOME)) page = PG_HOME;
    if (b & (1u << BTN_EDIT)) page = page == PG_PAD ? PG_PAD2 : PG_PAD;
    if (b & (1u << BTN_LFO)) page = PG_MOVE;
    if (b & (1u << BTN_FX)) page = page >= PG_SPACE && page < PG_PHASER ? page + 1u : PG_SPACE;
    if (b & (1u << BTN_GLO)) page = PG_MIDI;
    if (b & (1u << BTN_PLAY)) { set.latch ^= 1u; if (!set.latch && !sustain) release_all(); changed(); status_drawn = ~0u; }   /* LATCH: a drone with no hands */
    if ((b & (1u << BTN_OCT_DOWN) && fm1_in.buttons >> BTN_OCT_UP & 1u) || (b & (1u << BTN_OCT_UP) && fm1_in.buttons >> BTN_OCT_DOWN & 1u)) {
        sustain = 0; release_all(); for (k = 0; k < 128u; k++) voice_off(k);
    }
    if ((n = fm1_enc_take(ENC_ALGORITHM)) != 0 && page != PG_HOME) {   /* the group's pages, either way */
        uint32_t first = PAGE_GROUP[page][0], cnt = PAGE_GROUP[page][1];
        page = first + (uint32_t)(((int32_t)(page - first) + n + (int32_t)cnt * 4) % (int32_t)cnt);
    }
    for (k = 0; k < 4u; k++)
        if ((n = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
            const pdesc_t *d = &PAGES[page].p[k];
            uint8_t *p;
            int32_t v;
            if (!d->name) continue;
            p = param_ptr(d);
            v = clamp((int32_t)*p + n * (d->max >= 100u ? 2 : 1), 0, d->max);
            if (v != *p) { *p = (uint8_t)v; set.patch = patch; changed(); draw_param(k); }
        }
    while (midi_get(&st, &d1, &d2)) {
        uint32_t m = st & 0xF0u, chn = (st & 15u) + 1u;
        if (st != 0xF8u) last_midi_ms = fm1_ms;
        if (st == 0xF8u) { clock_tick(); continue; }
        if (st >= 0xF0u) continue;
        if (set.midi_chan && chn != set.midi_chan) continue;
        if (m == 0x90u && d2) note_on(d1, d2);
        else if (m == 0x80u || m == 0x90u) note_off(d1);
        else if (m == 0xD0u) press = d1;
        else if (m == 0xE0u) bend256 = (((int32_t)d2 << 7 | d1) - 8192) >> 4;
        else if (m == 0xB0u) {
            if (set.expr && d1 == set.expr) { press = d2; continue; }
            if (d1 == 64u) { sustain = d2 >= 64u; if (!sustain && !set.latch) release_all(); continue; }
            if (d1 == 120u || d1 == 123u) { sustain = 0; release_all(); for (k = 0; k < 128u; k++) voice_off(k); continue; }
            if (d1 >= 80u && d1 < 88u) { *((uint8_t *)&patch + MACRO[d1 - 80u].off) = d2; set.patch = patch; changed(); midi_redraw = 1; }
            else if (d1 == 7u) { patch.volume = d2; set.patch = patch; changed(); }
        }
    }
    if (midi_redraw) { midi_redraw = 0; if (page == PG_HOME) draw_home(); else for (k = 0; k < 4u; k++) draw_param(k); }
    {   /* the delay's clock sync */
        uint32_t d = patch.dly_sync & 7u;
        if (d) { uint32_t t = beat_ms * 441u / 10u, cur = sync_dly, diff; t = t < 64u ? 64u : t > DLY_LEN - 1u ? DLY_LEN - 1u : t; diff = t > cur ? t - cur : cur - t; if (diff > cur >> 6) sync_dly = t; }
    }
    sounding = voices_sounding();
    for (k = 0; k < NV; k++)
        if (sounding >> k & 1u) { nv++; if (voices[k].gate && voices[k].note >= base && voices[k].note < base + 27u) lit |= 1u << (voices[k].note - base); }   /* held keys lit, not tails */
    for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, oct() < 0); fm1_led_key(BTN_OCT_UP, oct() > 0);
    fm1_led_key(BTN_HOME, page == PG_HOME); fm1_led_key(BTN_EDIT, page == PG_PAD || page == PG_PAD2);
    fm1_led_key(BTN_LFO, page == PG_MOVE); fm1_led_key(BTN_FX, page >= PG_SPACE && page <= PG_PHASER); fm1_led_key(BTN_GLO, page == PG_MIDI);
    fm1_led_key(BTN_PLAY, set.latch);
    {
        static int32_t load_acc, wload_acc;
        int32_t l = (int32_t)(audio_cycles / (HALF_FRAMES * 10u * FM1_TICKS_PER_US / (FS / 1000u) + 1u));
        int32_t w = (int32_t)(audio_worker.last_job_ticks / (HALF_FRAMES * 10u * FM1_TICKS_PER_US / (FS / 1000u) + 1u));
        load_acc += ((l << 6) - load_acc) >> 6; wload_acc += ((w << 6) - wload_acc) >> 6;
        load_now = (uint32_t)(load_acc >> 6); wload_now = (uint32_t)(wload_acc >> 6);
    }
    if (page != page_drawn) { page_drawn = page; draw_page(); status_drawn = ~0u; }
    {
        int32_t pk = out_peak;
        if (pk > meter_hold) meter_hold = pk;
        if (fm1_ms - strip_ms >= 50u) { strip_ms = fm1_ms; draw_monitor(sounding); meter_hold -= meter_hold >> 3; }
    }
    k = (uint32_t)(oct() + 2) | nv << 3 | (load_now / 5u) << 8 | (uint32_t)set.latch << 14;
    if (k != status_drawn) { status_drawn = k; draw_status(oct(), nv); }
    if (out_peak > 512) loud_ms = fm1_ms;              /* (quiet is what matters: a pad's tail can run for half a minute) */
    if (dirty && fm1_ms - t_change > 2000u && fm1_ms - last_midi_ms > 1000u && fm1_ms - loud_ms > 500u) {
        settings_save(&set, sizeof set);
        dirty = 0;
    }
}
