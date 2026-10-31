/* SPDX-License-Identifier: GPL-3.0-only */
/* hal/fm1_input.h's quadrature decoder (fm1__frame) on simulated turns of encoder 6, PRESETS. A turn must count
 * every detent whether the scan sees each state for 4 frames, 2 or 1, or skips every other state (a flick); a
 * one-frame glitch at rest must count nothing; a pause halfway through a click, on either in-between state, must
 * not become a detent (the knob then counted two a click, or lost clicks); and a half-cycle encoder (one that rests
 * on 00 and 11 in turn) must settle to one step a click. After X0X's tests/host/encoder_test.c (GPL-3.0). */
#include <stdio.h>
#include <string.h>
#include "fm1_input.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

#define E 6u                                           /* PRESETS: A = col 0 bit 5, B = col 1 bit 5 */
static void frame(uint32_t st)
{
    fm1_in.raw[0] = (uint8_t)(((st >> 1) & 1u) << 5);
    fm1_in.raw[1] = (uint8_t)((st & 1u) << 5);
    fm1__frame();
}
static const uint32_t CW[4] = {1, 3, 2, 0};            /* 0 -> 1 -> 3 -> 2 -> 0: one detent clockwise, a full cycle */
static const uint32_t CCW[4] = {2, 3, 1, 0};

static void rest(uint32_t st, int n) { while (n--) frame(st); }
static int take(void) { int s = fm1_in.enc_steps[E]; fm1_in.enc_steps[E] = 0; return s; }

static void reset(uint32_t home)                      /* power-on with the knob on `home`: learned as the detent */
{
    int i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (i = 0; i < (int)FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
    rest(home, 60);
    take();
}

/* `det` full-cycle detents from state 0, `per` frames on each state; skip: only every other state is seen */
static int turn(int det, int per, int skip, int dir)
{
    int d, k, p;
    take();
    for (d = 0; d < det; d++)
        for (k = skip ? 1 : 0; k < 4; k += skip ? 2 : 1)
            for (p = 0; p < per; p++)
                frame(dir > 0 ? CW[k] : CCW[k]);
    rest(0, 60);
    return take();
}

int main(void)
{
    int per, got, k, d, p;
    for (per = 4; per >= 1; per /= 2) {
        reset(0);
        got = turn(10, per, 0, 1);
        CHECK(got == 10, "10 detents clockwise, %d frames a state: %d", per, got);
        reset(0);
        got = turn(10, per, 0, -1);
        CHECK(got == -10, "10 detents counter-clockwise, %d frames a state: %d", per, got);
    }
    /* a flick: one detent seen whole, then every other state skipped */
    reset(0);
    for (k = 0; k < 4; k++) frame(CW[k]);
    for (d = 0; d < 9; d++) for (k = 1; k < 4; k += 2) frame(CW[k]);
    rest(0, 60);
    got = take();
    CHECK(got == 10, "a flick (1 detent whole, then 9 with every other state skipped): %d", got);
    /* a bounce at rest */
    reset(0);
    frame(1); rest(0, 60);
    got = take();
    CHECK(got == 0, "a one-frame glitch at rest counted %d", got);
    /* a slow click that pauses halfway (state 1, a third of a second), then finishes, and five more */
    reset(0);
    rest(1, 500);
    for (k = 1; k < 4; k++) rest(CW[k], 4);
    rest(0, 60);
    got = take();
    got += turn(5, 4, 0, 1);
    CHECK(got == 6, "a long pause halfway through a click, then 5 more: %d (6)", got);
    /* the fault veecheech and the author saw: three clicks, a click left on the complement of the detent (state 3)
     * for a third of a second, finished, then five more: nine, not fourteen */
    reset(0);
    got = turn(3, 4, 0, 1);
    rest(1, 4); rest(3, 500); rest(2, 4); rest(0, 60);
    got += take();
    got += turn(5, 4, 0, 1);
    CHECK(got == 9, "a long pause on the complement halfway through a click, then 5 more: %d (9)", got);
    /* ...and a hand that pauses on the complement one click in four: still one a click. (A knob paused there on
     * every click is a half-cycle encoder as far as anyone can tell, and counts as one.) */
    reset(0);
    for (d = 0; d < 12; d++) { rest(1, 4); rest(3, d % 4 ? 4 : 300); rest(2, 4); rest(0, 300); }
    got = take();
    CHECK(got == 12, "12 slow clicks, one in four paused on the complement: %d (12)", got);
    /* a half-cycle encoder (it rests on 0 and 3 in turn): after a few clicks, one step a click, either way */
    for (p = 0; p < 2; p++) {
        uint32_t home = p ? 3u : 0u;
        int st = p ? 1 : 3;                                /* CW[st] is the state the knob is at */
        reset(home);
        for (d = 0; d < 6; d++) {                          /* warm-up: it learns its second rest */
            st = (st + 1) & 3; rest(CW[st], 4);
            st = (st + 1) & 3; rest(CW[st], 60);
        }
        take();
        for (d = 0; d < 10; d++) {
            st = (st + 1) & 3; rest(CW[st], 4);
            st = (st + 1) & 3; rest(CW[st], 60);
        }
        got = take();
        CHECK(got == 10, "a half-cycle encoder from %u, 10 slow clicks after warming up: %d (10)", home, got);
        for (d = 0; d < 10; d++) {
            st = (st + 3) & 3; rest(CW[st], 4);
            st = (st + 3) & 3; rest(CW[st], 60);
        }
        got = take();
        CHECK(got == -10, "a half-cycle encoder from %u, 10 slow clicks back: %d (-10)", home, got);
    }
    printf(fails ? "encoder: %d FAILED\n" : "encoder: ok (one step a click, whatever the detent)\n", fails);
    return fails != 0;
}
