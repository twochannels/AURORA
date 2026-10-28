# AURORA 0.4.1

The first release. An ambient instrument for the M-VAVE FM-1: a sound module for a dawless rig, built for playing
rather than sequencing.

**The sound.** A pad of eight voices of twelve sine partials (TILT, SPREAD, BRIGHT, BLOOM, DETUNE), with an ADSR and
velocity; a swarm of five saws under it through a filter that tracks the note; air, wind against crackle through a
resonant band-pass a side, breathing on its own; movement from four slow sources (a random LFO in every voice, a
random walk, a Lorenz attractor, a tide over bars of the MIDI clock) through a matrix into tilt, colour, pitch and
space; a Dattorro plate with a shimmer, a tape delay synced to clock, and a freeze that holds the delay line and
plays grains off it.

**The screen.** Every page is a picture the four knobs shift, each part drawn in the colour of the knob that moves it.
HOME is an aurora: bent live by the movement's sources, coloured by the settings, lit by what is sounding, with a
ray for every held note; pressure lifts it, a bend slides it.

**Playing it.** Twelve presets and sixteen user slots (PRESETS, SAVE). ARP morphs to a second scene and back, SEL
cuts. REC held is the freeze at full. SEQ is the autopilot, nine controls wandering round where they are set.
SELECT is pressure, ALGORITHM on HOME a bend that springs back. CC 80..87 are the eight macros in the Circuit
Tracks' order: MOVE SPACE AIR TILT WIDTH DRIFT FREEZE BLOOM; aftertouch or an expression CC swells the pad; program
change picks a preset; clock syncs the delay and the tide.

**Installing.** From the site's install page in Chrome or Edge over USB, or from the FM-1 Firmware Hub. At your own
risk; OCT- held at power-on is the USB rescue, and any other firmware installs over AURORA the same way. The package
identity is FM-1_960.

Site: https://twochannels.github.io/AURORA/ . Platform: Dinghy (Sven Trogus), Felucca and SLOOP's hardware layer
(Leo Kuroshita, Hügelton Instruments), FM-106's dual-core worker (PattF), Felucca's web installer. GPL-3.0.
