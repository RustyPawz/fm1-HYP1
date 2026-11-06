/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: BIT-1, a generative 8-bit tracker (a RedtailWorks fork of AMB-1, by way of HYP-1, GRAV-1,
 * CTY-1 and EVOL-1; tracker ideas after KAWA by Omnipotard).
 *
 *   SONG      five songs, each its own flash object: an order list of up to 32 patterns, sixteen patterns of
 *             sixteen rows (a bar of sixteenths), six tracks a row. A cell is a note (or a note-off) and one
 *             effect: a command and a value, 0..F.
 *   TRACKS    six, each playing one sound from the bank: three chip kits (a note picks the drum), and chip
 *             voices: an NES-style pulse (12.5, 25, 50%) and stepped triangle, Game Boy-style 4-bit waves, an
 *             LFSR noise (long or metal), SID-style saws and pulses through a resonant filter. Chip voices step
 *             their volume in sixteen levels and run their macros (arpeggio, vibrato, sweep) at frame rate.
 *             A track's sound is shaped by its own Tone (a pulse's duty, a filter's brightness), Decay, Level,
 *             Vibrato, Arp (the note played as a fast arpeggio in the scale), Evolve (the pattern drifting a
 *             little on each pass, in the scale), Pan, Echo, Reverb and Crush.
 *   SPACE     a tempo-synced ping-pong echo, Dattorro's plate, Lo-fi (a darker top and fewer bits), the kick's
 *             duck on everything else.
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
/* its effects (two a cell, after KAWA's): command << 4 | value */
enum { FX_NONE, FX_GLIDE, FX_CHANCE, FX_RATCHET, FX_DELAY, FX_TONE, FX_VIB, FX_BEND, FX_HOP, FX_CHORD,
       FX_MUTATE, FX_LENGTH, FX_CRUSH, FX_STUTTER, FX_SWELL, FX_JUMP };
extern const char EN_FX_CHAR[EN_NFX + 1];      /* the letter the grid shows ('.' none) */
extern const char *const EN_FX_NAME[EN_NFX];

/* a cell: its note, its volume (0 as the track plays it, 1..16 the levels 0..F), and two effects */
typedef struct {
    uint8_t note, vol, fx, fx2;
} en_cell_t;
#define EN_VOL(v) ((v) - 1)                    /* a volume's level, 0..15 (when v > 0) */

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
#define EN_TP 12
enum { TP_SOUND, TP_TONE, TP_DECAY, TP_LEVEL, TP_VIB, TP_ARP, TP_EVOLVE, TP_PAN, TP_ECHO, TP_VERB, TP_CRUSH,
       TP_FOLLOW };
/* a track's Arp: the shapes (Off, then in the scale) */
#define EN_NARP 8
extern const char *const EN_ARP_NAME[EN_NARP];
/* a track's Follow: its role under the lead (Off: it plays its own cells; Auto: the role its sound suits) */
enum { FR_OFF, FR_AUTO, FR_BASS, FR_CHORDS, FR_ARP, FR_HARMONY, FR_COUNTER, FR_DRUMS, EN_NROLE };
extern const char *const EN_ROLE_NAME[EN_NROLE];
enum {
    P_TEMPO, P_SWING, P_ROOT, P_MODE,                 /* the song */
    P_LEN, P_LOOP, P_ECHO_T, P_ECHO_FB,               /* its order list's length, Song / Pattern; the echo's time, feedback */
    P_VERB, P_LOFI, P_DUCK, P_ARP_T,                  /* the plate's size, Lo-fi, the kick's duck, the arps' rate */
    P_LEAD, P_FDENS, P_TAKE,                          /* Follow: the lead track (0 off, 1-6), how busy, the take */
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
#define EN_SONG_MAGIC 0x53544942u              /* "BITS" */
#define EN_SONG_FORMAT 3u
/* the order list: each position a pattern (low nibble) played 1..16 times (high nibble + 1) */
#define EN_ORD_PAT(o) ((o) & 15)
#define EN_ORD_REP(o) (((o) >> 4) + 1)
#define EN_ORD(pat, rep) ((uint8_t)(((pat) & 15) | (((rep) - 1) & 15) << 4))
typedef struct {
    uint32_t magic;
    uint16_t format, rsv;
    char name[16];
    int16_t par[P_NSONG];
    uint8_t order[EN_NORD];
    uint8_t len[EN_NPAT];                      /* each pattern's rows, 1..16 (KAWA's END) */
    en_cell_t cell[EN_NPAT][EN_NROW][EN_NTRK];
} en_song_t;
extern en_song_t en_song;                      /* the song playing (and being edited) */
int en_song_valid(const en_song_t *s);         /* 1: every value in range */
/* a song as saved: its cells packed (a bitmap of the cells used, then each used cell's note and effect, and its
 * volume and second effect when it has them). The size, or -1 when it won't fit in max bytes. */
int en_song_pack(const en_song_t *s, uint8_t *buf, int max);
int en_song_unpack(const uint8_t *buf, int n, en_song_t *s);   /* 0: a valid song */
int en_pat_len(const en_song_t *s, int pat);  /* its rows, 1..16 */
int en_song_size(const en_song_t *s);         /* its packed size, in bytes */
void en_song_recipe(int i, en_song_t *s);      /* song i as it comes */
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
/* GROW: write one track of one pattern in one of eight chip styles, from a seed; density and range 0..100.
 * Notes in the song's scale; a kit's track gets drums (36 KIK, 37 SNR, 38 HAT, 39 OHT, 40 TOM, 41-43 the kit's).
 * Then KAWA's MAGIC tools, on what's there: Euclid (density's hits, as evenly as they go), Vary (a few notes a
 * step in the scale, a few moved), Rolls (ratchets at the ends of beats), Accents (the beats loud, the rest
 * softer), Shift (on a row), Up (a step up the scale). */
#define EN_NGROW 14
enum { G_DRIVER, G_HOLD, G_OSTINATO, G_MELODY, G_TRIPLET, G_ARPRUN, G_BLEEPS, G_SPARSE,
       G_EUCLID, G_VARY, G_ROLLS, G_ACCENTS, G_SHIFT, G_UP };
extern const char *const EN_GROW_NAME[EN_NGROW];
void en_grow(en_song_t *s, int pat, int track, int style, int density, int range, uint32_t seed);
/* FOLLOW: with a lead (P_LEAD), each track given a role plays a take written from the lead's notes in the same
 * pattern instead of its own cells: the chord of each half bar heard in the lead (the scale's triads scored
 * against its notes, the strong beats weighing more), then a bassline, chords, an arp, a harmony a third under,
 * a counter-line in the lead's rests, or drums that answer its rhythm. The take is the same from the same seed;
 * the track's Evolve is how often a pass brings a new one (0: the take stays). */
int en_follow_role(const en_song_t *s, int track);   /* the role it plays now: FR_OFF, or FR_BASS..FR_DRUMS */
uint32_t en_take_seed(const en_song_t *s, int track, int pat);   /* the take playing (the render's, or the first) */
void en_follow(const en_song_t *s, int pat, int track, uint32_t seed, en_cell_t out[EN_NROW]);
int en_follow_chords(const en_song_t *s, int pat, int deg[2]);   /* the two chords heard: degrees of the scale; 0 no lead */
extern volatile uint32_t en_takes;             /* new takes since start (for tests) */
