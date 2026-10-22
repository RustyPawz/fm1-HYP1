#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the CTY-1 site: HYP-1's site has it under cty/ (the HYP-1 workflow runs this, then
tools/site/switcher.py adds the firmware dropdown to every page):

  index.html                  what CTY-1 is, and the ways in: try, install, manual, download, source
  img/                        screenshots (docs/img)
  emu/                        CTY-1 in the browser (build/emu, from web/emu/build_clang.sh)
  install/index.html          the web installer (web/amb_installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the manual (tools/manual_html.py, from docs/MANUAL.md)
  firmware/cty1-VERSION.fwsc    the package the installer writes; also the download

  tools/make_pages.py build/amb-1.0.1.fwsc 1.0.1 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_7XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

REPO = os.environ.get("REPO_URL", "https://github.com/RustyPawz/fm1-HYP1/tree/main/cty1")

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CTY-1</title>
<meta name="description" content="CTY-1: a city grows and plays the M-VAVE FM-1. Its roads, buildings, traffic, weather and incidents make a generative chiptune. By RedtailWorks.">
<meta name="theme-color" content="#07090f">
<link rel="icon" type="image/png" href="img/favicon.png">
<meta property="og:title" content="CTY-1: a city grows and plays the M-VAVE FM-1">
<meta property="og:image" content="img/cty-hero.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@400;500;600;700&display=swap">
<style>
/* the city at night: an asphalt ground, lane-paint yellow, sky blue, and the red panda's rust and
 * cream for RedtailWorks (the same on every page: landing, installer, manual) */
:root { --ground: #0c0d10; --card: #17191d; --card-2: #202328; --ink: #ecedea; --muted: #a2a69e; --rule: #34373d;
        --purple: #c9a23a; --purple-2: #e8b931; --orange: #e8873a; --rust: #c85420; --cream: #f6ecde; --ice: #9cc3ff; color-scheme: dark; }
* { box-sizing: border-box; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 28px 64px;
       font: 400 18px/1.6 "Barlow Semi Condensed", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 1040px; margin: 0 auto; }
h1, h2, h3, .ways strong, .tag { font-family: "Barlow Semi Condensed", "Helvetica Neue", Arial, sans-serif; }
header.site { display: flex; flex-wrap: wrap; align-items: baseline; gap: .4rem 1.4rem; margin: 0 0 1.2rem; }
header.site { align-items: center; }
header.site .home { font: 700 1.4rem "Barlow Semi Condensed", sans-serif; letter-spacing: .06em; color: var(--ink); text-decoration: none;
                    display: inline-flex; align-items: center; gap: .6rem; }
img.panda { image-rendering: pixelated; display: block; height: auto; }
header.site nav a.yt, footer a.yt { color: var(--orange); }
header.site nav { margin-left: auto; display: flex; flex-wrap: wrap; gap: .3rem 1.1rem; font-size: 1rem; }
img.hero { width: 100%; height: auto; display: block; border-radius: 14px; margin: 0 0 1.6rem; }
.lede { font-size: 1.3rem; line-height: 1.5; max-width: 44rem; margin: 0 0 1.8rem; }
.lede b { color: var(--purple-2); font-weight: 600; }
h2 { font-size: 1.5rem; font-weight: 700; letter-spacing: .04em; text-transform: uppercase; margin: 3rem 0 1rem;
     display: flex; align-items: center; gap: .7rem; }
h2::before { content: ""; width: 34px; height: 5px; background: linear-gradient(90deg, var(--orange) 0 62%, var(--purple) 62%); }
h3 { font-size: 1.1rem; font-weight: 600; letter-spacing: .03em; margin: 0 0 .3rem; color: var(--purple-2); }
.ways { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 10px; padding: 14px 16px; transition: border-color .15s, background .15s; }
.ways a:first-child { background: linear-gradient(135deg, #3a2f12, #121726); border-color: var(--purple); }
.ways a:nth-child(2) { border-color: var(--orange); }
.ways a:nth-child(5) { border-color: var(--rust); }
.ways a:hover, .ways a:focus-visible { border-color: var(--purple-2); background: var(--card-2); outline: none; }
.ways strong { font-weight: 700; font-size: 1.1rem; letter-spacing: .05em; text-transform: uppercase; display: block; }
.ways span { color: var(--muted); font-size: .98rem; }
.ways em.v { font-style: normal; text-transform: none; letter-spacing: 0; color: var(--orange); }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(290px, 1fr)); gap: 14px; }
.card { background: var(--card); border: 1px solid var(--rule); border-radius: 10px; padding: 16px 18px; }
.card p { margin: 0; color: var(--muted); }
.card p b { color: var(--ink); font-weight: 600; }
.pair { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 14px; margin: 0 0 1rem; }
figure { margin: 0; }
figure img { width: 100%; height: auto; display: block; border-radius: 10px; border: 1px solid var(--rule); }
figcaption { color: var(--muted); font-size: .95rem; margin-top: .4rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 12px; }
.shots img { image-rendering: pixelated; }
table { border-collapse: collapse; width: 100%; font-size: 1rem; }
th, td { text-align: left; vertical-align: top; padding: .5rem .8rem .5rem 0; border-bottom: 1px solid var(--rule); }
th { font: 600 .85rem "Barlow Semi Condensed", sans-serif; letter-spacing: .08em; text-transform: uppercase; color: var(--muted); }
td:first-child { font-family: "Barlow Semi Condensed", sans-serif; font-weight: 600; white-space: nowrap; color: var(--purple-2); }
kbd { font: 600 .85em "Barlow Semi Condensed", sans-serif; background: var(--card-2); border: 1px solid var(--rule); border-radius: 4px; padding: 0 .35em; color: var(--orange); }
p.small, .credits { color: var(--muted); font-size: .95rem; }
a { color: var(--ink); text-decoration-color: var(--purple); text-decoration-thickness: 2px; text-underline-offset: 3px; }
a:hover { color: var(--purple-2); }
code { font-size: .9em; background: var(--card-2); padding: .05em .35em; border-radius: 4px; }
footer { margin-top: 3.5rem; padding-top: 1.2rem; border-top: 1px solid var(--rule); display: flex; flex-wrap: wrap; gap: .6rem 1.6rem; align-items: center; }
footer .rw { font: 700 1.05rem "Barlow Semi Condensed", sans-serif; letter-spacing: .08em; text-decoration: none; display: inline-flex; align-items: center; gap: .55rem; }
footer .rw span:nth-child(2) { color: var(--purple-2); } footer .rw span:last-child { color: var(--orange); }
.video { margin: 0 0 1.8rem; }
.video .frame { position: relative; aspect-ratio: 16 / 9; border-radius: 12px; overflow: hidden; border: 1px solid var(--rule); background: #000; }
.video iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
.video figcaption { display: flex; flex-wrap: wrap; gap: .3rem 1rem; justify-content: space-between; }
.video figcaption a { color: var(--orange); }
footer .ver { font-family: "Barlow Semi Condensed", sans-serif; color: var(--muted); font-size: .9rem; }
a.kofi { display: inline-flex; align-items: center; gap: .4em; background: var(--rust, #c85420); color: #fff8f0 !important; text-decoration: none; border-radius: 999px; padding: .15em .8em; font-weight: 600; } a.kofi:hover { background: var(--orange, #e8873a); color: #120d03 !important; }
.support { display: flex; flex-wrap: wrap; align-items: center; gap: .8rem 1.4rem; background: var(--card); border: 1px solid var(--rust);
           border-radius: 10px; padding: 16px 18px; margin: 2.4rem 0 0; }
.support p { margin: 0; color: var(--muted); flex: 1 1 22rem; }
.support p b { color: var(--ink); font-weight: 600; }
.support a.kofi { font-size: 1.05rem; padding: .5em 1.2em; }
.exp { font: 700 .72rem "Barlow Semi Condensed", sans-serif; letter-spacing: .12em; text-transform: uppercase; color: #1a0f05;
       background: #e8873a; border-radius: 999px; padding: .15em .7em; margin-left: .2rem; }
.expnote { background: #2a1d10; border: 1px solid #e8873a; border-radius: 10px; padding: 12px 16px; margin: 0 0 1.4rem; color: #f3dcc4; }
.expnote b { color: #ffb070; }
@media (max-width: 560px) { .lede { font-size: 1.15rem; } h2 { font-size: 1.25rem; } }
</style>
</head>
<body>
<main>
<header class="site"><a class="home" href="./"><img class="panda" src="img/panda.png" width="64" height="50" alt="">CTY-1<span class="exp">Experimental</span></a>
  <nav><a href="emu/">Emulator</a><a href="install/">Install</a><a href="manual/">Manual</a><a href="__REPO__">Source</a><a class="yt" href="https://www.youtube.com/@RedtailWorks">YouTube</a><a href="https://www.redtail.works">RedtailWorks</a></nav></header>
<div class="expnote"><b>Experimental.</b> CTY-1 is new: built and tested in the simulator and in the emulator here, not yet on a real FM-1. Install it at your own risk; HYP-1, or M-VAVE's own firmware, goes back on the same way.</div>
<img class="hero" src="img/cty-hero.png" width="1600" height="1161" alt="An M-VAVE FM-1 running CTY-1: a pixel city seen from above, roads, a river and houses, the City page's Speed, Start as, Traffic and People below it.">
<p class="lede"><b>CTY-1</b> turns the M-VAVE FM-1 into a small city that builds itself and plays a generative chiptune.
Every time you switch it on it founds a new town on new land: forest, desert, alpine valley, volcano isles, plains, beach
or river lands, each with its own look, weather, disaster and sound. Week by week it grows: homes,
shops, offices and factories, farms at the edge, parks and landmarks, avenues, roundabouts and a beltway, until it's a
huge city of towers. Every car on its roads, every building that opens, every storm, fire and siren is a note, played
on NES, Game Boy or SID voices.</p>
<nav class="ways" aria-label="Get CTY-1">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. Mouse, touch or keyboard; eight channels and MIDI to your DAW.</span></a>
  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Experimental: at your own risk.</span></a>
  <a href="manual/"><strong>Manual</strong><span>Every control, every page, every city, on one page.</span></a>
  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
</nav>
<h2>The city</h2>
<div class="grid">
  <div class="card"><h3>It grows</h3><p>From a <b>town</b> at a crossroads to a <b>suburb</b>, a <b>city</b> and a <b>huge city</b>: blocks grow larger outward, avenues get planted medians, roundabouts and cul-de-sacs, bridges cross the river and a causeway the bay, and a beltway opens round it.</p></div>
  <div class="card"><h3>Zones</h3><p>Homes, shops and offices, and factories try to keep to their own districts, with some mixing. Rent and jobs set the balance; old lots are redeveloped, farms give way at the edge. A school, a hospital, a fire station and the police open as it needs them.</p></div>
  <div class="card"><h3>Life</h3><p>Cars drive real routes between homes and work; people walk, stroll the parks and stop to talk in speech bubbles. Day and night, four seasons, rain, storms, fog and snow, and birds overhead.</p></div>
  <div class="card"><h3>Incidents</h3><p>Lightning (out of rain, storms and snow) starts a fire and the fire station sends a truck. An earthquake shakes the screen and gates the music. A robbery brings the police, a crash an ambulance. Festivals with fireworks, blackouts, meteor showers. And, very rarely, a UFO.</p></div>
  <div class="card"><h3>The music</h3><p>The cars are the playheads: each road and building they pass is a rhythm and a bell. The bed of drums, bass and pad grows busier with the city; its mood picks the mode, the night closes the filter, rain and fog lengthen the echo, sirens wail in key.</p></div>
  <div class="card"><h3>You play</h3><p>The keys play a lead in the city's scale. Call any incident or the weather from the black keys, ride along with a car, or leave it: after a minute it takes the whole screen and follows the traffic, the lights playing along.</p></div>
</div>
<h2>Seven biomes</h2>
<figure><img src="img/cty-biomes.png" width="2040" height="1112" alt="Seven CTY-1 cities, one in each biome: forest, desert, alpine, volcano isles, plains, beach and river lands."></figure>
<p>Each switch-on founds a new city, in a biome of its own choosing (or the one you set). The same city grows in each, and the same
events happen, but each looks, storms and sounds its own way: the desert's mesas, cacti and dust storms over a drone in Hijaz; the
mountains' snowy peaks, blizzards and bells; the isles' palms, turquoise shallows and smoking volcano, with a clave and a steel-drum
wave; the plains' golden grass and boom-chick; the beach's surf backbeat; the river lands' shakers and floods.</p>
<div class="pair">
  <figure><img src="img/cty-device-eruption.png" width="1600" height="1161" alt="The FM-1 as the volcano erupts and lava runs down into the town."><figcaption><b>The biome's own disaster:</b> a wildfire, a dust devil, an avalanche, an eruption, a tornado, a tsunami or a flood. Its own look and sound; more in its weather.</figcaption></figure>
  <figure><img src="img/cty-device-desert.png" width="1600" height="1161" alt="The FM-1 showing a desert city among red mesas."><figcaption><b>The desert:</b> sand, mesas and cacti; the SID over a drone, a hand drum, Hijaz.</figcaption></figure>
</div>
<h2>Disasters</h2>
<figure><img src="img/cty-disasters.png" width="2040" height="1112" alt="Seven CTY-1 screens: a wildfire, a dust devil, an avalanche, an eruption, a tornado, a tsunami and a flood."></figure>
<h2>Weather</h2>
<figure><img src="img/cty-weather.png" width="2040" height="1112" alt="Seven CTY-1 screens of each biome's storm: a thunderstorm, a dust storm, a blizzard, a typhoon, a tropical storm."></figure>
<div class="pair">
  <figure><img src="img/cty-device-fire.png" width="1600" height="1161" alt="The FM-1 as lightning strikes a city building and it catches fire."><figcaption><b>Incidents:</b> the camera goes to them and stays until they're over. Black keys call each one.</figcaption></figure>
  <figure><img src="img/cty-device-night.png" width="1600" height="1161" alt="The FM-1 showing the city at night, its windows and streetlights lit."><figcaption><b>Night:</b> windows, streetlights and headlights; the music darkens to it.</figcaption></figure>
</div>
<h2>What happens</h2>
<figure><img src="img/cty-incidents.png" width="2040" height="1112" alt="Eight CTY-1 screens: lightning and fire, an earthquake, a robbery, a car crash, a UFO, a festival, the night, snow."></figure>
<h2>Ways to look</h2>
<figure><img src="img/cty-views.png" width="2040" height="568" alt="Four CTY-1 screens: the zones, the land value, the whole city, riding along with a car."></figure>
<figure><img src="img/cty-themes.png" width="1536" height="1112" alt="Six CTY-1 screens in its themes: Asphalt, Neon, Blueprint, Sunset, Terminal, Mono."></figure>
<p>Six themes for the frame on the Display page; the city keeps its own colours.</p>
<h2>To your DAW</h2>
<div class="pair">
  <figure><img src="img/cty-device-clock.png" width="1600" height="1161" alt="The FM-1 on MIDI clock: EXT and the tempo in the header, the Setup page with Sync on Clock."><figcaption><b>MIDI clock in:</b> START plays from step one, STOP rests it, and the city's music follows the clock.</figcaption></figure>
  <figure><img src="img/cty-device-midi.png" width="1600" height="1161" alt="The FM-1 on the MIDI out page."><figcaption><b>MIDI out:</b> each chip channel's notes on its own channel, the drums mapped to a kit, and the city as CCs: mood, brightness, rush, daylight, rain and its stage.</figcaption></figure>
</div>
<h2>Screens</h2>
<div class="shots">
  <figure><img src="img/cty-screen-splash.png" width="480" height="480" alt="The boot screen: a pixel red panda on the RedtailWorks name"><figcaption>Switching on</figcaption></figure>
  <figure><img src="img/cty-screen-splash-cty.png" width="480" height="480" alt="The second boot screen: CTY-1 in big pixel letters over a skyline lighting up"><figcaption>&hellip;then CTY-1</figcaption></figure>
  <figure><img src="img/cty-screen-city.png" width="480" height="480" alt="The City page"><figcaption>City</figcaption></figure>
  <figure><img src="img/cty-screen-b-isles.png" width="480" height="480" alt="A city on the volcano isles"><figcaption>The isles</figcaption></figure>
  <figure><img src="img/cty-screen-events.png" width="480" height="480" alt="The Events page"><figcaption>Events</figcaption></figure>
  <figure><img src="img/cty-screen-zones.png" width="480" height="480" alt="The zones map"><figcaption>Zones</figcaption></figure>
  <figure><img src="img/cty-screen-full.png" width="480" height="480" alt="The idle view: the city on the whole screen"><figcaption>Idle view</figcaption></figure>
  <figure><img src="img/cty-screen-help.png" width="480" height="480" alt="LFO's help card"><figcaption>Help cards</figcaption></figure>
</div>
<h2>Credits</h2>
<p class="credits">CTY-1 is a fork of <a href="https://github.com/charlesvestal/fm1-amb">AMB-1</a> by Charles Vestal, by way of HYP-1, ECO-1 and GRAV-1.
The platform (USB, update loader, storage, installer) is <a href="https://github.com/hugelton/Felucca">Felucca</a> by Leo Kuroshita (@kurogedelic), Hügelton Instruments.
Screen font: Barlow Semi Condensed (SIL OFL). GPL-3.0.
M-VAVE and FM-1 are trademarks of their owners; CTY-1 is not affiliated with M-VAVE.</p>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or
M-VAVE's updater, M-UPGRADE. Installing firmware is at your own risk.</p>
<footer><a class="rw" href="https://www.redtail.works"><img class="panda" src="img/panda.png" width="48" height="38" alt=""><span>REDTAIL</span><span>WORKS</span></a>
  <span class="small">CTY-1 is a RedtailWorks project &middot; <a href="https://www.redtail.works">www.redtail.works</a> &middot; <a class="yt" href="https://www.youtube.com/@RedtailWorks">YouTube</a> &middot; <a href="__REPO__">source</a></span><span class="ver">version __VERSION__</span></footer>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_7\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"cty1-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the installer
    page = (SRC / "web" / "amb_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"amb_installer.html must contain {mark} once")
    page = page.replace("https://github.com/charlesvestal/fm1-amb", REPO).replace("github.com/charlesvestal/fm1-amb", REPO[8:])
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    # the manual (a complete page; its images beside it)
    subprocess.run([sys.executable, str(SRC / "tools" / "manual_html.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True, capture_output=True)
    man = out / "manual" / "index.html"                 # the build's own version, wherever the manual names one
    man.write_text(re.sub(r"CTY-1 version \d+\.\d+\.\d+", "CTY-1 version " + html.escape(version), man.read_text(encoding="utf-8")),
                   encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    # the landing page and its screenshots
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "amb.wasm").exists():
        raise SystemExit("no build/emu/amb.wasm: run web/emu/build_clang.sh")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), manual/, emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
