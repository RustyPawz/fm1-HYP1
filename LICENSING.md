# HYP-1 licensing

HYP-1 (a RedtailWorks fork of Charles Vestal's AMB-1) is free software under the GNU General Public
License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on fm1-base and felucca-bsp (MPL-2.0, from Felucca),
and keeps the licence it has had from the start. If you distribute HYP-1, or firmware derived from it, you must give
your recipients its complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| The platform, in `base/` (a git submodule): the chip and board layer, the update loader, USB, storage, the screen and panel, the host simulator, the browser emulator's glue, the build, package, install and rescue tools, the web installer | [fm1-base](https://github.com/charlesvestal/fm1-base) (Charles Vestal and contributors) and, inside it as `base/bsp`, [felucca-bsp](https://github.com/hugelton/felucca-bsp), from [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton Instruments | MPL-2.0, file by file (see `base/LICENSING.md`); part of this GPL-3.0-only work as MPL-2.0 section 3.3 allows |
| `app/app.c`, `app/host.c`, `app/web.c`, `build.sh`, `tests/run_tests.sh`: the glue between HYP-1 and fm1-base | after AMB-1's on fm1-base (Charles Vestal, MPL-2.0 there), with HYP-1's own parts (RedtailWorks) | GPL-3.0-only (`build.sh` keeps its MPL-2.0 notice) |
| `app/web/emu/` (the browser emulator's page and worklet), `tools/{make_pages,manual_html}.py` | adapted from AMB-1's and X0X's (themselves from Felucca's) | GPL-3.0-only |
| Dattorro's plate reverb in `app/dsp/amb.c` | from [FoMni](https://github.com/charlesvestal/fm1-fomni) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else in `app/`, `tests/` | AMB-1 as it was on 2026-10-10 (Charles Vestal, GPL-3.0-only then), HYP-1 changes (RedtailWorks) | GPL-3.0-only |

Until November 2026 HYP-1 carried its own copy of Felucca's platform (GPL-3.0-only). The experimental firmwares in
`lof1/`, `eco1/`, `grav1/`, `cty1/` and `bit1/` still do: each has its own `LICENSING.md`.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Chakra Petch (Cadson Demak), the screen's face and the site's headings | SIL OFL 1.1 | `assets/fonts/ChakraPetch-*.ttf`, `assets/fonts/ChakraPetch-OFL.txt` |
| Barlow Semi Condensed (The Barlow Project Authors), the site's text and labels | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.
"M-VAVE" and "FM-1" are trademarks of their respective owners. HYP-1 is independent firmware; it is
not affiliated with, endorsed by or supported by any of them.

## Radio

HYP-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
