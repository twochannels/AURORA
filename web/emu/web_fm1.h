/* SPDX-License-Identifier: GPL-3.0-only */
/* The fake FM-1 in the browser (after tests/fake_fm1.h): the device clock advances with the audio the worklet
 * asks for (web_render), each millisecond renders the I2S halves due and one pass of the app's loop; the page
 * drives the panel and reads the screen, the LEDs, MIDI out and the settings through the exports below.
 * The design (the clock driven by the audio the worklet asks for, the exports) follows Felucca's web/emu/felucca_web.c
 * (Leo Kuroshita, Hügelton Instruments) and X0X's (Charles Vestal), both GPL-3.0. platform.c includes this file
 * when DINGHY_WEB is set; web/emu/aurora_web.c is the translation unit. */
static void platform_poll(void) {}             /* nothing to poll: no USB, no watchdog; the clock is web_ms's */

/* MIDI out: a ring the page drains (web_midi_out_take) and sends to a Web MIDI output */
static uint32_t mo_ring[64], mo_w, mo_r;
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mo_w - mo_r >= 64u) return;
    mo_ring[mo_w++ % 64u] = (st & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16 | 0x1000000u;
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due, web_booted;

static void web_ms(void)                        /* one millisecond of the device */
{
    fm1_ms++;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        web_samples_due -= 1000u * HALF_FRAMES;
        fake_render_half();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)fake_blk[2u * k] / 32768.0f;
            ring[i + 1] = (float)fake_blk[2u * k + 1u] / 32768.0f;
        }
    }
    if (web_booted)
        app_loop();                             /* the main loop: a pass a millisecond (the device runs it faster) */
}

#define EXPORT __attribute__((used, visibility("default")))
static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT uint32_t web_sample_rate(void) { return FS; }
/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u) n = 1024u;
    while (ring_w - ring_r < n)
        web_ms();
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}
EXPORT void web_boot(void)                      /* after the page put the saved settings into web_settings() */
{
    if (web_booted) return;
    app_init();
    web_booted = 1;
}
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }

/* the panel: buttons bit = BTN_... id, keys bit k = key k (F3 .. G5), encoders by ENC_... id, MASTER 0..1023 */
EXPORT void web_buttons(uint32_t mask) { fake_buttons(mask); }
EXPORT void web_keys(uint32_t mask) { fake_keys(mask); }
EXPORT void web_enc(uint32_t id, int32_t n) { if (id < 7u) fake_enc[id] += n; }
EXPORT void web_master(uint32_t v) { fake_master = v > 1023u ? 1023 : (int32_t)v; }
/* a channel message from Web MIDI, as USB-MIDI or the TRS jack would deliver it: 0 = the queue was full */
EXPORT uint32_t web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st < 0x80u || (st >= 0xF0u && st != 0xF8u && st != 0xFAu && st != 0xFBu && st != 0xFCu)) return 1;
    return (uint32_t)fake_midi_put(st, d1, d2);
}
/* MIDI out: the next message as st | d1 << 8 | d2 << 16 | 1 << 24, 0 when there is none */
EXPORT uint32_t web_midi_out_take(void) { return mo_r == mo_w ? 0u : mo_ring[mo_r++ % 64u]; }

/* the screen: 240 x 240 RGB565 (the panel's byte order: high byte first); draws bumps on every change */
EXPORT uint16_t *web_screen(void) { return fb; }
EXPORT uint32_t web_screen_draws(void) { return fake_draws; }
/* the LEDs: bit = BTN_... id; bit k = key k */
EXPORT uint32_t web_leds_buttons(void) { return (uint32_t)fake_leds & 0x3FFFu; }
EXPORT uint32_t web_leds_keys(void) { return (uint32_t)(fake_leds >> 14) & 0x7FFFFFFu; }
/* the settings sector: 4 + 256 bytes ([0] = 1 saved, [1..2] length, the bytes); the page keeps it between visits */
EXPORT uint8_t *web_settings(void) { return fake_store; }
EXPORT uint32_t web_settings_size(void) { return sizeof fake_store; }
EXPORT uint32_t web_settings_saves(void) { return fake_saves; }
/* the user bank: 4 KiB; the page keeps it between visits as it keeps the settings */
EXPORT uint8_t *web_bank(void) { return fake_bank; }
EXPORT uint32_t web_bank_size(void) { return BANK_LEN; }
EXPORT uint32_t web_bank_saves(void) { return fake_bank_saves; }
