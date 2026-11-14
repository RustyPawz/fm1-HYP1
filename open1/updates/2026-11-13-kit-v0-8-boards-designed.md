---
title: KIT v0.8: the boards are designed
date: 2026-11-13
tag: Milestone
---
The KIT base and two of the modules are now real circuit board designs: full schematics, every connection routed, and the manufacturing files ready for a first batch.

![The KIT v0.8 base board from the top: the screen, four knobs, six coloured MX keys, the volume knob and the plug-in RP2350 module](media/open1-v08-top.jpg)

**The screen got cheaper, and it's your choice.** Until now the screen had to be a module with its own graphics chip, about $70 for a 5″ one. In v0.8 that chip sits on the KIT base instead, so OPEN-1 drives a plain 40-pin RGB panel directly. Those cost about $15–20, and you can pick a 4.3″ or 5″ panel. Panels with a touch layer work too, so a firmware can use the screen as a control surface.

**Where the boards are**

- **KIT base:** a 4-layer board, 234 × 130 mm, about 210 parts. Every connection is routed and the design checks pass.
- **PADS and TOUCH:** routed and checked too.
- **KEYS:** on hold.

![The KIT base's copper traces, shown without the parts: the routed board from an angle](media/open1-v08-traces.jpg)

**What's next.** Before I pay for a batch, someone who does this for a living is going to review the boards and the parts list. Once that's done: the first boards, then bring-up. Meanwhile the firmware work starts on a breadboard.

If you review circuit boards, or you've brought up a board like this before and want to help, come say hi in the [OPEN-1 Discord](https://discord.gg/dpgSDDBVb).
