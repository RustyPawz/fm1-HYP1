---
title: OPEN-1 becomes a KIT: one base, add what you need
date: 2026-11-11
tag: Design
---
A lot has changed since the v0.4 beta, so here's the catch-up.

**Why it changed.** I drew a few versions after v0.4, including one big board with everything on it: pads, a stick, a dozen encoders. Each one got bigger and more expensive, and every player wanted a different mix of controls. So OPEN-1 is now a **KIT**: a base that does the thinking and the sound, plus modules you add if you want them.

**The KIT base**

- A 5″ screen, 4 push encoders and a volume knob
- 6 RGB-lit Cherry MX keys on hot-swap sockets, named on screen by each firmware
- Headphones, stereo line out, stereo line in and a built-in speaker
- TRS MIDI in and out, USB-A host for your keyboard or controller, microSD
- A rechargeable battery with a fuel gauge

**A new brain.** The Daisy Seed is gone. The KIT runs on a Raspberry Pi RP2350 (two Cortex-M33 cores, 8 MB of extra memory, 16 MB of storage) on a Pimoroni Pico Plus 2 that plugs into sockets. It costs less, it's easy to get, and there's a second socket for another one if a firmware wants two brains.

**Modules.** Each module plugs into a rail on the side of the base with a flat ribbon cable, and has a rail out the other side, so you can chain them. There are also two Qwiic ports for your own add-ons.

![The OPEN-1 PADS module: sixteen MX pads in a 4 × 4 grid with four encoders beside them, and rail headers on both sides](media/open1-mod-pads.jpg)

- **PADS:** 16 MX pads in a 4 × 4 grid, plus 4 encoders
- **TOUCH:** two 195 mm touch strips, for pitch, mod or anything a firmware wants

![The OPEN-1 TOUCH module: two long touch strips made of chevron-shaped copper segments](media/open1-mod-touch.jpg)

- **KEYS:** 25 copper touch keys. This one is on hold while I work out the layout.

Bring your own keys still holds: an FM-1 or any USB or TRS MIDI controller plays it. The specs further up this page now describe the KIT.
