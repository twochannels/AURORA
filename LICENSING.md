# Licensing

AURORA is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| Dinghy: the wiring `platform.c`, the host mode `tests/host.c`, the docs | zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |
| The second core as an audio worker (`firmware/hal/fm1_multicore.h`, `firmware/src/audio_worker.h`, the CPU1 entry in `crt0.S`, the checks in `tools/build.py`, `tests/audio_worker*_test.c`) | PatttF ([FM-106](https://github.com/patttf/fm-106)), after the AC79 SDK (Apache-2.0) |
| MIDI IN on the TRS jack, `firmware/src/midi_uart.c` | Leo Kuroshita (Felucca), as adapted in FM-106 |
| The app `firmware/src/aurora.c`, the browser emulator `web/emu/`, the CS-80 voice | twochannels |
| The keys' velocity and pressure sensing `firmware/src/sense.c` (its velocity map in fixed point here) and the analog rows in `firmware/hal/fm1_input.h`, `fm1_adc.h` (Velo, `charlesvestal/fm1-velocity`) | Charles Vestal, on the Felucca HAL |
| The browser installer `web/site/fm1ota.js` (Felucca's `web/fm1ota.js`, unchanged) | Leo Kuroshita, Hügelton Instruments |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

Dinghy's logo (the designburg signet) is not under the GPL and is not included; AURORA has no logo yet.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. "CS-80" is Yamaha's. AURORA is not made or endorsed by any of them.
