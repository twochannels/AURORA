/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point DSP building blocks for the AURORA voice (the chip has no FPU: docs/RULES.md). The polyBLEP
 * oscillators, the soft clip and the trapezoidal SVF follow Felucca's dsp.c (Copyright (C) 2026 Leo Kuroshita,
 * Hügelton Instruments, GPL-3.0-only); the tables are firmware/gen/aurora_tables.h (tools/gen_tables.py).
 * Conventions: audio is Q15 (+-32767 full scale); a phase is 32 bits, a cycle wraps; pitch is in 1/16 semitone
 * (pitch16: MIDI note x 16, 0..2047) or 1/256 semitone (pitch256) where it glides. */
#include "aurora_tables.h"

static inline int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a leaky integrator's leak that always moves the state at least a step toward 0 (a plain shift parks it a few LSB off) */
static inline int32_t leak10(int32_t y) { return y > 0 ? (y + 1023) >> 10 : y >> 10; }
static inline int32_t mulq15r(int32_t a, int32_t b) { return (a * b) / 32768; }   /* toward zero, either sign: no bias, and a feedback loop's gain is below one at the last bit, so it decays to 0 (a shift floors, and holds -1 for ever) */
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a sine of the phase, linearly interpolated between the 1024 table points */
static inline int32_t sine_i(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t osc_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc); }
/* a pulse of width pw (the phase offset of the falling edge: 2^31 = square) */
static inline int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw) { return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1; }

/* tanh(x / 32768) * 32767, linearly interpolated */
static inline int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
/* linear up to k, then a soft knee: only the peaks above k saturate */
static inline int32_t soft_knee(int32_t y, int32_t k)
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}

static inline uint32_t noise32(uint32_t *st)        /* xorshift32 */
{
    uint32_t s = *st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = s;
    return s;
}

/* the phase increment of a pitch: whole 1/16 semitones from the top octave's table, shifted down */
static inline uint32_t pitch_inc(int32_t pitch16)
{
    uint32_t p = (uint32_t)clamp(pitch16, 0, 2047), oct = (p * 683u) >> 17;   /* / 192, exact to 2047 */
    return PITCH16[p - oct * 192u] >> (10u - oct);
}
/* in 1/256 semitone: the rest as a factor 2^(r / 3072) ~ 1 + r x 14.8 / 65536 */
static inline uint32_t pitch_inc256(int32_t pitch256)
{
    int32_t p = clamp(pitch256, 0, 2047 * 16 + 15);
    uint32_t inc = pitch_inc(p >> 4);
    return inc + (inc >> 16) * (uint32_t)((p & 15) * 15);
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per control tick in Q13; the states stay
 * within +-150000 so the products fit in 32 bits. cut16: the cutoff as a pitch (1/16 semitone, MIDI 0..135),
 * k: the damping in Q12 (8192 = 2.0, no resonance; ~600 = 0.15, near self-oscillation). */
typedef struct { int32_t a1, a2, a3, k; } svf_t;
static inline void svf_coef(svf_t *c, int32_t cut16, int32_t k)
{
    int32_t i, g, den;
    cut16 = clamp(cut16, 0, 135 * 16);
    i = cut16 >> 4;
    g = SVF_G[i] + (((SVF_G[i + 1] - SVF_G[i]) * (cut16 & 15)) >> 4);
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}
#define SVF_K(res) ((int32_t)SVF_KT[(res)])        /* resonance 0..127 -> damping Q12 (aurora_tables.h): 2.0 .. 0.32, the peak
                                                     * rising fast then levelling near +10 dB; the CS-80's filters never self-oscillate */
/* one sample: lp, bp and hp of in; the states ic1, ic2 */
static inline void svf_step(const svf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *lp, int32_t *hp)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3 + 4096) >> 13;       /* rounded: the state settles at 0, not a few LSB below */
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3 + 4096) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    *lp = v2;
    *hp = in - ((c->k * v1) >> 12) - v2;
}
