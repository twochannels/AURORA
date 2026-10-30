/* SPDX-License-Identifier: GPL-3.0-only */
/* AURORA's effects after the voice: a stereo delay and a reverb with a shimmer, where the CS-80 had a studio's
 * outboard. Fixed point, the lines in the pool (int16), the controls read from the patch a byte at a time.
 *   delay    one line, the input the sum of both sides; TIME, FEEDBK (with a one-pole damping in the loop),
 *            MIX; PING puts the right side on a tap at half the time
 *   freeze   FREEZE holds the delay line (a crossfade a pass: a hold time of 1 s at the bottom to 30 s just under
 *            the top, for ever at 127; new sound comes in by what the hold leaves) and plays eight grains off it, Hann windows at random places (SPRAY sets how far back),
 *            lengths (GRAIN, +-12 %) and pans, at a pitch (SHIFT, +-12 semitones); the cloud goes on into the
 *            phaser and the plate, so FREEZE on the macro holds the texture after the hands have left
 *   reverb   a feedback delay network of eight lines (a Householder matrix: every line feeds every other),
 *            two all-pass diffusers on the way in and a 20 ms pre-delay; SIZE is the decay, DAMP a one-pole
 *            low-pass in every line, MIX; SHIMMER feeds the reverb's own output back in an octave up (a two-head
 *            granular pitch shifter), the way a shimmer pedal does */

/* ---- the delay */
#define DLY_LEN 49152u                   /* 1.1 s */
static int16_t dly_line[DLY_LEN] FM1_POOL;
static uint32_t dly_w;                               /* the write index, wrapped by a compare (no modulo: the chip has no divider) */
static int32_t dly_lp, dly_err;                      /* the loop's damping one-pole and its rounding remainder (no dead zone: a few LSB cannot sit in the loop for ever) */
static uint32_t dly_wow, dly_flt;
#if !defined(DINGHY_HOST)
static uint32_t dly_hot;                             /* writes the line had to clamp: a meter (tests/host.c declares it) */
#endif                    /* the tape's wow (0.7 Hz) and flutter (6 Hz) phases */
static int32_t dly_m16, dly_m16_d;                   /* the tape's wander, in 1/16 frames, and its step a frame across the block */
static uint32_t dly_tl, dly_tr, dly_ol, dly_or, dly_xf;   /* the taps' times now and before, and the crossfade left between them (frames): a time change is a 46 ms fade, never a jump */
#define DLY_XF 2048u
static inline uint32_t dly_back(uint32_t back) { uint32_t p = dly_w + DLY_LEN - back; return p >= DLY_LEN ? p - DLY_LEN : p; }
static inline int32_t dly_read(uint32_t t, int32_t m16, int32_t tape)   /* the tap at time t: between frames at the wandering position with TAPE, else the frame */
{
    if (tape) {
        int32_t back = clamp((int32_t)t + (m16 >> 4) - 32, 2, (int32_t)DLY_LEN - 2);   /* (the wander kept inside the line at the extremes) */
        uint32_t a = dly_back((uint32_t)back), b = dly_back((uint32_t)back + 1u);
        return dly_line[a] + (((dly_line[b] - dly_line[a]) * (m16 & 15)) >> 4);   /* (the fraction and the whole frame from the same position) */
    }
    return dly_line[dly_back(t)];
}
static void dly_retime(uint32_t tl, uint32_t tr)   /* new tap times: the old ones fade out over DLY_XF frames (a fade still running is finished first) */
{
    if (tl == dly_tl && tr == dly_tr) return;
    dly_ol = dly_tl; dly_or = dly_tr; dly_tl = tl; dly_tr = tr; dly_xf = DLY_XF;
}
#define NGRAIN 8u
static struct { uint32_t pos, wph, winc; int32_t pl, pr; } grain[NGRAIN];   /* the read position (16.16 frames), the window's phase and step, the pan */
static uint32_t frz_rng = 0x3C6EF35Fu, frz_len, frz_spray, frz_rate = 65536u;   /* this block's grain length (frames), reach (frames), rate (16.16) */
static int32_t frz_now, frz_new = 32767;               /* FREEZE, Q15, slewed over ~0.1 s; how much new the line takes a pass, from the hold time */
static void grain_new(uint32_t g)
{
    uint32_t r = noise32(&frz_rng), r2 = noise32(&frz_rng), len = frz_len - (frz_len >> 3) + (((frz_len >> 2) * (r2 & 255u)) >> 8);
    uint32_t minb = ((len * (frz_rate >> 8)) >> 8) + 4u, maxb = DLY_LEN - 4u, span = maxb - minb, back;
    if (span > frz_spray) span = frz_spray;
    back = minb + (((r >> 8) & 0xFFFFu) * span >> 16);
    grain[g].pos = dly_back(back) << 16;
    grain[g].wph = 0; grain[g].winc = 0xFFFFFFFFu / len;
    grain[g].pl = sine_i((r2 >> 2 & 0x3FFFFFFFu) + 0x40000000u); grain[g].pr = sine_i(r2 >> 2 & 0x3FFFFFFFu);   /* constant power */
}
static void freeze_block(void)
{
    int32_t was = frz_now, target = (int32_t)freeze_now * 258;
    uint32_t g;
    frz_now += (target - frz_now) >> 4;
    if (freeze_now >= 127u) frz_new = 0;      /* held for ever */
    else {                                      /* a hold time: the line down 40 dB in 1 s at 0 to 30 s at 126 (log), one pass 1.1 s */
        uint32_t t_q15 = exp2_q15((uint32_t)freeze_now * 40u);                      /* seconds, Q15 */
        frz_new = 32767 - (int32_t)((1u << 30) / exp2_q15(245268480u / t_q15));      /* 1 - 2^(-7.31 / T) */
    }
    if (!frz_now) return;
    frz_len = (441u * exp2_q15((uint32_t)patch.frz_grain * 45u)) >> 15;          /* 10 ms .. 480 ms (log) */
    frz_spray = (882u * exp2_q15((uint32_t)patch.frz_spray * 46u)) >> 15;        /* 20 ms .. the whole line */
    frz_rate = exp2_q15(((uint32_t)(patch.frz_shift > 24u ? 24u : patch.frz_shift) * 256u) / 3u);   /* 0.5 .. 2 (an octave either way), 16.16 */
    if (!was) for (g = 0; g < NGRAIN; g++) { grain_new(g); grain[g].wph = noise32(&frz_rng); }   /* in at once, the windows staggered */
}
static inline int32_t grains_step(int32_t *gl, int32_t *gr)   /* one frame of the cloud: the sum into gl / gr */
{
    uint32_t g;
    int32_t sl = 0, sr = 0;
    for (g = 0; g < NGRAIN; g++) {
        uint32_t p = grain[g].pos >> 16, q = p + 1u >= DLY_LEN ? 0u : p + 1u, w;
        int32_t sm = dly_line[p] + (((dly_line[q] - dly_line[p]) * (int32_t)((grain[g].pos >> 8) & 255u)) >> 8);
        grain[g].wph += grain[g].winc;
        if (grain[g].wph < grain[g].winc) grain_new(g);                      /* the window wrapped: the next grain */
        w = (uint32_t)(32767 - SINE_RAM[((grain[g].wph >> 22) + 256u) & 1023u]) >> 1;   /* Hann, (1 - cos) / 2, from the RAM table in 1024 steps */
        sm = mulq15(sm, (int32_t)w);
        sl += mulq15(sm, grain[g].pl); sr += mulq15(sm, grain[g].pr);
        grain[g].pos += frz_rate; if (grain[g].pos >= (DLY_LEN << 16)) grain[g].pos -= DLY_LEN << 16;
    }
    *gl = sl; *gr = sr;
    return 0;
}
static void fx_delay(int32_t *st, uint32_t n)       /* st: L R L R ..., in place */
{
    uint32_t i, time = dly_synced ? sync_dly : DLY_TIME[patch.dly_time], half = time >> 1;
    dly_retime(time, patch.dly_ping ? half : time);
    int32_t fb = patch.dly_fb * 250, mix = patch.dly_mix * 258, damp = 32767 - patch.dly_damp * 230;
    int32_t tape = patch.dly_tape, drive = 32767 + tape * 258, undrive = (32767 << 15) / drive, m16;   /* TAPE's drive, and exactly its inverse after the clip: the saturation can only compress */
    if (tape) {                                     /* TAPE: the repeats wander (wow and flutter), darken and saturate */
        int32_t target;
        damp -= tape * 120;
        dly_wow += 68173u * n; dly_flt += 584000u * n;   /* 0.7 Hz and 6 Hz, stepped a block */
        target = ((sine_i(dly_wow) * 40 + sine_i(dly_flt) * 6) >> 15) * tape / 127 * 16;   /* +-46 frames (1 ms) at full, in 1/16 */
        dly_m16_d = (target - dly_m16) / (int32_t)n;
    } else dly_m16 = dly_m16_d = 0;
    if (damp < 4000) damp = 4000;
    freeze_block();
    if (!mix) fb = 0;                               /* MIX off: the line holds the dry alone, so turning MIX up finds a clean tail */
    if (!mix && !frz_now) {                         /* off: the line keeps filling, so turning MIX up finds a tail */
        for (i = 0; i < n; i++) { dly_line[dly_w] = (int16_t)clamp((st[2u * i] + st[2u * i + 1u]) >> 1, -32767, 32767); if (++dly_w == DLY_LEN) dly_w = 0; }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t in = (st[2u * i] + st[2u * i + 1u]) >> 1;
        int32_t tap, tap2;
        if (frz_now) {                              /* the cloud, at FREEZE x -2.7 dB a grain, before the line is written */
            int32_t gl, gr, lvl = mulq15(frz_now, 24000);
            grains_step(&gl, &gr);
            st[2u * i] += mulq15(gl, lvl); st[2u * i + 1u] += mulq15(gr, lvl);
        }
        if (tape) { dly_m16 += dly_m16_d; m16 = dly_m16 + 32 * 16; } else m16 = 32 * 16;   /* the wander, kept positive */
        tap = dly_read(dly_tl, m16, tape);
        tap2 = dly_tr == dly_tl ? tap : dly_read(dly_tr, m16, tape);
        if (dly_xf) {                               /* a time just changed: the old taps fading out, the new in */
            int32_t w = (int32_t)dly_xf * (32768 / (int32_t)DLY_XF), o = dly_read(dly_ol, m16, tape);
            tap += ((o - tap) * w) >> 15;
            o = dly_or == dly_ol ? o : dly_read(dly_or, m16, tape);
            tap2 += ((o - tap2) * w) >> 15;
            dly_xf--;
        }
        { int32_t e = (tap - dly_lp) * damp + dly_err; dly_lp += e / 32768; dly_err = e % 32768; }   /* the loop's damping, the remainder carried: every difference moves it */
        { int32_t f = mulq15r(dly_lp, fb);
          if (tape) f = mulq15r(softclip(mulq15r(f, drive)), undrive);   /* saturates: driven in, clipped, the drive taken back out, so the loop's gain stays FEEDBK's */
          if (in + f > 32767 || in + f < -32767) dly_hot++;
          f = clamp(in + f, -32767, 32767);
          if (frz_now) f = dly_line[dly_w] + mulq15(f - dly_line[dly_w], frz_new);   /* held: the old stays, the new comes in by what FREEZE leaves */
          dly_line[dly_w] = (int16_t)f; }
        if (++dly_w == DLY_LEN) dly_w = 0;
        st[2u * i] += mulq15(tap, mix);
        st[2u * i + 1u] += mulq15(tap2, mix);
    }
}

/* ---- the reverb: a plate after Dattorro (1997), in fixed point. The input is diffused by four allpasses and fed to
 * two cross-coupled tanks, each a slowly modulated allpass, a delay, the damping low-pass, an allpass and a delay,
 * with the decay applied twice round; the output is Dattorro's seven taps a side. SIZE is the decay time (0.5 .. 25 s),
 * the input is scaled by the loss so a held chord sits at one level whatever the SIZE. Lengths are Dattorro's at
 * 29.8 kHz scaled to 44.1 kHz. */
#define RV_MOD 24                                   /* the tank allpasses wander +-24 frames (Dattorro's 16 at 29.8 kHz) */
#define RV_AP0_L 995u
#define RV_AP0_R 1345u
#define RV_D1_L 6590u
#define RV_D1_R 5505u
#define RV_AP1_L 2664u
#define RV_AP1_R 3931u
#define RV_D2_L 4681u
#define RV_D2_R 6241u
static int16_t rv_pre[1024] FM1_POOL;               /* the pre-delay: 20 ms */
static int32_t rv_in1[210] FM1_POOL, rv_in2[158] FM1_POOL, rv_in3[561] FM1_POOL, rv_in4[410] FM1_POOL;   /* input diffusion, at the signal's scale */
static int32_t rv_ap0l[RV_AP0_L + 64u] FM1_POOL, rv_ap0r[RV_AP0_R + 64u] FM1_POOL;   /* the tanks: 32 bits at Q4 (sixteen times the signal) */
static int32_t rv_d1l[RV_D1_L] FM1_POOL, rv_d1r[RV_D1_R] FM1_POOL;
static int32_t rv_ap1l[RV_AP1_L] FM1_POOL, rv_ap1r[RV_AP1_R] FM1_POOL;
static int32_t rv_d2l[RV_D2_L] FM1_POOL, rv_d2r[RV_D2_R] FM1_POOL;
#define SH_LEN 8192u                     /* the shimmer's grain: 186 ms, two heads half a grain apart */
static int16_t sh_line[SH_LEN] FM1_POOL;
static uint32_t rv_w, sh_w, sh_ph, rv_lfo[2];   /* sh_ph: the heads' position behind the write, in 1/256 frame */
static int32_t rv_mq[2];                             /* the tank allpasses' modulation now, in 1/64 frames x 256: ramped every frame */
static uint32_t rv_wi[4], rv_wap0[2], rv_wd1[2], rv_wap1[2], rv_wd2[2];   /* each buffer's write index */
static int32_t rv_bw, rv_lpl, rv_lpr, rv_fbl, rv_fbr;   /* the input bandwidth, the two dampers, the cross-feeds */
static int32_t rv_dcxl, rv_dcyl, rv_dcxr, rv_dcyr;   /* the output DC blockers' last input and output */
static int32_t sh_fb, sh_lp;                        /* the shimmer's last sample, low-passed, into the next input */
#if !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static uint32_t rv_hot;                             /* samples a tank ran far beyond a chord's level: a meter (tests/fake_fm1.h) */
#define dbg_meter(k, p, n) ((void)0)
#endif

/* every buffer keeps its own write index and wraps by a compare: no modulo (the chip has no divider). The tanks are
 * 32-bit at Q4 (sixteen times the signal): the 16-bit tanks at an eighth of the signal truncated on every pass, and
 * the recirculated rounding was a hiss under every tail */
static inline uint32_t wrap_back(uint32_t w, uint32_t len, uint32_t back) { uint32_t p = w + len - back; return p >= len ? p - len : p; }
/* a x b / 32768 for |a| up to 2^23 (a Q2 tank value), toward zero: two products, neither can wrap */
static inline int32_t mulq15w(int32_t a, int32_t b)
{
    int32_t neg = (a < 0) != (b < 0), ua = a < 0 ? -a : a, ub = b < 0 ? -b : b;
    int32_t r = ((ua >> 8) * ub >> 7) + (((ua & 255) * ub) >> 15);
    return neg ? -r : r;
}
static inline int32_t allpass32(int32_t *z, uint32_t len, uint32_t *w, int32_t x, int32_t g)   /* the lattice form: unity gain at every frequency */
{
    int32_t d = z[*w], v = x + mulq15w(d, g);
    z[*w] = v;
    if (++*w == len) *w = 0;
    return d - mulq15w(v, g);
}
/* the modulated allpass: its delay wanders RV_MOD either side of len, read between frames; the buffer is len + 64 */
static inline int32_t allpass_mod(int32_t *z, uint32_t len, uint32_t *w, int32_t x, int32_t g, uint32_t m64)
{
    uint32_t size = len + 64u, dl = len - RV_MOD + (m64 >> 6), pa = wrap_back(*w, size, dl), pb = pa ? pa - 1u : size - 1u;
    int32_t a = z[pa], b = z[pb];
    int32_t d = a + (((b - a) * (int32_t)(m64 & 63u)) >> 6), v = x + mulq15w(d, g);   /* (|b - a| < 2^24: the tanks stay under 2^23) */
    z[*w] = v;
    if (++*w == size) *w = 0;
    return d - mulq15w(v, g);
}
/* a delay line: write the new frame, return the oldest (len frames ago) */
static inline int32_t delay32(int32_t *z, uint32_t len, uint32_t *w, int32_t x)
{
    int32_t d = z[*w];
    z[*w] = x;
    if (++*w == len) *w = 0;
    return d;
}
#define TAP(z, len, w, back) ((z)[wrap_back((w), (len), (back))])

static void fx_reverb(const int32_t *st, int32_t *wet, uint32_t n)   /* st: L R ... in; wet: L R ... out (the wet alone) */
{
    uint32_t i;
    int32_t decay = PLATE_D[patch.rv_size], mix = rv_mix_now * 258, damp = 32767 - patch.rv_damp * 150;
    int32_t shim = patch.rv_shim * 240;          /* 0 .. 0.93 of the loop's unity gain (below) */
    int32_t in_g, dq0, dq1;
    if (!mix && !shim) { for (i = 0; i < 2u * n; i++) wet[i] = 0; return; }
    /* a sustained input builds up ~1 / (1 - decay) in the tanks: the input is scaled by the loss so a held chord sits
     * at the same level whatever the SIZE (a tail then starts quieter the longer it is to last) */
    in_g = clamp((32768 - decay) * 7, 0, 32767);
    shim = mulq15(shim, in_g);                        /* the shimmer's injection, scaled the same way */
    if (shim > 8500) shim = 8500;                     /* and never past 0.35: short tails would ring otherwise */
    /* the tank allpasses' modulation: 1.0 Hz and 0.7 Hz, the LFO read at the block's end and the delay ramped
     * towards it every frame (stepped once a block it jumped up to a frame at a time, 172 times a second: a hiss) */
    rv_lfo[0] += 97391u * n; rv_lfo[1] += 68173u * n;
    {
        int32_t t0 = (int32_t)((uint32_t)(sine_i(rv_lfo[0]) + 32768) * (RV_MOD * 2u * 64u) >> 16);   /* 0 .. 2 RV_MOD, 1/64 frames */
        int32_t t1 = (int32_t)((uint32_t)(sine_i(rv_lfo[1]) + 32768) * (RV_MOD * 2u * 64u) >> 16);
        dq0 = ((t0 << 8) - rv_mq[0]) / (int32_t)n; dq1 = ((t1 << 8) - rv_mq[1]) / (int32_t)n;
    }
    for (i = 0; i < n; i++) {
        int32_t in = mulq15((st[2u * i] + st[2u * i + 1u]) >> 1, in_g), x, tl, tr, l, r;
        uint32_t ml, mr;
        rv_mq[0] += dq0; rv_mq[1] += dq1;
        if (shim) {
            sh_lp += (sh_fb - sh_lp) >> 2;               /* ~2.5 kHz: the octaves stack up softly */
            in += softclip(mulq15(sh_lp, shim));         /* and the loop can never run away: the feedback saturates */
        }
        rv_pre[rv_w & 1023u] = (int16_t)clamp(in, -32767, 32767);
        x = rv_pre[(rv_w - 882u) & 1023u];           /* 20 ms later */
        rv_bw += mulq15r(x - rv_bw, 26000);           /* the input bandwidth: ~9 kHz */
        x = allpass32(rv_in1, 210u, &rv_wi[0], rv_bw, 24576);   /* 0.75, 0.75, 0.625, 0.625 */
        x = allpass32(rv_in2, 158u, &rv_wi[1], x, 24576);
        x = allpass32(rv_in3, 561u, &rv_wi[2], x, 20480);
        x = allpass32(rv_in4, 410u, &rv_wi[3], x, 20480);
        x <<= 4;                                     /* into the tanks' Q4 */
        /* the two tanks, each fed by the input plus the other's end */
        ml = (uint32_t)(rv_mq[0] >> 8); mr = (uint32_t)(rv_mq[1] >> 8);
        /* the taps are read before this frame's writes move the indices (Dattorro's nodes, scaled) */
        l = TAP(rv_d1r, RV_D1_R, rv_wd1[1], 394u) + TAP(rv_d1r, RV_D1_R, rv_wd1[1], 4401u) - TAP(rv_ap1r, RV_AP1_R, rv_wap1[1], 2831u)
          + TAP(rv_d2r, RV_D2_R, rv_wd2[1], 2954u) - TAP(rv_d1l, RV_D1_L, rv_wd1[0], 2945u) - TAP(rv_ap1l, RV_AP1_L, rv_wap1[0], 277u)
          - TAP(rv_d2l, RV_D2_L, rv_wd2[0], 1578u);
        r = TAP(rv_d1l, RV_D1_L, rv_wd1[0], 522u) + TAP(rv_d1l, RV_D1_L, rv_wd1[0], 5368u) - TAP(rv_ap1l, RV_AP1_L, rv_wap1[0], 1817u)
          + TAP(rv_d2l, RV_D2_L, rv_wd2[0], 3956u) - TAP(rv_d1r, RV_D1_R, rv_wd1[1], 3124u) - TAP(rv_ap1r, RV_AP1_R, rv_wap1[1], 496u)
          - TAP(rv_d2r, RV_D2_R, rv_wd2[1], 179u);
        tl = allpass_mod(rv_ap0l, RV_AP0_L, &rv_wap0[0], x + rv_fbr, -22938, ml);   /* -0.7 */
        tr = allpass_mod(rv_ap0r, RV_AP0_R, &rv_wap0[1], x + rv_fbl, -22938, mr);
        tl = delay32(rv_d1l, RV_D1_L, &rv_wd1[0], tl);
        tr = delay32(rv_d1r, RV_D1_R, &rv_wd1[1], tr);
        rv_lpl += mulq15w(tl - rv_lpl, damp);        /* the damping, then the decay */
        rv_lpr += mulq15w(tr - rv_lpr, damp);
        tl = mulq15w(rv_lpl, decay);
        tr = mulq15w(rv_lpr, decay);
        tl = allpass32(rv_ap1l, RV_AP1_L, &rv_wap1[0], tl, 16384);   /* 0.5 */
        tr = allpass32(rv_ap1r, RV_AP1_R, &rv_wap1[1], tr, 16384);
        tl = delay32(rv_d2l, RV_D2_L, &rv_wd2[0], tl);
        tr = delay32(rv_d2r, RV_D2_R, &rv_wd2[1], tr);
        if (tl > 7000000 || tl < -7000000 || tr > 7000000 || tr < -7000000) rv_hot++;   /* near the wide multiply's limit (2^23): a meter */
        rv_fbl = mulq15w(tl, decay);                 /* into the other tank next frame */
        rv_fbr = mulq15w(tr, decay);
        rv_w++;
        { int32_t l2 = (l * 3 + r) >> 7, r2 = (r * 3 + l) >> 7; l = l2; r = r2; }   /* 0.375 with a quarter of each in the other (wide, mono-safe), out of Q4 */
        { int32_t yl = ((l - rv_dcxl) << 6) + rv_dcyl - leak10(rv_dcyl), yr = ((r - rv_dcxr) << 6) + rv_dcyr - leak10(rv_dcyr);   /* DC blockers, ~7 Hz, state in 1/64 */
          rv_dcxl = l; rv_dcyl = yl; l = yl >> 6; rv_dcxr = r; rv_dcyr = yr; r = yr >> 6; }
        l = soft_knee(l, 20000); r = soft_knee(r, 20000);   /* a loud wash bends rather than slams the master knee */
        if (shim) {                                  /* the shimmer: the output an octave up, back to the input */
            int32_t m = (l + r) >> 1, a, b, wa, fr = (int32_t)(sh_ph & 255u);
            uint32_t da = sh_ph >> 8, db = (da + SH_LEN / 2u) & (SH_LEN - 1u);   /* two heads, half a grain apart */
            sh_line[sh_w & (SH_LEN - 1u)] = (int16_t)clamp(m, -32767, 32767);
            a = sh_line[(sh_w - da) & (SH_LEN - 1u)]; a += ((sh_line[(sh_w - da - 1u) & (SH_LEN - 1u)] - a) * fr) >> 8;
            b = sh_line[(sh_w - db) & (SH_LEN - 1u)]; b += ((sh_line[(sh_w - db - 1u) & (SH_LEN - 1u)] - b) * fr) >> 8;
            wa = (32767 - sine_i((da << 19) + 0x40000000u)) >> 1;   /* a raised cosine over the grain: the two sum to one */
            sh_fb = mulq15(a, wa) + mulq15(b, 32767 - wa);
            sh_ph = (sh_ph + (SH_LEN << 8) - SHIFT_INC[patch.rv_shift > 24u ? 24u : patch.rv_shift]) & ((SH_LEN << 8) - 1u);   /* the heads run ahead: the interval */
            sh_w++;
        }
        wet[2u * i] = mulq15(l, mix);
        wet[2u * i + 1u] = mulq15(r, mix);
    }
}
