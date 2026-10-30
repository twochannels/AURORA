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
 *   space        the plate and its shimmer, the tape delay from CS-1; FREEZE holds the delay line and
 *                scatters grains over it (GRAIN their length, SPRAY how far back they pick, SHIFT their pitch)
 *   MIDI         notes on the MIDI page's channel; CC 80..87 are the eight macros, in the Circuit Tracks' order:
 *                MOVE SPACE AIR TILT WIDTH DRIFT FREEZE BLOOM; channel pressure or the expression CC swells
 *                (brightness and level), poly aftertouch a note; sustain (CC 64); pitch bend; MIDI clock: with one in, the
 *                delay's TIME is a note division of the beat (SYNC), and the tide counts its bars
 *   keys         play, with their own velocity and pressure (Velo's sensing on the analog rows: VELO, SWELL; KEYS on the MIDI
 *                page turns it off); OCT- OCT+ the octave; PLAY latches (hold a drone with no hands: a latched key pressed again lets go,
 *                the octave buttons move you to add notes elsewhere, the lights show what is latched here); REC held is FREEZE at full;
 *                SEQ is the autopilot (nine controls wander round where they are set); ARP morphs to scene B over
 *                four seconds and back (held, PRESETS picks B), SEL cuts; SAVE, PRESETS, SAVE keeps the sound in a user slot;
 *                SELECT is the pressure
 *                (the swell, with no aftertouch to press), ALGORITHM on HOME a bend that springs back
 *   pages        HOME (the aurora: the sound drawn; FILTER TILT AIR METAL under the knobs), EDIT: TONE, TONE 2, METAL, SWARM, AIR; ENV: SHAPE TOUCH;
 *                LFO: MOVE SOURCES TARGETS; FX: SPACE DELAY FREEZE; GLO: MIDI. Every page is a graphic the four knobs
 *                shift, each part in the colour of its knob. The panel is kept 2 s after the last change.
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
    uint8_t tilt, spread, bright, attack, decay, sustain, release, bloom, detune, velo;   /* the pad: its tone, its envelope, VELO how much velocity counts */   /* the pad; DETUNE spreads each low partial into a pair */
    uint8_t sw_level, sw_spread, sw_cut, sw_res;                 /* the swarm: its level, detune, filter cutoff and resonance */
    uint8_t air_tone, air_tex, air_breath;                       /* the air: its colour, wind against crackle, how slowly it breathes */
    uint8_t move, rate, drift, width, air;                       /* movement, width, the air's level (the AIR macro) */
    uint8_t src_wander, src_walk, src_chaos, src_tide;           /* the movement sources' amounts */
    uint8_t to_tilt, to_colour, to_pitch, to_space;              /* the destinations' depths */
    uint8_t swell;                                               /* how much pressure / expression opens the pad */
    uint8_t dly_time, dly_fb, dly_mix, dly_ping, dly_damp, dly_sync, dly_tape;   /* the delay (aurora_fx.h) */
    uint8_t rv_mix, rv_size, rv_damp, rv_shim, rv_shift;         /* the plate and its shimmer */
    uint8_t freeze, frz_grain, frz_spray, frz_shift;              /* FREEZE: the delay line held, grains over it (aurora_fx.h) */
    uint8_t volume;
    uint8_t stretch, sub, tune, sat, filter, glide;               /* TONE 2: the partials stretched apart, a sub-octave, a transpose (+-12), a drive; the master filter (HP .. LP); glide */
    uint8_t ring, ratio;                                         /* METAL: the pad ring-modulated by a sine at RATIO x the pitch (0.5 .. 8, 1 at 32), RING how much */
} patch_t;
static patch_t patch;
/* the fields' names, in the struct's order: the emulator's "Copy sound" and any dump name them by these */
static const char PATCH_FIELDS[] = "tilt,spread,bright,attack,decay,sustain,release,bloom,detune,velo,sw_level,sw_spread,sw_cut,sw_res,"
    "air_tone,air_tex,air_breath,move,rate,drift,width,air,src_wander,src_walk,src_chaos,src_tide,to_tilt,to_colour,to_pitch,to_space,"
    "swell,dly_time,dly_fb,dly_mix,dly_ping,dly_damp,dly_sync,dly_tape,rv_mix,rv_size,rv_damp,rv_shim,rv_shift,freeze,frz_grain,frz_spray,frz_shift,volume,"
    "stretch,sub,tune,sat,filter,glide,ring,ratio";
typedef char patch_fields_count_check[sizeof(patch_t) == 56u ? 1 : -1];   /* a field added: add its name above, and bump this */
static const patch_t BASE_PATCH = {      /* the presets build on this; the boot sound is preset 0, AURORA, Ben's */
    .tilt = 70, .spread = 64, .bright = 90, .attack = 60, .decay = 60, .sustain = 127, .release = 80, .bloom = 40, .detune = 36, .velo = 40,
    .sw_level = 40, .sw_spread = 50, .sw_cut = 56, .sw_res = 20,
    .air_tone = 60, .air_tex = 30, .air_breath = 70,
    .move = 64, .rate = 30, .drift = 30, .width = 70, .air = 0, .swell = 64,
    .src_wander = 100, .src_walk = 50, .src_chaos = 40, .src_tide = 80, .to_tilt = 80, .to_colour = 50, .to_pitch = 25, .to_space = 40,
    .dly_time = 90, .dly_fb = 50, .dly_mix = 0, .dly_ping = 1, .dly_damp = 50, .dly_sync = 0, .dly_tape = 40,
    .rv_mix = 60, .rv_size = 100, .rv_damp = 50, .rv_shim = 0, .rv_shift = 12,
    .freeze = 0, .frz_grain = 80, .frz_spray = 70, .frz_shift = 12,
    .volume = 100,
    .stretch = 0, .sub = 0, .tune = 12, .sat = 0, .filter = 64, .glide = 0,
    .ring = 0, .ratio = 32,
};
static volatile uint32_t sync_dly;                 /* the delay's time from the MIDI clock, frames (the main loop's) */
static volatile uint8_t dly_synced;                /* a clock is in: the delay runs at sync_dly, a note division of the beat (TIME picks it) */
#define NDIV 12u                                   /* TIME while synced: a division of the beat, 1/32 to 1/2 */
static const uint8_t DLY_DIV[NDIV][2] = {{1, 8}, {1, 6}, {1, 4}, {1, 3}, {3, 8}, {1, 2}, {2, 3}, {3, 4}, {1, 1}, {4, 3}, {3, 2}, {2, 1}};
static const char *const DIV_NAME[NDIV] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"};
static uint32_t clk_last;                          /* when the last clock tick came (below) */
static int32_t rv_mix_now;                         /* SPACE this block, 0..127: the knob plus the movement */
static int16_t SINE_RAM[1024];                        /* the sine table copied out of the flash: the oscillators read it 12 times a frame */
static volatile uint8_t freeze_now;                   /* FREEZE as it applies: the knob, or 127 while REC is held */
#include "aurora_fx.h"

/* ---- the voices (the render's, but for the main loop's note, vel, gate, trig, at) */
typedef struct {
    volatile uint8_t note, vel, gate, at;
    volatile uint32_t trig;
    uint32_t seen, age;
    volatile uint8_t active;
    uint32_t ph[NP], ph2[NPAIR];                   /* each partial's phase, and the pairs' beat phases */
    uint32_t sph[NSW];                             /* the swarm's phases */
    uint32_t rph;                                  /* the ring modulator's phase */
    int32_t ramp;                                  /* RING's amount last block (Q15), ramped a block */
    int32_t sdrift[NSW], sdtarget[NSW];            /* each saw's own pitch wander (random LFOs, DRIFT) */
    int32_t sic[2][2];                             /* the swarm's two filters' states (left-leaning, right-leaning) */
    int32_t samp;                                  /* the swarm's level last block (Q15), ramped a block */
    int32_t amp[NP];                               /* each partial's level now (Q15), ramped a block */
    int32_t walk[NP];                              /* each partial's random walk (Q15 around 0) */
    int32_t wander, wtarget;                       /* the voice's own random LFO (Q15): a slew towards random targets */
    int32_t env;                                   /* the envelope, Q24 */
    int32_t gl;                                    /* the glide: the pitch offset still to slide away (1/256 semitone) */
    uint32_t subph;                                /* the sub-octave's phase */
    int32_t subamp;                                /* and its level last block */
    uint8_t stage;                                 /* 0 off 1 attack 2 held 3 release */
    uint32_t t;                                    /* blocks since the note began (BLOOM) */
    uint32_t rng;
} voice_t;
static voice_t voices[NV];
static volatile int32_t bend256;                   /* pitch bend, 1/256 semitone */
static volatile int32_t bend_knob;                 /* the ALGORITHM encoder's bend on HOME, springing back */
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
static int32_t mv_tilt16, mv_spread, mv_pitch256, mv_space, mv_gain;   /* the destinations' offsets this block */
static volatile uint32_t tide_period_ms = 128000;  /* the tide's turn, from RATE and the clock (the main loop's) */
static volatile uint32_t tide_reset;               /* MIDI start: the tide starts over */
/* which source goes where, and which way: TILT, COLOUR, PITCH, SPACE x walk0 walk1 cx cy cz sin cos */
static const int8_t MV_PATTERN[4][7] = {{1, 0, 1, 0, 0, 1, 0}, {0, -1, 0, 1, 0, 0, -1}, {-1, 0, 0, 0, 1, 0, 1}, {0, 1, -1, 0, 0, 1, 0}};
static void movement_block(void)
{
    int32_t move, amount[7], depth[4], j, i, dt;
    mv_gain = mulq15((int32_t)patch.move * 258, (int32_t)patch.move * 258) * 2;   /* MOVE squared, to x2: 64 is as it was, the top half overdrives (the sources clip at the extremes) */
    move = mv_gain;
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
        m = mulq15(clamp(mulq15(clamp(g, -32767, 32767), move), -32767, 32767), depth[j]);
        if (j == 0) mv_tilt16 = (m * 2047) >> 15;                           /* the whole of TILT */
        else if (j == 1) mv_spread = (m * 64) >> 15;                         /* +-64 of SPREAD and BRIGHT */
        else if (j == 2) mv_pitch256 = (m * 512) >> 15;                      /* +-2 semitones */
        else mv_space = m;                                                   /* of SPACE: see below */
    }
    {   /* SPACE moves by up to the distance to the knob's nearer end: 0 stays off and 127 stays full, 64 swings +-64 */
        int32_t r = patch.rv_mix < 64 ? patch.rv_mix : 127 - patch.rv_mix;
        rv_mix_now = clamp((int32_t)patch.rv_mix + ((mv_space * r) >> 15), 0, 127);
    }
}

/* ---- one voice, one block */
static int32_t env_k(uint32_t t)             /* an exponential stage's step: 60 dB in 0.1 s at 0 .. ~30 s at 127, log-spaced */
{
    uint32_t sh = t * 9u / 127u, fr = t * 9u % 127u;
    return (22000 >> sh) - (int32_t)(((22000u >> sh) - (22000u >> (sh + 1u))) * fr / 127u);
}
static void voice_block(voice_t *w, int32_t *outl, int32_t *outr, uint32_t core)
{
    uint32_t k, i, trig = w->trig;
    int32_t target[NP], from[NP], env_q15, pr = (w->at > press ? w->at : press) * 258, sum = 0;
    int32_t tilt16 = (int32_t)patch.tilt * 16 + mv_tilt16, spread = (int32_t)patch.spread - 64 + mv_spread, bright = (int32_t)patch.bright + mv_spread;
    int32_t pitch256 = ((int32_t)w->note + (int32_t)patch.tune - 12) * 256 + bend256 + bend_knob + mv_pitch256 + w->gl;
    int32_t swell = mulq15(pr, (int32_t)patch.swell * 258);        /* 0..32767: pressure opening the pad */
    int32_t wl, wr, width = (int32_t)patch.width * 258;
    if (trig != w->seen) {                         /* a new note */
        w->seen = trig;
        w->stage = 1;
        w->t = 0;
        if (!w->rng) w->rng = 0x9E3779B9u * (uint32_t)(w - voices + 1);
        if (!w->env) {                             /* from silence: Schroeder phases, so the harmonics never all peak together */
            for (k = 0; k < NP; k++) w->ph[k] = (uint32_t)(k * (k + 1u)) * 178956970u;   /* pi k (k + 1) / NP */
            w->subph = 0; w->subamp = 0; w->rph = 0; w->ramp = 0;
            for (k = 0; k < NPAIR; k++) w->ph2[k] = 0;                 /* the beats start at full */
            for (k = 0; k < NSW; k++) w->sph[k] = k * 858993459u + (k * k) * 300000000u;   /* the saws spread round the cycle */
            w->sic[0][0] = w->sic[0][1] = w->sic[1][0] = w->sic[1][1] = 0;
        }
    }
    if (!w->gate && w->stage && w->stage < 3) w->stage = 3;
    if (w->gl) w->gl -= w->gl / (1 + (int32_t)patch.glide * patch.glide / 16);   /* the glide: towards the note, 1 block .. 6 s */
    /* the envelope, a block at a time: linear attack, exponential release */
    if (w->stage == 1) {
        w->env += (1 << 24) / (int32_t)(4u + ((uint32_t)patch.attack * patch.attack >> 4));   /* 4 .. 1000 blocks */
        if (w->env >= (1 << 24)) { w->env = 1 << 24; w->stage = 2; }
    } else if (w->stage == 2) {                    /* the decay, towards the sustain, at DECAY's pace; at SUSTAIN 0 the voice ends there */
        int32_t sus = (int32_t)patch.sustain * 132104;   /* 2^24 / 127 */
        if (w->env > sus) {
            w->env -= (((w->env - sus) >> 9) * env_k(patch.decay)) >> 6;
            if (w->env - sus < 2048) w->env = sus;
            if (w->env < 2048) { w->env = 0; w->stage = 0; }
        }
    } else if (w->stage == 3) {                    /* the release, exponential: 60 dB in 0.1 s (0) .. ~30 s (127), log-spaced */
        w->env -= ((w->env >> 9) * env_k(patch.release)) >> 6;   /* (not mulq15 << 9: that rounds to 0 below 2^15 / k and the tail would hang there for ever) */
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
        int32_t wv = clamp(mulq15(mulq15(w->wander, patch.src_wander * 258), mv_gain), -32767, 32767), c;
        tilt16 += (mulq15(wv, patch.to_tilt * 258) * 2047) >> 15;
        c = (mulq15(wv, patch.to_colour * 258) * 64) >> 15;
        spread -= c; bright -= c;                                                /* (against the global colour) */
        pitch256 += (mulq15(wv, patch.to_pitch * 258) * 512) >> 15;
    }
    tilt16 = clamp(tilt16, 0, 2047); spread = clamp(spread, -64, 63); bright = clamp(bright, 0, 127);
    env_q15 = w->env >> 9;
    env_q15 = mulq15(env_q15, 32767 - mulq15((int32_t)patch.velo * 258, (127 - (int32_t)w->vel) * 258));   /* VELO: how much a soft note is softer */
    /* each partial's level: 1 / (k + 1) ^ tilt, the odd / even balance, the brightness ceiling, the walk, the bloom */
    for (k = 0; k < NP; k++) {
        int32_t lv = 32767, n = (int32_t)k, j;
        for (j = 0; j < n; j++) lv -= (lv * tilt16) >> 11;                   /* geometric fall: tilt 127 = x0.0 a step */
        if (k & 1u) lv = mulq15(lv, 32767 - spread * 256); else lv = mulq15(lv, 32767 + spread * 256);   /* SPREAD: odd (k even) against even */
        if ((int32_t)k * 10 > bright) lv = mulq15(lv, (int32_t)clamp(32767 - ((int32_t)k * 10 - bright) * 400, 0, 32767));   /* BRIGHT: a ceiling */
        lv = mulq15(lv, 32767 + (mulq15(w->walk[k], 16000)));               /* the walk: +-half */
        if (k && swell) lv += mulq15(32767 - lv, mulq15(swell, (int32_t)k * 2700));   /* pressure opens the upper partials */
        if (patch.bloom && w->t * 4u < (uint32_t)patch.bloom * k) lv = 0;   /* BLOOM: partial k enters bloom x k / 4 blocks in */
        target[k] = mulq15(lv, env_q15) / (int32_t)(2 + (k >> 1));   /* the spectral shape */
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
        uint32_t stretch_q15 = (uint32_t)patch.stretch * patch.stretch * 4u / 5u;              /* S: 0 .. 0.39 (squared: fine at the low end) */
        for (i = 0; i < HALF_FRAMES; i++) odd[i] = even[i] = 0;
        for (k = 0; k < NP; k++) {
            int32_t *o = (k & 1u) ? even : odd, a = from[k] << 8, d = target[k] - from[k];
            uint32_t ph = w->ph[k], inc = inc0 * (k + 1u);
            if (patch.stretch) inc += (inc >> 16) * ((stretch_q15 * k) >> 1);   /* STRETCH: partial k at (k + 1)(1 + S k): a string's stretch, then a bell's */
            if ((!from[k] && !target[k]) || inc >= 0x80000000u) {           /* silent, or past Nyquist: it would fold back */
                w->ph[k] = ph + inc * HALF_FRAMES;
                if (k < NPAIR) w->ph2[k] = w->ph2[k] + (inc >> 16) * dq * HALF_FRAMES;
                continue;
            }
            if (k < NPAIR && dq) {                 /* a pair as one sine at the pitch whose level beats at the pair's difference: exact
                                                    * (sin p + sin q = 2 sin((p + q) / 2) cos((p - q) / 2)), the beat ramped across the block: one oscillator, not two */
                uint32_t db = (inc >> 16) * dq, bph = w->ph2[k];            /* the beat's phase: half the pair's difference */
                int32_t c0 = (sine_r(bph + 0x40000000u) * 23170) >> 14, c1 = (sine_r(bph + db * HALF_FRAMES + 0x40000000u) * 23170) >> 14;   /* 1.414 cos: two halves at 0.707 */
                int32_t a0 = (from[k] * c0) >> 15, a1 = (target[k] * c1) >> 15;
                a = a0 << 8; d = a1 - a0;
                w->ph2[k] = bph + db * HALF_FRAMES;
            }
            for (i = 0; i < HALF_FRAMES; i++) {
                o[i] += mulq15(sine_r(ph), a >> 8);
                a += d;
                ph += inc;
            }
            w->ph[k] = ph;
        }
        if (patch.sub || w->subamp) {              /* SUB: a sine an octave under the fundamental, at its level x SUB */
            int32_t a = w->subamp << 8, t = mulq15(target[0], (int32_t)patch.sub * 258), d = t - w->subamp;
            uint32_t ph = w->subph, inc = inc0 >> 1;
            for (i = 0; i < HALF_FRAMES; i++) { odd[i] += mulq15(sine_r(ph), a >> 8); a += d; ph += inc; }
            w->subph = ph; w->subamp = t;
        }
        if (patch.ring || w->ramp) {               /* RING: the pad (and its sub) times a sine at RATIO x the pitch, mixed in by RING:
                                                    * the gain 1 - a + a sin runs from 1 to 1 - 2a, so at full it is the ring product alone
                                                    * and every partial becomes its sum and difference with the modulator */
            int32_t a = w->ramp << 8, t = (int32_t)patch.ring * 258, d = t - w->ramp;
            uint32_t ph = w->rph, inc = (inc0 >> 8) * (exp2_q15((uint32_t)patch.ratio * 32u) >> 8);   /* 2^(RATIO / 32 - 1): 0.5 .. 7.9 x, Q8 */
            for (i = 0; i < HALF_FRAMES; i++) {   /* the gain in Q14: the ring product x 1.414, so the level at full RING matches dry (a sine's rms) */
                int32_t a14 = a >> 9, g = 16384 - a14 + mulq15(a14, (sine_r(ph) * 23170) >> 14);
                odd[i] = (odd[i] * g) >> 14; even[i] = (even[i] * g) >> 14;
                a += d; ph += inc;
            }
            w->rph = ph; w->ramp = t;
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
    int32_t gain;                                  /* the breath now, Q15 (the screen) */
    int32_t ic[2][2];
    svf_t c[2];
} air = {{0x1D872B41u, 0x7A3F9C25u}, {0, 0}, 0, 0, 0, 0x99999999u, 32767, {{0, 0}, {0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}};
static volatile int32_t air_kick;                  /* a note just struck: a puff for the air (voice_on) */
static void air_block(int32_t *st, uint32_t n)
{
    int32_t level = ((int32_t)patch.air * patch.air * patch.air) >> 6, target = 0, i, side;   /* cubic (127^3 / 64 = 32005): a soft undertone over most of the knob, loud at the top */
    uint32_t v, p_crk, wind, crackle;
    if (!level && !air.env) return;
    for (v = 0; v < NV; v++) if ((voices[v].env >> 9) > target) target = voices[v].env >> 9;
    air.env += ((target - air.env) * (target > air.env ? 630 : 47)) >> 15;   /* with the pad: in over ~0.3 s, lingering ~4 s after it */
    if (air_kick) { air.env += air_kick; if (air.env > 32767) air.env = 32767; air_kick = 0; }   /* a note struck: a puff, by its velocity */
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
        level = mulq15(level, target); air.gain = target;
    } else { air.bph = 0x99999999u; air.gain = 32767; }                  /* held: ready to leave from the peak */
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
static svf_t mf_c;
static int32_t mf_ic[2][2], mf_cutq = 128 * 16 * 16, mf_mix, mf_hp;   /* the master filter's states, a side; its cutoff now (cut16 x 16), slewed; its fade-in; its side */
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
    }
    for (i = 0; i < HALF_FRAMES; i++) {            /* (the chord gain before the drive, so a chord is no grittier than a note; VOLUME after it, so it is a level and not less drive) */
        int32_t l = g_l[0][i] + (submitted ? g_l[1][i] : 0), r = g_r[0][i] + (submitted ? g_r[1][i] : 0);
        g_st[2u * i] = mulq15(soft_knee(l, 20000), poly_gain);
        g_st[2u * i + 1u] = mulq15(soft_knee(r, 20000), poly_gain);
    }
    if (patch.sat) {                               /* DRIVE: the pad into the clip, the level matched, so it warms, compresses, then grits */
        int32_t d8 = 256 + (int32_t)patch.sat * patch.sat, k = (9681 << 15) / softclip((10000 * d8) >> 8);   /* x1 .. x64, squared so the first half warms and the second grits; 9681 = softclip(10000): x1 at DRIVE 0, the level matched at a -10 dBFS peak, about the pad's */
        k = mulq15(k, vol);
        for (i = 0; i < 2u * HALF_FRAMES; i++) g_st[i] = mulq15(softclip((g_st[i] * d8) >> 8), k);
    } else {
        for (i = 0; i < 2u * HALF_FRAMES; i++) g_st[i] = mulq15(g_st[i], vol);
    }
    air_block(g_st, HALF_FRAMES);
    fx_delay(g_st, HALF_FRAMES);
    {                                              /* SPACE: the wet on top of the dry, the dry down to half at full */
        int32_t dry = 32767 - rv_mix_now * 129;
        if (submitted) {
            for (i = 0; i < 2u * HALF_FRAMES; i++) { rv_src[i] = g_st[i]; g_st[i] = mulq15(g_st[i], dry) + rv_wet[i]; }
        } else {
            fx_reverb(g_st, rv_wet, HALF_FRAMES);
            for (i = 0; i < 2u * HALF_FRAMES; i++) g_st[i] = mulq15(g_st[i], dry) + rv_wet[i];
        }
    }
    {   /* FILTER: the master, like the Circuit Tracks': left a high-pass sweeping up, right a low-pass sweeping down, off in
         * the middle. The cutoff slews towards the knob over ~50 ms and the coefficients slide across the block, so a sweep
         * is a sweep and not a staircase; the filter always runs, parked at the edge of the knob's side through the middle,
         * and its output fades in over the last steps out of the middle, so a fast turn through the centre is a crossfade */
        int32_t hp = patch.filter < 64u, v = hp ? 60 - (int32_t)patch.filter : (int32_t)patch.filter - 68;
        int32_t target = (hp ? 16 + v * 95 / 60 : 128 - v * 77 / 59) * 16 * 16;   /* 20 Hz .. 5 kHz up; 13 kHz .. 150 Hz down, x16 for the slew */
        int32_t mix_to = v > 0 ? 32767 : clamp((v + 4) * 8192, 0, 32767);          /* the fade: all in from 4 steps outside the middle */
        svf_t c0 = mf_c;
        int32_t d1, d2, d3, m0 = mf_mix, dm;
        if (v <= 0) target = (hp ? 16 : 128) * 16 * 16;
        if (hp != mf_hp) {                                                            /* the other side wanted: fade right out, then switch tap and edge with the state cleared */
            mix_to = 0;
            if (!mf_mix) { mf_hp = hp; mf_cutq = target; mf_ic[0][0] = mf_ic[0][1] = mf_ic[1][0] = mf_ic[1][1] = 0; }
            else target = mf_cutq;
        }
        hp = mf_hp;
        mf_cutq += (target - mf_cutq) >> 3;
        mf_mix += clamp(mix_to - mf_mix, -4096, 4096);                                  /* a straight fade, 46 ms end to end, to exactly nothing */
        svf_coef(&mf_c, mf_cutq >> 4, SVF_K(28));
        d1 = mf_c.a1 - c0.a1; d2 = mf_c.a2 - c0.a2; d3 = mf_c.a3 - c0.a3; dm = mf_mix - m0;
        for (i = 0; i < HALF_FRAMES; i++) {
            svf_t c = c0;
            int32_t lp, hpo, f = (int32_t)((i << 8) / HALF_FRAMES), m = m0 + ((dm * f) >> 8), y;   /* 0 .. 255 across the block */
            c.a1 += (d1 * f) >> 8; c.a2 += (d2 * f) >> 8; c.a3 += (d3 * f) >> 8;
            svf_step(&c, g_st[2u * i], &mf_ic[0][0], &mf_ic[0][1], &lp, &hpo); y = hp ? hpo : lp; g_st[2u * i] += mulq15(y - g_st[2u * i], m);
            svf_step(&c, g_st[2u * i + 1u], &mf_ic[1][0], &mf_ic[1][1], &lp, &hpo); y = hp ? hpo : lp; g_st[2u * i + 1u] += mulq15(y - g_st[2u * i + 1u], m);
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
static uint32_t last_note = 60;
static void voice_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, held = 0;
    if (note > 127u) return;
    for (v = 0; v < NV; v++) if (voices[v].gate) held++;
    for (v = 0; v < NV; v++) {
        voice_t *w = &voices[v];
        uint32_t score = w->note == note && w->gate ? 0u : w->gate ? 0xC0000000u + w->age : w->active ? 0x80000000u + w->age : w->age;
        if (score < best) { best = score; pick = v; }
    }
    voices[pick].note = (uint8_t)note;
    voices[pick].vel = (uint8_t)(vel > 127u ? 127u : vel);
    voices[pick].age = ++voice_ages;
    voices[pick].gate = 1;
    voices[pick].at = 0;
    voices[pick].gl = held && patch.glide ? ((int32_t)last_note - (int32_t)note) * 256 : 0;   /* GLIDE: a note over a held one slides in from the last */
    last_note = note;
    air_kick += (int32_t)vel * 48;
    voices[pick].trig++;
}
static void voice_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voices[v].note == note && voices[v].gate) voices[v].gate = 0;
}
static void voices_pressure(uint32_t note, uint32_t p)   /* a held note's own pressure (a key, or poly aftertouch) */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voices[v].note == note && voices[v].gate) voices[v].at = (uint8_t)(p > 127u ? 127u : p);
}
static uint32_t voices_sounding(void)
{
    uint32_t v, m = 0;
    for (v = 0; v < NV; v++) if (voices[v].active) m |= 1u << v;
    return m;
}

/* ---- what survives a power-off */
typedef struct {
    uint8_t magic, oct_plus2, midi_chan, expr, latch, preset, scene_b, key_sense;   /* scene_b: the preset ARP morphs to; key_sense: the keys' own velocity and pressure */
    patch_t patch;                                                                  /* scene A, the sound as set */
    patch_t patch_b;                                                                /* scene B as edited (a preset, until the knobs touch it at B) */
} settings_t;
typedef struct { uint8_t magic, oct_plus2, midi_chan, expr, latch, preset, scene_b, key_sense; patch_t patch; } settings_v1_t;   /* up to 0.7.6: B was rebuilt from its preset at power-on, its edits lost */
static settings_t set;
#define SET_MAGIC 0xABu
/* ---- the pages: a title, a graphic, four knobs. The knobs are the graphic's own controls, drawn in its colours. */
enum { K_NUM = 0, K_ONOFF, K_MCHAN, K_EXPR, K_SEMI, K_BIP, K_DIV, K_RATIO };   /* K_DIV: a time, a note division while a clock is in */
typedef struct { const char *name; uint8_t off, max, kind, global; } pdesc_t;
#define PA(f, name, max, kind) {name, (uint8_t)offsetof(patch_t, f), max, kind, 0}
#define ST(f, name, max, kind) {name, (uint8_t)offsetof(settings_t, f), max, kind, 1}
#define NONE {0, 0, 0, 0, 0}
enum { PG_HOME = 0, PG_TONE, PG_TONE2, PG_METAL, PG_SWARM, PG_AIR, PG_SHAPE, PG_TOUCH, PG_MOVE, PG_SRC, PG_DST, PG_SPACE, PG_DELAY, PG_FREEZE, PG_MIDI, NPAGE };
typedef struct { const char *title; void (*draw)(void); uint8_t live; pdesc_t p[4]; } page_t;   /* live: redrawn ten times a second */
static void draw_aurora(void), draw_tone(void), draw_metal(void), draw_tone2(void), draw_shape(void), draw_touch(void), draw_swarm(void), draw_air(void), draw_move(void),
            draw_matrix(void), draw_space(void), draw_delay(void), draw_freeze(void), draw_midi(void);
static const page_t PAGES[NPAGE] = {
    {"AURORA",  draw_aurora, 1, {PA(filter, "FILTER", 127, K_BIP), PA(tilt, "TILT", 127, K_NUM), PA(air, "AIR", 127, K_NUM), PA(ring, "METAL", 127, K_NUM)}},
    {"TONE",    draw_tone,   0, {PA(tilt, "TILT", 127, K_NUM), PA(spread, "SPREAD", 127, K_NUM), PA(bright, "BRIGHT", 127, K_NUM), PA(detune, "DETUNE", 127, K_NUM)}},
    {"TONE 2",  draw_tone2,  0, {PA(stretch, "STRETCH", 127, K_NUM), PA(sub, "SUB", 127, K_NUM), PA(tune, "TUNE", 24, K_SEMI), PA(sat, "DRIVE", 127, K_NUM)}},
    {"METAL",   draw_metal,  0, {PA(ring, "RING", 127, K_NUM), PA(ratio, "RATIO", 127, K_RATIO), NONE, NONE}},
    {"SWARM",   draw_swarm,  0, {PA(sw_level, "LEVEL", 127, K_NUM), PA(sw_spread, "SPREAD", 127, K_NUM), PA(sw_cut, "CUTOFF", 127, K_NUM), PA(sw_res, "RESO", 127, K_NUM)}},
    {"AIR",     draw_air,    1, {PA(air, "AIR", 127, K_NUM), PA(air_tone, "TONE", 127, K_NUM), PA(air_tex, "TEXTUR", 127, K_NUM), PA(air_breath, "BREATH", 127, K_NUM)}},
    {"SHAPE",   draw_shape,  1, {PA(attack, "ATTACK", 127, K_NUM), PA(decay, "DECAY", 127, K_NUM), PA(sustain, "SUSTAIN", 127, K_NUM), PA(release, "RELEAS", 127, K_NUM)}},
    {"TOUCH",   draw_touch,  1, {PA(bloom, "BLOOM", 127, K_NUM), PA(swell, "SWELL", 127, K_NUM), PA(velo, "VELO", 127, K_NUM), PA(glide, "GLIDE", 127, K_NUM)}},
    {"MOVE",    draw_move,   1, {PA(move, "MOVE", 127, K_NUM), PA(rate, "RATE", 127, K_NUM), PA(drift, "DRIFT", 127, K_NUM), PA(width, "WIDTH", 127, K_NUM)}},
    {"SOURCES", draw_matrix, 1, {PA(src_wander, "WANDER", 127, K_NUM), PA(src_walk, "WALK", 127, K_NUM), PA(src_chaos, "CHAOS", 127, K_NUM), PA(src_tide, "TIDE", 127, K_NUM)}},
    {"TARGETS", draw_matrix, 1, {PA(to_tilt, "TILT", 127, K_NUM), PA(to_colour, "COLOUR", 127, K_NUM), PA(to_pitch, "PITCH", 127, K_NUM), PA(to_space, "SPACE", 127, K_NUM)}},
    {"SPACE",   draw_space,  0, {PA(rv_mix, "MIX", 127, K_NUM), PA(rv_size, "SIZE", 127, K_NUM), PA(rv_damp, "DAMP", 127, K_NUM), PA(rv_shim, "SHIMMR", 127, K_NUM)}},
    {"DELAY",   draw_delay,  0, {PA(dly_time, "TIME", 127, K_DIV), PA(dly_fb, "FEEDBK", 127, K_NUM), PA(dly_mix, "MIX", 127, K_NUM), PA(dly_tape, "TAPE", 127, K_NUM)}},
    {"FREEZE",  draw_freeze, 1, {PA(freeze, "FREEZE", 127, K_NUM), PA(frz_grain, "GRAIN", 127, K_NUM), PA(frz_spray, "SPRAY", 127, K_NUM), PA(frz_shift, "SHIFT", 24, K_SEMI)}},
    {"MIDI",    draw_midi,   1, {ST(midi_chan, "CHAN", 16, K_MCHAN), ST(expr, "EXPR", 127, K_EXPR), PA(volume, "VOLUME", 127, K_NUM), ST(key_sense, "KEYS", 1, K_ONOFF)}},
};
static const uint8_t PAGE_GROUP[NPAGE][2] = {{PG_HOME, 1}, {PG_TONE, 5}, {PG_TONE, 5}, {PG_TONE, 5}, {PG_TONE, 5}, {PG_TONE, 5}, {PG_SHAPE, 2}, {PG_SHAPE, 2}, {PG_MOVE, 3}, {PG_MOVE, 3}, {PG_MOVE, 3},
                                             {PG_SPACE, 3}, {PG_SPACE, 3}, {PG_SPACE, 3}, {PG_MIDI, 1}};
static const char *const N_ONOFF[2] = {"OFF", "ON"};
/* the macros, CC 80..87 in the Circuit Tracks' order, each a patch field */
static const struct { const char *name; uint8_t off; } MACRO[8] = {
    {"MOVE", offsetof(patch_t, move)}, {"SPACE", offsetof(patch_t, rv_mix)}, {"AIR", offsetof(patch_t, air)}, {"TILT", offsetof(patch_t, tilt)},
    {"WIDTH", offsetof(patch_t, width)}, {"DRIFT", offsetof(patch_t, drift)}, {"FREEZE", offsetof(patch_t, freeze)}, {"BLOOM", offsetof(patch_t, bloom)}};
/* ---- the presets: each a name and what it changes from the default sound (the PRESETS encoder, or a program change). From the slow and vast to the short and struck, then the horns, then the sounds players sent in. */
typedef struct { uint8_t off, v; } pdelta_t;
#define D(f, v) {(uint8_t)offsetof(patch_t, f), v}
#define END {255, 0}
typedef struct { const char *name; pdelta_t d[sizeof(patch_t) + 1u]; } preset_t;   /* room for a sound that differs from the default everywhere */
static const preset_t PRESETS[] = {
    {"AURORA", {D(tilt, 63), D(spread, 34), D(bright, 46), D(attack, 30), D(sustain, 105), D(detune, 69), D(sw_level, 58), D(sw_spread, 58), D(sw_cut, 48), D(sw_res, 66), D(air_tone, 96), D(air_tex, 52), D(air_breath, 47), D(move, 20), D(rate, 69), D(drift, 58), D(width, 96), D(air, 36), D(src_walk, 62), D(to_pitch, 83), D(to_space, 116), D(dly_time, 50), D(dly_fb, 88), D(dly_mix, 99), D(dly_tape, 92), D(rv_mix, 100), D(rv_damp, 30), D(rv_shim, 80), D(freeze, 62), D(frz_grain, 50), D(frz_spray, 40), END}},
    {"DEEP", {D(attack, 127), D(decay, 0), D(sustain, 127), D(release, 127), D(tilt, 85), D(bright, 70), D(detune, 50), D(bloom, 127), D(drift, 70), D(move, 127), D(rate, 30), D(width, 127),
              D(src_wander, 100), D(src_walk, 80), D(src_chaos, 90), D(src_tide, 100), D(to_tilt, 100), D(to_colour, 70), D(to_pitch, 40), D(to_space, 80),
              D(air, 40), D(air_tone, 40), D(air_breath, 110), D(sw_level, 50), D(sw_cut, 30), D(sw_res, 25), D(rv_mix, 100), D(rv_size, 127), D(rv_shim, 35), D(velo, 10), END}},
    {"TIDE", {D(src_tide, 127), D(src_walk, 20), D(src_chaos, 0), D(src_wander, 40), D(to_space, 127), D(to_tilt, 110), D(to_pitch, 10), D(rate, 10), D(move, 127),
              D(attack, 110), D(sustain, 127), D(release, 127), D(tilt, 75), D(bright, 100), D(detune, 40), D(air, 35), D(air_breath, 127), D(air_tone, 80),
              D(rv_mix, 60), D(rv_size, 120), D(width, 100), D(velo, 10), END}},
    {"CHOIR", {D(attack, 95), D(decay, 60), D(sustain, 110), D(release, 100), D(tilt, 60), D(bright, 80), D(detune, 80), D(bloom, 60), D(sw_level, 30), D(sw_cut, 36), D(sw_res, 10), D(sw_spread, 70),
               D(air, 20), D(air_tone, 70), D(air_breath, 90), D(move, 60), D(rate, 25), D(width, 110), D(rv_mix, 80), D(rv_size, 115), D(velo, 30), END}},
    {"ICE", {D(tilt, 100), D(spread, 90), D(bright, 127), D(detune, 60), D(attack, 70), D(sustain, 127), D(release, 110), D(air, 90), D(air_tone, 110), D(air_tex, 15), D(air_breath, 60),
             D(rv_mix, 95), D(rv_size, 120), D(rv_shim, 70), D(rv_damp, 10), D(move, 50), D(rate, 20), D(width, 127), D(sw_level, 0), D(velo, 20), END}},
    {"STORM", {D(air, 127), D(air_tone, 50), D(air_tex, 95), D(air_breath, 100), D(sw_level, 60), D(sw_cut, 50), D(sw_res, 60), D(tilt, 90), D(attack, 80), D(sustain, 127), D(release, 90),
               D(move, 127), D(rate, 100), D(drift, 80), D(src_chaos, 127), D(to_tilt, 127), D(to_colour, 100), D(rv_mix, 80), D(rv_size, 100),
               D(dly_mix, 50), D(dly_fb, 85), D(dly_tape, 120), D(dly_time, 100), D(width, 110), D(velo, 20), END}},
    {"SWARM", {D(sw_level, 110), D(sw_cut, 64), D(sw_res, 70), D(sw_spread, 100), D(tilt, 110), D(bright, 60), D(attack, 50), D(sustain, 127), D(release, 70), D(drift, 100), D(move, 90), D(rate, 50),
               D(width, 100), D(rv_mix, 50), D(dly_mix, 30), D(dly_fb, 50), D(velo, 40), END}},
    {"SUB", {D(tilt, 127), D(bright, 127), D(detune, 0), D(sw_level, 90), D(sw_cut, 18), D(sw_res, 35), D(sw_spread, 12), D(attack, 40), D(sustain, 127), D(release, 90), D(move, 30), D(width, 50),
             D(rv_mix, 20), D(rv_size, 60), D(air, 0), D(velo, 30), END}},
    {"ORGAN", {D(attack, 4), D(decay, 0), D(sustain, 127), D(release, 8), D(tilt, 26), D(spread, 24), D(bright, 127), D(detune, 12), D(bloom, 0), D(sw_level, 0), D(move, 0), D(width, 40),
               D(rv_mix, 25), D(rv_size, 60), D(velo, 0), END}},
    {"GLASS", {D(attack, 0), D(decay, 70), D(sustain, 0), D(release, 60), D(tilt, 36), D(spread, 110), D(bright, 127), D(detune, 24), D(bloom, 0), D(swell, 30), D(sw_level, 0),
               D(move, 30), D(rate, 40), D(width, 90), D(rv_mix, 70), D(rv_size, 100), D(rv_damp, 30), D(velo, 90), D(volume, 127), D(stretch, 30), END}},
    {"MALLET", {D(attack, 0), D(decay, 45), D(sustain, 0), D(release, 30), D(tilt, 80), D(spread, 50), D(bright, 90), D(detune, 0), D(bloom, 0), D(sw_level, 25), D(sw_cut, 90), D(sw_res, 40),
                D(move, 10), D(width, 50), D(rv_mix, 40), D(rv_size, 70), D(dly_mix, 45), D(dly_fb, 55), D(dly_time, 70), D(dly_tape, 60), D(velo, 110), D(volume, 127), D(stretch, 16), END}},
    {"BELL", {D(attack, 0), D(decay, 90), D(sustain, 0), D(release, 80), D(tilt, 40), D(spread, 80), D(bright, 127), D(detune, 10), D(stretch, 48), D(velo, 90), D(bloom, 0),
              D(sw_level, 0), D(air, 0), D(move, 20), D(rate, 30), D(width, 100), D(rv_mix, 70), D(rv_size, 110), D(rv_damp, 40), D(volume, 127), END}},
    {"GAMELAN", {D(attack, 0), D(decay, 55), D(sustain, 0), D(release, 40), D(tilt, 55), D(spread, 100), D(bright, 127), D(detune, 0), D(stretch, 70), D(tune, 24), D(velo, 110), D(bloom, 0),
                 D(sw_level, 0), D(air, 0), D(move, 10), D(dly_mix, 40), D(dly_time, 60), D(dly_fb, 50), D(dly_tape, 50), D(rv_mix, 50), D(rv_size, 80), D(volume, 127), END}},
    {"PIANO", {D(attack, 0), D(decay, 80), D(sustain, 20), D(release, 50), D(tilt, 60), D(spread, 50), D(bright, 110), D(detune, 0), D(stretch, 12), D(sub, 30), D(velo, 100), D(bloom, 0),
               D(sw_level, 0), D(air, 0), D(move, 10), D(width, 60), D(dly_mix, 20), D(dly_fb, 30), D(rv_mix, 40), D(rv_size, 70), D(volume, 127), END}},
    {"MONOLITH", {D(sub, 110), D(sat, 70), D(tilt, 110), D(bright, 90), D(detune, 30), D(sw_level, 60), D(sw_cut, 24), D(sw_res, 30), D(sw_spread, 30), D(attack, 90), D(sustain, 127), D(release, 110),
                  D(glide, 70), D(move, 60), D(rate, 20), D(width, 120), D(air, 25), D(air_tone, 30), D(rv_mix, 80), D(rv_size, 115), D(velo, 10), END}},
    {"SLIDE", {D(glide, 60), D(attack, 20), D(sustain, 127), D(release, 60), D(tilt, 70), D(spread, 40), D(detune, 20), D(sub, 40), D(sat, 30), D(sw_level, 50), D(sw_cut, 70), D(sw_res, 50),
               D(dly_mix, 45), D(dly_fb, 60), D(dly_time, 70), D(rv_mix, 50), D(width, 60), D(move, 30), D(velo, 50), END}},
    {"GRIT", {D(sat, 110), D(tilt, 50), D(bright, 100), D(detune, 50), D(sw_level, 80), D(sw_cut, 90), D(sw_res, 40), D(sw_spread, 70), D(attack, 30), D(sustain, 127), D(release, 50),
              D(filter, 50), D(move, 50), D(drift, 50), D(rv_mix, 40), D(dly_mix, 30), D(dly_fb, 45), D(width, 90), D(velo, 40), D(volume, 90), END}},
    {"RADIO", {D(filter, 34), D(sat, 60), D(tilt, 40), D(bright, 70), D(detune, 20), D(sw_level, 30), D(sw_cut, 60), D(air, 50), D(air_tex, 95), D(air_tone, 70), D(air_breath, 40),
               D(attack, 40), D(sustain, 127), D(release, 60), D(dly_mix, 50), D(dly_tape, 110), D(dly_fb, 55), D(dly_time, 80), D(rv_mix, 30), D(width, 30), D(move, 40), D(volume, 72), END}},
    {"ABYSS", {D(tune, 0), D(sub, 127), D(tilt, 127), D(sw_level, 70), D(sw_cut, 10), D(sw_res, 20), D(sw_spread, 20), D(attack, 70), D(sustain, 127), D(release, 100), D(filter, 100),
               D(move, 40), D(rate, 15), D(rv_mix, 60), D(rv_size, 120), D(width, 40), D(air, 0), D(velo, 20), END}},
    {"PLUCK", {D(attack, 0), D(decay, 25), D(sustain, 0), D(release, 15), D(tilt, 60), D(spread, 40), D(bright, 127), D(detune, 10), D(bloom, 0), D(sw_level, 70), D(sw_cut, 110), D(sw_res, 50), D(sw_spread, 30),
               D(dly_mix, 55), D(dly_fb, 70), D(dly_time, 60), D(dly_tape, 30), D(rv_mix, 35), D(rv_size, 70), D(move, 20), D(velo, 120), D(volume, 127), END}},
    /* the horns: the film-trailer braam, built on RING. A ratio a hair off 1 throbs (the difference tone is a slow beat), 0.5 growls an
     * octave under, 1.5 is a half-integer series (brass, snarling), 2.8 is metal; a slow attack and BLOOM open it "bwaa", DRIVE grits it */
    {"BRAAM", {D(tune, 0), D(sub, 90), D(sat, 80), D(ring, 80), D(ratio, 33), D(tilt, 45), D(bright, 110), D(detune, 70), D(attack, 55), D(sustain, 127), D(release, 85), D(bloom, 50),
               D(sw_level, 70), D(sw_spread, 60), D(sw_cut, 50), D(sw_res, 30), D(move, 40), D(rate, 20), D(drift, 40), D(width, 110), D(rv_mix, 85), D(rv_size, 120), D(rv_damp, 40), D(velo, 30), END}},
    {"FOGHORN", {D(tune, 0), D(sub, 127), D(sat, 50), D(ring, 100), D(ratio, 1), D(tilt, 90), D(bright, 60), D(detune, 40), D(attack, 75), D(sustain, 127), D(release, 100), D(bloom, 70),
                 D(sw_level, 40), D(sw_cut, 30), D(sw_res, 20), D(sw_spread, 40), D(air, 30), D(air_tone, 30), D(air_tex, 20), D(air_breath, 100), D(move, 50), D(rate, 15), D(width, 90),
                 D(rv_mix, 90), D(rv_size, 127), D(rv_damp, 60), D(velo, 20), END}},
    {"WARHORN", {D(ring, 100), D(ratio, 51), D(sat, 85), D(sub, 40), D(tilt, 35), D(bright, 127), D(detune, 50), D(attack, 45), D(sustain, 127), D(release, 80), D(bloom, 30), D(glide, 40),
                 D(sw_level, 75), D(sw_cut, 70), D(sw_res, 50), D(sw_spread, 80), D(dly_mix, 35), D(dly_fb, 50), D(dly_time, 80), D(dly_tape, 60), D(rv_mix, 70), D(rv_size, 100), D(width, 120),
                 D(move, 50), D(drift, 50), D(velo, 40), D(volume, 76), END}},
    {"TITAN", {D(tune, 0), D(ring, 110), D(ratio, 79), D(sat, 110), D(sub, 80), D(tilt, 50), D(bright, 127), D(stretch, 20), D(detune, 30), D(attack, 60), D(sustain, 127), D(release, 95), D(bloom, 40),
               D(sw_level, 60), D(sw_cut, 40), D(sw_res, 60), D(air, 40), D(air_tex, 80), D(air_tone, 50), D(air_breath, 60), D(dly_mix, 40), D(dly_fb, 60), D(dly_time, 90), D(dly_tape, 90),
               D(rv_mix, 80), D(rv_size, 115), D(rv_shim, 40), D(width, 127), D(move, 60), D(rate, 25), D(drift, 60), D(velo, 30), D(volume, 78), END}},
    /* sent in by players (the share page): QUANTUM, "lush and beautiful pads", and PULSAR, its bass, "meant to pair as a B to Quantum's A" (veecheech) */
    {"QUANTUM", {D(tilt, 64), D(spread, 45), D(bright, 98), D(attack, 16), D(decay, 41), D(sustain, 116), D(release, 70), D(bloom, 0), D(detune, 22), D(velo, 100), D(sw_level, 75), D(sw_spread, 38), D(sw_cut, 86),
              D(sw_res, 0), D(air_breath, 69), D(move, 56), D(rate, 35), D(drift, 12), D(width, 52), D(air, 46), D(src_wander, 31), D(src_walk, 35), D(src_chaos, 48), D(src_tide, 98), D(to_tilt, 57), D(to_colour, 86),
              D(to_pitch, 13), D(to_space, 84), D(swell, 14), D(dly_time, 80), D(dly_fb, 101), D(dly_mix, 71), D(dly_tape, 60), D(rv_mix, 56), D(rv_size, 90), D(rv_damp, 70), D(rv_shim, 82), D(freeze, 32), D(frz_grain, 84),
              D(frz_spray, 71), D(frz_shift, 24), D(volume, 126), D(stretch, 20), D(sub, 36), D(filter, 56), D(glide, 3), D(ratio, 0), END}},
    {"PULSAR", {D(tilt, 67), D(spread, 46), D(bright, 64), D(attack, 0), D(decay, 40), D(sustain, 16), D(release, 30), D(bloom, 17), D(detune, 33), D(velo, 109), D(sw_level, 75), D(sw_spread, 31), D(sw_cut, 64),
             D(sw_res, 11), D(air_breath, 69), D(move, 46), D(rate, 90), D(drift, 62), D(width, 55), D(air, 16), D(src_wander, 45), D(src_walk, 53), D(src_chaos, 65), D(src_tide, 88), D(to_tilt, 72), D(to_colour, 64),
             D(to_pitch, 12), D(to_space, 72), D(swell, 31), D(dly_time, 69), D(dly_fb, 72), D(dly_mix, 45), D(dly_tape, 60), D(rv_mix, 46), D(rv_size, 70), D(rv_damp, 64), D(rv_shim, 12), D(freeze, 34), D(frz_grain, 67),
             D(frz_spray, 67), D(frz_shift, 0), D(volume, 126), D(stretch, 20), D(sub, 104), D(tune, 0), D(sat, 45), D(filter, 54), D(ring, 67), D(ratio, 0), END}},
};
#define NPRESET (sizeof PRESETS / sizeof PRESETS[0])
/* the user bank: sixteen slots after the factory ones, USER 1..16, in the bank sector (SAVE); an empty slot is AURORA */
#define NUSER 16u
#define NALL (NPRESET + NUSER)
#define USLOT_MAGIC 0x5Bu
typedef struct { uint8_t magic; patch_t p; } uslot_t;
static uslot_t bank[NUSER];
static char uname[8];
static void preset_build(uint32_t i, patch_t *p)
{
    const pdelta_t *d;
    i %= NALL;
    if (i >= NPRESET) { if (bank[i - NPRESET].magic == USLOT_MAGIC) { *p = bank[i - NPRESET].p; return; } i = 0; }
    *p = BASE_PATCH;
    for (d = PRESETS[i].d; d->off != 255u; d++) ((uint8_t *)p)[d->off] = d->v;
}
static const char *preset_name(uint32_t i)
{
    i %= NALL;
    if (i < NPRESET) return PRESETS[i].name;
    i -= NPRESET - 1u;
    uname[0] = 'U'; uname[1] = 'S'; uname[2] = 'E'; uname[3] = 'R'; uname[4] = ' ';
    if (i >= 10u) { uname[5] = (char)('0' + i / 10u); uname[6] = (char)('0' + i % 10u); uname[7] = 0; } else { uname[5] = (char)('0' + i); uname[6] = 0; }
    return uname;
}
/* the sound out over MIDI (SAVE, then GLO): one SysEx, F0 7D 'A' 'U' <format> <size> then each byte of the patch as two
 * nibbles, a checksum (the nibbles' sum, mod 128), F7. 7D is the non-commercial manufacturer id. The site's share page
 * (share.html) reads it back into the emulator's "Copy sound" text; the field names are PATCH_FIELDS, in order */
#define DUMP_FORMAT 1u
static void patch_send(void)
{
    uint8_t m[8 + 2 * sizeof(patch_t)];
    const uint8_t *p = (const uint8_t *)&patch;
    uint32_t i, n = 0, sum = 0;
    m[n++] = 0xF0; m[n++] = 0x7D; m[n++] = 'A'; m[n++] = 'U'; m[n++] = DUMP_FORMAT; m[n++] = (uint8_t)sizeof(patch_t);
    for (i = 0; i < sizeof(patch_t); i++) { m[n++] = p[i] >> 4; m[n++] = p[i] & 15u; sum += (p[i] >> 4) + (p[i] & 15u); }
    m[n++] = (uint8_t)(sum & 127u); m[n++] = 0xF7;
    midi_sysex(m, n);
}
/* scenes: A is the sound as set (set.patch), B another preset (set.scene_b) as edited (set.patch_b: the knobs edit the
 * scene you are at, and both are kept across a power-off); ARP morphs between them over four seconds, SEL cuts. */
static int32_t morph, morph_to;                  /* 0 at A .. 32767 at B, and where it is going */
static uint8_t save_mode, save_slot;             /* SAVE: picking a slot with PRESETS */
static uint32_t sent_ms = 0x80000000u;           /* when the sound was last sent out (SAVE, GLO): the header says so for a moment */
static uint8_t *scene_ptr(uint32_t off) { return (morph_to && morph == 32767 ? (uint8_t *)&set.patch_b : (uint8_t *)&set.patch) + off; }
static void live_update(void)                    /* patch, the sound as played, from the scenes and the morph (the autopilot adds its drift after) */
{
    uint32_t i;
    if (!morph) { patch = set.patch; return; }
    for (i = 0; i < sizeof(patch_t); i++) {
        int32_t va = ((uint8_t *)&set.patch)[i], vb = ((uint8_t *)&set.patch_b)[i];
        ((uint8_t *)&patch)[i] = (uint8_t)(i == offsetof(patch_t, dly_ping) || i == offsetof(patch_t, dly_sync) ? (morph >= 16384 ? vb : va) : va + (((vb - va) * morph) >> 15));
    }
}
static int preset_edited(void)              /* the sound differs from its preset */
{
    patch_t p;
    preset_build(set.preset, &p);
    return memcmp(&p, &set.patch, sizeof p) != 0;
}

static uint32_t last_midi_ms;                 /* (the last message's time: below) */
static uint8_t autopilot;                     /* (SEQ's autopilot: below) */
static uint32_t load_now, wload_now;          /* the cores' load, % (the MIDI page) */
/* ---- the screen: a header, a graphic the four knobs shift (240 x 124 at y 26), the four knobs under it in their
 * colours, a status line. Every page's graphic is the thing its knobs change, each part drawn in the colour of the
 * knob that moves it: green, blue, violet, amber, left to right. HOME is the aurora: the sound, drawn. */
#define C_PANEL RGB(14, 16, 24)                           /* a night sky */
#define C_SILK RGB(214, 222, 232)
#define C_LEG RGB(104, 116, 136)
#define C_RULE RGB(60, 70, 90)
#define C_SLOT RGB(6, 7, 12)
#define C_GROUND RGB(5, 7, 12)                           /* the ground under the sky: HOME's dials stand on it */
#define GY 26
#define GH 124
#define KY 154
static const uint16_t KC[4] = {RGB(120, 230, 190), RGB(90, 170, 255), RGB(220, 120, 230), RGB(240, 190, 70)};
#define KEY0 53u
static uint32_t page = PG_HOME;
static uint32_t ui_rng = 0x9E3779B9u;
static uint8_t *param_ptr(const pdesc_t *d) { return d->global ? (uint8_t *)&set + d->off : (uint8_t *)&patch + d->off; }
static inline uint16_t blend(uint16_t a, uint16_t b, uint32_t t)   /* a to b by t, 0..256 */
{
    uint32_t u = 256u - t;
    uint32_t r = ((a >> 11) * u + (b >> 11) * t) >> 8, g = (((a >> 5) & 63u) * u + ((b >> 5) & 63u) * t) >> 8, bl = ((a & 31u) * u + (b & 31u) * t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static inline uint16_t dim(uint16_t c, uint32_t t) { return blend(C_PANEL, c, t); }   /* a colour at t / 256 over the panel */
static void cv_hline(int32_t x0, int32_t x1, int32_t y, uint16_t c) { if (x1 < x0) { int32_t t = x0; x0 = x1; x1 = t; } cv_rect(x0, y, x1 - x0 + 1, 1, c); }
static void cv_vline(int32_t x, int32_t y0, int32_t y1, uint16_t c) { if (y1 < y0) { int32_t t = y0; y0 = y1; y1 = t; } cv_rect(x, y0, 1, y1 - y0 + 1, c); }
static void cv_dot(int32_t x, int32_t y, int32_t r, uint16_t c)
{
    int32_t dx, dy;
    for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r + r) cv_pset(x + dx, y + dy, c);
}
static void cv_curve(const int16_t *ys, int32_t x0, int32_t n, uint16_t c)   /* a polyline of n heights from x0 */
{
    int32_t i;
    for (i = 1; i < n; i++) cv_line(x0 + i - 1, ys[i - 1], x0 + i, ys[i], c);
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
    case K_DIV: if (fm1_ms - clk_last < 2000u) src = DIV_NAME[(v * NDIV) >> 7]; break;
    case K_RATIO: {   /* 2^(v / 32 - 1) to two places: 0.50 .. 7.92 */
        uint32_t r = (exp2_q15(v * 32u) * 100u) >> 16;
        s[i++] = (char)('0' + r / 100u); s[i++] = '.'; s[i++] = (char)('0' + r / 10u % 10u); s[i++] = (char)('0' + r % 10u); s[i] = 0; return; }
    case K_BIP: n -= 64; if (n >= -4 && n <= 4) { src = "OFF"; break; } if (n < 0) { s[i++] = '-'; n = -n; } else s[i++] = '+'; break;
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
    char s[16];
    const char *title = pg->title;
    int32_t w, x0;
    if (page == PG_HOME && !save_mode) { cv_begin(240, 24, C_PANEL); cv_blit(0, 0); return; }   /* HOME: the name is on the sky */
    if (save_mode) {                            /* SAVE: the slot being picked */
        int32_t i = 0;
        const char *n = "SAVE > ";
        while (*n) s[i++] = *n++;
        n = preset_name(NPRESET + save_slot);
        while (*n && i < 15) s[i++] = *n++;
        s[i] = 0; title = s;
    } else if (fm1_ms - sent_ms < 2000u) {      /* the sound just went out */
        title = "SOUND SENT";
    } else if (page == PG_HOME) {               /* the preset's name, starred once the sound has been changed; scene B's while there */
        int32_t i = 0;
        const char *n = preset_name(morph >= 16384 ? set.scene_b : set.preset);
        if (morph >= 16384) { s[i++] = 'B'; s[i++] = ':'; s[i++] = ' '; }
        while (*n && i < 12) s[i++] = *n++;
        if (morph < 16384 && preset_edited()) s[i++] = '*';
        s[i] = 0; title = s;
    }
    w = text_w(&FONT_S, title); x0 = (240 - w) / 2;
    int32_t rl = PAGE_GROUP[page][1] > 1u ? 32 : 8, rr = page == PG_MIDI ? 232 - text_w(&FONT_S, "V" APP_VER) : 240 - 8;
    cv_begin(240, 24, C_PANEL);
    cv_rect(rl, 12, x0 - 6 - rl, 1, C_RULE);
    cv_rect(x0 + w + 6, 12, rr - (x0 + w + 6), 1, C_RULE);
    cv_text(x0, 4, &FONT_S, title, save_mode ? KC[3] : page == PG_HOME ? (morph >= 16384 ? KC[2] : KC[0]) : C_SILK);
    if (page == PG_MIDI) cv_text(238 - text_w(&FONT_S, "V" APP_VER), 4, &FONT_S, "V" APP_VER, C_LEG);
    if (PAGE_GROUP[page][1] > 1u) {
        char t[4];
        t[0] = (char)('1' + page - PAGE_GROUP[page][0]); t[1] = '/'; t[2] = (char)('0' + PAGE_GROUP[page][1]); t[3] = 0;
        cv_text(2, 4, &FONT_S, t, C_LEG);
    }
    cv_blit(0, 0);
}
static void cv_arc(int32_t cx, int32_t cy, int32_t r, uint32_t from, uint32_t to, uint16_t c)   /* an arc, phases in turns (uint32), clockwise, 3 px thick */
{
    int32_t i, n = 2 + (int32_t)(((to - from) >> 24) * r >> 5);   /* a step every couple of pixels */
    for (i = 0; i <= n; i++) {
        uint32_t ph = from + ((((to - from) >> 8) / (uint32_t)n) * (uint32_t)i << 8);   /* (the parentheses matter: + binds tighter than <<) */
        cv_dot(cx + ((sine_i(ph + 0x40000000u) * r) >> 15), cy + ((sine_i(ph) * r) >> 15), 1, c);
    }
}
#define ARC_FROM 0x60000000u                    /* the dial's travel: from bottom-left, three quarters of a turn clockwise, to bottom-right */
#define ARC_SWEEP 0xC0000000u
static uint8_t knob_last[4];
static uint32_t knob_flare[4];                 /* when each dial's value last moved: it flares for 400 ms, so you see which knob the sequencer or the autopilot is turning */
static void draw_knob(uint32_t k)       /* a knob's cell: a dial in its colour, the value, the name */
{
    const pdesc_t *d = &PAGES[page].p[k];
    char s[8];
    cv_begin(60, 86, page == PG_HOME ? C_GROUND : C_PANEL);
    if (d->name) {
        uint32_t v = *param_ptr(d), mx = d->max ? d->max : 1u, sw = ARC_SWEEP / mx * v, age;
        uint16_t c = KC[k];
        int32_t w;
        if (v != knob_last[k]) { knob_last[k] = (uint8_t)v; knob_flare[k] = fm1_ms | 1u; }
        age = fm1_ms - knob_flare[k];
        if (knob_flare[k] && age < 400u) c = blend(KC[k], RGB(255, 255, 255), (400u - age) * 160u / 400u);
        cv_arc(30, 27, 17, ARC_FROM, ARC_FROM + ARC_SWEEP, C_RULE);
        if (v) cv_arc(30, 27, 17, ARC_FROM, ARC_FROM + sw, c);
        cv_dot(30 + ((sine_i(ARC_FROM + sw + 0x40000000u) * 17) >> 15), 27 + ((sine_i(ARC_FROM + sw) * 17) >> 15), knob_flare[k] && age < 400u ? 3 : 2, C_SILK);   /* the pointer */
        fmt_value(d, v, s);
        w = text_w(&FONT_S, s); cv_text(30 - (w >> 1), 19, &FONT_S, s, C_SILK);
        w = text_w(&FONT_S, d->name); cv_text(30 - (w >> 1), 60, &FONT_S, d->name, C_LEG);
    }
    cv_blit(60u * k, KY);
}
static void draw_graphic(void) { cv_begin(240, GH, C_PANEL); PAGES[page].draw(); cv_blit(0, GY); }
static int32_t decay_px(int32_t h, uint32_t e) { return e >> 10 > 14u ? 0 : (h * (int32_t)EXP2N_Q15[(e & 1023u) >> 2]) >> (15 + (int32_t)(e >> 10)); }   /* h x 2^(-e / 1024) */
static uint32_t sound_level(void)       /* the voices' envelopes summed, Q15, capped */
{
    uint32_t v, n = 0;
    for (v = 0; v < NV; v++) n += (uint32_t)voices[v].env >> 9;
    return n > 32767u ? 32767u : n;
}

/* HOME: the aurora. Three curtains, each a sum of two waves across the screen, bent by the movement's own sources
 * (the walk, the chaos, the tide) and lit by what is sounding: TILT the green, SPREAD the top colour, BRIGHT the
 * height, WIDTH the wavelength, RATE the drift, SPACE a haze, AIR sparks and stronger rays, FREEZE ice. Each held
 * note is a ray at its pitch. */
static const uint8_t AU_PROF[32] = {150, 255, 245, 232, 218, 204, 190, 176, 163, 150, 138, 126, 115, 104, 94, 84, 75, 66, 58, 50, 43, 37, 31, 26, 21, 17, 13, 10, 7, 5, 3, 1};
static uint32_t au_ph[3];
static int32_t au_glow;
static int16_t au_yc[3][240];
static uint8_t au_ray[240];
static void draw_aurora(void)
{
    static const int16_t BASE[3] = {98, 74, 54}, AMP1[3] = {14, 10, 8}, AMP2[3] = {6, 5, 4};
    uint16_t col[3][8];
    uint32_t c, x, y, kx, kx2, inc, lv = sound_level();
    int32_t th[3], step[3], off[3], ints, frz = (int32_t)freeze_now * 2, m = patch.move, v;
    int32_t pr = press, sh = bend_knob >> 2;      /* pressure lifts the sky and lights it; the bend slides it sideways, 64 px a semitone */
    { uint32_t v; for (v = 0; v < NV; v++) if (voices[v].gate && voices[v].at > pr) pr = voices[v].at; }   /* (a key leant on counts) */
    au_glow += ((int32_t)lv - au_glow) >> 2;
    ints = 150 + ((au_glow * 106) >> 15);                                                  /* the light: 150 at rest, full while sounding (the FM-1's panel is dim: the mids must sit well above its black) */
    ints = ints - ((ints * patch.rv_mix) >> 9);                                            /* the haze dims it a little */
    ints += pr * 80 / 127; if (ints > 255) ints = 255;
    if (patch.filter > 68u) ints -= ints * ((int32_t)patch.filter - 68) / 90;        /* the low-pass dims the sky */
    inc = (10000000u + (uint32_t)patch.rate * 1600000u) / 127u * (uint32_t)(127 - freeze_now);   /* the waves' drift: 40 s a cycle to 2 s, stopped by the ice */
    au_ph[0] += inc; au_ph[1] -= inc + (inc >> 1); au_ph[2] += inc >> 1;
    kx = 0xFFFFFFFFu / (120u + (uint32_t)patch.width * 240u / 127u); kx2 = kx * 2u + (kx >> 1);  /* the wavelength: 120 px narrow, 360 wide */
    off[0] = ((mv.src[0] * (20 + m * 40 / 127)) >> 15) - pr * 22 / 127; off[1] = ((mv.src[2] * (16 + m * 32 / 127)) >> 15) - pr * 16 / 127; off[2] = ((mv.src[5] * (12 + m * 24 / 127)) >> 15) - pr * 10 / 127;
    th[0] = 30 + patch.bright * 24 / 127 + patch.rv_mix * 16 / 127; th[1] = 24 + patch.rv_mix * 12 / 127; th[2] = 18 + patch.rv_mix * 8 / 127;
    for (c = 0; c < 3u; c++) {
        uint16_t lo, hi;
        uint32_t i;
        step[c] = (32 << 8) / th[c];
        if (c == 0) { lo = blend(RGB(0, 255, 100), RGB(160, 255, 200), (uint32_t)(127 - patch.tilt) * 2u); hi = blend(RGB(110, 40, 255), RGB(255, 30, 110), (uint32_t)patch.spread * 2u); }   /* saturated: the panel washes colour out */
        if (patch.filter < 60u) lo = blend(lo, hi, (uint32_t)(60 - patch.filter) * 4u);   /* the high-pass thins the green out of the bottom */
        else if (c == 1) { lo = RGB(0, 230, 255); hi = RGB(170, 50, 255); }
        else { lo = RGB(60, 110, 255); hi = RGB(255, 60, 200); }
        for (i = 0; i < 8u; i++) col[c][i] = blend(blend(lo, hi, i * 36u), RGB(190, 220, 255), (uint32_t)frz);
    }
    for (x = 0; x < 240u; x++) {
        uint32_t xs = (uint32_t)((int32_t)x + sh);                                           /* the sky, slid by the bend */
        int32_t r = 128 + ((sine_i(xs * (kx * 3u) + au_ph[1] * 2u) * (100 + patch.air * 100 / 127 + pr * 60 / 127)) >> 15);
        for (c = 0; c < 3u; c++) {
            int32_t w = (AMP1[c] * sine_i(xs * kx + au_ph[0] + c * 0x30000000u) + AMP2[c] * sine_i(xs * kx2 - au_ph[1] + c * 0x50000000u)) >> 15;
            au_yc[c][x] = (int16_t)(BASE[c] + off[c] + w);
        }
        au_ray[x] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
    }
    for (v = 0; v < (int32_t)NV; v++)                   /* a ray a note, at its pitch */
        if (voices[v].active) {
            int32_t xn = ((int32_t)voices[v].note - 36) * 4 + (bend_knob >> 6), e = voices[v].env >> 9, d;   /* (the ray at the bent pitch) */
            for (d = -5; d <= 5; d++) {
                int32_t xx = xn + d, b;
                if (xx < 0 || xx > 239) continue;
                b = au_ray[xx] + ((e * (6 - (d < 0 ? -d : d)) * 50) >> 15);
                au_ray[xx] = (uint8_t)(b > 255 ? 255 : b);
            }
        }
    for (y = 0; y < GH; y++) {
        uint16_t *row = cv_px + y * 240u;
        for (x = 0; x < 240u; x++) {
            uint16_t p = C_PANEL;
            int32_t hit = 0;
            for (c = 3; c-- > 0;) {                    /* the faint high one first, the main one over it */
                int32_t d = au_yc[c][x] - (int32_t)y, i, t;
                if (d < 0 || d >= th[c]) continue;
                i = (d * step[c]) >> 8;
                t = (((int32_t)AU_PROF[i] * au_ray[x]) >> 8) * ints >> 8;
                if (t > 255) t = 255;
                t = 255 - (((255 - t) * (255 - t)) >> 8);                                  /* lifted: the fade's mids brighter, the panel's black left black */
                p = blend(p, col[c][i >> 2], (uint32_t)t);
                hit = 1;
            }
            if (hit) row[x] = swap16(p);
        }
    }
    {   /* the ground: a ridge along the bottom the curtains dip behind, the dials standing on it below */
        uint32_t ky = 0x40000000u / 240u;
        for (x = 0; x < 240u; x++) {
            int32_t yr = 104 + ((sine_i(x * ky * 3u + 0x20000000u) * 5 + sine_i(x * ky * 7u + au_ph[2] / 64u) * 3) >> 15);
            for (y = (uint32_t)yr; y < GH; y++) cv_px[y * 240u + x] = swap16(C_GROUND);
            if (yr > 0) cv_px[(uint32_t)(yr - 1) * 240u + x] = swap16(blend(C_GROUND, KC[0], 40));   /* a thread of light along the edge */
        }
    }
    for (v = 0; v < (int32_t)NV; v++)                   /* the chord: a soft dot at the foot of each note's ray, pulsing with its envelope */
        if (voices[v].active) {
            int32_t xn = clamp(((int32_t)voices[v].note - 36) * 4 + (bend_knob >> 6), 4, 235), e = voices[v].env >> 9, yn = au_yc[0][xn] + 2;
            if (yn > 98) yn = 98;
            cv_dot(xn, yn, 3 + pr / 50, dim(RGB(220, 255, 240), 60u + (uint32_t)(e * 196 >> 15)));
            cv_dot(xn, yn, 1, dim(RGB(255, 255, 255), 120u + (uint32_t)(e * 135 >> 15)));
        }
    {   /* the name, large, top left on the sky: the preset's, starred once the sound has changed; scene B's while there */
        char s[16];
        int32_t i = 0;
        const char *n = preset_name(morph >= 16384 ? set.scene_b : set.preset);
        if (morph >= 16384) { s[i++] = 'B'; s[i++] = ':'; s[i++] = ' '; }
        while (*n && i < 12) s[i++] = *n++;
        if (morph < 16384 && preset_edited()) s[i++] = '*';
        s[i] = 0;
        cv_text(9, 7, &FONT_L, s, C_GROUND);        /* a shadow, then the name */
        cv_text(8, 6, &FONT_L, s, morph >= 16384 ? KC[2] : KC[0]);
    }
    {   /* stars, and the air's sparks */
        uint32_t s = 0x1234567u, i, n = 24u + (uint32_t)patch.air * 40u / 127u;
        for (i = 0; i < n; i++) {
            uint32_t r = i < 24u ? noise32(&s) : noise32(&ui_rng);
            cv_pset((int32_t)((r >> 8) % 240u), (int32_t)((r >> 16) % GH), dim(i < 24u ? C_SILK : RGB(230, 240, 255), i < 24u ? 110u + (r & 63u) : 140u + (r & 127u)));
        }
    }
}

/* TONE: the twelve partials as bars, the real shape; the tilt's slope, the brightness ceiling, the pairs */
static int32_t shape_level(uint32_t k, int32_t tilt16, int32_t spread, int32_t bright)
{
    int32_t lv = 32767, j;
    for (j = 0; j < (int32_t)k; j++) lv -= (lv * tilt16) >> 11;
    if (k & 1u) lv = mulq15(lv, 32767 - spread * 256); else lv = mulq15(lv, 32767 + spread * 256);
    if ((int32_t)k * 10 > bright) lv = mulq15(lv, clamp(32767 - ((int32_t)k * 10 - bright) * 400, 0, 32767));
    return lv / (2 + (int32_t)(k >> 1));
}
static void draw_tone(void)
{
    int32_t tilt16 = patch.tilt * 16, spread = (int32_t)patch.spread - 64, bright = patch.bright, k, top = 112, hf_prev = 0;
    cv_hline(4, 236, top + 1, C_RULE);
    for (k = 0; k < (int32_t)NP; k++) {
        int32_t lv = shape_level((uint32_t)k, tilt16, spread, bright) * 2, h = (lv * 100) >> 15, x = 8 + k * 19;
        int32_t flat = shape_level((uint32_t)k, 0, 0, 127) * 2, hf = (flat * 100) >> 15;
        uint16_t c = spread > 0 ? (k & 1 ? KC[1] : C_LEG) : spread < 0 ? (k & 1 ? C_LEG : KC[1]) : C_LEG;   /* the side SPREAD favours in its colour */
        if (h < 1) h = 1;
        if (x > 236 - 14) break;
        if (k < (int32_t)NPAIR && patch.detune) {                                           /* a pair: two bars a little apart */
            int32_t g = 1 + patch.detune * 4 / 127;
            cv_rect(x, top - h, 7 - g / 2, h, c); cv_rect(x + 7 + g, top - h, 7 - g / 2, h, KC[3]);
        } else cv_rect(x, top - h, 14, h, c);
        if (k) cv_line(x - 12, top - hf_prev - 1, x + 7, top - hf - 1, dim(KC[0], 90));   /* the untilted slope: a faint curve through where the tops would be */
        hf_prev = hf;
        cv_rect(x, top - h - 1, 14, 1, KC[0]);
    }
    {   /* BRIGHT: the ceiling, the shade over everything above it */
        int32_t xb = 8 + (bright / 10) * 19 + (bright % 10) * 19 / 10, x;
        for (x = xb + 3; x < 236; x += 3) cv_vline(x, 2, top - 1, dim(KC[2], 28u));
        cv_vline(xb, 2, top, KC[2]);
    }
}

/* TONE 2: the partials along a log frequency axis, where the harmonics would be and where STRETCH puts them; the sub
 * under the fundamental; TUNE's shift; the drive's curve */
static int32_t log2_q8(uint32_t x)              /* log2 of x (Q16), in 1/256 octave */
{
    int32_t e = 0;
    while (x >= 131072u) { x >>= 1; e++; }
    while (x < 65536u && x) { x <<= 1; e--; }
    return e * 256 + (int32_t)(((x - 65536u) * 256u) >> 16);   /* (the fraction linear: a bit flat mid-octave, fine for a picture) */
}
static void draw_tone2(void)
{
    int32_t k, base = 96, tilt16 = patch.tilt * 16, spread = (int32_t)patch.spread - 64;
    uint32_t S = (uint32_t)patch.stretch * patch.stretch * 4u / 5u;
    cv_hline(4, 236, base + 1, C_RULE);
    for (k = 0; k < (int32_t)NP; k++) {
        uint32_t r = ((uint32_t)(k + 1) << 16) + ((((uint32_t)(k + 1) << 16) >> 16) * ((S * (uint32_t)k) >> 1));   /* (k + 1)(1 + S k), Q16 */
        int32_t sh = ((int32_t)patch.tune - 12) * 34 / 12, xh = 24 + sh + log2_q8((uint32_t)(k + 1) << 16) * 34 / 256, xs = 24 + sh + log2_q8(r) * 34 / 256;   /* (the axis shifted by TUNE) */
        int32_t h = (shape_level((uint32_t)k, tilt16, spread, patch.bright) * 2 * 48) >> 15;
        if (xh < 236) cv_vline(xh, base - 6, base, C_RULE);                                 /* where the harmonic would be */
        if (xs < 236) { cv_vline(xs, base - (h < 2 ? 2 : h), base, KC[0]); if (xs != xh) cv_hline(xh, xs, base - 3, dim(KC[0], 90)); }
    }
    if (patch.sub) { int32_t h = patch.sub * 50 / 127; cv_rect(24 + ((int32_t)patch.tune - 12) * 34 / 12 - 34 - 3, base - h, 7, h, KC[1]); }   /* the sub, an octave left */
    cv_text(4, 100, &FONT_S, "-8VA", patch.sub ? KC[1] : C_LEG); cv_text(214, 100, &FONT_S, "6", C_LEG);
    {   /* TUNE: the transpose, as the axis's shift */
        int32_t t = (int32_t)patch.tune - 12;
        char tx[6]; int32_t n = t < 0 ? -t : t, q = 0;
        tx[q++] = t < 0 ? '-' : '+'; if (n >= 10) tx[q++] = (char)('0' + n / 10); tx[q++] = (char)('0' + n % 10); tx[q] = 0;
        cv_text(150, 6, &FONT_S, "TUNE", C_LEG); cv_text(196, 6, &FONT_S, tx, t ? KC[2] : C_LEG);
    }
    {   /* DRIVE: the curve in to out */
        int32_t x, d8 = 256 + (int32_t)patch.sat * patch.sat, k = (9681 << 15) / softclip((10000 * d8) >> 8);
        cv_hline(8, 56, 40, C_RULE); cv_vline(8, 6, 40, C_RULE);
        for (x = 0; x < 48; x++) {
            int32_t in = x * 32767 / 47, y = mulq15(softclip((in * d8) >> 8), k);
            cv_pset(8 + x, 40 - (y * 34 >> 15), KC[3]);
        }
    }
}

/* METAL: the partials on TONE 2's axis, and where RING sends each: its sum and difference with the modulator (blue, at RATIO) */
static void draw_metal(void)
{
    int32_t k, base = 96, tilt16 = patch.tilt * 16, spread = (int32_t)patch.spread - 64, ring = patch.ring * 258, sh = ((int32_t)patch.tune - 12) * 34 / 12;
    uint32_t S = (uint32_t)patch.stretch * patch.stretch * 4u / 5u, ratio = exp2_q15((uint32_t)patch.ratio * 32u) << 1;   /* Q16 */
    cv_hline(4, 236, base + 1, C_RULE);
    for (k = 0; k < (int32_t)NP; k++) {
        uint32_t r = ((uint32_t)(k + 1) << 16) + ((((uint32_t)(k + 1) << 16) >> 16) * ((S * (uint32_t)k) >> 1));   /* (k + 1)(1 + S k), Q16 */
        int32_t h = (shape_level((uint32_t)k, tilt16, spread, patch.bright) * 2 * 48) >> 15, hs = (h * ring) >> 16, hk = h - hs;
        int32_t x = 24 + sh + log2_q8(r) * 34 / 256, xu = 24 + sh + log2_q8(r + ratio) * 34 / 256;
        uint32_t dr = r > ratio ? r - ratio : ratio - r;
        if (x < 236) cv_vline(x, base - (hk < 1 ? 1 : hk), base, ring ? dim(C_SILK, 160) : KC[0]);
        if (hs > 0) {
            if (xu < 236) cv_vline(xu, base - hs, base, KC[0]);
            if (dr > 1024u) { int32_t xd = 24 + sh + log2_q8(dr) * 34 / 256; if (xd >= 4 && xd < 236) cv_vline(xd, base - hs, base, KC[0]); }
        }
    }
    {   /* the modulator: a marker at RATIO x the pitch */
        int32_t x = 24 + sh + log2_q8(ratio) * 34 / 256;
        if (x >= 4 && x < 236) { cv_vline(x, base - 58, base, KC[1]); cv_dot(x, base - 60, 2, KC[1]); }
    }
    cv_text(4, 100, &FONT_S, "1", C_LEG); cv_text(214, 100, &FONT_S, "6", C_LEG);
    cv_text(150, 6, &FONT_S, "RING", patch.ring ? KC[0] : C_LEG);
}

/* SHAPE: the envelope. A linear attack, an exponential decay to the sustain, held, an exponential release; the
 * loudest voice's place on it, live. */
static void draw_shape(void)
{
    int32_t ax = 6 + patch.attack * 60 / 127, dx = 6 + patch.decay * 60 / 127, rx = 10 + patch.release * 80 / 127, hold = 30, x0 = 10, base = 108, top = 30;
    int32_t ys = base - ((base - top) * patch.sustain) / 127, xd = x0 + ax, xs = xd + dx, xr = xs + hold, i;
    cv_line(x0, base, xd, top, KC[0]);                        /* the attack */
    for (i = 0; i < dx; i++)                                  /* the decay: 2^(-6 i / dx) of the way down to the sustain */
        cv_line(xd + i, ys - decay_px(ys - top, (uint32_t)(i * 6 * 1024 / dx)), xd + i + 1, ys - decay_px(ys - top, (uint32_t)((i + 1) * 6 * 1024 / dx)), KC[1]);
    cv_hline(xs, xr, ys, KC[2]);                              /* the sustain */
    for (i = 0; i < rx && xr + i < 238; i++)                  /* the release */
        cv_line(xr + i, base - decay_px(base - ys, (uint32_t)(i * 6 * 1024 / rx)), xr + i + 1, base - decay_px(base - ys, (uint32_t)((i + 1) * 6 * 1024 / rx)), KC[3]);
    cv_hline(4, 236, base + 1, C_RULE);
    cv_vline(xr, ys, base, dim(KC[2], 70));
    {   /* where the loudest voice is */
        int32_t v, best = -1, e = 0;
        for (v = 0; v < (int32_t)NV; v++) if (voices[v].active && voices[v].env > e) { e = voices[v].env; best = v; }
        if (best >= 0) {
            int32_t x, y = base - (((e >> 9) * (base - top)) >> 15), sus = (int32_t)patch.sustain * 132104, lg = 0, q;
            if (voices[best].stage == 1) x = x0 + ((e >> 9) * ax >> 15);
            else if (voices[best].stage == 2) {
                if (e > sus + 2048) { q = e - sus; while (q < (1 << 24) && lg < 24) { q <<= 1; lg++; } x = xd + lg * dx / 9; }
                else x = xs + ((int32_t)(voices[best].t > 4000u ? 4000u : voices[best].t) * hold / 4000);
            } else { q = e; while (q < (1 << 24) && lg < 24) { q <<= 1; lg++; } x = xr + lg * rx / 20; }
            cv_dot(x > 236 ? 236 : x, y, 3, C_SILK);
        }
    }
}

/* TOUCH: BLOOM the partials' entries in turn, SWELL how far pressure lifts the sound, VELO how much a soft note
 * gives, VOLUME */
static void draw_touch(void)
{
    int32_t k, x, base = 108, top = 30;
    for (k = 0; k < (int32_t)NP; k++) {                       /* BLOOM: when each partial comes in, a stair */
        int32_t xb = 8 + k * 7, h = patch.bloom ? 4 + (int32_t)patch.bloom * k * 68 / (127 * 11) : 4;
        cv_rect(xb, base - 4, 5, 4, dim(KC[0], 120));
        cv_vline(xb + 2, base - h, base - 4, KC[0]);
        cv_rect(xb, base - h - 2, 5, 2, KC[0]);
    }
    cv_text(8, 2, &FONT_S, "BLOOM", C_LEG);
    {   /* SWELL: pressure's reach, a lift above the held line */
        int32_t ys = base - 30, yr = ys - patch.swell * 40 / 127;
        cv_hline(106, 150, ys, C_SILK);
        for (x = 106; x < 150; x += 6) cv_hline(x, x + 2, yr, KC[1]);
        cv_line(128, ys - 2, 128, yr + 2, dim(KC[1], 160));
        cv_text(106, 2, &FONT_S, "SWELL", C_LEG);
    }
    {   /* VELO: level against velocity, a line from the soft end to the top */
        int32_t y0 = base - ((base - top) * (127 - patch.velo)) / 127;
        cv_hline(170, 232, base + 1, C_RULE); cv_vline(170, top, base, C_RULE);
        cv_line(170, y0, 232, top, KC[2]);
        cv_text(170, 2, &FONT_S, "VELO", C_LEG);
    }
    cv_rect(8, 116, 92, 4, C_SLOT); cv_rect(8, 116, patch.volume * 92 / 127, 4, KC[3]);   /* VOLUME */
}

/* SWARM: five saws fanned apart, under the filter's curve */
static void draw_swarm(void)
{
    int32_t k, x, lvl = 40 + patch.sw_level * 216 / 127, fan = 3 + patch.sw_spread * 9 / 127;
    int32_t xc = 24 + patch.sw_cut * 190 / 127, bump = patch.sw_res * 28 / 127;
    for (k = 0; k < (int32_t)NSW; k++) {
        int32_t yc = 86 + (k - 2) * fan, ph = k * 9;
        uint16_t c = dim(k == 2 ? KC[0] : KC[1], (uint32_t)lvl);
        for (x = 8; x < 230; x++) {
            int32_t p = (x + ph) % 12, y0 = yc + 6 - p * 12 / 11;
            if (p == 11) cv_vline(x + 1, yc - 6, yc + 6, c); else cv_line(x, y0, x + 1, yc + 6 - (p + 1) * 12 / 11, c);
        }
    }
    for (x = 8; x < 232; x++) {                               /* the low-pass: flat, a bump at the cutoff, then falling */
        int32_t d = x - xc, y;
        if (d < -16) y = 28;
        else if (d <= 0) y = 28 - bump * (16 + d) / 16;
        else y = 28 - bump + d * d / 40 + d / 2;
        if (y > 70) break;
        cv_pset(x, y, d > -16 && d <= 4 ? KC[3] : KC[2]);
        cv_pset(x, y + 1, dim(d > -16 && d <= 4 ? KC[3] : KC[2], 120));
    }
    cv_vline(xc, 30 - bump, 68, dim(KC[2], 90));
}

/* AIR: the wind, a trace that breathes; the band's hump at TONE; the crackle's sparks */
static void draw_air(void)
{
    int32_t x, amp, lvl = ((int32_t)patch.air * patch.air * patch.air) >> 6, y = 70, xt = 20 + patch.air_tone * 200 / 127;
    uint32_t i, n = (uint32_t)patch.air_tex * 30u / 127u;
    amp = 2 + ((lvl * mulq15(air.env, air.gain)) >> 15) * 40 / 32005;
    for (x = 8; x < 232; x++) {                               /* a random walk, wider by the level, fresh each frame */
        int32_t ny = y + (int32_t)(noise32(&ui_rng) >> 29) - 3;
        if (ny < 70 - amp) ny = 70 - amp; if (ny > 70 + amp) ny = 70 + amp;
        cv_line(x, y, x + 1, ny, dim(KC[0], 120u + (uint32_t)lvl * 136u / 32005u)); y = ny;
    }
    for (x = 8; x < 232; x++) {                               /* the band-pass: a hump around TONE */
        int32_t d = x - xt, h = 30 * 400 / (400 + d * d / 4);
        cv_pset(x, 40 - h, KC[1]);
    }
    cv_vline(xt, 8, 40, dim(KC[1], 100));
    for (i = 0; i < n; i++) {                                 /* the crackle */
        uint32_t r = noise32(&ui_rng);
        cv_dot(8 + (int32_t)((r >> 8) % 224u), 70 - amp - 2 - (int32_t)((r >> 16) % 12u), (int32_t)(r & 1u), KC[2]);
    }
    {   /* the breath: a bar filling and emptying with it */
        int32_t w = (air.gain * 60) >> 15;
        cv_rect(170, 110, 62, 5, C_SLOT); cv_rect(171, 111, w, 3, KC[3]);
        cv_text(170 - text_w(&FONT_S, "BREATH") - 4, 104, &FONT_S, "BREATH", C_LEG);
    }
}

/* MOVE: the four sources scrolling, twelve seconds of them; DRIFT a partial's own walk; WIDTH the stereo */
static int8_t hist[5][120];
static uint32_t hist_n;
static void hist_tick(void)             /* every 100 ms, whatever page */
{
    int32_t v[5], k, n = 0;
    v[0] = 0;
    for (k = 0; k < (int32_t)NV; k++) if (voices[k].active) { v[0] += voices[k].wander; n++; }
    v[0] = n ? v[0] / n : 0;
    v[1] = mv.src[0]; v[2] = mv.src[2]; v[3] = mv.src[5]; v[4] = mulq15(voices[0].walk[1], 32767);
    for (k = 0; k < 5; k++) hist[k][hist_n % 120u] = (int8_t)(clamp(v[k], -16383, 16383) >> 7);
    hist_n++;
}
static void draw_traces(const uint16_t *cols, const int32_t *scale, int32_t rows, int32_t x0, int32_t w)
{
    static const char *const NAMES[4] = {"WANDER", "WALK", "CHAOS", "TIDE"};
    int32_t k, i;
    for (k = 0; k < rows; k++) {
        int32_t yc = 14 + k * 28, py = yc;
        cv_hline(x0, x0 + w, yc, C_RULE);
        cv_text(0, yc - 8, &FONT_S, NAMES[k], C_LEG);
        for (i = 0; i < w; i++) {                              /* the source itself, dim; what MOVE lets through, bright: both bent into the lane */
            int32_t h = hist[k][(hist_n + 120u - (uint32_t)w + (uint32_t)i) % 120u] * 256, y = yc - (softclip(h) * 12 >> 15);
            cv_line(x0 + i, i ? py : y, x0 + i + 1, y, dim(cols[k], 90)); py = y;
        }
        for (i = 0; i < w; i++) {
            int32_t h = hist[k][(hist_n + 120u - (uint32_t)w + (uint32_t)i) % 120u] * 256, y = yc - (softclip(mulq15(h, scale[k])) * 12 >> 15);
            cv_line(x0 + i, i ? py : y, x0 + i + 1, y, cols[k]); py = y;
        }
    }
}
static void draw_move(void)
{
    uint16_t cols[4];
    int32_t scale[4], k, i, py = 112;
    for (k = 0; k < 4; k++) { cols[k] = dim(KC[0], 120u + (uint32_t)patch.move); scale[k] = mv_gain; }
    draw_traces(cols, scale, 4, 64, 110);
    for (i = 0; i < 50; i++) {                                /* DRIFT: one partial's walk */
        int32_t y = 112 - (softclip(hist[4][(hist_n + 70u + (uint32_t)i) % 120u] * patch.drift * 4) * 10 >> 15);
        cv_line(180 + i, i ? py : y, 181 + i, y, KC[2]); py = y;
    }
    cv_text(180, 86, &FONT_S, "DRIFT", C_LEG);
    {   /* WIDTH: L and R spreading from the centre */
        int32_t s = 2 + patch.width * 48 / 127;
        cv_hline(60, 160, 118, C_RULE);
        cv_dot(110 - s, 118, 2, KC[3]); cv_dot(110 + s, 118, 2, KC[3]);
        cv_text(40, 110, &FONT_S, "L", C_LEG); cv_text(166, 110, &FONT_S, "R", C_LEG);
    }
    cv_text(180, 2, &FONT_S, "RATE", C_LEG);
    cv_rect(180, 20, 50, 5, C_SLOT); cv_rect(181, 21, patch.rate * 48 / 127, 3, KC[1]);
}
/* SOURCES and TARGETS: the matrix. Rows the sources, live; columns the targets; a dot where a source drives a
 * target, as big as the amount in, the depth out */
static void draw_matrix(void)
{
    static const char *const SRC[4] = {"WANDER", "WALK", "CHAOS", "TIDE"}, *const DST[4] = {"TILT", "COLOUR", "PITCH", "SPACE"};
    static const uint8_t SI[4] = {7, 0, 2, 5};                /* the pattern's column a source (wander: a voice's own) */
    const uint8_t *amt = &patch.src_wander, *dep = &patch.to_tilt;
    int32_t r, c, x0 = 76, y0 = 34, cw = 40, rh = 22;
    for (c = 0; c < 4; c++) {                                 /* the targets' names on two lines: wider than their columns */
        int32_t x = x0 + c * cw + cw / 2;
        cv_text(x - (text_w(&FONT_S, DST[c]) >> 1), c & 1 ? 15 : 0, &FONT_S, DST[c], page == PG_DST ? KC[c] : C_LEG);
        cv_vline(x, c & 1 ? 31 : 16, 31, C_RULE);
    }
    for (r = 0; r < 4; r++) {
        int32_t live = r == 0 ? hist[0][(hist_n + 119u) % 120u] : mv.src[SI[r]] >> 7, y = y0 + r * rh + rh / 2;
        cv_text(0, y - 8, &FONT_S, SRC[r], page == PG_SRC ? KC[r] : C_LEG);
        cv_hline(x0 - 6, x0 + 4 * cw, y, C_RULE);
        for (c = 0; c < 4; c++) {
            int32_t on = SI[r] == 7u ? (c == 0 || c == 1 || c == 2) : MV_PATTERN[c][SI[r]] != 0;   /* wander goes to tilt, colour, pitch in the voice */
            int32_t sz = on ? (int32_t)amt[r] * dep[c] / (127 * 127 / 7) : 0, x = x0 + c * cw + cw / 2;
            if (!on) { cv_pset(x, y, C_RULE); continue; }
            cv_dot(x, y, sz + 1, dim(page == PG_SRC ? KC[r] : KC[c], 140u + (uint32_t)sz * 16u));
        }
        cv_dot(x0 - 6, y - (live * 10 >> 7), 2, page == PG_SRC ? KC[r] : C_SILK);   /* the source now, as a float on its rail */
    }
}

/* SPACE: the tail, falling as SIZE says; DAMP the highs under it, falling faster; SHIMMER the sparks climbing */
static void draw_space(void)
{
    int32_t x, len = 16 + patch.rv_size * 200 / 127, len2 = len - len * patch.rv_damp / 180, base = 108, h0 = 20 + patch.rv_mix * 60 / 127;
    uint32_t i, n = (uint32_t)patch.rv_shim * 24u / 127u;
    cv_hline(4, 236, base + 1, C_RULE);
    for (x = 0; x < 224; x++) {
        int32_t h = decay_px(h0, (uint32_t)(x * 6 * 1024 / len)), h2 = decay_px(h0, (uint32_t)(x * 6 * 1024 / (len2 < 4 ? 4 : len2)));
        cv_vline(8 + x, base - h, base, dim(KC[1], 70u + (uint32_t)patch.rv_mix));
        cv_vline(8 + x, base - h2, base, dim(KC[2], 110u + (uint32_t)patch.rv_mix));
        if (h) cv_pset(8 + x, base - h - 1, KC[0]);
    }
    for (i = 0; i < n; i++) {                                 /* the shimmer: sparks climbing out of the tail */
        uint32_t r = noise32(&ui_rng);
        int32_t x1 = 8 + (int32_t)((r >> 8) % 200u), y1 = base - decay_px(h0, (uint32_t)((x1 - 8) * 6 * 1024 / len)) - 4 - (int32_t)((r >> 20) % 30u);
        cv_pset(x1, y1, dim(KC[3], 120u + (r & 127u)));
    }
}

/* DELAY: the repeats, spaced by TIME, dying by FEEDBACK, as loud as MIX; TAPE leans them over */
static void draw_delay(void)
{
    int32_t sp = dly_synced ? 14 + (int32_t)(sync_dly * 44u / DLY_LEN) : 14 + patch.dly_time * 44 / 127, h = 80, x = 12, base = 108, i, tape = patch.dly_tape;
    uint32_t fb = (uint32_t)patch.dly_fb * 250u;
    cv_hline(4, 236, base + 1, C_RULE);
    cv_rect(x, base - h, 6, h, C_SILK);                      /* the note itself */
    h = patch.dly_mix ? h * patch.dly_mix / 127 : 60;                                        /* (off: ghosts, so the page still shows the time and feedback) */
    for (i = 1; i < 16 && x + sp * i < 232; i++) {
        int32_t xi = x + sp * i, lean = tape ? ((sine_i((uint32_t)i * 0x30000000u) * tape) >> 15) / 24 : 0, hi;
        h = (int32_t)(((uint32_t)h * fb) >> 15);
        if (h < 2) break;
        hi = patch.dly_ping && (i & 1) ? h * 3 / 4 : h;
        cv_rect(xi + lean, base - hi, 6, hi, dim(KC[2], patch.dly_mix ? 120u + (uint32_t)hi * 2u : 50u));
        cv_rect(xi + lean, base - hi, 6, 1, KC[1]);
        if (i == 1) { cv_hline(x + 6, xi + lean, base - 6, KC[0]); cv_vline(xi + lean, base - 8, base - 4, KC[0]); }
    }
    if (tape) { int32_t w = 10 + tape * 60 / 127; cv_hline(230 - w, 230, 10, KC[3]); cv_text(230 - w - text_w(&FONT_S, "TAPE") - 4, 2, &FONT_S, "TAPE", C_LEG); }
}

/* FREEZE: the delay line, its write head, and the grains on it, live when frozen; SPRAY the reach, SHIFT the lift */
static void draw_freeze(void)
{
    int32_t y = 70, head = (int32_t)(dly_w * 240u / DLY_LEN), reach = (int32_t)(frz_spray * 240u / DLY_LEN), lift = ((int32_t)patch.frz_shift - 12) * 2, g, i;
    uint32_t spray = (882u * exp2_q15((uint32_t)patch.frz_spray * 46u)) >> 15, len = (441u * exp2_q15((uint32_t)patch.frz_grain * 45u)) >> 15;
    reach = (int32_t)(spray * 240u / DLY_LEN);
    cv_rect(0, y - 2, 240, 5, dim(KC[0], 60u + (uint32_t)freeze_now));
    cv_vline(head, y - 8, y + 8, KC[0]);
    for (i = head - reach; i < head; i += 4) cv_pset(((i % 240) + 240) % 240, y + 10, KC[2]);   /* SPRAY: where grains may come from */
    for (g = 0; g < (int32_t)NGRAIN; g++) {
        int32_t x, w, h;
        if (frz_now) {                                        /* the real grains */
            x = (int32_t)((grain[g].pos >> 16) * 240u / DLY_LEN); w = (int32_t)((0xFFFFFFFFu / grain[g].winc) * 240u / DLY_LEN);
            h = (32767 - SINE_RAM[((grain[g].wph >> 22) + 256u) & 1023u]) >> 1;
        } else {                                              /* a picture of them */
            uint32_t s = 0x7777u + (uint32_t)g * 977u; x = ((head - (int32_t)((noise32(&s) >> 8) % (uint32_t)(reach + 1))) % 240 + 240) % 240;
            w = (int32_t)(len * 240u / DLY_LEN); h = 16000 + (int32_t)(noise32(&s) >> 18);
        }
        if (w < 3) w = 3;
        for (i = 0; i <= w; i++) {                            /* a Hann hump, lifted by SHIFT */
            int32_t a = (32767 - SINE_RAM[((uint32_t)(i * 1024 / (w + 1)) + 256u) & 1023u]) >> 1, hh = (a * h >> 15) * 36 >> 15;
            int32_t xx = ((x + i) % 240 + 240) % 240;
            cv_vline(xx, y - 3 - lift - hh, y - 3 - lift, dim(KC[1], 100u + (uint32_t)(h >> 7)));
        }
    }
    if (lift) cv_text(200, lift > 0 ? 100 : 6, &FONT_S, lift > 0 ? "UP" : "DOWN", KC[3]);
}

/* MIDI: what it listens to */
static void draw_midi(void)
{
    char s[12];
    cv_text(8, 2, &FONT_S, "CHANNEL", C_LEG);
    if (set.midi_chan) { s[0] = (char)('0' + set.midi_chan / 10u); s[1] = (char)('0' + set.midi_chan % 10u); s[2] = 0; cv_text(100, 2, &FONT_S, set.midi_chan < 10u ? s + 1 : s, KC[0]); }
    else cv_text(100, 2, &FONT_S, "OMNI", KC[0]);
    cv_text(8, 23, &FONT_S, "SWELL BY", C_LEG);
    if (set.expr) { s[0] = 'C'; s[1] = 'C'; s[2] = ' '; fmt_value(&PAGES[PG_MIDI].p[2], set.expr, s + 3); cv_text(100, 23, &FONT_S, s, KC[1]); }
    else cv_text(100, 23, &FONT_S, "AFTERTOUCH", KC[1]);
    cv_text(8, 44, &FONT_S, "MACROS", C_LEG); cv_text(100, 44, &FONT_S, "CC 80..87", C_SILK);
    cv_text(8, 65, &FONT_S, "CLOCK", C_LEG); cv_text(100, 65, &FONT_S, fm1_ms - clk_last < 2000u ? "SYNCED" : "NONE", C_SILK);
    cv_text(8, 86, &FONT_S, "IN", C_LEG); cv_dot(106, 94, 4, fm1_ms - last_midi_ms < 200u ? KC[0] : C_SLOT);
    {   /* along the bottom: the octave, the voices sounding, the cores' load */
        char t[32] = "OCT +0  0 VOICES  LOAD 00/00%";
        uint32_t v, n = 0, l = load_now > 99u ? 99u : load_now, w = wload_now > 99u ? 99u : wload_now;
        int32_t o = (int32_t)set.oct_plus2 - 2;
        for (v = 0; v < NV; v++) if (voices[v].active) n++;
        t[4] = o < 0 ? '-' : '+'; t[5] = (char)('0' + (o < 0 ? -o : o)); t[8] = (char)('0' + n);
        t[23] = (char)('0' + l / 10u); t[24] = (char)('0' + l % 10u); t[26] = (char)('0' + w / 10u); t[27] = (char)('0' + w % 10u);
        cv_text(4, 107, &FONT_S, t, n == NV ? KC[3] : C_LEG);
    }
}

static uint32_t page_filled = ~0u;
static void draw_page(void)
{
    uint32_t k;
    if (page != page_filled) {                     /* the background only when the page changes: a preset change redraws the strips over it, no flash */
        page_filled = page;
        lcd_fill(0, 0, 240, 240, page == PG_HOME ? C_GROUND : C_PANEL);
        if (page == PG_HOME) lcd_fill(0, 0, 240, GY + GH, C_PANEL);   /* (the ground runs from the sky's bottom row down) */
    }
    for (k = 0; k < 4u; k++) { const pdesc_t *d = &PAGES[page].p[k]; knob_last[k] = d->name ? *param_ptr(d) : 0u; knob_flare[k] = 0; }   /* (a page change is no flare) */
    draw_header();
    draw_graphic();
    for (k = 0; k < 4u; k++) draw_knob(k);
}

/* ---- MIDI clock: the delay's SYNC follows it */
static uint32_t clk_n, clk_t0, beat_ms = 500;
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

/* ---- the autopilot (SEQ): with it on, nine of the sound's controls wander slowly round where they are set, a new
 * aim every half minute or so, reached over ten seconds; set.patch stays the sound as set, patch is the sound as
 * played, and the knobs on the screen show it drifting */
#define NAUTO 9u
static const uint8_t AUTO_OFF[NAUTO] = {offsetof(patch_t, tilt), offsetof(patch_t, bright), offsetof(patch_t, spread), offsetof(patch_t, sw_cut), offsetof(patch_t, air),
                                        offsetof(patch_t, rv_mix), offsetof(patch_t, width), offsetof(patch_t, air_tone), offsetof(patch_t, rate)};
static uint8_t autopilot;
static int32_t auto_v[NAUTO], auto_aim[NAUTO];
static uint32_t auto_rng = 0x2545F491u;
static void auto_tick(void)                      /* every 100 ms */
{
    uint32_t k;
    for (k = 0; k < NAUTO; k++) {
        uint32_t r = noise32(&auto_rng);
        if ((r & 255u) == 0u || auto_aim[k] == 99) auto_aim[k] = (int32_t)((r >> 16) % 57u) - 28;   /* a new aim, +-28, about every 25 s; at once when just switched on */
        auto_v[k] += ((auto_aim[k] << 8) - auto_v[k]) >> 7;
        ((uint8_t *)&patch)[AUTO_OFF[k]] = (uint8_t)clamp((int32_t)((uint8_t *)&set.patch)[AUTO_OFF[k]] + (auto_v[k] >> 8), 0, 127);
    }
}
/* ---- the main loop */
static uint32_t prev_keys, t_change, dirty, page_drawn = ~0u, strip_ms, last_midi_ms, loud_ms, midi_redraw, arp_was, arp_turned;
static uint8_t held[27], sustain, sus_held[128 / 8];
/* the keys' own pressure (Velo, after CS-1's key_lean): the lean on a held key, 0..127, counted from where the key RESTS, not
 * where it landed (a key goes on settling after the note). The rest follows the driven reading down quickly for 250 ms, then
 * creeps after it, and comes back up at once when the key is eased; the lean is the pill's resistance below its rest on a
 * log scale: 0.04 octave is noise, 0.35 more is full (a soft and a firm press measured a fifth of an octave apart). */
static uint32_t p_base8[27], p_t0[27], at_poll_ms, key_sense_applied = 0xFFu;
static int32_t p_s[27];
static uint8_t p_on[27], key_at_sent[27];
static uint32_t key_lean(uint32_t k)
{
    uint32_t d, base, dead = 10, span = 90;
    int32_t lean, x;
    if (sn_key[k].st != SN_DOWN || sn_key[k].d <= SN_DBASE) { p_on[k] = 0; p_s[k] = 0; return 0; }
    d = sn_key[k].d > SN_DBASE + 1u ? sn_key[k].d - SN_DBASE : 1u;
    if (d > 1023u) d = 1023u;
    if (!p_on[k]) { p_on[k] = 1; p_base8[k] = d << 8; p_t0[k] = fm1_ms; }
    if ((d << 8) > p_base8[k]) p_base8[k] = d << 8;
    else p_base8[k] -= ((p_base8[k] - (d << 8)) * (fm1_ms - p_t0[k] < 250u ? 28672u : 105u)) >> 16;
    base = p_base8[k] >> 8; if (base > 1023u) base = 1023u; if (base < 1u) base = 1u;
    lean = (int32_t)LOG2_Q8[base] - (int32_t)LOG2_Q8[d];
    x = ((lean - (int32_t)dead) << 15) / (int32_t)span;
    x = x < 0 ? 0 : x > 32767 ? 32767 : x;
    p_s[k] += ((x - p_s[k]) * 10158) >> 15;
    return (uint32_t)(p_s[k] * 127 + 16384) >> 15;
}
static void keys_apply(void)                        /* KEYS into the sensing module and the scan: Velo's DRIVE 2, its curve and lightest press as CS-1 settled on */
{
    sn_cfg.drive = 2; sn_cfg.d_curve = 70; sn_cfg.d_ceil = 897;
    keys_sense_set(set.key_sense, 2);
    key_sense_applied = set.key_sense;
}
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
    if (bank_load(bank, sizeof bank)) memset(bank, 0, sizeof bank);
    if (settings_load(&set, sizeof set) || set.magic != SET_MAGIC || set.oct_plus2 > 4u || set.preset >= NALL || set.scene_b >= NALL) {
        settings_v1_t old;
        if (!settings_load(&old, sizeof old) && old.magic == SET_MAGIC && old.oct_plus2 <= 4u && old.preset < NALL && old.scene_b < NALL) {
            memcpy(&set, &old, sizeof old);               /* settings from up to 0.7.6: the same, with B rebuilt from its preset */
            preset_build(set.scene_b, &set.patch_b);
        } else {
            set.magic = SET_MAGIC; set.oct_plus2 = 2; set.midi_chan = 0; set.expr = 1; set.latch = 0; set.preset = 0; set.scene_b = 1; set.key_sense = 1;
            preset_build(0, &set.patch);
            preset_build(1, &set.patch_b);
        }
    }
    sense_init(keys_analog_mask());
    keys_apply();
    patch = set.patch;
    lcd_fill(0, 0, 240, 240, C_PANEL);
}
static void key_down(uint32_t k, uint32_t n, uint32_t vel)   /* a key pressed: its note, unless it was latched (then let go) */
{
    if (set.latch && (sus_held[n >> 3] >> (n & 7u) & 1u)) {
        sus_held[n >> 3] &= (uint8_t)~(1u << (n & 7u)); voice_off(n); midi_send(0x80, n, 0); held[k] = 0xFF;
    } else { held[k] = (uint8_t)n; key_at_sent[k] = 0; note_on(n, vel); midi_send(0x90, n, vel); }
}
static void key_up(uint32_t k)
{
    if (held[k] != 0xFF) { note_off(held[k]); midi_send(0x80, held[k], 0); }
}
static void app_loop(void)
{
    uint32_t keys, down, up, b, k, lit = 0, sounding, live;
    uint32_t base = (uint32_t)(KEY0 + 12 * oct());
    uint8_t st, d1, d2;
    int32_t n;
    if (platform_screen_take()) page_drawn = page_filled = ~0u;   /* (the screen was lost: everything again) */
    keys = fm1_in.notes; down = keys & ~prev_keys; up = prev_keys & ~keys; prev_keys = keys;
    if (set.key_sense != key_sense_applied) keys_apply();
    live = keys_sense_live() && set.key_sense;        /* the keys' own velocity and pressure: the sensed keys' notes come from the sensing */
    for (k = 0; k < 27u; k++) {
        if (live && sense_is_analog(k)) continue;
        if (down >> k & 1u) key_down(k, base + k, 100);
        if (up >> k & 1u) key_up(k);
    }
    {   /* the sensed keys: landed with a velocity, lifted */
        sense_ev_t e;
        while (sense_take(&e)) {
            if (!live || e.key >= 27u) continue;
            if (e.type == EV_ON) key_down(e.key, base + e.key, e.vel ? e.vel : 1u);
            else if (e.type == EV_OFF) key_up(e.key);
        }
        if (live && fm1_ms - at_poll_ms >= 10u) {       /* the held keys' pressure, a hundred times a second: each its own note's swell */
            at_poll_ms = fm1_ms;
            for (k = 0; k < 27u; k++)
                if (held[k] != 0xFF && sense_is_analog(k)) {
                    uint32_t p = key_lean(k);
                    if (p != key_at_sent[k]) { key_at_sent[k] = (uint8_t)p; voices_pressure(held[k], p); midi_send(0xA0, held[k], p); }
                }
        }
    }
    b = fm1_input_edges(&k);
    if (b & (1u << BTN_OCT_DOWN) && set.oct_plus2 > 0u) { set.oct_plus2--; changed(); }
    if (b & (1u << BTN_OCT_UP) && set.oct_plus2 < 4u) { set.oct_plus2++; changed(); }
    if (b & (1u << BTN_HOME)) page = PG_HOME;
    if (b & (1u << BTN_EDIT)) page = page >= PG_TONE && page < PG_AIR ? page + 1u : PG_TONE;
    if (b & (1u << BTN_ENV)) page = page == PG_SHAPE ? PG_TOUCH : PG_SHAPE;
    if (b & (1u << BTN_LFO)) page = page >= PG_MOVE && page < PG_DST ? page + 1u : PG_MOVE;
    if (b & (1u << BTN_FX)) page = page >= PG_SPACE && page < PG_FREEZE ? page + 1u : PG_SPACE;
    if (b & (1u << BTN_GLO)) page = PG_MIDI;
    if (b & (1u << BTN_SEQ)) {                                           /* AUTO: the sound wanders on its own */
        autopilot ^= 1u;
        if (autopilot) { for (k = 0; k < NAUTO; k++) { auto_v[k] = 0; auto_aim[k] = 99; } } else patch = set.patch;   /* on: from where it is set, aims drawn at once; off: back to the sound as set */
        page_drawn = ~0u;
    }
    if (b & (1u << BTN_SAVE)) {                                          /* SAVE: once to pick a slot (PRESETS), again to write it */
        if (!save_mode) { save_mode = 1; save_slot = set.preset >= NPRESET ? (uint8_t)(set.preset - NPRESET) : 0u; }
        else {
            bank[save_slot].magic = USLOT_MAGIC; bank[save_slot].p = patch;   /* the sound as played: what you hear is what you keep */
            bank_save(bank, sizeof bank);
            set.preset = (uint8_t)(NPRESET + save_slot); set.patch = patch; morph = morph_to = 0; changed(); save_mode = 0;
        }
        page_drawn = ~0u;
    } else if (save_mode && (b & (1u << BTN_GLO))) { patch_send(); sent_ms = fm1_ms; save_mode = 0; page_drawn = ~0u; }   /* SAVE, GLO: the sound out over USB MIDI, to share */
    else if (save_mode && (b & ~(1u << BTN_SAVE))) { save_mode = 0; page_drawn = ~0u; }   /* any other button: not saving */
    if (b & (1u << BTN_SEL)) { morph_to = morph_to ? 0 : 32767; morph = morph_to; live_update(); page_drawn = ~0u; }   /* SEL: cut to the other scene */
    {   /* ARP: let go without turning PRESETS while held: morph to the other scene; turned: B was picked instead */
        uint32_t arp = fm1_in.buttons >> BTN_ARP & 1u;
        if (arp_was && !arp && !arp_turned) { morph_to = morph_to ? 0 : 32767; page_drawn = ~0u; }
        if (!arp) arp_turned = 0;
        arp_was = arp;
    }
    if (b & (1u << BTN_PLAY)) { set.latch ^= 1u; if (!set.latch && !sustain) release_all(); changed(); }   /* LATCH: a drone with no hands */
    if ((b & (1u << BTN_OCT_DOWN) && fm1_in.buttons >> BTN_OCT_UP & 1u) || (b & (1u << BTN_OCT_UP) && fm1_in.buttons >> BTN_OCT_DOWN & 1u)) {
        sustain = 0; release_all(); for (k = 0; k < 128u; k++) voice_off(k);
    }
    if ((n = fm1_enc_take(ENC_PRESETS)) != 0) {                         /* PRESETS: the presets, on any page; a slot while saving; B while ARP is held */
        if (save_mode) save_slot = (uint8_t)(((int32_t)save_slot + n + (int32_t)NUSER * 4) % (int32_t)NUSER);
        else if (fm1_in.buttons >> BTN_ARP & 1u) { set.scene_b = (uint8_t)(((int32_t)set.scene_b + n + (int32_t)NALL * 4) % (int32_t)NALL); preset_build(set.scene_b, &set.patch_b); arp_turned = 1; changed(); live_update(); }
        else { set.preset = (uint8_t)(((int32_t)set.preset + n + (int32_t)NALL * 4) % (int32_t)NALL); preset_build(set.preset, &set.patch); morph = morph_to = 0; patch = set.patch; changed(); }
        page_drawn = ~0u;
    }
    if ((n = fm1_enc_take(ENC_SELECT)) != 0) press = (uint8_t)clamp((int32_t)press + n * 4, 0, 127);   /* SELECT: the pressure, with no hands to press: the swell */
    if ((n = fm1_enc_take(ENC_ALGORITHM)) != 0 && page == PG_HOME) bend_knob = clamp(bend_knob + n * 64, -512, 512);   /* HOME: a bend, springing back */
    else if (n != 0) {                                                   /* ALGORITHM elsewhere: the group's pages, either way */
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
            if (v != *p) { *p = (uint8_t)v; if (!d->global) *scene_ptr(d->off) = (uint8_t)v; changed(); draw_knob(k); draw_graphic(); if (page == PG_HOME) draw_header(); }
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
        else if (m == 0xA0u) voices_pressure(d1, d2);
        else if (m == 0xC0u) { set.preset = (uint8_t)(d1 % NALL); preset_build(set.preset, &set.patch); morph = morph_to = 0; patch = set.patch; changed(); page_drawn = ~0u; }
        else if (m == 0xE0u) bend256 = (((int32_t)d2 << 7 | d1) - 8192) >> 4;
        else if (m == 0xB0u) {
            if (set.expr && d1 == set.expr) { press = d2; continue; }
            if (d1 == 64u) { sustain = d2 >= 64u; if (!sustain && !set.latch) release_all(); continue; }
            if (d1 == 120u || d1 == 123u) { sustain = 0; release_all(); for (k = 0; k < 128u; k++) voice_off(k); continue; }
            if (d1 >= 80u && d1 < 88u) { *((uint8_t *)&patch + MACRO[d1 - 80u].off) = d2; *scene_ptr(MACRO[d1 - 80u].off) = d2; changed(); midi_redraw = 1; }
            else if (d1 == 7u) { patch.volume = d2; *scene_ptr(offsetof(patch_t, volume)) = d2; changed(); }
            else if (d1 == 74u) { patch.filter = d2; *scene_ptr(offsetof(patch_t, filter)) = d2; changed(); midi_redraw = 1; }   /* CC 74: the master filter */
        }
    }
    if (midi_redraw) { midi_redraw = 0; for (k = 0; k < 4u; k++) draw_knob(k); if (!PAGES[page].live) draw_graphic(); }
    {   /* the tide's turn: 64 bars at RATE 0 to 4 at 127 (log), of the MIDI clock's tempo, or of 120 bpm without one */
        uint32_t f = (uint32_t)patch.rate * 8u, beat = fm1_ms - clk_last < 2000u ? beat_ms : 500u;
        uint32_t bars_q15 = (64u * EXP2N_Q15[f & 255u]) >> (f >> 8);        /* 64 x 2^(-rate / 32), Q15 */
        tide_period_ms = (4u * beat * bars_q15) >> 15;
    }
    {   /* the delay's SYNC: with a clock in, TIME is a division of the beat; the time follows the tempo with a little hysteresis */
        uint32_t on = fm1_ms - clk_last < 2000u;
        if (on) {
            const uint8_t *dv = DLY_DIV[((uint32_t)patch.dly_time * NDIV) >> 7];
            uint32_t t = (beat_ms * 441u / 10u) * dv[0] / dv[1], cur = sync_dly, diff;
            t = t < 64u ? 64u : t > DLY_LEN - 1u ? DLY_LEN - 1u : t;
            diff = t > cur ? t - cur : cur - t;
            if (diff > cur >> 6) sync_dly = t;
        }
        if (on != dly_synced) { dly_synced = (uint8_t)on; if (page == PG_DELAY) midi_redraw = 1; }
    }
    sounding = voices_sounding();
    for (k = 0; k < NV; k++)
        if (sounding >> k & 1u) { if (voices[k].gate && voices[k].note >= base && voices[k].note < base + 27u) lit |= 1u << (voices[k].note - base); }   /* held keys lit, not tails */
    for (k = 0; k < 27u; k++) if (sus_held[(base + k) >> 3] >> ((base + k) & 7u) & 1u) lit |= 1u << k;   /* and the latched ones, in this octave */
    for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, oct() < 0); fm1_led_key(BTN_OCT_UP, oct() > 0);
    fm1_led_key(BTN_HOME, page == PG_HOME); fm1_led_key(BTN_EDIT, page >= PG_TONE && page <= PG_AIR); fm1_led_key(BTN_ENV, page == PG_SHAPE || page == PG_TOUCH);
    fm1_led_key(BTN_LFO, page >= PG_MOVE && page <= PG_DST); fm1_led_key(BTN_FX, page >= PG_SPACE && page <= PG_FREEZE); fm1_led_key(BTN_GLO, page == PG_MIDI);
    fm1_led_key(BTN_PLAY, set.latch); fm1_led_key(BTN_REC, fm1_in.buttons >> BTN_REC & 1u); fm1_led_key(BTN_SEQ, autopilot); fm1_led_key(BTN_ARP, morph_to != 0); fm1_led_key(BTN_SEL, morph >= 16384); fm1_led_key(BTN_SAVE, save_mode);
    {
        static int32_t load_acc, wload_acc;
        int32_t l = (int32_t)(audio_cycles / (HALF_FRAMES * 10u * FM1_TICKS_PER_US / (FS / 1000u) + 1u));
        int32_t w = (int32_t)(audio_worker.last_job_ticks / (HALF_FRAMES * 10u * FM1_TICKS_PER_US / (FS / 1000u) + 1u));
        load_acc += ((l << 6) - load_acc) >> 6; wload_acc += ((w << 6) - wload_acc) >> 6;
        load_now = (uint32_t)(load_acc >> 6); wload_now = (uint32_t)(wload_acc >> 6);
    }
    if (page != page_drawn) { page_drawn = page; draw_page(); }
    freeze_now = (fm1_in.buttons >> BTN_REC & 1u) ? 127u : patch.freeze;   /* REC held: frozen for as long as it is */
    if (fm1_ms - strip_ms >= 100u) {                                      /* ten a second: the live graphics, the bend's spring, the autopilot */
        uint32_t moving = morph != morph_to;
        strip_ms = fm1_ms; hist_tick(); bend_knob -= bend_knob / 4;
        if (moving) {                                                     /* the morph: four seconds end to end */
            morph += morph_to > morph ? 819 : -819; morph = clamp(morph, 0, 32767);
            live_update();
            if (morph == morph_to || (morph >= 16384) != (morph - (morph_to > morph ? 819 : -819) >= 16384)) draw_header();
        } else if (morph || autopilot) live_update();
        if (autopilot) auto_tick();
        if (autopilot || moving) { for (k = 0; k < 4u; k++) draw_knob(k); draw_graphic(); }
        else { if (PAGES[page].live) draw_graphic(); for (k = 0; k < 4u; k++) if (knob_flare[k] && fm1_ms - knob_flare[k] < 450u) draw_knob(k); }
    }
    if (out_peak > 512) loud_ms = fm1_ms;              /* (quiet is what matters: a pad's tail can run for half a minute) */
    if (dirty && fm1_ms - t_change > 2000u && fm1_ms - last_midi_ms > 1000u && fm1_ms - loud_ms > 500u) {
        settings_save(&set, sizeof set);
        dirty = 0;
    }
}
