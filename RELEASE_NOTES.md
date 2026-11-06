# AURORA 0.9.1

Epic film sounds for the M-VAVE FM-1: newer FM-1s, a drone under another sound, a bus on the way out, the horns, the keys playing with their own velocity and
pressure, and the delay following the clock. Everything since 0.6.0.

**Newer FM-1s.** Units sold since autumn 2026 have a different flash chip (the UC25HQ80, where the first units have
a Puya P25Q80). AURORA took it for unknown: its update loader refused to install, so those units could not run it at
all. The loader, the firmware and the recovery screen now accept both chips. Nothing changes on the first units.

**A drone under another sound.** A note keeps the sound it was played with. A cut (SEL, a turn of PRESETS, a
program change) leaves every note that is sounding as it was, release tails included, and only what you play next
is the new sound. So: PLAY, hold a chord, let go, SEL or PRESETS, play. The chord goes on in its own sound under a
lead from the other, a mono lead included; the latch comes off at the cut so the lead plays free, PLAY stays lit
while the drone is held and lets it go. A lead note never takes a drone voice while another is free. The space
(delay, plate, FREEZE, the master FILTER, DRIVE, AIR) is the live sound's, and the drone goes through it with the
lead. ARP's morph is not a cut: it carries everything along, as before. Nothing in the settings changes.

**The bus.** A fourth page under FX, after everything else: LOW and HIGH shelves (about 120 Hz and 4 kHz, 12 dB
either way) and a limiter. LIMIT at 0 is off; turning it up brings the level it holds down from -2 dBFS to -20 and
makes the same up underneath, so a little catches the odd peak and a lot is a wall. It looks three milliseconds
ahead, so the gain is down before a peak arrives rather than snapping at it, and nothing gets past it; RELEAS sets
how fast it lets go, 30 ms to 3 s; the page shows how far it is holding the sound down. (0.8.0's looked only a
third of a millisecond ahead and snapped at every swell of a pad: crackles and pops. 0.8.1 fixes that, the top two
steps of RELEAS, and the shelves stepping when turned.) The bus adds three milliseconds on the way out. The bus is the rig's, not the sound's: it stays through every preset and is not
part of a sent sound. At its defaults the output is exactly what it was. (The settings grow by four bytes; your
sound, scene B and MIDI settings carry over.)

**METAL.** A new page under EDIT, after TONE 2: a ring modulator on the pad and its sub. RING is how much, RATIO the
second sine's pitch as a multiple of the note, 0.5 to 8. Whole numbers stay in tune and thicken; the places between
are bells, gongs and metal. The page draws where every overtone goes. On HOME the fourth knob is now METAL, in place
of MOVE, so the clang is under a hand while playing; MOVE keeps its own page.

**Four horns.** BRAAM, FOGHORN, WARHORN and TITAN follow PLUCK as presets 20 to 23: the film-trailer horn built on
RING, with a sub, a drive, a slow attack and BLOOM opening it. Play them one note at a time, low, and lean on the
key. Then two sounds sent in through the share page by veecheech: QUANTUM, lush strings, and PULSAR, the bass meant
as its scene B. Twenty-six presets now; the USER slots are program changes 26 to 41.

**The keys.** They sense their own velocity and pressure: a strike is VELO, a lean is SWELL on that note, and both go
out over MIDI as velocity and polyphonic aftertouch. KEYS on the MIDI page turns the sensing off if a key misbehaves.

**SYNC.** Under a MIDI clock the delay's TIME becomes a note division, 1/32 to 1/2, following the tempo; the DELAY
page shows the division. Without a clock it is a time, as before.

**Sending a sound.** SAVE, then GLO sends the current sound over USB MIDI as one SysEx message. The site's share page
catches it and turns it into text: every control's value, to keep, to paste, or to send in as a preset with one click.

**The load, and no clicks.** A player heard clicks on the heaviest settings: eight voices, METAL, DRIVE, a long
SHIMMR, the delay and FREEZE at once ran the main core at 85 % of a block while the second core sat at 61, because
the main core rendered four voices, waited for the other's four and the reverb, then did everything after on its
own. Now the second core also takes the delay (its echoes a block, 5.8 ms, later: nothing to hear), and the
voices are split by measurement, a voice a block, so the two cores finish together. The swarm's five saws are
written out instead of looped, the same to the sample; partials under -66 dB keep time but are not summed; and
a guard lets the top partials go, a block at a time, if a block still runs past 90 % of its time, bringing them
back when there is room: a dulling at the edge, never a click. (0.7.15's guard measured the block wrongly and
trimmed from a quarter of the load; 0.7.16 corrects it.) On an FM-1 the same heaviest settings now read 76/65, and the
guard never fires. The MIDI page's bottom line now reads LOAD for
each core and the split (5+3); a ! before the split means the guard is trimming.

**The save screen.** Press SAVE and the screen now shows the sixteen USER slots, the one PRESETS has picked lit and the
used ones marked, with the three choices under them: turn PRESETS to pick, SAVE keeps the sound there, GLO sends it
out. Before, only the header changed, and how to reach another slot was anyone's guess.

**The knobs.** A click at a time moves a 0..127 control by one, so every value is reachable; clicks in quick
succession move it by two as before. A value no longer slips down by one at scene B (the morph mix is now
rounded). And the fault behind PRESETS skipping every other sound is gone: a click left half-finished, with the
encoder parked between detents, used to teach it a second detent, and every click after that counted twice (the
knobs then jumped by four). The position the knob is in at power-on is now the detent, and a second one is only
believed when the knob keeps resting there, as an encoder with two clicks a cycle does. After X0X's fix of the
same fault. A host test now turns a simulated encoder through every case.

**Fixed.** SAVE while at scene B kept B but also replaced A with it, and offered A's slot, so saving a B sound could
wipe the A sound twice over; now B goes to its own slot (or the first empty one) and A is left alone. Scene B's edits are kept across a power-off. Until now B was rebuilt from its preset at power-on, so a
sound built at B was lost unless it had been saved to a USER slot from B (the knobs edit the scene you are at; the
manual now says so). Settings from earlier versions carry over. VOLUME is applied after DRIVE, so on a driven sound it is a level and not less grit. The manual says
MASTER is the output volume (the FM-1 has one) and lists USB audio recording.

**Saved sounds survive updates.** USER slots are now read by the length of the sound when they were written, so
from here on a new control loads at its default instead of clearing the bank. Slots saved on 0.7.1 to 0.7.10 carry
over. (Slots from 0.6.0 were cleared at 0.7.1, when the sound grew; that was the last time.)

**Installing.** From the site's install page in Chrome or Edge over USB, or from the FM-1 Firmware Hub. At your own
risk; OCT- held at power-on is the USB rescue, and any other firmware installs over AURORA the same way. The package
identity is FM-1_4747 (it was FM-1_960, which another firmware already had). The manual is on the site, with three recordings of the firmware on the front page.

Site: https://twochannels.github.io/AURORA/ . Manual: https://twochannels.github.io/AURORA/manual.html . Platform:
Dinghy (Sven Trogus), Felucca and SLOOP's hardware layer (Leo Kuroshita, Hügelton Instruments), FM-106's dual-core
worker (PattF), Felucca's web installer. GPL-3.0.
