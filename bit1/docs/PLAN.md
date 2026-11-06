# BIT-1: the plan

BIT-1 is a generative 8-bit tracker for the M-VAVE FM-1. It started as a copy of EVOL-1's tracker. It takes
features from [KAWA](https://github.com/Guizmoh/Kawa-Tracker-Fm1) by Omnipotard (GPL-3.0; features ported, and
credited wherever code is borrowed), and the generative coherence of CTY-1. Generation can be switched off,
which leaves a plain tracker.

## Phase 0: scaffold (done)
- `bit1/` copied from `evol1/`, renamed: BIT-1, `bit/` on the site, project magic "BIT1", song magic "BITS",
  so a song saved by EVOL-1 is never read as BIT-1's.
- At first still EVOL-1 inside: the sound bank, the five songs, the themes, the splash, the manual, the pages
  (all replaced in phase 1).

## Phase 1: the chip engine (done in part: the bank, the songs, the themes, the splash, the pages)
- Swap EVOL-1's sound bank for CTY-1's chip voices (`cty1/firmware/src/dsp/amb.c`): NES pulse (12.5/25/50%,
  PWM), Game Boy 4-bit waves, NES noise (long, metal), SID-style, chip kits.
- Per-track sound pages: shape, sweep, vibrato, tone, crush, detune, arp speed, glide; ADSR.
- New factory songs, themes and splash; the bank and the songs written from scratch.

## Phase 2: KAWA's tracker features (done in part)
- Done: a cell is a note, a volume (0-F, or none) and two effects (SEL held turns the Pattern knobs to them).
  The fifteen effects keep BIT-1's, with KAWA's VIB (V, in Volume's place now there's a column) and HOP (H,
  once a pass, in Echo's place); ROL is R, CHN ?, DLY D, CUT L, BND B, CRU X, ARP chords A.
- Done: each pattern's rows, 1 to 16 (KAWA's END); each position played x1 to x16 (the order byte's high
  nibble); the six MAGIC tools on Grow (Euclid, Vary, Rolls, Accents, Shift, Up a step).
- Done: songs saved packed (a bitmap of the cells used, 2 or 4 bytes each): 600-1200 bytes as they come, of
  3840; the Song page shows how full; SAVE refuses one that won't fit (SONG FULL). The song and its load buffer
  live in the pool (6 KB each), not the 96 KB of RAM.
- Not yet: 32-row patterns and 64-step links (the cells would double), grooves beyond Swing, per-track tables
  (16 steps of pitch, volume, trick), KAWA's PIT, SHP, VSL, PAN as effects, undo.

## Phase 3: follow the lead (done)
- The lead: one track (Follow page, Lead; Off is a plain tracker). Each other track a Role: Off, Auto (by its
  sound), Bass, Chords, Arp, Harmony, Counter, Drums. A following track plays a take written from the lead's
  notes in the same pattern (`en_follow`), not its cells.
- The chords: one each half bar, the scale's triads scored against the lead's notes (strong beats and starts
  weigh more; I, IV, V, vi favoured; no diminished triads in a seven-note scale).
- LIVE and LOCK are one knob: a following track's Evolve is how often a pass brings a new take (0: locked).
  EDIT: a new take for all (the song's Take); SEL + EDIT: WRITE, the takes into the cells of every pattern the
  song plays, the roles Off. The same take from the same Take number: a die that brings it back.
- Overworld, the first song, plays by Follow.
- Still to come: energy over a song (CTY-1's builds and falls), parts on their own clocks, a take per pattern
  kept when a pattern repeats.

## Phase 4: GBA ROM voices
- In the installer page, in the browser, from a ROM the user owns; nothing uploaded, no ROM or extracted
  audio in this repository, the site or a release.
- Sappy (MP2K) games only at first: find the voicegroups, list and preview them, pick up to N.
- Game Boy-style instruments become chip-voice presets (bytes each); PCM samples go to the sample region
  (0xA0000 to 0xDBFFF, about 240 KB, which replaces M-VAVE's own user samples).
- The emulator takes them too.

## Phase 5: release
- The workflow (`.github/workflows/firmware.yml`), its pages under `bit/`, the dropdown, front page, 404.
- Manual, README, renders.

## Building on this Windows machine
- Simulator and tests: `CC=zcc sh tests/run_tests.sh` (`~/bin/zcc` runs `python -m ziglang cc`).
- `tools/gen_font.py` needs Pillow with libraqm; the font in `build/gen/` was copied from `evol1/build/gen/`.
- The device build (`./build.sh`) needs the JieLi toolchain (Linux); the workflow builds it.
