# AURORA

An ambient instrument for the M-VAVE FM-1: big, wide sounds that keep moving on their own and bend to MIDI. A
sound module for a dawless rig, built for playing rather than sequencing.

- **The pad**: eight voices of twelve sine partials with an ADSR envelope and velocity (VELO sets how much). TILT sets how fast the partials fall off, SPREAD balances odd
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
- **Space**: a Dattorro plate with a shimmer at any interval, a tape delay synced to MIDI clock.
- **Freeze**: FREEZE holds the delay line and plays a cloud of grains off it (GRAIN their length, SPRAY how far back
  they pick, SHIFT their pitch, an octave either way), on into the phaser and the plate: the texture stays after the
  hands have left. Half way it smears away over a minute, at the top it holds for ever.
- **The screen**: every page is a graphic the four knobs shift, each part drawn in the colour of the knob that
  moves it (EDIT, ENV, LFO and FX step through their groups): TONE is the spectrum, SHAPE the ADSR, TOUCH the bloom, swell and velocity, SWARM the saws under their filter, AIR the wind, MOVE the
  sources' traces, SOURCES and TARGETS the matrix, SPACE the tail, DELAY the repeats, FREEZE the grains on the
  line. HOME is the aurora itself, bent by the movement's sources and lit by what is sounding, with TILT, SPACE,
  AIR and FREEZE under the knobs for playing.
- **Presets**: twelve, from the slow and vast (DEEP, TIDE, CHOIR, ICE, STORM) through SWARM, SUB and ORGAN to the struck
  (GLASS, MALLET, PLUCK): the PRESETS encoder, or a program change. HOME shows the name, starred once edited.
- **Expression**: CC 80..87 (the Circuit Tracks' macros) are MOVE, SPACE, AIR, TILT, WIDTH, DRIFT, FREEZE, BLOOM;
  channel pressure or an expression CC swells the pad, and so does the SELECT encoder, with no controller;
  ALGORITHM on HOME bends the pitch and springs back; PLAY latches a drone.

Early days. To come: scenes with a morph, and an autopilot.

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
