# EVOL-1

EVOL-1 turns the M-VAVE FM-1 into a tracker built round the sound of *Evol*, RedtailWorks' album: dark,
sub-heavy, slow-moving electronics. You write notes into a grid of six tracks and sixteen rows, one effect a
cell, and chain the patterns into a song. The tracks play the album's own palette: soft sine kicks, the Eb
harmonic stack over D, pads pumping at the tempo, FM bells on fourths and fifths, a phrygian lead gliding a
semitone, a throat in the low mids, glitches and noise, through a dotted-eighth ping-pong and a long dark
plate. Evolve lets a pattern drift a little on every pass; Grow writes one in the album's ways.

This manual covers EVOL-1 version 0.1.0.

> **Experimental.** EVOL-1 is new: it has been built and tested in the simulator and the browser emulator,
> not yet on a real FM-1. Install it at your own risk; HYP-1, or M-VAVE's own firmware, goes back on the
> same way.

![EVOL-1 on the FM-1: Deep Mutation playing](img/evol-hero.png)

---

## Contents

1. Install
2. Getting started
3. Controls
4. The screen
5. Writing a pattern
6. The song
7. The sounds
8. The effects
9. Evolve and Grow
10. The pages
11. MIDI clock and your DAW
12. Saving and the songs
13. Troubleshooting
14. Credits

---

## 1. Install

**Try it first:** the emulator runs the same code in the browser, with sound, played with the mouse, a
touch screen or the keyboard.

**Install:** open the installer in Chrome or Edge, connect the FM-1 with a USB data cable and press
Install. Don't unplug it while it's writing; when it finishes, the FM-1 restarts into EVOL-1.

From the command line instead (needs `pip3 install mido python-rtmidi`):
`python3 tools/fm1_install.py evol1-VERSION.fwsc`, with the file from the site's download.

**Go back:** "Back to the stock firmware", at the bottom of the installer, puts M-VAVE's firmware back.
M-VAVE's own updater, M-UPGRADE, works too. EVOL-1 keeps its songs clear of the flash M-VAVE's firmware uses
for samples.

> Installing other firmware is at your own risk. If the FM-1 won't start, hold OCT− and OCT+ together for
> five seconds for the update mode, then install again.

---

## 2. Getting started

EVOL-1 plays from the moment it's on: the first song, Deep Mutation, from its drone intro. Then:

1. Press **PLAY** to stop it. You're on the **Pattern** page (HOME): the grid of the pattern at the song's
   first position.
2. Turn **SELECT** to move down the rows, **ALGORITHM** across the tracks. The cursor's cell is boxed.
3. Play the **keys**: they play the cursor's track. On a kit's track the white keys are its eight drums.
4. Press **REC**. Now each key writes its note into the cell and the cursor steps down a row. Write a few.
5. Press **PLAY**: the pattern plays from the top, the row playing lit orange. Write while it plays and
   the notes go into the row that's playing.
6. Turn **KNOB 2** on a cell with a note: an effect. **KNOB 3** its value. Try R (Ratchet) or B (Bend).
7. Press **SAVE** to keep the song.

Hold any button for a second and a card says what it does.

---

## 3. Controls

![Every control, labelled](img/fm1-panel.svg)

| Control | What it does |
| --- | --- |
| Keys | The cursor's track (a kit: white keys 1–8 its drums). With REC on, on Pattern or Grow, they write |
| PLAY | Play from the top of the position on screen / stop. SEL + PLAY: from the cursor's row |
| REC | Edit: the keys write notes, and the cursor steps down |
| EDIT | Clear the cell (and step down, with REC on). SEL + EDIT: a note-off. On Grow and Song, see there |
| SEL | Follow on / off (the screen follows the song as it plays); held, a shift |
| FX | Mute the cursor's track; held, the mutes: black keys 1–6 mute, white keys 1–6 solo |
| SELECT | The row (with SEL: four at a time); on Song, the position; elsewhere every page in turn. HOME + SELECT: tempo |
| ALGORITHM | The track (the cursor's, on every page) |
| PRESETS | The song (five) |
| HOME · SEQ · ENV · LFO · ARP · GLO | Pattern · Song · Sound (again: Move, Sends) · Space (again: Groove) · Grow · Setup (again: Device) |
| KNOB 1–4 | The page's four values. On Pattern: the cell's note, effect and value, and the position's pattern |
| OCT− / OCT+ | The keys an octave down / up. With SEL, the cell's note a semitone |
| SAVE | Save the song (and the device's settings). Held 1.5 s on Setup: the song as it came |
| SEL + GLO | Remote: the FM-1's panel plays EVOL-1 in the browser emulator |

---

## 4. The screen

![The pages](img/evol-pages.png)

The **header** shows the song's number and name, REC when you're writing, play or stop, and the tempo (EXT
and the clock's tempo while MIDI clock comes). A dot top right: something isn't saved yet.

The **Pattern** page's grid: the pattern's number top left, then a head for each track (its sound, short,
and a level bar under it, in the track's colour), and ten of the sixteen rows. Rows 00, 04, 08 and 12 (the
beats) are marked. Each cell shows its note (C#3), a kit's drum (KIK), or a note-off (two bars), then its
effect's letter and value (R3). The cursor's cell is boxed, magenta (orange while REC is on), and the row
playing is lit orange.

The other track pages show a **mixer**: the six tracks, their sounds and levels, the cursor's ringed.

The **knob strip** at the bottom names the page (and the cursor's track and sound) and shows the four knobs.

---

## 5. Writing a pattern

A **pattern** is sixteen rows, a bar of sixteenths, for six tracks. Each cell holds:

- a **note** (C1 to B8), a kit's **drum**, a **note-off** (the track's notes let go), or nothing;
- one **effect**: a letter and a value, 0 to F (see 8).

A note on a track holds until the next note or a note-off on that track (a drum rings out on its own). A pad
or drone given the note it already holds keeps holding it, so a drone can sit through pattern after pattern.

To write: press **REC**, put the cursor where you want with SELECT and ALGORITHM, and play the keys. Each
note goes in and the cursor steps down a row. **EDIT** clears the cell; **SEL + EDIT** writes a note-off;
**SEL + OCT−/OCT+** moves the cell's note a semitone. Or turn **KNOB 1** to set the note: empty, then off,
then the notes (on a kit, its eight drums).

While the song plays and **Follow** is on (SEL), the keys write into the row that's playing on the cursor's
track: play along and it's recorded, a row at a time.

**OCT− / OCT+** move the keys an octave (three each way). The keys start at F3.

---

## 6. The song

![The Song page](img/evol-device-song.png)

The **Song** page (SEQ) is the order list: up to 32 positions, each playing one of the sixteen patterns.
SELECT moves along it; **KNOB 2** sets the position's pattern (KNOB 4 on Pattern does too). **Length** sets how
many positions play, and **Loop** on Groove plays the song through (Song) or round the pattern playing
(Pattern).

**EDIT** on Song copies the position's pattern into the first pattern no position plays, and the position
plays the copy: a quick way to make a variation. **SEL + EDIT** clears the position's pattern.

A **J** effect jumps to a position after its row.

---

## 7. The sounds

Each track plays one sound, chosen with **Sound** (ENV, KNOB 1). Twenty-four, after the album. Measured from
the record: two thirds of its energy under 150 Hz, kicks at 45–55 Hz falling from about 120, the Eb series
(78, 156, 312 Hz...) over a D centre, volume pulsing at 1.9–2.7 Hz, a dotted-eighth echo and a dark reverb of
three to five seconds.

| Sound | What it is |
| --- | --- |
| Evol kit | KIK (a soft sine falling from 120 Hz), TIK, SNR (muffled), TOM, MTL (metal), HAT, CLP, DRP (a bass drop) |
| Ritual kit | FRM (frame drum), LOG, BLK (block), SHK (shaker), CLV (clave), TOM, BRE (breath), GNG (gong) |
| Glitch kit | KIK (crushed), CLK (click), BIT, ZAP, NZE, BLP, CRK (crackle), SUB |
| Sub root | Sine and a little triangle, an octave down: the album's floor |
| Drop bass | A sine falling an octave as it plays |
| Pulse bass · Reso bass | A 30% pulse, low passed; a saw through a resonant sweep |
| Eb stack | Three saws a few cents apart through a resonant 1.2 kHz low pass, an octave down: the harmonic drone |
| Pump pad · Wide pad | Three detuned saws, made to pump with Pulse; wider, slower, softer |
| Throat | A saw through a band near 320 Hz: the low mids' growl (Motion moves the vowel) |
| Sweep | A resonant band sweeping two octaves over seconds |
| Cold choir | Slow FM, a vowel's band |
| FM bell · Dark pluck | Two-operator FM at 1:3.5 and 1:2: the fourths-and-fifths ostinatos |
| Phrygian | A 30% pulse, two a few cents apart, low passed at 1.5 kHz: the lead (glide it with G) |
| Glass · Hollow | High thin FM; a triangle through a soft band |
| Noise bed · Static · Wind | Noise low passed, in a radio's band, through a slow resonant band |
| Glitch · Bugs | Crushed noise bursts; FM chirps falling fast |
| Rumble | A low saw cluster through a dark low pass, driven |

On a kit, a note's pitch picks its drum (36–43, C2–G2, the eight in order); the white keys play them.

---

## 8. The effects

One effect a cell: a letter (KNOB 2) and a value 0–F (KNOB 3).

| | Effect | Value |
| --- | --- | --- |
| G | Glide from the track's last note | how slow (20 ms to half a second) |
| ? | Chance: the note plays or not | value + 1 in 16 |
| R | Ratchet: the row split into hits | value + 1 hits |
| D | Delay: the note late | value sixteenths of a row |
| T | Tone: this note's filter | 7 as set; lower darker, higher brighter |
| V | Volume | 0 silent to F full |
| B | Bend: a drop over the row (the album's pitch drops) | semitones (0: an octave) |
| E | Echo throw: this note into the echo | how much |
| A | Chord (in the scale): fifth, triad, sus4, seventh, add9, octave, fourths, triad + octave | 0–7; 8–F the same, spread wider |
| M | Mutate: another note in the scale | up to value degrees away |
| L | Length: let go early | value + 1 sixteenths of a row |
| X | Crush | how much |
| S | Stutter: the album's glitches, 30–60 ms repeats, crushed, falling away | how far apart |
| W | Swell: a slow rise in | value + 1 quarters of a row |
| J | Jump: after this row, on to a position | the position (0 the first) |

---

## 9. Evolve and Grow

**Evolve** (Move page, ENV twice) lets a track change as it plays without changing what you wrote. After
each pass of a pattern, a row's note may drift a degree up or down the scale (three at most), and drifts lean
back home over time. On a kit, drums drop out now and then where the drift is low, and ghost in where it's
high. Turn Evolve to 0 and the notes are as written at once. Evolve, Mutate, Chord and Grow keep to the song's
key and mode (Setup).

**Grow** (ARP) writes a track of the pattern on screen in one of the album's ways:

| Style | What it writes |
| --- | --- |
| Pulse | Roots and fifths on the dotted grid; a half-time kit |
| Drone | A held note, perhaps a move at the half bar |
| Ostinato | A figure of fourths and fifths, round and round (The Garden) |
| Phrygian | The semitone above the root and back, gliding (Ritual, Deep Mutation) |
| Triplet | Every three rows, against the bar (the 3:2 of its tempos) |
| Drift | A few far notes, swelling in |
| Swarm | Bugs: short notes and drums, ratchets, stutters and crush |
| Sparse | A note, maybe two, out of near silence |

**Density** and **Range** shape it; **Seed** picks one of a hundred. **EDIT** grows the cursor's track; **SEL +
EDIT** every track. Each grow moves the seed on, so EDIT again gives another.

![Grow](img/evol-device-grow.png)

---

## 10. The pages

### Pattern (HOME)

The grid (see 4 and 5). **Note**, **Effect**, **Value**: the cursor's cell. **Pattern**: the pattern this
position plays.

### Song (SEQ)

**Position**, **Pattern** (the position's), **Length** (positions that play), **Follow** (the screen follows
the song). EDIT: a fresh copy; SEL + EDIT: clear.

### Sound (ENV)

The cursor's track: **Sound**, **Tone** (its filter; a kit's tuning too), **Decay** (the envelope's length,
10–400%), **Level**.

### Move (ENV again)

**Motion** (a slow sweep of the filter, eight bars round), **Pulse** (a tremolo locked to the tempo: the
album's pumping drones; its note on Sends), **Evolve**, **Pan**.

### Sends (ENV a third time)

**Echo**, **Reverb**, **Grit** (drive; past halfway, crush too), and **Pulse note** (for the whole song: 1/16,
1/8, dotted 1/8, triplet 1/8, 1/4).

### Space (LFO)

**Echo time** (1/16 to dotted 1/4, in time with the tempo), **Feedback**, **Reverb** (the plate's size),
**Tape** (hiss and a soft top).

### Groove (LFO again)

**Tempo** (40–200), **Swing** (the off sixteenths late), **Duck** (how much each kick ducks the other tracks,
the echo and the plate), **Loop** (Song or Pattern).

### Grow (ARP)

**Style**, **Density**, **Range**, **Seed** (see 9).

### Setup (GLO), Device (GLO again)

**Root**, **Mode** (Aeolian, Phrygian, Dorian, Mixolydian, Major, Lydian, Pent min, Chromatic), **Tune**
(cents), **Sync** (Free, or Clock). Device: **Theme** (Evol, Static, Bruise, Ember, Mono), **Lights** (the key
lights: Off, On, Keys), **MIDI out** (Off, Notes, Clock, Both), **Channel** (the six tracks' channels).

![The themes](img/evol-themes.png)

### Mutes (FX held)

The six tracks: black keys 1–6 mute or unmute, white keys 1–6 solo (again: all back). FX tapped mutes the
cursor's track. FX lights while a track is muted.

---

## 11. MIDI clock and your DAW

![On MIDI clock](img/evol-device-clock.png)

**Clock in** (USB or the TRS jack): with Sync on Clock (Setup), START plays from the top, STOP stops, and the
rows follow the clock, six ticks a row. CONTINUE plays on from where it was. EXT shows in the header while the
clock comes; without it, EVOL-1 keeps its own tempo.

**Notes in** play the cursor's track, and with REC on they're written, as the keys are.

**MIDI out** (Device): each track's notes on its own channel, Channel + 1 to Channel + 6 (1–6 by default), a
kit's drums as notes 36–43. **Clock** (or Both) also sends the clock, 24 a beat, with START and STOP.

In the browser, the emulator gives the tracks apart on an eight-channel output: the mix on 1–2, tracks 1–2 on 3,
tracks 3–6 on 4–7, the echo and reverb on 8, to record into a DAW.

---

## 12. Saving and the songs

Five songs, each after a track of the album, at its tempo and in its key as measured from the record:

![The five songs](img/evol-songs.png)

| Song | |
| --- | --- |
| Deep Mutation | 97 BPM, D phrygian: the Eb stack over a half-time pulse, the phrygian lead |
| Psychic Weather | 64, D aeolian: sweeps, far notes, a wide pad |
| Ritual | 62, D phrygian: the throat, the Ritual kit's triplet wood, rumble |
| The Garden | 125, F major: the fourths-and-fifths ostinato over a pumping pad |
| bugs | 96, D aeolian: the glitch kit, bugs and their swarm |

Each is eight patterns layered from a drone intro to an outro, the order list playing each twice.

**SAVE** keeps the song you're in (and the device's settings: tune, sync, MIDI, theme, lights), each song in
its own place in the FM-1's flash. A dot top right shows something isn't saved. **PRESETS** opens another song;
with changes unsaved, the first turn warns and a second turn drops them. Hold **SAVE** for 1.5 s on the Setup
page to have the song back as it came (SAVE again to keep that).

---

## 13. Troubleshooting

- **No sound:** turn MASTER up; check PLAY is lit; check the track's Level (Sound) and that it isn't muted
  (FX lit).
- **The keys don't write:** REC must be on, and you must be on the Pattern or Grow page.
- **Notes go in the wrong row while playing:** with Follow on, they go into the row playing; turn Follow off
  (SEL) to write at the cursor.
- **A drone stops at a new pattern:** give it the same note on the new pattern's row 0 (it holds on), or no
  note at all.
- **The clock does nothing:** Sync must be Clock (Setup). EXT shows while the clock comes.
- **No MIDI out:** MIDI out (Device) must not be Off, and your DAW must listen on the tracks' channels.
- **The FM-1 won't start:** hold OCT− and OCT+ together for five seconds for the update mode, then install
  again.

---

## 14. Credits

EVOL-1 is a RedtailWorks project, built round the sound of RedtailWorks' album *Evol*. It's a fork of
[AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, by way of HYP-1 and GRAV-1. The
platform (USB, update loader, storage, installer) is [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (@kurogedelic), Hügelton Instruments. The reverb is Jon Dattorro's plate. Screen font: Barlow Semi
Condensed (SIL OFL). GPL-3.0.

M-VAVE and FM-1 are trademarks of their owners; EVOL-1 is not affiliated with M-VAVE.
