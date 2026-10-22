# CTY-1 for the M-VAVE FM-1 (experimental)

CTY-1 turns the M-VAVE FM-1 into a small city that builds itself and plays a generative chiptune. Each power-on
founds a new town on new land (forest, desert, alpine, volcano isles, plains, beach or river lands, each with its
own look, weather, disaster and sound), then grows it week by week, zoned into homes, shops and
offices, factories and farms, with parks, landmarks, avenues, roundabouts, bridges and a beltway, to a huge
city of towers. Cars drive real routes and are the music's playheads; people stop to talk in speech bubbles;
days, seasons and weather go round; lightning starts fires, earthquakes knock buildings down, robbers bring the
police and crashes an ambulance; each land has its own disaster (wildfire, dust devil, avalanche, eruption,
tornado, tsunami, flood); festivals, blackouts and meteor showers; and very rarely a UFO comes. All of it plays on NES, Game Boy or SID voices.
A RedtailWorks project, alongside HYP-1 in this repository.

> **Experimental.** Built and tested in the simulator and the browser emulator, not yet on a real FM-1.
> Versions are 0.x until it has been. Install it at your own risk; HYP-1 or M-VAVE's firmware goes back on
> the same way.

![CTY-1 on the FM-1](docs/img/cty-hero.png)

![The seven biomes](docs/img/cty-biomes.png)

![Their disasters](docs/img/cty-disasters.png)

- **Try it in the browser:** <https://rustypawz.github.io/fm1-HYP1/cty/emu/>: with sound, MIDI in and
  out, eight channels (each voice apart) for your DAW, and a stream view.
- **Install it** (Chrome or Edge, the FM-1 plugged in): <https://rustypawz.github.io/fm1-HYP1/cty/install/>
- **Manual:** <https://rustypawz.github.io/fm1-HYP1/cty/manual/> (source: [docs/MANUAL.md](docs/MANUAL.md))

## In short

| | |
|---|---|
| Keys | The lead, in the city's scale. On Time: black keys 1–4 the hour. On Weather: black keys 1–7 the sky. On Events: black keys 1–10 call lightning, a quake, a robbery, a crash, a UFO, birds, the land's disaster, a festival, a blackout, meteors |
| PLAY · REC · EDIT | The music plays / rests · hold the growth · a new city on the same land |
| HOME · ENV · ARP · LFO · SEQ · FX · GLO | The pages: City, Music, Sound, Time (again: Weather), Voices (again: Voices 2), Events (again: Events 2, 3), Setup (again: Display, MIDI out) |
| SELECT | Every page in turn; with HOME held, the tempo |
| SEL · ALGORITHM · PRESETS | Ride along with a car · look at the landmarks · the next biome (a new city there) |
| OCT− / OCT+ | The lead's octave |
| SAVE | On the Setup pages, saves your settings; elsewhere, the city again from its founding; held, the biome's music back |

**To your DAW:** MIDI clock in (USB or TRS) drives the music; MIDI notes in play the lead; MIDI out sends each
chip channel's notes on its own channel, the drums as a kit, and the city's mood, brightness, rush, daylight,
rain and stage as CCs 20–25.

## Inside

- `firmware/src/app/city.c`: the city: terrain, the road plan, growth, zoning, land value, redevelopment,
  landmarks. Deterministic from the seed; `tests/host/city_test.c` grows five cities and checks them.
- `firmware/src/app/life.c`: the day, seasons, each biome's weather, routes, cars, people, birds and the
  incidents, and what they tell the music; `region.c` the biomes' disasters, festivals, blackouts, meteors.
- `firmware/src/dsp/amb.c`: the chiptune engine: 32 voices on six chip channels, the bed, the events,
  sirens, weather, the echo, MIDI.
- `firmware/src/app/ui.c`: the pages, the camera and the renderer; `game.c` a secret.

## Renders

`docs/img/` holds the renders: the device on several pages (`cty-device-*.png`, `cty-hero.png`), every
screen at 2× (`cty-screen-*.png`), and the sheets `cty-pages.png`, `cty-incidents.png`,
`cty-views.png`, `cty-themes.png`, `cty-boot.png`, `cty-biomes.png`, `cty-disasters.png` and `cty-weather.png`, all drawn from the firmware itself in the simulator. The
workflow redraws them on each push.

## Building

Self-contained in this folder: see [BUILDING.md](BUILDING.md). HYP-1's workflow
(`../.github/workflows/firmware.yml`) builds it on every push, after HYP-1. Licensing:
[LICENSING.md](LICENSING.md) (GPL-3.0-only).

CTY-1 is a fork of [AMB-1](https://github.com/charlesvestal/fm1-amb) by Charles Vestal, by way of HYP-1, ECO-1
and GRAV-1, on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic), Hügelton
Instruments. M-VAVE and FM-1 are trademarks of their owners; CTY-1 is not affiliated with M-VAVE.
