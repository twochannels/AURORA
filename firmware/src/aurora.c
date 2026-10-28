/* SPDX-License-Identifier: GPL-3.0-only */
/* AURORA: an ambient texture instrument for the M-VAVE FM-1, a sound module for a dawless rig. Big, wide sounds
 * that keep moving on their own and bend to MIDI. On the Dinghy platform (platform.c) with FM-106's dual-core
 * worker, as CS-1 was.
 *
 *   the pad      eight voices of twelve sine partials: TILT (how fast the partials fall off), SPREAD (odd against
 *                even partials: hollow to full), BRIGHT (a ceiling the partials fade under), an attack and a
 *                release; BLOOM staggers the partials' entry so a chord opens like a flower; DETUNE spreads each
 *                of the six lowest partials into a pair, so the pad shimmers and a chord's beats are smeared
 *   the swarm    under the pad, five saws a little apart (SPREAD) through a low-pass (CUTOFF, RESO) that tracks
 *                the note and follows TILT, the movement's colour and pressure; each saw wanders by DRIFT
 *   the air      noise shaped: wind against crackle (TEXTURE) through a resonant band-pass a side (TONE, wandering
 *                at RATE, bent by the movement's colour) that breathes with the pad (BREATH); AIR sets how much
 *   movement     four slow sources, WANDER (a random LFO in every voice, its own), WALK (a random walk), CHAOS (a
 *                Lorenz attractor) and TIDE (a slow sine over bars of the MIDI clock, or minutes), through a fixed
 *                sign pattern into four destinations, TILT, COLOUR (spread and brightness), PITCH and SPACE; the
 *                sources' amounts and the destinations' depths are knobs, MOVE scales it all, RATE its speed, and
 *                DRIFT is a random walk on every partial besides
 *   width        odd partials lean left, even right, by WIDTH: the pad is as wide as you like, mono-safe
 *   space        the plate and its shimmer, the tape delay and the phaser from CS-1; FREEZE holds the delay line and
 *                scatters grains over it (GRAIN their length, SPRAY how far back they pick, SHIFT their pitch)
 *   MIDI         notes on the MIDI page's channel; CC 80..87 are the eight macros, in the Circuit Tracks' order:
 *                MOVE SPACE AIR TILT WIDTH DRIFT FREEZE BLOOM; channel pressure or the expression CC swells
 *                (brightness and level); sustain (CC 64); pitch bend; MIDI clock for the delay
 *   keys         play; OCT- OCT+ the octave; PLAY latches (hold a drone with no hands)
 *   pages        HOME (the macros, a weather strip), EDIT: PAD .. AIR; LFO: MOVE .. TARGETS; FX: SPACE, DELAY, PHASER, FREEZE;
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
#define NPAIR 6u                                   /* the lowest partials are detuned pairs (DETUNE) */
#define NSW 5u                                     /* the swarm: detuned saws a voice, through one filter */
#define VBUDGET 13000                              /* a voice's partials sum to at most this: 8 voices, chords and swells clear of the knee */

/* ---- the sound: everything a knob or a macro sets, kept with the settings */
typedef struct {
    uint8_t tilt, spread, bright, attack, release, bloom, detune, pd_level;   /* the pad; DETUNE spreads each low partial into a pair */
    uint8_t sw_level, sw_spread, sw_cut, sw_res;                 /* the swarm: its level, detune, filter cutoff and resonance */
    uint8_t air_tone, air_tex, air_breath;                       /* the air: its colour, wind against crackle, how slowly it breathes */
    uint8_t move, rate, drift, width, air;                       /* movement, width, the air's level (the AIR macro) */
    uint8_t src_wander, src_walk, src_chaos, src_tide;           /* the movement sources' amounts */
    uint8_t to_tilt, to_colour, to_pitch, to_space;              /* the destinations' depths */
    uint8_t swell;                                               /* how much pressure / expression opens the pad */
    uint8_t dly_time, dly_fb, dly_mix, dly_ping, dly_damp, dly_sync, dly_tape;   /* the delay (aurora_fx.h) */
    uint8_t rv_mix, rv_size, rv_damp, rv_shim, rv_shift;         /* the plate and its shimmer */
    uint8_t ph_rate, ph_depth, ph_fb, ph_mix;                    /* the phaser */
    uint8_t freeze, frz_grain, frz_spray, frz_shift;              /* FREEZE: the delay line held, grains over it (aurora_fx.h) */
    uint8_t volume;
} patch_t;
static patch_t patch;
static const patch_t INIT_PATCH = {
    .tilt = 70, .spread = 64, .bright = 90, .attack = 60, .release = 80, .bloom = 40, .detune = 36, .pd_level = 127,
    .sw_level = 40, .sw_spread = 50, .sw_cut = 56, .sw_res = 20,
    .air_tone = 60, .air_tex = 30, .air_breath = 70,
    .move = 64, .rate = 30, .drift = 30, .width = 70, .air = 0, .swell = 64,
    .src_wander = 100, .src_walk = 50, .src_chaos = 40, .src_tide = 80, .to_tilt = 80, .to_colour = 50, .to_pitch = 25, .to_space = 40,
    .dly_time = 90, .dly_fb = 50, .dly_mix = 0, .dly_ping = 1, .dly_damp = 50, .dly_sync = 0, .dly_tape = 40,
    .rv_mix = 60, .rv_size = 100, .rv_damp = 50, .rv_shim = 0, .rv_shift = 12,
    .ph_rate = 20, .ph_depth = 64, .ph_fb = 30, .ph_mix = 0,
    .freeze = 0, .frz_grain = 80, .frz_spray = 70, .frz_shift = 12,
    .volume = 100,
};
static volatile uint32_t sync_dly;                 /* the delay's time from the MIDI clock, frames (the main loop's) */
static int32_t rv_mix_now;                         /* SPACE this block, 0..127: the knob plus the movement */
static int16_t SINE_RAM[1024];                        /* the sine table copied out of the flash: the oscillators read it 12 times a frame */
#include "aurora_fx.h"

/* ---- the voices (the render's, but for the main loop's note, vel, gate, trig, at) */
typedef struct {
    volatile uint8_t note, vel, gate, at;
    volatile uint32_t trig;
    uint32_t seen, age;
    volatile uint8_t active;
    uint32_t ph[NP], ph2[NPAIR];                   /* each partial's phase, and the pairs' second halves */
    uint32_t sph[NSW];                             /* the swarm's phases */
    int32_t sdrift[NSW], sdtarget[NSW];            /* each saw's own pitch wander (random LFOs, DRIFT) */
    int32_t sic[2][2];                             /* the swarm's two filters' states (left-leaning, right-leaning) */
    int32_t samp;                                  /* the swarm's level last block (Q15), ramped a block */
    int32_t amp[NP];                               /* each partial's level now (Q15), ramped a block */
    int32_t walk[NP];                              /* each partial's random walk (Q15 around 0) */
    int32_t wander, wtarget;                       /* the voice's own random LFO (Q15): a slew towards random targets */
    int32_t env;                                   /* the envelope, Q24 */
    uint8_t stage;                                 /* 0 off 1 attack 2 held 3 release */
    uint32_t t;                                    /* blocks since the note began (BLOOM) */
    uint32_t rng;
} voice_t;
static voice_t voices[NV];
static volatile int32_t bend256;                   /* pitch bend, 1/256 semitone */
static volatile uint8_t press;                     /* channel pressure / expression, 0..127 (the main loop's) */
static int32_t g_l[2][HALF_FRAMES], g_r[2][HALF_FRAMES];   /* each core's voices, left and right */
static int32_t osc_acc[2][2][HALF_FRAMES];           /* each core's odd and even partial sums for the voice in hand */
static inline int32_t sine_r(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE_RAM[i], b = SINE_RAM[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static int32_t g_st[2u * HALF_FRAMES];
static int32_t rv_src[2u * HALF_FRAMES], rv_wet[2u * HALF_FRAMES];
static uint32_t audio_cycles;
static int32_t poly_gain = 32767;                 /* the chord compensation, Q15, slewed a block at a time */
static volatile int32_t out_peak;

/* ---- the movement: the global sources once a block (the first core, before the hand-off), then the four
 * destinations' offsets this block. WANDER lives in each voice (voice_block). Sources are Q15 around 0. */
static int32_t mv_fb;                              /* RATE as a frequency a block, Q16: 0.01 Hz (0) .. 2 Hz (127), log */
/* a random LFO: a new random target now and then (at the rate), slewed towards at the rate: smooth at any speed */
static inline void random_lfo(int32_t *v, int32_t *target, uint32_t *rng)
{
    uint32_t r = noise32(rng);
    if ((int32_t)(r & 0xFFFFu) < mv_fb) *target = (int32_t)(r >> 17) - 16384 + ((int32_t)(noise32(rng) >> 17) - 16384);   /* a new target: +-32767, triangular */
    *v += ((*target - *v) * (mv_fb * 6 > 65536 ? 65536 : mv_fb * 6)) >> 16;                              /* 2 pi f a block */
}
static struct {
    int32_t walk[2], wtarget[2];                   /* two random LFOs */
    int32_t x, y, z;                               /* the Lorenz attractor, Q8 */
    uint32_t tide;                                 /* the tide's phase, a turn = 2^32 */
    uint32_t rng;
    int32_t src[7];                                /* walk 0, walk 1, chaos x, y, z, tide sin, tide cos: the view's */
} mv = {{0, 0}, {0, 0}, 256, 256, 5120, 0, 0x2545F491u, {0}};
static int32_t mv_tilt16, mv_spread, mv_pitch256, mv_space;   /* the destinations' offsets this block */
static volatile uint32_t tide_period_ms = 128000;  /* the tide's turn, from RATE and the clock (the main loop's) */
static volatile uint32_t tide_reset;               /* MIDI start: the tide starts over */
/* which source goes where, and which way: TILT, COLOUR, PITCH, SPACE x walk0 walk1 cx cy cz sin cos */
static const int8_t MV_PATTERN[4][7] = {{1, 0, 1, 0, 0, 1, 0}, {0, -1, 0, 1, 0, 0, -1}, {-1, 0, 0, 0, 1, 0, 1}, {0, 1, -1, 0, 0, 1, 0}};
static void movement_block(void)
{
    int32_t move = patch.move * 258, amount[7], depth[4], j, i, dt;
    uint32_t k, e = (uint32_t)patch.rate * 61u;    /* 2^(rate / 16.8): x200 over the knob */
    mv_fb = (int32_t)((((e & 1023u) ? (uint32_t)EXP2N_Q15[(1024u - (e & 1023u)) >> 2] : 16384u) << (3u + (e >> 10))) >> 15);   /* 4 .. 760: f / 172 in Q16 */
    for (k = 0; k < 2u; k++) random_lfo(&mv.walk[k], &mv.wtarget[k], &mv.rng);
    {                                              /* Lorenz, sigma 10, rho 28, beta 8/3, Euler in Q8, its time at the rate */
        int32_t x = mv.x, y = mv.y, z = mv.z;
        dt = 3 + ((mv_fb * 7) >> 3);                                             /* 0.00005 .. 0.01 a block */
        int32_t dx = (10 * (y - x) * dt) >> 16, dy = ((((x * (28 * 256 - z)) >> 8) - y) * dt) >> 16, dz = ((((x * y) >> 8) - (8 * z) / 3) * dt) >> 16;
        mv.x = clamp(x + dx, -16000, 16000); mv.y = clamp(y + dy, -16000, 16000); mv.z = clamp(z + dz, 0, 16000);
    }
    if (tide_reset) { tide_reset = 0; mv.tide = 0; }
    mv.tide += (0xFFFFFFFFu / tide_period_ms / 100u) * (HALF_FRAMES * 100000u / FS);   /* a block's share of the turn */
    mv.src[0] = mv.walk[0]; mv.src[1] = mv.walk[1];
    mv.src[2] = clamp((mv.x * 1638) >> 8, -32767, 32767);                     /* x / 20 */
    mv.src[3] = clamp((mv.y * 1638) >> 8, -32767, 32767);
    mv.src[4] = clamp(((mv.z - 6400) * 1638) >> 8, -32767, 32767);            /* (z - 25) / 20 */
    mv.src[5] = sine_i(mv.tide); mv.src[6] = sine_i(mv.tide + 0x40000000u);
    amount[0] = amount[1] = patch.src_walk * 258; amount[2] = amount[3] = amount[4] = patch.src_chaos * 258; amount[5] = amount[6] = patch.src_tide * 258;
    depth[0] = patch.to_tilt * 258; depth[1] = patch.to_colour * 258; depth[2] = patch.to_pitch * 258; depth[3] = patch.to_space * 258;
    for (j = 0; j < 4; j++) {
        int32_t g = 0, m;
        for (i = 0; i < 7; i++) if (MV_PATTERN[j][i]) g += MV_PATTERN[j][i] * mulq15(mv.src[i], amount[i]);
        m = mulq15(mulq15(clamp(g, -32767, 32767), move), depth[j]);
        if (j == 0) mv_tilt16 = (m * 1200) >> 15;                           /* +-75 of TILT */
        else if (j == 1) mv_spread = (m * 64) >> 15;                         /* +-64 of SPREAD and BRIGHT */
        else if (j == 2) mv_pitch256 = (m * 96) >> 15;                       /* +-37 cents */
        else mv_space = m;                                                   /* of SPACE: see below */
    }
    {   /* SPACE moves by up to the distance to the knob's nearer end: 0 stays off and 127 stays full, 64 swings +-64 */
        int32_t r = patch.rv_mix < 64 ? patch.rv_mix : 127 - patch.rv_mix;
        rv_mix_now = clamp((int32_t)patch.rv_mix + ((mv_space * r) >> 15), 0, 127);
    }
}

/* ---- one voice, one block */
static void voice_block(voice_t *w, int32_t *outl, int32_t *outr, uint32_t core)
{
    uint32_t k, i, trig = w->trig;
    int32_t target[NP], from[NP], env_q15, pr = press * 258, sum = 0;
    int32_t tilt16 = (int32_t)patch.tilt * 16 + mv_tilt16, spread = (int32_t)patch.spread - 64 + mv_spread, bright = (int32_t)patch.bright + mv_spread;
    int32_t pitch256 = (int32_t)w->note * 256 + bend256 + mv_pitch256;
    int32_t swell = mulq15(pr, (int32_t)patch.swell * 258);        /* 0..32767: pressure opening the pad */
    int32_t wl, wr, width = (int32_t)patch.width * 258;
    if (trig != w->seen) {                         /* a new note */
        w->seen = trig;
        w->stage = 1;
        w->t = 0;
        if (!w->rng) w->rng = 0x9E3779B9u * (uint32_t)(w - voices + 1);
        if (!w->env) {                             /* from silence: Schroeder phases, so the harmonics never all peak together */
            for (k = 0; k < NP; k++) w->ph[k] = (uint32_t)(k * (k + 1u)) * 178956970u;   /* pi k (k + 1) / NP */
            for (k = 0; k < NPAIR; k++) w->ph2[k] = w->ph[k];
            for (k = 0; k < NSW; k++) w->sph[k] = k * 858993459u + (k * k) * 300000000u;   /* the saws spread round the cycle */
            w->sic[0][0] = w->sic[0][1] = w->sic[1][0] = w->sic[1][1] = 0;
        }
    }
    if (!w->gate && w->stage && w->stage < 3) w->stage = 3;
    /* the envelope, a block at a time: linear attack, exponential release */
    if (w->stage == 1) {
        w->env += (1 << 24) / (int32_t)(4u + ((uint32_t)patch.attack * patch.attack >> 4));   /* 4 .. 1000 blocks */
        if (w->env >= (1 << 24)) { w->env = 1 << 24; w->stage = 2; }
    } else if (w->stage == 3) {                    /* exponential: 60 dB in 0.1 s (0) .. ~30 s (127), log-spaced */
        uint32_t sh = (uint32_t)patch.release * 9u / 127u, fr = (uint32_t)patch.release * 9u % 127u;
        int32_t k = (22000 >> sh) - (int32_t)(((22000u >> sh) - (22000u >> (sh + 1u))) * fr / 127u);
        w->env -= ((w->env >> 9) * k) >> 6;         /* (not mulq15 << 9: that rounds to 0 below 2^15 / k and the tail would hang there for ever) */
        if (w->env < 2048) { w->env = 0; w->stage = 0; }
    }
    w->active = w->stage != 0;
    if (!w->active) return;
    w->t++;
    /* the voice's own random LFO (WANDER, at RATE) and the partials' random walks (DRIFT) */
    random_lfo(&w->wander, &w->wtarget, &w->rng);
    {
        for (k = 0; k < NP; k++) {
            int32_t d = (int32_t)(noise32(&w->rng) >> 22) - 512;
            w->walk[k] += (d * (int32_t)patch.drift) >> 5;
            w->walk[k] = clamp(w->walk[k], -32767, 32767);
            w->walk[k] -= w->walk[k] >> 8;
        }
    }
    {                                              /* WANDER: this voice's own, into the same four destinations */
        int32_t wv = mulq15(mulq15(w->wander, patch.src_wander * 258), patch.move * 258), c;
        tilt16 += (mulq15(wv, patch.to_tilt * 258) * 1200) >> 15;
        c = (mulq15(wv, patch.to_colour * 258) * 64) >> 15;
        spread -= c; bright -= c;                                                /* (against the global colour) */
        pitch256 += (mulq15(wv, patch.to_pitch * 258) * 96) >> 15;
    }
    tilt16 = clamp(tilt16, 0, 2047); spread = clamp(spread, -64, 63); bright = clamp(bright, 0, 127);
    env_q15 = w->env >> 9;
    /* each partial's level: 1 / (k + 1) ^ tilt, the odd / even balance, the brightness ceiling, the walk, the bloom */
    for (k = 0; k < NP; k++) {
        int32_t lv = 32767, n = (int32_t)k, j;
        for (j = 0; j < n; j++) lv -= (lv * tilt16) >> 11;                   /* geometric fall: tilt 127 = x0.0 a step */
        if (k & 1u) lv = mulq15(lv, 32767 - spread * 256); else lv = mulq15(lv, 32767 + spread * 256);   /* SPREAD: odd (k even) against even */
        if ((int32_t)k * 10 > bright) lv = mulq15(lv, (int32_t)clamp(32767 - ((int32_t)k * 10 - bright) * 400, 0, 32767));   /* BRIGHT: a ceiling */
        lv = mulq15(lv, 32767 + (mulq15(w->walk[k], 16000)));               /* the walk: +-half */
        if (k && swell) lv += mulq15(32767 - lv, mulq15(swell, (int32_t)k * 2700));   /* pressure opens the upper partials */
        if (patch.bloom && w->t * 4u < (uint32_t)patch.bloom * k) lv = 0;   /* BLOOM: partial k enters bloom x k / 4 blocks in */
        target[k] = mulq15(mulq15(lv, env_q15), (int32_t)patch.pd_level * 258) / (int32_t)(2 + (k >> 1));   /* the spectral shape */
        sum += target[k];
    }
    {                                              /* the sum of the partials is the voice's worst-case peak: capped, so */
        int32_t budget = VBUDGET + mulq15(swell, VBUDGET / 2);               /* colour is not loudness; pressure is, by half */
        if (sum > budget) for (k = 0; k < NP; k++) target[k] = target[k] * budget / sum;   /* (< 2^31) */
    }
    for (k = 0; k < NP; k++) { from[k] = w->amp[k]; w->amp[k] = target[k]; }
    wl = 24576 + mulq15(width, 8191); wr = wl - mulq15(width, 28000);        /* odd partials: left and a little right (mono: 0.75, so it is no louder) */
    {   /* the oscillators: a partial at a time over the block (the increment once a block, the level a running sum,
         * the sine from RAM), into the core's odd and even sums; a partial silent at both ends only keeps time.
         * The lowest NPAIR partials are pairs a little above and below the pitch (DETUNE: up to +-8 cents), each
         * half the level: every partial shimmers at its own slow rate, and the beats between a chord's notes
         * (a tempered fifth's 785 Hz, once a second) are smeared into it rather than pulsing bare */
        int32_t *odd = osc_acc[core][0], *even = osc_acc[core][1];
        uint32_t inc0 = pitch_inc256(pitch256), dq = ((uint32_t)patch.detune * 611u) >> 8;   /* the spread, 1/65536 of the pitch */
        for (i = 0; i < HALF_FRAMES; i++) odd[i] = even[i] = 0;
        for (k = 0; k < NP; k++) {
            int32_t *o = (k & 1u) ? even : odd, a = from[k] << 8, d = target[k] - from[k];
            uint32_t ph = w->ph[k], inc = inc0 * (k + 1u);
            if ((!from[k] && !target[k]) || inc >= 0x80000000u) {           /* silent, or past Nyquist: it would fold back */
                w->ph[k] = ph + inc * HALF_FRAMES;
                if (k < NPAIR) w->ph2[k] = w->ph2[k] + inc * HALF_FRAMES;
                continue;
            }
            if (k < NPAIR && dq) {
                uint32_t ph2 = w->ph2[k], inca = inc + (inc >> 16) * dq, incb = inc - (inc >> 16) * dq;
                a = ((a >> 8) * 23170) >> 7; d = (d * 23170) >> 15;          /* each half at 0.707: the pair keeps the partial's power */
                for (i = 0; i < HALF_FRAMES; i++) {
                    o[i] += mulq15(sine_r(ph), a >> 8) + mulq15(sine_r(ph2), a >> 8);
                    a += d;
                    ph += inca; ph2 += incb;
                }
                w->ph2[k] = ph2;
            } else {
                for (i = 0; i < HALF_FRAMES; i++) {
                    o[i] += mulq15(sine_r(ph), a >> 8);
                    a += d;
                    ph += inc;
                }
            }
            w->ph[k] = ph;
        }
        /* the swarm: NSW saws a little apart (SPREAD, up to +-50 cents, each wandering by DRIFT), through a low-pass
         * that tracks the note, opened by CUTOFF, darkened by TILT, bent by the movement's colour and pressure;
         * the saws alternate between the left-leaning and right-leaning sums, each with its own filter */
        if (patch.sw_level) {
            int32_t lvl = mulq15(env_q15, patch.sw_level * 258), a = w->samp << 8, d = lvl - w->samp;
            int32_t cut16 = (int32_t)w->note * 16 + ((int32_t)patch.sw_cut - 32) * 16 - ((int32_t)patch.tilt - 64) * 8 + mv_spread * 4 + mulq15(swell, 48 * 16);
            svf_t c;
            uint32_t sinc[NSW];
            int32_t *sl = osc_acc[core][0], *sr = osc_acc[core][1];
            svf_coef(&c, cut16, SVF_K(patch.sw_res));
            for (k = 0; k < NSW; k++) {
                random_lfo(&w->sdrift[k], &w->sdtarget[k], &w->rng);
                sinc[k] = pitch_inc256(pitch256 + ((int32_t)k - 2) * (int32_t)patch.sw_spread / 2 + ((mulq15(w->sdrift[k], patch.drift * 258) * 16) >> 15));
            }
            w->samp = lvl;
            for (i = 0; i < HALF_FRAMES; i++) {
                int32_t l = 0, r = 0, lp, hp, g = (a >> 8) * 4000 >> 15;      /* each saw at up to 4000: five, filtered, about as loud as the pad */
                for (k = 0; k < NSW; k++) {
                    int32_t v = mulq15(osc_saw(w->sph[k], sinc[k]), g);
                    if (k & 1u) r += v; else l += v;
                    w->sph[k] += sinc[k];
                }
                svf_step(&c, l, &w->sic[0][0], &w->sic[0][1], &lp, &hp); sl[i] += lp;
                svf_step(&c, r, &w->sic[1][0], &w->sic[1][1], &lp, &hp); sr[i] += lp;
                a += d;
            }
        } else w->samp = 0;
        for (i = 0; i < HALF_FRAMES; i++) {
            outl[i] += mulq15(odd[i], wl) + mulq15(even[i], wr);                /* ... even partials: right and a little left */
            outr[i] += mulq15(even[i], wl) + mulq15(odd[i], wr);
        }
    }
}
static void voices_render(uint32_t first, uint32_t step, int32_t *l, int32_t *r)
{
    uint32_t v, i;
    for (i = 0; i < HALF_FRAMES; i++) l[i] = r[i] = 0;
    for (v = first; v < NV; v += step) voice_block(&voices[v], l, r, first);
}
static void worker_render(void *ctx) { (void)ctx; voices_render(1, 2, g_l[1], g_r[1]); fx_reverb(rv_src, rv_wet, HALF_FRAMES); }
/* ---- the air: noise, shaped. Wind (white noise) against crackle (bursts of random clicks), through a resonant
 * band-pass a side, its centre set by TONE, wandering at RATE and bent by the movement's colour; it breathes
 * with the pad (a slow follower of the loudest envelope, BREATH its pace) and AIR sets how much. Global, on the
 * first core, into the mix before the space. */
static struct {
    uint32_t rng[2];
    int32_t crk[2];                                /* the crackle bursts, decaying */
    int32_t wander, wtarget;                       /* the centre's random LFO */
    int32_t env;                                   /* follows the pad, Q15 */
    uint32_t bph;                                  /* the breath's phase */
    int32_t ic[2][2];
    svf_t c[2];
} air = {{0x1D872B41u, 0x7A3F9C25u}, {0, 0}, 0, 0, 0, 0x99999999u, {{0, 0}, {0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}};
static void air_block(int32_t *st, uint32_t n)
{
    int32_t level = ((int32_t)patch.air * patch.air * patch.air) >> 6, target = 0, i, side;   /* cubic (127^3 / 64 = 32005): a soft undertone over most of the knob, loud at the top */
    uint32_t v, p_crk, wind, crackle;
    if (!level && !air.env) return;
    for (v = 0; v < NV; v++) if ((voices[v].env >> 9) > target) target = voices[v].env >> 9;
    air.env += ((target - air.env) * (target > air.env ? 630 : 47)) >> 15;   /* with the pad: in over ~0.3 s, lingering ~4 s after it */
    random_lfo(&air.wander, &air.wtarget, &air.rng[0]);
    if (patch.air_breath) {                        /* the breath: in and out on its own, one every 20 s at 1 to one every 2 s at 127 (log),
                                                    * the in-breath 60 % of it, the pace nudged +-25 % by the wander: no two alike */
        uint32_t inc = 38u * exp2_q15((uint32_t)patch.air_breath * 27u), ph;
        int32_t breath;
        inc += (uint32_t)(((int32_t)(inc >> 8) * (air.wander >> 7)) >> 1);
        air.bph += inc;
        ph = air.bph < 0x99999999u ? (air.bph >> 8) * 213u : 0x80000000u + ((air.bph - 0x99999999u) >> 8) * 320u;   /* 0..60 % -> the rise, the rest the fall */
        breath = (32767 - sine_r(ph + 0x40000000u)) >> 1;                      /* 0 at the trough, 1 at the peak */
        target = 8192 + mulq15(breath, 24575);                                   /* -12 dB at the trough, full at the peak */
        level = mulq15(level, target);
    } else air.bph = 0x99999999u;                  /* held: ready to leave from the peak */
    for (side = 0; side < 2; side++) {             /* two filters, a little apart: the air is wide */
        int32_t cut16 = (40 + (int32_t)patch.air_tone * 80 / 127) * 16 + ((mulq15(air.wander, 12 * 16)) ) + mv_spread * 4 + (side ? 3 * 16 : -3 * 16);
        svf_coef(&air.c[side], cut16, SVF_K(70));
    }
    wind = (uint32_t)(127 - patch.air_tex) * 258u; crackle = (uint32_t)patch.air_tex * 258u;
    p_crk = 1u + (uint32_t)patch.air_tex * 40u;    /* clicks a second x 65536 / FS: ~1 .. 120 a second */
    level = mulq15(level, air.env);
    for (i = 0; i < (int32_t)n; i++) {
        for (side = 0; side < 2; side++) {
            uint32_t r = noise32(&air.rng[side]);
            int32_t x, bp, lp, hp;
            if ((r >> 8 & 0xFFFFu) < p_crk * 65536u / FS + 1u) air.crk[side] = (int32_t)(r >> 17) - 16384;   /* a click lands */
            air.crk[side] -= air.crk[side] >> 4;                                                            /* and rings out in ~1 ms */
            x = mulq15((int32_t)(r >> 17) - 16384, (int32_t)wind) * 2 + mulq15(air.crk[side] + (air.crk[side] >> 2), (int32_t)crackle);   /* wind at -24 dBFS rms, crackle at -22, at full */
            svf_step(&air.c[side], x, &air.ic[side][0], &air.ic[side][1], &lp, &hp);
            bp = x - lp - hp;                                                                                /* the band-pass: in - lp - hp (k scaled) */
            st[2u * (uint32_t)i + (uint32_t)side] += mulq15(bp, level);
        }
    }
}
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, t0 = fm1_ticks();
    int32_t vol = patch.volume * 258;
    int submitted;
    (void)n;
    movement_block();
    submitted = audio_worker_submit(worker_render, 0);
    voices_render(0, submitted ? 2u : 1u, g_l[0], g_r[0]);
    if (submitted) audio_worker_join();
    {   /* the chord: n voices (by their envelopes) at 1 / (1 + (n - 1) / 6): one note full, four at -3.5 dB, eight
         * at -6.7, so a bright chord in mono stays under the knee; slewed over ~0.4 s so a note joining is no dip */
        int32_t n = 0, target;
        for (i = 0; i < NV; i++) n += voices[i].env >> 9;
        target = n > 32767 ? 32767 * 32768 / (32768 + (n - 32767) / 6) : 32767;
        poly_gain += (target - poly_gain) >> 6;
        vol = mulq15(vol, poly_gain);
    }
    for (i = 0; i < HALF_FRAMES; i++) {
        int32_t l = g_l[0][i] + (submitted ? g_l[1][i] : 0), r = g_r[0][i] + (submitted ? g_r[1][i] : 0);
        g_st[2u * i] = mulq15(soft_knee(l, 20000), vol);
        g_st[2u * i + 1u] = mulq15(soft_knee(r, 20000), vol);
    }
    air_block(g_st, HALF_FRAMES);
    fx_delay(g_st, HALF_FRAMES);
    fx_phaser(g_st, HALF_FRAMES);
    {                                              /* SPACE: the wet on top of the dry, the dry down to half at full */
        int32_t dry = 32767 - rv_mix_now * 129;
        if (submitted) {
            for (i = 0; i < 2u * HALF_FRAMES; i++) { rv_src[i] = g_st[i]; g_st[i] = mulq15(g_st[i], dry) + rv_wet[i]; }
        } else {
            fx_reverb(g_st, rv_wet, HALF_FRAMES);
            for (i = 0; i < 2u * HALF_FRAMES; i++) g_st[i] = mulq15(g_st[i], dry) + rv_wet[i];
        }
    }
    {
        int32_t pk = 0;
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t o = soft_knee_full(g_st[i], 20000), a = o < 0 ? -o : o;
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
#define SET_MAGIC 0xA6u

/* ---- the pages */
enum { K_NUM = 0, K_ONOFF, K_MCHAN, K_EXPR, K_DIV, K_SEMI };
enum { CW = 0, CG, CR, CK, CY, CS };                     /* a cap: white, green, red, black, yellow, silver */
typedef struct { const char *name; uint8_t off, max, kind, global, color; const char *hi, *lo; } pdesc_t;
#define PA(f, name, max, kind, c, hi, lo) {name, (uint8_t)offsetof(patch_t, f), max, kind, 0, c, hi, lo}
#define ST(f, name, max, kind, c, hi, lo) {name, (uint8_t)offsetof(settings_t, f), max, kind, 1, c, hi, lo}
#define NONE {0, 0, 0, 0, 0, 0, 0, 0}
typedef struct { const char *title; pdesc_t p[4]; } page_t;
enum { PG_HOME = 0, PG_PAD, PG_PAD2, PG_PAD3, PG_SWARM, PG_AIR, PG_MOVE, PG_SRC, PG_DST, PG_SPACE, PG_DELAY, PG_PHASER, PG_FREEZE, PG_MIDI, NPAGE };
static const page_t PAGES[NPAGE] = {
    {"AURORA", {NONE, NONE, NONE, NONE}},
    {"PAD",    {PA(tilt, "TILT", 127, K_NUM, CW, "DARK", "FULL"), PA(spread, "SPREAD", 127, K_NUM, CW, "EVEN", "ODD"),
                PA(bright, "BRIGHT", 127, K_NUM, CG, "OPEN", "LOW"), PA(bloom, "BLOOM", 127, K_NUM, CY, "SLOW", "AT ONCE")}},
    {"PAD 2",  {PA(attack, "ATTACK", 127, K_NUM, CK, "LONG", "SHORT"), PA(release, "RELEASE", 127, K_NUM, CK, "LONG", "SHORT"),
                PA(swell, "SWELL", 127, K_NUM, CG, "10", "0"), PA(volume, "VOLUME", 127, K_NUM, CK, "10", "0")}},
    {"PAD 3",  {PA(detune, "DETUNE", 127, K_NUM, CW, "WIDE", "PURE"), PA(pd_level, "LEVEL", 127, K_NUM, CW, "10", "0"), NONE, NONE}},
    {"SWARM",  {PA(sw_level, "LEVEL", 127, K_NUM, CW, "10", "0"), PA(sw_spread, "SPREAD", 127, K_NUM, CW, "WIDE", "TIGHT"),
                PA(sw_cut, "CUTOFF", 127, K_NUM, CG, "OPEN", "DARK"), PA(sw_res, "RESO", 127, K_NUM, CR, "10", "0")}},
    {"AIR",    {PA(air, "AIR", 127, K_NUM, CW, "10", "0"), PA(air_tone, "TONE", 127, K_NUM, CG, "BRIGHT", "DARK"),
                PA(air_tex, "TEXTUR", 127, K_NUM, CY, "CRACKL", "WIND"), PA(air_breath, "BREATH", 127, K_NUM, CS, "FAST", "HELD")}},
    {"MOVE",   {PA(move, "MOVE", 127, K_NUM, CW, "10", "0"), PA(rate, "RATE", 127, K_NUM, CW, "FAST", "SLOW"),
                PA(drift, "DRIFT", 127, K_NUM, CW, "10", "0"), PA(width, "WIDTH", 127, K_NUM, CS, "WIDE", "MONO")}},
    {"SOURCES", {PA(src_wander, "WANDER", 127, K_NUM, CW, "10", "0"), PA(src_walk, "WALK", 127, K_NUM, CW, "10", "0"),
                PA(src_chaos, "CHAOS", 127, K_NUM, CY, "10", "0"), PA(src_tide, "TIDE", 127, K_NUM, CS, "10", "0")}},
    {"TARGETS", {PA(to_tilt, "TILT", 127, K_NUM, CW, "10", "0"), PA(to_colour, "COLOUR", 127, K_NUM, CW, "10", "0"),
                PA(to_pitch, "PITCH", 127, K_NUM, CY, "10", "0"), PA(to_space, "SPACE", 127, K_NUM, CS, "10", "0")}},
    {"SPACE",  {PA(rv_mix, "MIX", 127, K_NUM, CS, "10", "0"), PA(rv_size, "SIZE", 127, K_NUM, CW, "LONG", "SHORT"),
                PA(rv_damp, "DAMP", 127, K_NUM, CG, "10", "0"), PA(rv_shim, "SHIMMR", 127, K_NUM, CY, "10", "0")}},
    {"DELAY",  {PA(dly_time, "TIME", 127, K_NUM, CW, "LONG", "SHORT"), PA(dly_fb, "FEEDBK", 127, K_NUM, CW, "10", "0"),
                PA(dly_mix, "MIX", 127, K_NUM, CS, "10", "0"), PA(dly_tape, "TAPE", 127, K_NUM, CY, "10", "0")}},
    {"PHASER", {PA(ph_rate, "RATE", 127, K_NUM, CW, "FAST", "SLOW"), PA(ph_depth, "DEPTH", 127, K_NUM, CG, "10", "0"),
                PA(ph_fb, "FEEDBK", 127, K_NUM, CR, "10", "0"), PA(ph_mix, "MIX", 127, K_NUM, CS, "10", "0")}},
    {"FREEZE", {PA(freeze, "FREEZE", 127, K_NUM, CS, "10", "0"), PA(frz_grain, "GRAIN", 127, K_NUM, CW, "LONG", "SHORT"),
                PA(frz_spray, "SPRAY", 127, K_NUM, CY, "WIDE", "TIGHT"), PA(frz_shift, "SHIFT", 24, K_SEMI, CG, "+12", "-12")}},
    {"MIDI",   {ST(midi_chan, "CHAN", 16, K_MCHAN, CW, "16", "OMNI"), ST(expr, "EXPR", 127, K_EXPR, CW, "CC", "AT"), NONE, NONE}},
};
static const uint8_t PAGE_GROUP[NPAGE][2] = {{PG_HOME, 1}, {PG_PAD, 5}, {PG_PAD, 5}, {PG_PAD, 5}, {PG_PAD, 5}, {PG_PAD, 5}, {PG_MOVE, 3}, {PG_MOVE, 3}, {PG_MOVE, 3},
                                             {PG_SPACE, 4}, {PG_SPACE, 4}, {PG_SPACE, 4}, {PG_SPACE, 4}, {PG_MIDI, 1}};
static const char *const N_ONOFF[2] = {"OFF", "ON"};
/* the macros, CC 80..87 in the Circuit Tracks' order, each a patch field */
static const struct { const char *name; uint8_t off; } MACRO[8] = {
    {"MOVE", offsetof(patch_t, move)}, {"SPACE", offsetof(patch_t, rv_mix)}, {"AIR", offsetof(patch_t, air)}, {"TILT", offsetof(patch_t, tilt)},
    {"WIDTH", offsetof(patch_t, width)}, {"DRIFT", offsetof(patch_t, drift)}, {"FREEZE", offsetof(patch_t, freeze)}, {"BLOOM", offsetof(patch_t, bloom)}};

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
    case K_SEMI: n -= 12; if (n < 0) { s[i++] = '-'; n = -n; } else if (n > 0) s[i++] = '+'; break;
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
/* the strip at 166..196: the output level, the pressure, the voices; on the MOVE pages the four sources, live */
static int32_t meter_hold;
static void draw_sources(void)
{
    static const char *const NAMES[4] = {"WANDER", "WALK", "CHAOS", "TIDE"};
    int32_t v[4], k;
    uint32_t n = 0;
    v[0] = 0;
    for (k = 0; k < (int32_t)NV; k++) if (voices[k].active) { v[0] += voices[k].wander; n++; }   /* the voices' mean */
    v[0] = n ? v[0] / (int32_t)n : 0;
    v[1] = mv.src[0]; v[2] = mv.src[2]; v[3] = mv.src[5];
    cv_begin(240, 28, C_PANEL);
    for (k = 0; k < 4; k++) {
        int32_t x = k * 60, w = (v[k] * 27) >> 15;
        if (page != PG_SRC) cv_text(x + 30 - (text_w(&FONT_S, NAMES[k]) >> 1), 0, &FONT_S, NAMES[k], C_LEG);   /* (under its knob on SOURCES) */
        cv_rect(x + 2, 16, 56, 9, C_SLOT);
        cv_rect(x + 30, 17, 1, 7, C_RULE);
        if (w > 0) cv_rect(x + 30, 17, w, 7, k & 1 ? C_ACC2 : C_ACC); else if (w < 0) cv_rect(x + 30 + w, 17, -w, 7, k & 1 ? C_ACC2 : C_ACC);
    }
    cv_blit(0, 166);
}
static void draw_monitor(uint32_t sounding)
{
    uint32_t v;
    int32_t w;
    if (page >= PG_MOVE && page <= PG_DST) { draw_sources(); return; }
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
    for (uint32_t k = 0; k < 1024u; k++) SINE_RAM[k] = SINE[k];
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
    if (b & (1u << BTN_EDIT)) page = page >= PG_PAD && page < PG_AIR ? page + 1u : PG_PAD;
    if (b & (1u << BTN_LFO)) page = page >= PG_MOVE && page < PG_DST ? page + 1u : PG_MOVE;
    if (b & (1u << BTN_FX)) page = page >= PG_SPACE && page < PG_FREEZE ? page + 1u : PG_SPACE;
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
        if (st == 0xFAu) { tide_reset = 1; continue; }
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
    {   /* the tide's turn: 64 bars at RATE 0 to 4 at 127 (log), of the MIDI clock's tempo, or of 120 bpm without one */
        uint32_t f = (uint32_t)patch.rate * 8u, beat = fm1_ms - clk_last < 2000u ? beat_ms : 500u;
        uint32_t bars_q15 = (64u * EXP2N_Q15[f & 255u]) >> (f >> 8);        /* 64 x 2^(-rate / 32), Q15 */
        tide_period_ms = (4u * beat * bars_q15) >> 15;
    }
    {   /* the delay's clock sync */
        uint32_t d = patch.dly_sync & 7u;
        if (d) { uint32_t t = beat_ms * 441u / 10u, cur = sync_dly, diff; t = t < 64u ? 64u : t > DLY_LEN - 1u ? DLY_LEN - 1u : t; diff = t > cur ? t - cur : cur - t; if (diff > cur >> 6) sync_dly = t; }
    }
    sounding = voices_sounding();
    for (k = 0; k < NV; k++)
        if (sounding >> k & 1u) { nv++; if (voices[k].gate && voices[k].note >= base && voices[k].note < base + 27u) lit |= 1u << (voices[k].note - base); }   /* held keys lit, not tails */
    for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, oct() < 0); fm1_led_key(BTN_OCT_UP, oct() > 0);
    fm1_led_key(BTN_HOME, page == PG_HOME); fm1_led_key(BTN_EDIT, page >= PG_PAD && page <= PG_AIR);
    fm1_led_key(BTN_LFO, page >= PG_MOVE && page <= PG_DST); fm1_led_key(BTN_FX, page >= PG_SPACE && page <= PG_FREEZE); fm1_led_key(BTN_GLO, page == PG_MIDI);
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
