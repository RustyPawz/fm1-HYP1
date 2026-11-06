/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: EVOL-1, a tracker built round the sound of RedtailWorks' album Evol (a RedtailWorks fork of
 * AMB-1, by way of HYP-1 and GRAV-1).
 *
 *   SONG      five songs, each its own flash object: an order list of up to 32 patterns, sixteen patterns of
 *             sixteen rows (a bar of sixteenths), six tracks a row. A cell is a note (or a note-off) and one
 *             effect: a command and a value, 0..F.
 *   TRACKS    six, each playing one sound from the bank: the album's kits (a note picks the drum), its sub,
 *             drop and pulse basses, its drones and pumping pads, its FM bells and phrygian leads, its noise
 *             beds and glitches. A track's sound is shaped by its own Tone, Decay, Level, Motion (a slow
 *             sweep of the filter), Pulse (a tremolo locked to the tempo), Evolve (the pattern drifting a
 *             little on each pass, in the scale), Pan, Echo, Reverb and Grit.
 *   SPACE     a tempo-synced ping-pong echo, Dattorro's plate (dark and long), a tape (hiss, a soft top,
 *             a little crush), the kick's duck on everything else.
 *
 * The song lives in en_song: the app edits its cells and the render reads them (a cell is two bytes; a torn
 * read is one cell, one row, and harmless). Parameters go through the command queue like every RedtailWorks
 * instrument: en_render() runs in the audio interrupt and drains the commands at the start of a block; state
 * marked (UI) is written by the render and only read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define EN_SR 44100.0f
#define EN_NTRK 6                      /* tracks */
#define EN_NROW 16                     /* rows a pattern: a bar of sixteenths */
#define EN_NPAT 16                     /* patterns a song */
#define EN_NORD 32                     /* the order list's length, at most */
#define EN_NSOUND 24                   /* the sound bank */
#define EN_NDRUM 8                     /* drums in a kit: a note's pitch class picks one */
#define EN_NFX 16                      /* effect commands, 0 none */
#define EN_NSONG 5

/* a cell's note: 0 empty, 1 a note-off, 12..119 a MIDI note */
#define NOTE_EMPTY 0
#define NOTE_OFF 1
#define NOTE_LO 12
#define NOTE_HI 119
/* its effect: command << 4 | value */
enum { FX_NONE, FX_GLIDE, FX_CHANCE, FX_RATCHET, FX_DELAY, FX_TONE, FX_VOLUME, FX_BEND, FX_ECHO, FX_CHORD,
       FX_MUTATE, FX_LENGTH, FX_CRUSH, FX_STUTTER, FX_SWELL, FX_JUMP };
extern const char EN_FX_CHAR[EN_NFX + 1];      /* the letter the grid shows ('.' none) */
extern const char *const EN_FX_NAME[EN_NFX];

typedef struct {
    uint8_t note, fx;
} en_cell_t;

/* modes: pools relative to the root (bit i: root + i semitones) */
enum { MODE_AEOLIAN, MODE_PHRYGIAN, MODE_DORIAN, MODE_MIXOLYDIAN, MODE_MAJOR, MODE_LYDIAN, MODE_PENT_MIN,
       MODE_CHROMATIC, EN_NMODES };
extern const char *const EN_MODE_NAME[EN_NMODES];
extern const uint16_t EN_MODE_POOL[EN_NMODES];
extern const char *const EN_NOTE_NAME[12];
extern const char *const EN_SOUND_NAME[EN_NSOUND];
int en_sound_is_kit(int s);
const char *en_drum_name(int sound, int note);  /* a kit's drum for a note: three letters ("KIK") */

/* parameters: the song's (saved with it), then the device's (saved in the project); ranges in en_param_info() */
#define EN_TP 11
enum { TP_SOUND, TP_TONE, TP_DECAY, TP_LEVEL, TP_MOTION, TP_PULSE, TP_EVOLVE, TP_PAN, TP_ECHO, TP_VERB, TP_GRIT };
enum {
    P_TEMPO, P_SWING, P_ROOT, P_MODE,                 /* the song */
    P_LEN, P_LOOP, P_ECHO_T, P_ECHO_FB,               /* its order list's length, Song / Pattern; the echo's time, feedback */
    P_VERB, P_TAPE, P_DUCK, P_PULSE_T,                /* the plate's size, the tape, the kick's duck, the Pulse's note */
    P_TRK,                                            /* each track: P_T(t, TP_*) */
    P_NSONG = P_TRK + EN_TP * EN_NTRK,                /* the parameters a song keeps */
    P_TUNE = P_NSONG, P_SYNC, P_MIDI, P_MCH,          /* the device's */
    P_THEME, P_LEDS,
    P_NPARAMS
};
#define P_T(t, k) (P_TRK + EN_TP * (t) + (k))
#define EN_NTHEMES 5
enum { LOOP_SONG, LOOP_PATTERN };
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} en_param_t;
const en_param_t *en_param_info(int i);
void en_param_text(int i, int v, char *buf);   /* the value as the screen shows it (16 bytes) */

/* the song, as saved (one flash object, PLAT_STORE_MAX at most) */
#define EN_SONG_MAGIC 0x4C4F5645u              /* "EVOL" */
#define EN_SONG_FORMAT 1u
typedef struct {
    uint32_t magic;
    uint16_t format, rsv;
    char name[16];
    int16_t par[P_NSONG];
    uint8_t order[EN_NORD];
    en_cell_t cell[EN_NPAT][EN_NROW][EN_NTRK];
} en_song_t;
extern en_song_t en_song;                      /* the song playing (and being edited) */
int en_song_valid(const en_song_t *s);         /* 1: every value in range */
void en_song_recipe(int i, en_song_t *s);      /* song i as it comes, each after a track of the album */
const char *en_song_title(int i);

/* commands (main loop -> render) */
void en_init(void);
void en_set(int param, int value);
void en_play(int pos, int row);                /* play from an order position and row */
void en_stop(void);
void en_audition(int track, int note, int on); /* a key into a track: its note on (1) or let go (0) */
void en_reload(void);                          /* the song changed under the engine (loaded): every value again */
void en_seed(uint32_t s);
void en_panic(void);
void en_mute(int on);                          /* remote: silent and no MIDI out (1), or back (0) */
void en_track_mute(uint32_t mask);             /* the tracks muted (bit t) */
/* MIDI clock in. With SYNC on, the song follows it while it comes: 24 ticks a beat, six a row; START plays
 * from the top, STOP stops. Without clock (or SYNC off), its own tempo. */
enum { EN_CLK_TICK, EN_CLK_START, EN_CLK_CONTINUE, EN_CLK_STOP };
void en_clock(int msg);
enum { EN_MIDI_OFF, EN_MIDI_NOTES, EN_MIDI_CLOCK, EN_MIDI_BOTH };   /* P_MIDI: what goes out */
/* MIDI out (USB): track t's notes on channel MCH + t; the clock (24 a beat, START, STOP) when asked */
extern volatile float en_bpm;                  /* the tempo now: the clock's while it comes, else TEMPO (UI) */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void en_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);
/* the tracks apart (the browser emulator): with en_bus_on, each en_render also leaves its frames in en_bus,
 * six floats a frame: tracks 1-2, 3, 4, 5, 6, the echo and the reverb; dry, mono, at the mix's level */
#define EN_NBUS 6
extern volatile uint8_t en_bus_on;
extern float en_bus[];                         /* EN_NBUS * 1024 on the host and in the browser */

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile uint8_t en_playing, en_ext;
extern volatile uint8_t en_pos, en_row;        /* the order position and row playing */
extern volatile uint8_t en_pat_now;            /* the pattern playing */
extern volatile float en_trk_glow[EN_NTRK];    /* 0..1: the track just played */
extern volatile uint8_t en_trk_note[EN_NTRK];  /* its last note (MIDI) */
extern volatile float en_level[EN_NTRK];       /* how loud each track is now, about 0..1 */
extern volatile uint32_t en_notes;             /* notes since start (for tests) */
extern volatile uint32_t en_rows;              /* rows played since start */
extern volatile uint32_t en_passes;            /* patterns played through */
extern volatile uint32_t en_evolved;           /* notes Evolve has moved */

/* what happened, for the screen's messages: codes in a ring, en_ev_w counts them */
enum { EV_LOOP = 1, EV_JUMP };
#define EN_NEV 16
extern volatile uint16_t en_ev[EN_NEV];        /* code << 8 | detail */
extern volatile uint32_t en_ev_w;

/* the pure parts, for the tests, the screen and Grow */
int en_scale(uint32_t pool, int *steps);       /* the pool's pitch classes (relative), ascending; count */
int en_snap(int note, int root, uint32_t pool);   /* a note moved to the nearest in the scale (below on a tie) */
/* GROW: write one track of one pattern in one of the album's styles, from a seed; density and range 0..100.
 * Notes in the song's scale; a kit's track gets drums. */
#define EN_NGROW 8
extern const char *const EN_GROW_NAME[EN_NGROW];
void en_grow(en_song_t *s, int pat, int track, int style, int density, int range, uint32_t seed);
