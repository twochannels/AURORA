# AURORA 0.6.0

The first release. An ambient instrument for the M-VAVE FM-1: a sound module for a dawless rig, built for playing
rather than sequencing. Vast, slow, alive.

**The sound.** A pad of eight voices of twelve sine partials (TILT, SPREAD, BRIGHT, BLOOM, DETUNE), with an ADSR,
velocity and a glide; on TONE 2 a STRETCH of the partials from string to bell, a SUB an octave under, a TUNE and a
DRIVE. A swarm of five saws under it through a filter that tracks the note. Air: wind against crackle through a
resonant band-pass a side, breathing on its own. Movement from four slow sources (a random LFO in every voice, a
random walk, a Lorenz attractor, a tide over bars of the MIDI clock) through a matrix into tilt, colour, pitch and
space. A Dattorro plate with a shimmer, a tape delay, a freeze that holds the delay line and plays grains off it, and
a master filter the Circuit Tracks' way: one knob, off in the middle, high-pass to the left, low-pass to the right.

**The screen.** Every page is a picture the four knobs shift, each part drawn in the colour of the knob that moves it.
HOME is an aurora: bent live by the movement's sources, coloured by the settings, lit by what is sounding, with a
ray for every held note; pressure lifts it, a bend slides it, the filter dims or thins it. FILTER, TILT, AIR and
MOVE are under its knobs, for playing.

**Playing it.** Twenty presets, from the slow and vast to the short and struck, and sixteen user slots (PRESETS;
SAVE, PRESETS, SAVE). PLAY latches: a latched key pressed again lets go, and the octave buttons take you to add
notes elsewhere. REC held is the freeze at full. SEQ is the autopilot, nine controls wandering round where they are
set. ARP morphs to a second scene over four seconds and back, SEL cuts. SELECT is pressure, ALGORITHM on HOME a
bend that springs back. Over MIDI: CC 80..87 are the eight macros in the Circuit Tracks' order (MOVE SPACE AIR TILT
WIDTH DRIFT FREEZE BLOOM), CC 74 the master filter, CC 7 the volume; aftertouch or an expression CC swells the pad;
program change picks a preset; the tide counts its bars from the clock.

**Installing.** From the site's install page in Chrome or Edge over USB, or from the FM-1 Firmware Hub. At your own
risk; OCT- held at power-on is the USB rescue, and any other firmware installs over AURORA the same way. The package
identity is FM-1_960. The manual is on the site.

Site: https://twochannels.github.io/AURORA/ . Manual: https://twochannels.github.io/AURORA/manual.html . Platform:
Dinghy (Sven Trogus), Felucca and SLOOP's hardware layer (Leo Kuroshita, Hügelton Instruments), FM-106's dual-core
worker (PattF), Felucca's web installer. GPL-3.0.
