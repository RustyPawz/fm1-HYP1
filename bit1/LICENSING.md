# BIT-1 licensing

BIT-1 is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on Felucca's platform layer (through X0X and FoMni)
and keeps Felucca's licence. If you distribute BIT-1, or firmware derived from it, you must give
your recipients its complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and FoMni, then AMB-1 | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `host/amb_host.c`, `web/emu/`, `tools/{make_pages,manual_html}.py`, `tools/publish_pages.sh` | adapted from X0X (itself from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| Dattorro's plate reverb in `firmware/src/dsp/amb.c` | from [FoMni](https://github.com/charlesvestal/fm1-fomni) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| The tracker, the voices, the sound bank, GROW and the songs (`firmware/src/dsp/amb.c` but its plate), the screen (`firmware/src/app/ui.c`), the project (`firmware/src/app/project.c`), the pages, the boot cards, `tests/`, `tools/site/` | BIT-1 (RedtailWorks), from AMB-1 by way of HYP-1, GRAV-1, CTY-1 and EVOL-1 | GPL-3.0-only |
| The sound bank and the five songs | BIT-1's own, synthesised (the chip voices after CTY-1's); no audio from any game or album is in this tree | GPL-3.0-only |
| Tracker ideas | [KAWA](https://github.com/Guizmoh/Kawa-Tracker-Fm1) by Omnipotard (GPL-3.0); ideas so far, no code from it; code ported from it later will be marked where it is used | credit only |
| `firmware/src/app/` and the rest not named above | AMB-1 (Charles Vestal), changed for BIT-1 | GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. BIT-1 is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

## Radio

AMB-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
