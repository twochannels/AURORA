# AURORA

An ambient instrument for the M-VAVE FM-1: big, wide sounds that keep moving on their own and bend to MIDI. A
sound module for a dawless rig, built for playing rather than sequencing.

- **The pad**: eight voices of twelve sine partials. TILT sets how fast the partials fall off, SPREAD balances odd
  against even from hollow to full, BRIGHT is a ceiling they fade under, BLOOM staggers their entry, DETUNE spreads
  each low partial into a pair so the pad shimmers and a chord's beats are smeared rather than bare.
- **The swarm**: under the pad, five saws a little apart (SPREAD) through a low-pass (CUTOFF, RESO) that tracks the
  note and follows TILT, the movement's colour and pressure; each saw wanders by DRIFT. LEVEL brings it in.
- **The air**: noise shaped. Wind against crackle (TEXTURE) through a resonant band-pass a side (TONE, wandering at
  RATE, bent by the movement's colour), swelling in with the pad and lingering after it, and breathing in and out on its own at the
  pace BREATH sets; AIR, also the macro, sets how much.
- **Movement**: four slow sources, WANDER (a random LFO in every voice), WALK (a random walk), CHAOS (a Lorenz
  attractor) and TIDE (a slow sine over bars of the MIDI clock), into TILT, COLOUR, PITCH and SPACE through a fixed
  pattern; MOVE scales it all, RATE its speed, and DRIFT walks every partial besides.
- **Width**: odd partials lean left, even right (WIDTH); mono-safe.
- **Space**: a Dattorro plate with a shimmer at any interval, a tape delay synced to MIDI clock, a phaser.
- **Expression**: CC 80..87 (the Circuit Tracks' macros) are MOVE, SPACE, AIR, TILT, WIDTH, DRIFT, FREEZE, BLOOM;
  channel pressure or an expression CC swells the pad; PLAY latches a drone.

Early days. To come: freeze (grains on the delay line), scenes with a morph, and an autopilot.

## Building

The same platform and tools as [CS-1](https://github.com/twochannels/CS-1): `tests/run_host.sh` runs the firmware on
a computer (a WAV and the screens), `web/emu/build.sh` makes the browser emulator, `web/site/build.sh` the site,
`./build.sh` the device package (the JieLi toolchain in Docker; `tools/fm1_install.py build/aurora.fwsc` installs it).
`tests/run_tests.sh` runs everything. The package identity is `FM-1_96N`.

Installing any custom firmware is at your own risk: hold OCT- at power-on for the USB rescue. The official firmware
and any firmware with Felucca's or M-VAVE's update loader install over AURORA.

## Licence

GPL-3.0. The platform is Dinghy (Sven Trogus) on Felucca and SLOOP's hardware layer (Leo Kuroshita, Hügelton
Instruments), with FM-106's dual-core worker (PattF) and Felucca's web installer: see LICENSING.md.
