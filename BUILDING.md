# Building HYP-1

Every push to `main` builds a release on GitHub (see [Releases on GitHub](#releases-on-github)); building locally
is only needed to work on the firmware.

HYP-1 is an app on [fm1-base](https://github.com/charlesvestal/fm1-base), which is in `base/` as a git submodule
(with felucca-bsp inside it): the platform, the update loader, the simulator and the tools are fm1-base's.
HYP-1's own code is in `app/`.

```
git submodule update --init --recursive
```

The build makes these in `app/build/`:

| File | What |
| --- | --- |
| `hyp.bin` | the firmware app |
| `loader/ota.bin` | the update loader (felucca-bsp's) |
| `hyp.fwsc` | the installable package (app + loader); a release build's is `hyp-X.Y.Z.fwsc` |

## Prerequisites

As fm1-base's (`base/README.md`):

- Python 3 with Pillow: `pip3 install Pillow`
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2): `base/tools/get_toolchain.sh` installs it to
  `~/.jieli/toolchain`. It is Linux x86-64 only: on macOS the build runs it in Docker or podman.
- The JieLi AC79 SDK (Apache-2.0); the package takes three of its files, which are not in this tree:

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Emscripten (optional: the browser emulator) and Node.js (optional: its test).

## Build

```
./build.sh                    # the firmware: app/build/hyp.fwsc
./build.sh --release 1.6.0    # a release build: app/build/hyp-1.6.0.fwsc (the site publishes it as hyp1-1.6.0.fwsc)
./build.sh --host             # the simulator: app/build/host/hyp_host
./build.sh --web              # the browser emulator: app/build/emu/ (needs emcc)
```

`build.sh` runs `base/tools/build.py app` with HYP-1's screen font (Chakra Petch, `assets/fonts`). The build's
settings are in `app/fm1.toml`. `JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations.

The build fails if a soft-double routine is linked (a `double` crept in) or on a register access outside
felucca-bsp, and checks the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs, on the build machine: the instrument (`tests/host/amb_test.c`: Euclidean patterns, scales and chords, voice
leading, the render's levels, drift, evolving loops, freeze); the whole app in fm1-base's simulator
(`tests/scenarios/*.amb`), with its screenshots and audio in `app/build/scenarios/`; the browser emulator (with
Emscripten); and fm1-base's update-path tests against `app/build/hyp.fwsc`. The platform's own tests are
fm1-base's: `base/tests/run_tests.sh`.

`app/build/host/hyp_host SCRIPT OUTDIR` runs one script (the commands are at the top of
`base/host/plat_host.c`; HYP-1's own checks and commands in `app/host.c`).

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 base/tools/fm1_install.py app/build/hyp.fwsc
python3 base/tools/fm1_install.py --info          # identity of the connected FM-1
```

Installing firmware is at your own risk. Hold REC while switching the FM-1 on for update mode. If the FM-1 no
longer starts but reaches the chip's update mode (4C4A:8057 on USB), `base/bsp/tools/fm1_rescue.sh` puts stock
firmware back; otherwise recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Releases on GitHub

Every push to `main` runs `.github/workflows/firmware.yml`:

1. It fetches fm1-base (the submodule), JieLi's toolchain, the AC79 SDK (cached between runs, with retries) and
   Emscripten (for the browser emulator).
2. It builds a release numbered from the workflow's run number N: `1.0.N` up to run 32, then `1.5.0`
   at run 33 and up from there (`1.5.(N - 33)`, a new minor version every hundred), so each push is a new
   version (the FM-1 reports it as FM-1_7XXYYZZ, which the installer checks).
3. It runs the tests, and fails the build on a size, pool or soft-double problem (shown as
   annotations on the run).
4. It builds the site with `tools/make_pages.py` (landing page, manual, PDF manual, browser emulator,
   web installer, the board, firmware download) and deploys it to GitHub Pages at
   <https://rustypawz.github.io/fm1-HYP1/>. The firmware is also kept as the run's artifact.

The board (`web/board.html`) shows the repository's GitHub Discussions with [giscus](https://giscus.app):
the workflow looks up the Discussions categories each build (`board/config.json`), so it switches itself
on once Discussions is enabled (Settings > General > Features) and the giscus app is installed on the
repository. Feature requests go in the **Ideas** category, comments in **General**; moderate them in
Discussions.

The manual's source is `docs/MANUAL.md` (`tools/manual_html.py` turns it into the page; the site
stamps the build's version into it). The PDF manual, `docs/HYP-1_Manual.pdf`, is built separately and
committed.

## The site's pictures and videos

All made from the firmware itself, run in the simulator:

```
./build.sh --host
app/build/host/hyp_host tools/site/shots.amb build/site_shots      # the screens
python3 tools/site/mockups.py                                  # docs/img: the device, screens, themes
python3 tools/site/panda.py                                    # the red panda mark and favicon
python3 tools/site/video.py [--vertical]                       # the tour; --seq, --voices, --links,
                                                               # --song for the other videos
```

`tools/publish_pages.sh` (pushing a site to a `gh-pages` branch by hand) is AMB-1's older route and
isn't needed with the workflow.
