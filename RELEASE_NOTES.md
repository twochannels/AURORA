# AURORA 0.7.7

Epic film sounds for the M-VAVE FM-1: the horns arrive, the keys play with their own velocity and pressure, and the
delay follows the clock. Everything since 0.6.0.

**METAL.** A new page under EDIT, after TONE 2: a ring modulator on the pad and its sub. RING is how much, RATIO the
second sine's pitch as a multiple of the note, 0.5 to 8. Whole numbers stay in tune and thicken; the places between
are bells, gongs and metal. The page draws where every overtone goes. On HOME the fourth knob is now METAL, in place
of MOVE, so the clang is under a hand while playing; MOVE keeps its own page.

**Four horns.** BRAAM, FOGHORN, WARHORN and TITAN follow PLUCK as presets 20 to 23: the film-trailer horn built on
RING, with a sub, a drive, a slow attack and BLOOM opening it. Play them one note at a time, low, and lean on the
key. Twenty-four presets now; the USER slots are program changes 24 to 39.

**The keys.** They sense their own velocity and pressure: a strike is VELO, a lean is SWELL on that note, and both go
out over MIDI as velocity and polyphonic aftertouch. KEYS on the MIDI page turns the sensing off if a key misbehaves.

**SYNC.** Under a MIDI clock the delay's TIME becomes a note division, 1/32 to 1/2, following the tempo; the DELAY
page shows the division. Without a clock it is a time, as before.

**Sending a sound.** SAVE, then GLO sends the current sound over USB MIDI as one SysEx message. The site's share page
catches it and turns it into text: every control's value, to keep, to paste, or to send in as a preset with one click.

**Fixed.** Scene B's edits are kept across a power-off. Until now B was rebuilt from its preset at power-on, so a
sound built at B was lost unless it had been saved to a USER slot from B (the knobs edit the scene you are at; the
manual now says so). Settings from earlier versions carry over. VOLUME is applied after DRIVE, so on a driven sound it is a level and not less grit. The manual says
MASTER is the output volume (the FM-1 has one) and lists USB audio recording.

**Note.** The sound's data grew, so the sixteen USER slots are cleared by this update. Save them again afterwards.

**Installing.** From the site's install page in Chrome or Edge over USB, or from the FM-1 Firmware Hub. At your own
risk; OCT- held at power-on is the USB rescue, and any other firmware installs over AURORA the same way. The package
identity is FM-1_4747 (it was FM-1_960, which another firmware already had). The manual is on the site, with three recordings of the firmware on the front page.

Site: https://twochannels.github.io/AURORA/ . Manual: https://twochannels.github.io/AURORA/manual.html . Platform:
Dinghy (Sven Trogus), Felucca and SLOOP's hardware layer (Leo Kuroshita, Hügelton Instruments), FM-106's dual-core
worker (PattF), Felucca's web installer. GPL-3.0.
