/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: CTY-1, a city grows and plays the FM-1 (a RedtailWorks fork of AMB-1, by way of HYP-1,
 * ECO-1 and GRAV-1). The city lives in the main loop (app/city.c); this is its chiptune: five chip channels
 * and a lead.
 *
 *   PULSE A   bright squares: the shops, the landmarks' motifs, the jingles; and the lead (the keys)
 *   PULSE B   softer squares: the homes' arpeggios, the people's chirps
 *   TRIANGLE  the bass, the kick, the factories' thuds
 *   NOISE     an LFSR: hats, snares, the roads' rhythm, the weather, the sirens' crackle
 *   WAVE      a wavetable pad: the chord, the parks' swells
 *
 * The cars on screen are the playheads: as one rolls into a tile it plays the buildings beside it, snapped
 * to the next sixteenth. Under them the CITY BED: Euclidean drums, a bass and a pad, busier as the town
 * grows. The seed picks the key and a chord progression; the neighbourhood on screen, the time of day,
 * the season and the weather pick the mode and the colour of the sound. Each time the town grows into a
 * new stage the bar before it fills, and the key moves up a fourth.
 *
 * Threads: en_render() runs in the audio interrupt. Everything else posts commands (en_set, en_event, the
 * lead), which the render drains at the start of a block; the city's state (en_city) is written by the main
 * loop a field at a time and only read here. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NCH 6                       /* the channels: Pulse A, Pulse B, Triangle, Noise, Wave, Lead */
enum { CH_PA, CH_PB, CH_TRI, CH_NOI, CH_WAV, CH_LEAD };
extern const char *const EN_CH_NAME[EN_NCH];
extern const char *const EN_NOTE_NAME[12];
#define EN_NMODES 5
extern const char *const EN_MODE_NAME[EN_NMODES];   /* Lydian, Ionian, Mixolydian, Dorian, Aeolian */
#define EN_NFLAVOR 3
extern const char *const EN_FLAVOR_NAME[EN_NFLAVOR];

/* parameters: value ranges in en_param_info(). Some are the city's or the screen's: the engine keeps them
 * only to name them. */
enum {
    P_SPEED, P_START, P_TRAFFIC, P_PEOPLE,           /* the city: growth speed (x0.25..x64), start as, traffic, people */
    P_TEMPO, P_DENSITY, P_SWING, P_BED,              /* the music */
    P_FLAVOR, P_LEAD, P_ECHO, P_CRUSH,               /* the sound: NES / GB / SID, the lead's wave, the echo, grit */
    P_DAYLEN, P_SEASON, P_SEASONDAYS, P_NIGHT,       /* time: a day's length, the seasons, how long each, night lights */
    P_WEATHER, P_WPACE, P_BIRDS, P_CHATS,            /* weather: auto or fixed, how fast it changes, birds, conversations */
    P_VBELLS, P_VARPS, P_VBASS, P_VDRUMS,            /* the voices' levels */
    P_VPAD, P_VLEAD, P_VFX, P_VSIREN,
    P_LIGHTNING, P_QUAKES, P_ROBBERY, P_CRASH,       /* incidents: how often */
    P_UFO, P_FOLLOW, P_DISASTER, P_IDLE,             /* UFOs; the camera follows incidents; disasters destroy; idle view */
    P_KEY, P_TUNE, P_SYNC, P_MOOD,                   /* setup: the key (Seed, or C..B), tune, MIDI clock, mood */
    P_THEME, P_ZOOM, P_MAP, P_LABELS,                /* the screen */
    P_MIDI, P_MCH,                                   /* MIDI out: what it sends, its first channel */
    P_SEED_HI, P_SEED_LO,                            /* the city's seed (each city its own) */
    P_NPARAMS
};
#define EN_NTHEMES 6
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

/* the city, for the music: the main loop writes it (city.c), the render reads it */
typedef struct {
    volatile uint8_t stage;            /* 0 small town .. 3 huge city */
    volatile uint8_t key;              /* the seed's key (pitch class), and its progression */
    volatile uint8_t prog;
    volatile float mood;               /* -1 dark .. 1 bright: the neighbourhood, the hour, the season, the sky */
    volatile float bright;             /* land value on screen, 0..1: downtown opens the pulses' duty */
    volatile float grit;               /* industry on screen, 0..1: the bitcrush */
    volatile float rush;               /* the traffic now, 0..1.1: the hats */
    volatile float day;                /* daylight, 0..1 */
    volatile float rain, storm, fog, snow;   /* the sky, 0..1 */
    volatile uint8_t hwy, birds, gulls, fires;   /* the beltway's cars, birds and gulls, fires on screen */
    volatile uint8_t siren_fire, siren_police, siren_amb, alarm, ufo;
    volatile uint8_t tension;          /* the demand far off balance: a suspended note */
    volatile uint8_t season;           /* 0 spring .. 3 winter */
} en_city_t;
extern en_city_t en_city;

/* events from the city (en_event): played on the next sixteenth, a few a step */
enum {
    E_RES = 1,       /* a home, level a */
    E_COM,           /* a shop, level a, big b>>7, its note b&127 */
    E_IND,           /* a factory */
    E_GREEN,         /* a park, its note a */
    E_CIVIC,         /* a landmark's motif: a = civic kind (city.h CV_*) */
    E_HAT, E_SNARE, E_TOM, E_TUNK,   /* the roads: an intersection, an avenue's, a roundabout, a bridge */
    E_COIN,          /* a building goes up */
    E_RISE,          /* a tower tops out */
    E_JINGLE,        /* a first bridge, the beltway, the dawn */
    E_CHIRP,         /* a moodlet: a = its icon */
    E_SOLO,          /* the followed car's lead note: a = scale degree */
    /* cues, now (not on the grid) */
    E_LIGHTNING, E_THUNDER, E_QUAKE, E_CRUMBLE, E_SWEEP, E_DUSK, E_CRASH, E_BEAM, E_UFO_ON, E_UFO_OFF,
    E_STAGE,         /* the town grows into stage a: the fill, then up a fourth */
    E_NEV
};
void en_event(int type, int a, int b);

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int on);                          /* the music plays / rests (the city lives on) */
void en_reset(void);                           /* a new city: the key and the progression from en_city, the stage too */
void en_panic(void);
void en_mute(int on);                          /* remote: silent and no MIDI out (1), or back (0) */
void en_lead(int id, int semi);                /* a key down: the lead plays semitone `semi` above F3, snapped to the scale */
void en_lead_off(int id);
void en_lead_oct(int oct);                     /* the lead's octave, -2..2 */
/* MIDI clock in. With SYNC on, the music follows it while it comes: 24 ticks a beat, START plays from the bar,
 * STOP rests it. Without clock (or SYNC off), its own tempo. */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);
enum { EN_MIDI_OFF, EN_MIDI_NOTES, EN_MIDI_BOTH, EN_MIDI_CC };   /* P_MIDI: what goes out */
/* MIDI out (USB): each channel's notes on MCH + its number (Pulse A 0, Pulse B 1, Triangle 2, Noise 3 (drums:
 * 36 kick, 38 snare, 42 hat, 45 tom, 49 crash), Wave 4, Lead 5); the city as CCs on MCH + 6, about 20 times a
 * second when they change: */
enum { EN_CC_MOOD = 20, EN_CC_BRIGHT, EN_CC_RUSH, EN_CC_DAY, EN_CC_RAIN, EN_CC_STAGE };
extern volatile float en_bpm;                  /* the tempo now: the clock's while it comes, else TEMPO */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
/* the voices apart (the browser emulator): with en_bus_on, each en_render also leaves its frames in en_bus, six
 * floats a frame: Pulse A and the lead, Pulse B, Triangle, Noise, Wave, the echo and the weather; dry, at the
 * mix's level */
#define EN_NBUS 6
extern volatile uint8_t en_bus_on;
extern float en_bus[];                         /* EN_NBUS * 1024 on the host and in the browser */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen and the lights */
extern volatile uint8_t en_playing, en_ext;
extern volatile uint8_t en_step;               /* the sixteenth playing, 0..15 */
extern volatile uint32_t en_bars;              /* bar lines since start */
extern volatile uint8_t en_root, en_mode;      /* the key now (pitch class) and the mode */
extern volatile uint8_t en_chord;              /* the chord's degree in the progression */
extern volatile uint16_t en_pcs;               /* the scale now: bit p = pitch class p */
extern volatile float en_glow[EN_NCH];         /* 0..1: each channel just played */
extern volatile uint8_t en_last[EN_NCH];       /* each channel's last note (MIDI) */
extern volatile uint32_t en_notes, en_hits, en_events, en_dropped;   /* for the tests */

/* the pure parts, for the tests and the screen */
int en_snap(int midi, uint16_t pcs);           /* the nearest note in the scale */
uint32_t en_hash_str(const char *s);           /* FNV-1a, as the city's seed */
