// SPDX-License-Identifier: GPL-3.0-only
// AURORA in an AudioWorklet: aurora.wasm is the whole firmware against a fake FM-1 (web/emu/web_fm1.h). Each render
// quantum asks it for 128 frames, which runs the device's clock forward; between quanta the page's input goes in
// and, about 30 times a second (4 when the page is hidden), the screen, the LEDs, MIDI out and a changed settings
// sector go out. After Felucca's web/emu/worklet.js (Leo Kuroshita, Hügelton Instruments) and X0X's (Charles Vestal).

export const HOLD_MS = 24;        // a button or key change stays at least this long of device time before the next

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class AURORA extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentSaves = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          if (m.settings) {                       // the saved settings sector, before boot
            const st = new Uint8Array(ex.memory.buffer, ex.web_settings(), ex.web_settings_size());
            const b = new Uint8Array(m.settings);
            if (b.length === st.length) st.set(b);
          }
          if (m.bank) {                           // the user bank
            const bk = new Uint8Array(ex.memory.buffer, ex.web_bank(), ex.web_bank_size());
            const b = new Uint8Array(m.bank);
            if (b.length === bk.length) bk.set(b);
          }
          ex.web_master(m.master ?? 1023);
          ex.web_boot();
          this.ex = ex;
          this.sentSaves = ex.web_settings_saves();
          this.sentBank = ex.web_bank_saves();
          {                                       // the patch's field names, for "Copy sound"
            const mem = new Uint8Array(ex.memory.buffer), p = ex.web_patch_fields();
            let e = p; while (mem[e]) e++;
            this.fields = new TextDecoder().decode(mem.subarray(p, e)).split(",");
          }
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate(), fields: this.fields });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.web_enc(m.id | 0, m.n | 0);
      else if (m.type === "master") ex.web_master(m.value | 0);
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [ex.web_leds_buttons(), ex.web_leds_keys()];
      const key = leds.join(",");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const saves = ex.web_settings_saves();
      if (saves !== this.sentSaves) {
        this.sentSaves = saves;
        msg.settings = new Uint8Array(mem, ex.web_settings(), ex.web_settings_size()).slice();
        transfer.push(msg.settings.buffer);
      }
      const bsaves = ex.web_bank_saves();
      if (bsaves !== this.sentBank) {
        this.sentBank = bsaves;
        msg.bank = new Uint8Array(mem, ex.web_bank(), ex.web_bank_size()).slice();
        transfer.push(msg.bank.buffer);
      }
      {                                           // the sound now, when it changed
        const pt = new Uint8Array(mem, ex.web_patch(), ex.web_patch_size());
        if (!this.sentPatch || pt.some((v, i) => v !== this.sentPatch[i])) { this.sentPatch = pt.slice(); msg.patch = Array.from(pt); }
      }
      const out = [];
      for (let p; (p = ex.web_midi_out_take()); ) out.push([p & 0xFF, (p >> 8) & 0x7F, (p >> 16) & 0x7F]);
      if (out.length) msg.midi = out;
      msg.ms = ex.web_now_ms();
      this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce and the app reads them once a pass: each change stays HOLD_MS of device time before the next.
    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
        else ex.web_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.web_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      return true;
    }
  }
  registerProcessor("aurora", AURORA);
}
