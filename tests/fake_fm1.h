/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1, the part a computer and the browser share: the clock, the panel (keys, buttons, encoders, the
 * MASTER knob), the LEDs, the screen as a 240 x 240 RGB565 frame, the MIDI in queue and the second core (a job
 * runs at once on the caller). No stdio here: the browser build has none. What differs is in tests/host.c
 * (a script, a WAV, PPM screens) and web/emu/web_fm1.h (the exports). Derived from Dinghy's tests/host.c
 * (zvenson). platform.c includes this instead of the hardware when DINGHY_HOST or DINGHY_WEB is set. */
#include <stdint.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "AURORA"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FM1_POOL                                /* big buffers: an ordinary static on a computer */

/* ---- the clock */
static volatile uint32_t fm1_ms;
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * FM1_TICKS_PER_US; }
static uint32_t fm1_micros(void) { return fm1_ms * 1000u; }
static inline void fm1_irq_off(void) {}
static inline void fm1_irq_on(void) {}
static volatile uint32_t audio_lates;

/* ---- the second core: the job runs at once on the caller (the device's submit / join path, serial) */
typedef void (*audio_worker_fn)(void *);
static struct { uint32_t jobs, max_job_ticks, max_wait_ticks, timeouts, last_job_ticks; } audio_worker;
static int audio_worker_online = 1;
#ifndef HOST_THREAD
static int audio_worker_submit(audio_worker_fn fn, void *context) { fn(context); audio_worker.jobs++; return 1; }
static void audio_worker_join(void) {}
#else                                           /* -DHOST_THREAD: the job really runs on another thread, to catch races */
#include <pthread.h>
static pthread_mutex_t aw_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t aw_cv = PTHREAD_COND_INITIALIZER;
static audio_worker_fn aw_fn; static void *aw_ctx; static int aw_busy, aw_started;
static void *aw_thread(void *a)
{
    (void)a;
    for (;;) {
        pthread_mutex_lock(&aw_mu);
        while (!aw_busy) pthread_cond_wait(&aw_cv, &aw_mu);
        pthread_mutex_unlock(&aw_mu);
        aw_fn(aw_ctx);
        pthread_mutex_lock(&aw_mu); aw_busy = 0; pthread_cond_broadcast(&aw_cv); pthread_mutex_unlock(&aw_mu);
    }
    return 0;
}
static int audio_worker_submit(audio_worker_fn fn, void *context)
{
    pthread_t t;
    if (!aw_started) { aw_started = 1; pthread_create(&t, 0, aw_thread, 0); }
    pthread_mutex_lock(&aw_mu); aw_fn = fn; aw_ctx = context; aw_busy = 1; pthread_cond_broadcast(&aw_cv); pthread_mutex_unlock(&aw_mu);
    audio_worker.jobs++;
    return 1;
}
static void audio_worker_join(void)
{
    pthread_mutex_lock(&aw_mu);
    while (aw_busy) pthread_cond_wait(&aw_cv, &aw_mu);
    pthread_mutex_unlock(&aw_mu);
}
#endif

/* ---- the panel: whoever drives the fake sets fm1_in, fake_pressed / fake_released and fake_enc */
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t fake_pressed, fake_released, fake_notes_pressed;
static int32_t fake_enc[7];
static int32_t fake_master = 1023;              /* the MASTER knob as the ADC reads it, 0..1023 */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = fake_pressed;
    if (released) *released = fake_released;
    fake_pressed = fake_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = fake_notes_pressed; fake_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = fake_enc[e % 7u]; fake_enc[e % 7u] = 0; return s; }
static void fake_buttons(uint32_t mask)         /* the 14 buttons held now (bit = BTN_... id) */
{
    uint32_t old = fm1_in.buttons;
    mask &= (1u << 14) - 1u;
    fake_pressed |= mask & ~old;
    fake_released |= old & ~mask;
    fm1_in.buttons = mask;
}
static void fake_keys(uint32_t mask)            /* the 27 keys held now (bit k = key k, 0 = F3) */
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    fake_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? fake_master : 620; }
static uint32_t keys_analog_mask(void) { return 0; }          /* no analog rows on the fake: the keys are switches */
static void keys_sense_set(uint32_t on, uint32_t drive) { (void)on; (void)drive; }
static int keys_sense_live(void) { return 0; }
static uint32_t midi_in_drops(void) { return 0; }

/* ---- the LEDs: bit id, 0..13 the buttons, 14..40 the keys (KEY_LED) */
static uint64_t fake_leds;
static void fm1_led_key(uint32_t id, int on)
{
    if (id > 40u) return;
    if (on) fake_leds |= (uint64_t)1u << id; else fake_leds &= ~((uint64_t)1u << id);
}

/* ---- the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static uint32_t fake_draws;                     /* bumps on every draw: the page sends the frame when it changed */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fake_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fake_draws++;
}
#ifndef CV_MAX
#define CV_MAX (240u * 124u)
#endif
#include "gfx.c"

/* ---- MIDI in: a queue whoever drives the fake fills (fake_midi_put) */
static uint8_t mq[256][3];
static uint32_t mq_w, mq_r;
static int fake_midi_put(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mq_w - mq_r >= 256u) return 0;
    mq[mq_w % 256u][0] = (uint8_t)st; mq[mq_w % 256u][1] = (uint8_t)d1; mq[mq_w % 256u][2] = (uint8_t)d2;
    mq_w++;
    return 1;
}
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 256u][0]; *d1 = mq[mq_r % 256u][1]; *d2 = mq[mq_r % 256u][2];
    mq_r++;
    return 1;
}

/* ---- settings: a 256-byte blob in RAM; the driver keeps it (host: for the run; the page: in the browser) */
static uint8_t fake_store[4 + 256];             /* [0] = 1 when saved, [1..3] = length, then the bytes */
static uint32_t fake_saves;
static int settings_load(void *p, uint32_t n)
{
    uint32_t len = fake_store[1] | (uint32_t)fake_store[2] << 8;
    if (fake_store[0] != 1 || n > 256u || len != n) return -1;
    memcpy(p, fake_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > 256u) return -1;
    fake_store[0] = 1;
    fake_store[1] = (uint8_t)n; fake_store[2] = (uint8_t)(n >> 8); fake_store[3] = 0;
    memcpy(fake_store + 4, p, n);
    fake_saves++;
    return 0;
}

/* ---- the user bank: 4 KiB the app lays out; the driver keeps it (the page: in the browser) */
#define BANK_LEN 0x1000u
static uint8_t fake_bank[BANK_LEN];
static uint32_t fake_bank_saves;
static int bank_load(void *p, uint32_t n) { if (n > BANK_LEN) return -1; memcpy(p, fake_bank, n); return 0; }
static int bank_save(const void *p, uint32_t n) { if (n > BANK_LEN) return -1; memcpy(fake_bank, p, n); fake_bank_saves++; return 0; }

/* ---- meters for the host tests: the reverb lines' knee count, each stage's peak in the effects chain */
static uint32_t rv_hot, dly_hot;
static int platform_screen_take(void) { return 0; }   /* (the updater is the device's) */
static int32_t dbg_peak[4];                     /* the voices' sum, after the chorus, the delay, the reverb */
static inline void dbg_meter(uint32_t k, const int32_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) { int32_t a = p[i] < 0 ? -p[i] : p[i]; if (a > dbg_peak[k]) dbg_peak[k] = a; }
}

/* ---- what the app supplies, and what the driver (host.c / web_fm1.h) supplies */
static void app_audio(int32_t *out, uint32_t n);
static void app_init(void);
static void app_loop(void);
static void platform_poll(void);
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2);

/* one audio half, as the ISR renders it: zeroed, the app adds, Q15 clipped (the MASTER knob all the way up:
 * the WAV and the page hear what USB audio hears) */
static int32_t fake_blk[HALF_WORDS];
static void fake_render_half(void)
{
    uint32_t i;
    memset(fake_blk, 0, sizeof fake_blk);
    app_audio(fake_blk, HALF_FRAMES);
    for (i = 0; i < HALF_WORDS; i++) {
        int32_t v = fake_blk[i];
        fake_blk[i] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    }
}
