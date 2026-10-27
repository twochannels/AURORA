// SPDX-License-Identifier: GPL-3.0-only
// The browser build (build/emu/aurora.wasm) in Node, as the worklet runs it. After Felucca's web/emu/emu_test.mjs and
// X0X's. Checks: it boots and draws, a held key sounds and lights its LED and goes out over MIDI, a MIDI note in
// sounds, OCT+ changes the octave and the settings reach the sector once the sound stops, the saved sector comes back
// in a fresh instance, the same gestures twice give the same audio, and 1 s of audio costs under BENCH_MAX of real time.
//   node web/emu/emu_test.mjs [build/emu/aurora.wasm]
import fs from "fs";

const wasmPath = process.argv[2] || "build/emu/aurora.wasm";
const bytes = fs.readFileSync(wasmPath);
const BTN = { OCT_DOWN: 0, OCT_UP: 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
let fails = 0;
const check = (what, ok) => { console.log(`emu: ${what.padEnd(70)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

async function device(settings) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports, mem = ex.memory;
  ex._initialize();
  if (settings) new Uint8Array(mem.buffer, ex.web_settings(), ex.web_settings_size()).set(settings);
  ex.web_boot();
  const d = { ex, peak: 0, sig: 0, rec: false, frames: 0, midi: [] };
  d.render = (ms) => {
    const n = Math.round(ms * 44.1);
    d.peak = 0;
    for (let k = 0; k < n; k += 128) {
      const m = Math.min(128, n - k);
      ex.web_render(m);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), m), r = new Float32Array(mem.buffer, ex.web_out_r(), m);
      for (let i = 0; i < m; i++) {
        d.peak = Math.max(d.peak, Math.abs(l[i]), Math.abs(r[i]));
        if (d.rec) d.sig = (d.sig * 31 + Math.round(l[i] * 32768) + 7 * Math.round(r[i] * 32768)) % 1000000007;
      }
      d.frames += m;
    }
    for (let p; (p = ex.web_midi_out_take()); ) d.midi.push([p & 0xFF, (p >> 8) & 0x7F, (p >> 16) & 0x7F]);
  };
  d.tap = (b) => { ex.web_buttons(1 << b); d.render(60); ex.web_buttons(0); d.render(60); };
  d.screen = () => new Uint16Array(mem.buffer, ex.web_screen(), 240 * 240).slice();
  d.settings = () => new Uint8Array(mem.buffer, ex.web_settings(), ex.web_settings_size()).slice();
  return d;
}
const colours = (fb) => new Set(fb).size;

// ---- boot, a key, MIDI out, the LED, MIDI in
const a = await device(null);
a.render(300);
check("boots silent, draws the home screen", a.peak === 0 && colours(a.screen()) >= 3);
a.ex.web_keys(1 << 7);                            // C4
a.render(300);
check("a held key sounds", a.peak > 0.05);
check("its LED is lit", (a.ex.web_leds_keys() >> 7) & 1);
check("the note went out over MIDI", a.midi.some((m) => m[0] === 0x90 && m[1] === 60));
a.ex.web_keys(0);
a.render(100);                                    // the release
const early = a.peak;
a.render(2400);
a.render(500);                                    // the reverb tail, well down by now
check("released: the tail falls by 10 dB in 3 s, its LED out", a.peak < early * 0.32 && !((a.ex.web_leds_keys() >> 7) & 1));
a.ex.web_midi(0x90, 69, 100);
a.render(200);
check("a MIDI note in sounds", a.peak > 0.05);
a.ex.web_midi(0x80, 69, 0);
a.render(300);

// ---- OCT+, the settings sector, back in a new instance
const saves0 = a.ex.web_settings_saves();
a.tap(BTN.OCT_UP);
check("OCT+ lights its LED", (a.ex.web_leds_buttons() >> BTN.OCT_UP) & 1);
a.render(9000);                                   // 2 s after the change, once the reverb tail is under -36 dBFS
check("the settings reach the sector once the tail is quiet", a.ex.web_settings_saves() === saves0 + 1);
const b = await device(a.settings());
b.render(100);
check("a fresh instance with the saved sector starts an octave up", (b.ex.web_leds_buttons() >> BTN.OCT_UP) & 1);


// ---- deterministic
async function run() {
  const d = await device(null);
  d.render(100); d.rec = true;
  d.ex.web_keys(1 << 7 | 1 << 11); d.render(400); d.ex.web_keys(0); d.render(400);
  return d.sig;
}
check("the same gestures twice give the same audio", (await run()) === (await run()));

// ---- the cost: 1 s of four voices against real time
const c = await device(null);
c.render(100);
c.ex.web_keys(1 << 0 | 1 << 4 | 1 << 7 | 1 << 11);
const t0 = performance.now();
c.render(1000);
const share = (performance.now() - t0) / 1000;
const max = +(process.env.BENCH_MAX || 0.5);
check(`1 s of audio in ${(share * 100).toFixed(1)} % of real time (max ${max * 100} %)`, share < max);

console.log(fails ? `emu: ${fails} FAILED` : "emu: all ok");
process.exit(fails ? 1 : 0);
