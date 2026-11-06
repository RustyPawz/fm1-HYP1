/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (amb.h): EVOL-1, a tracker round the sound of the album Evol. One compilation unit, -O2.
 * The plate reverb, the command queue and the fast maths are AMB-1's (Charles Vestal), by way of GRAV-1; the
 * tracker, the voices and the sound bank are new. The sounds come from measuring the album: two thirds of
 * its energy under 150 Hz, sine kicks at 45-55 Hz falling from 120, an Eb harmonic stack over D, pads pumping
 * at the tempo, FM bells at 1:2 and 1:3.5 on fourths and fifths, a phrygian lead gliding a semitone, a throat
 * in the low mids, a dotted-eighth ping-pong and a dark plate of three to five seconds. */
#include "amb.h"
#include "fastmath.h"

#ifdef EN_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const EN_MODE_NAME[EN_NMODES] = {"Aeolian", "Phrygian", "Dorian", "Mixolydian", "Major", "Lydian",
                                             "Pent min", "Chromatic"};
const uint16_t EN_MODE_POOL[EN_NMODES] = {0x5ADu, 0x5ABu, 0x6ADu, 0x6B5u, 0xAB5u, 0xAD5u, 0x4A9u, 0xFFFu};
const char EN_FX_CHAR[EN_NFX + 1] = ".G?RDTVBEAMLXSWJ";
const char *const EN_FX_NAME[EN_NFX] = {"None", "Glide", "Chance", "Ratchet", "Delay", "Tone", "Volume", "Bend",
                                        "Echo", "Chord", "Mutate", "Length", "Crush", "Stutter", "Swell", "Jump"};
const char *const EN_SOUND_NAME[EN_NSOUND] = {
    "Evol kit", "Ritual kit", "Glitch kit",
    "Sub root", "Drop bass", "Pulse bass", "Reso bass",
    "Eb stack", "Pump pad", "Wide pad", "Throat", "Sweep", "Cold choir",
    "FM bell", "Dark pluck", "Phrygian", "Glass", "Hollow",
    "Noise bed", "Glitch", "Static", "Wind", "Bugs", "Rumble"};
const char *const EN_GROW_NAME[EN_NGROW] = {"Pulse", "Drone", "Ostinato", "Phrygian", "Triplet", "Drift", "Swarm", "Sparse"};
static const char *const THEME_NAME[EN_NTHEMES] = {"Evol", "Static", "Bruise", "Ember", "Mono"};

/* ---------------------------------------------------------- parameters --- */
#define TRK(n, s, lv, ev) {"Sound", 0, EN_NSOUND - 1, s}, {"Tone", -50, 50, 0}, {"Decay", 10, 400, 100}, \
    {"Level", 0, 100, lv}, {"Motion", 0, 100, 0}, {"Pulse", 0, 100, 0}, {"Evolve", 0, 100, ev}, {"Pan", -50, 50, 0}, \
    {"Echo", 0, 100, 0}, {"Reverb", 0, 100, 30}, {"Grit", 0, 100, 0}
static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 40, 200, 96},   {"Swing", 0, 75, 0},     {"Root", 0, 11, 2},       {"Mode", 0, EN_NMODES - 1, MODE_PHRYGIAN},
    {"Length", 1, EN_NORD, 8}, {"Loop", 0, 1, 0},      {"Echo time", 0, 5, 2},   {"Feedback", 0, 95, 30},
    {"Reverb", 0, 100, 70},   {"Tape", 0, 100, 30},    {"Duck", 0, 100, 35},     {"Pulse", 0, 4, 1},
    TRK(1, 0, 70, 0), TRK(2, 3, 80, 0), TRK(3, 7, 60, 20), TRK(4, 8, 55, 0), TRK(5, 13, 50, 25), TRK(6, 18, 30, 0),
    {"Tune", -50, 50, 0},     {"Sync", 0, 1, 1},       {"MIDI out", 0, 3, 1},    {"Channel", 0, 10, 0},
    {"Theme", 0, EN_NTHEMES - 1, 0}, {"Lights", 0, 2, 1},
};
#undef TRK
const en_param_t *en_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static char *itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static void cpy(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}

void en_param_text(int i, int v, char *b)
{
    static const char *const MIDI_NAME[4] = {"Off", "Notes", "Clock", "Both"};
    static const char *const ECHO_NAME[6] = {"1/16", "1/8", "1/8 dot", "1/4 tri", "1/4", "1/4 dot"};
    static const char *const PULSE_NAME[5] = {"1/16", "1/8", "1/8 dot", "1/8 tri", "1/4"};
    static const char *const LEDS_NAME[3] = {"Off", "On", "Keys"};
    int k = i >= P_TRK && i < P_NSONG ? (i - P_TRK) % EN_TP : -1;
    char *e;
    if (i == P_MIDI)
        cpy(b, MIDI_NAME[(unsigned)v < 4u ? v : 0]);
    else if (i == P_SYNC)
        cpy(b, v ? "Clock" : "Free");
    else if (i == P_LOOP)
        cpy(b, v ? "Pattern" : "Song");
    else if (i == P_ROOT)
        cpy(b, EN_NOTE_NAME[(unsigned)v < 12u ? v : 0]);
    else if (i == P_MODE)
        cpy(b, EN_MODE_NAME[(unsigned)v < EN_NMODES ? v : 0]);
    else if (i == P_ECHO_T)
        cpy(b, ECHO_NAME[(unsigned)v < 6u ? v : 0]);
    else if (i == P_PULSE_T)
        cpy(b, PULSE_NAME[(unsigned)v < 5u ? v : 0]);
    else if (i == P_THEME)
        cpy(b, THEME_NAME[(unsigned)v < EN_NTHEMES ? v : 0]);
    else if (i == P_LEDS)
        cpy(b, LEDS_NAME[(unsigned)v < 3u ? v : 0]);
    else if (i == P_MCH) {
        b[0] = 'C', b[1] = 'h', b[2] = ' ';
        e = itoa_s(v + 1, b + 3);
        *e++ = '-';
        itoa_s(v + 6, e);
    } else if (k == TP_SOUND)
        cpy(b, EN_SOUND_NAME[(unsigned)v < EN_NSOUND ? v : 0]);
    else if (k == TP_PAN) {
        if (!v)
            cpy(b, "Centre");
        else {
            b[0] = v < 0 ? 'L' : 'R';
            itoa_s(v < 0 ? -v : v, b + 1);
        }
    } else if (k == TP_TONE || i == P_TUNE) {
        e = b;
        if (v > 0)
            *e++ = '+';
        itoa_s(v, e);
    } else {
        e = itoa_s(v, b);
        if (i == P_TEMPO || i == P_LEN)
            return;
        *e++ = '%';
        *e = 0;
    }
}

int en_scale(uint32_t pool, int *steps)
{
    int n = 0, i;
    for (i = 0; i < 12; i++)
        if (pool >> i & 1u)
            steps[n++] = i;
    return n;
}
int en_snap(int note, int root, uint32_t pool)
{
    int d;
    if (!(pool & 0xFFFu))
        return note;
    for (d = 0; d < 7; d++) {                   /* the nearer first; on a tie, below */
        if (pool >> (((note - d - root) % 12 + 24) % 12) & 1u)
            return note - d;
        if (d && pool >> (((note + d - root) % 12 + 24) % 12) & 1u)
            return note + d;
    }
    return note;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_STOP, C_AUD_ON, C_AUD_OFF, C_RELOAD, C_SEED, C_PANIC, C_CLOCK, C_MUTE, C_TMUTE };
static uint8_t muted;                           /* remote: the FM-1 is playing the browser */
static float mute_g = 1.0f;
#define QN 256u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int pos, int row) { post(C_PLAY, pos, row); }
void en_stop(void) { post(C_STOP, 0, 0); }
void en_audition(int t, int note, int on) { post(on ? C_AUD_ON : C_AUD_OFF, t, note); }
void en_reload(void) { post(C_RELOAD, 0, 0); }
void en_seed(uint32_t s) { post(C_SEED, 0, (int)(s & 0xFFFFu)); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_mute(int on) { post(C_MUTE, on, 0); }
void en_track_mute(uint32_t m) { post(C_TMUTE, (int)(m & 0x3Fu), 0); }

/* -------------------------------------------------------------- state --- */
en_song_t en_song;
volatile uint8_t en_playing, en_ext, en_pos, en_row, en_pat_now;
volatile float en_bpm = 96.0f;
volatile uint8_t en_bus_on;
#ifdef EN_HOST
float en_bus[EN_NBUS * 1024];
#else
float en_bus[EN_NBUS];                          /* the FM-1 has no use for them yet: no room taken */
#endif
volatile float en_trk_glow[EN_NTRK], en_level[EN_NTRK];
volatile uint8_t en_trk_note[EN_NTRK];
volatile uint32_t en_notes, en_rows, en_passes, en_evolved;
volatile uint16_t en_ev[EN_NEV];
volatile uint32_t en_ev_w;

static int16_t par[P_NPARAMS];
static uint32_t pool = 0x5ABu;
static float tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u, noise_s = 22222u, noise_t = 7777u;
static uint32_t tmute;                          /* the tracks muted */

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static inline float wnoise(uint32_t *s)         /* white, -1..1 */
{
    *s = *s * 1664525u + 1013904223u;
    return (float)(int32_t)*s * (1.0f / 2147483648.0f);
}
static float lvl(int v) { return (float)(v * v) * 0.0001f; }
static float pct(int v) { return (float)v * 0.01f; }
static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static float onepole_k(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(fc, 18000.0f) * (1.0f / EN_SR)); }
static float coef(float secs) { return secs > 0.00002f ? fm_expf(-6.9f / (secs * EN_SR)) : 0.0f; }   /* -60 dB in secs */

static void event(int code, int detail)
{
    uint32_t w = en_ev_w;
    en_ev[w % EN_NEV] = (uint16_t)(code << 8 | (detail & 0xFF));
    BARRIER();
    en_ev_w = w + 1;
}

/* a sine table: 1024 points a turn, plus one */
#define SN 1024
static float sint[SN + 1];
static inline float sine(float ph)              /* ph in turns, any value >= 0 */
{
    float x = (ph - (float)(int)ph) * (float)SN;
    int i = (int)x;
    float f = x - (float)i;
    return sint[i] + (sint[i + 1] - sint[i]) * f;
}
static void pan_gains(float pn, float *gl, float *gr)
{
    pn = fm_clampf(pn, -1.0f, 1.0f);
    *gl = fm_cosf((pn + 1.0f) * (FM_PI / 4.0f));
    *gr = fm_sinf((pn + 1.0f) * (FM_PI / 4.0f));
}

/* ----------------------------------------------------------- the sounds --- */
/* One voice for everything: one to three oscillators (sine, triangle, saw, pulse, three detuned saws, two-op
 * FM, noise), a sub an octave down, noise mixed in, a pitch that falls (or glides) to the note, a TPT state-
 * variable filter (low, band or high pass) with its own envelope, an ADSR, a drive and a crush. A kit is eight
 * of these, each with its own fixed pitch. */
enum { O_SINE, O_TRI, O_SAW, O_PULSE, O_SUPER, O_FM, O_NOISE };
enum { F_LP, F_BP, F_HP };
typedef struct {
    uint8_t osc, fmode;
    float hz;                                   /* a fixed pitch (drums), 0: the note's */
    float det, pw, sub, ratio, idx, itime, noise;
    float cut, res, fenv, ftime;                /* Hz; 0..1; octaves; s */
    float att, dec, sus, rel;                   /* s, s, 0..1, s */
    float penv, ptime;                          /* semitones above, falling to the note in ptime */
    float drive, crush, verb, echo, gain;
    int8_t reg;                                 /* register: semitones */
} snd_t;
#define S_(o, f) .osc = o, .fmode = f
static const snd_t MEL[EN_NSOUND - 3] = {
    /* Sub root: sine and a little triangle, mono weight, a slow breath in */
    {S_(O_SINE, F_LP), .sub = 0.0f, .noise = 0.0f, .det = 0.0f, .cut = 260.0f, .res = 0.1f, .att = 0.4f, .dec = 2.0f,
     .sus = 1.0f, .rel = 1.2f, .drive = 1.6f, .verb = 0.1f, .gain = 0.6f, .reg = -12},
    /* Drop bass: a sine falling an octave over 0.4 s, a click of noise */
    {S_(O_SINE, F_LP), .noise = 0.05f, .cut = 900.0f, .fenv = 1.5f, .ftime = 0.08f, .att = 0.002f, .dec = 0.9f,
     .sus = 0.3f, .rel = 0.4f, .penv = 12.0f, .ptime = 0.4f, .drive = 1.8f, .verb = 0.15f, .gain = 0.8f, .reg = 0},
    /* Pulse bass: a 30% pulse, low pass, a little envelope */
    {S_(O_PULSE, F_LP), .pw = 0.3f, .sub = 0.5f, .cut = 380.0f, .res = 0.25f, .fenv = 1.8f, .ftime = 0.18f, .att = 0.003f,
     .dec = 0.5f, .sus = 0.6f, .rel = 0.15f, .drive = 1.3f, .verb = 0.1f, .gain = 0.55f, .reg = 0},
    /* Reso bass: a saw into a resonant low pass, the envelope sweeping it */
    {S_(O_SAW, F_LP), .sub = 0.3f, .cut = 220.0f, .res = 0.72f, .fenv = 3.2f, .ftime = 0.22f, .att = 0.002f, .dec = 0.4f,
     .sus = 0.4f, .rel = 0.12f, .drive = 1.6f, .verb = 0.12f, .echo = 0.2f, .gain = 0.5f, .reg = 0},
    /* Eb stack: saw and pulse a few cents apart, the harmonic stack through a resonant 1.2 kHz low pass */
    {S_(O_SUPER, F_LP), .det = 7.0f, .sub = 0.35f, .cut = 1200.0f, .res = 0.45f, .att = 1.6f, .dec = 3.0f, .sus = 0.85f,
     .rel = 2.5f, .drive = 1.4f, .verb = 0.6f, .echo = 0.15f, .gain = 0.62f, .reg = -12},
    /* Pump pad: three saws +/-9 cents, low pass at 1.4 kHz (made to pump with Pulse) */
    {S_(O_SUPER, F_LP), .det = 9.0f, .cut = 1400.0f, .res = 0.15f, .att = 0.25f, .dec = 2.0f, .sus = 0.8f, .rel = 1.2f,
     .drive = 1.1f, .verb = 0.7f, .echo = 0.2f, .gain = 0.72f, .reg = 0},
    /* Wide pad: wider and softer, slower */
    {S_(O_SUPER, F_LP), .det = 14.0f, .cut = 900.0f, .res = 0.1f, .att = 2.2f, .dec = 3.0f, .sus = 0.9f, .rel = 3.0f,
     .drive = 1.0f, .verb = 0.9f, .echo = 0.25f, .gain = 0.75f, .reg = 0},
    /* Throat: a saw through a band near 320 Hz, the low mids' growl (Motion moves the vowel) */
    {S_(O_SAW, F_BP), .sub = 0.25f, .cut = 330.0f, .res = 0.82f, .att = 0.3f, .dec = 2.0f, .sus = 0.9f, .rel = 0.9f,
     .drive = 2.0f, .verb = 0.5f, .gain = 1.0f, .reg = -12},
    /* Sweep: a resonant band, its envelope sweeping two octaves over seconds */
    {S_(O_SAW, F_BP), .det = 5.0f, .cut = 300.0f, .res = 0.88f, .fenv = 3.0f, .ftime = 3.5f, .att = 0.8f, .dec = 4.0f,
     .sus = 0.7f, .rel = 2.5f, .drive = 1.2f, .verb = 0.8f, .echo = 0.3f, .gain = 0.95f, .reg = 0},
    /* Cold choir: FM, slow, a vowel's band */
    {S_(O_FM, F_LP), .ratio = 2.0f, .idx = 0.35f, .noise = 0.02f, .cut = 1600.0f, .res = 0.3f, .att = 1.5f, .dec = 3.0f,
     .sus = 0.9f, .rel = 2.5f, .verb = 0.9f, .echo = 0.2f, .gain = 0.85f, .reg = 0},
    /* FM bell: 1:3.5, the index falling, long */
    {S_(O_FM, F_LP), .ratio = 3.5f, .idx = 2.2f, .itime = 0.5f, .cut = 5000.0f, .att = 0.002f, .dec = 2.4f, .sus = 0.0f,
     .rel = 2.0f, .verb = 0.8f, .echo = 0.35f, .gain = 0.75f, .reg = 0},
    /* Dark pluck: 1:2 FM, short, low passed: the ostinatos */
    {S_(O_FM, F_LP), .ratio = 2.0f, .idx = 1.6f, .itime = 0.18f, .cut = 1500.0f, .res = 0.2f, .fenv = 1.0f, .ftime = 0.2f,
     .att = 0.002f, .dec = 0.6f, .sus = 0.0f, .rel = 0.5f, .verb = 0.6f, .echo = 0.4f, .gain = 0.85f, .reg = 0},
    /* Phrygian: a 30% pulse, low passed at 1.5 kHz, gliding (Glide, or always a little) */
    {S_(O_PULSE, F_LP), .pw = 0.3f, .det = 4.0f, .cut = 1500.0f, .res = 0.3f, .fenv = 0.6f, .ftime = 0.3f, .att = 0.02f,
     .dec = 1.0f, .sus = 0.75f, .rel = 0.35f, .penv = 0.0f, .drive = 1.3f, .verb = 0.5f, .echo = 0.45f, .gain = 0.55f, .reg = 0},
    /* Glass: high FM, thin, for the top */
    {S_(O_FM, F_HP), .ratio = 5.0f, .idx = 1.0f, .itime = 0.8f, .cut = 400.0f, .att = 0.004f, .dec = 1.6f, .sus = 0.0f,
     .rel = 1.5f, .verb = 0.9f, .echo = 0.5f, .gain = 0.55f, .reg = 12},
    /* Hollow: a triangle through a soft band, a flute in the dark */
    {S_(O_TRI, F_BP), .noise = 0.08f, .cut = 900.0f, .res = 0.35f, .att = 0.12f, .dec = 1.5f, .sus = 0.7f, .rel = 0.8f,
     .verb = 0.7f, .echo = 0.4f, .gain = 1.0f, .reg = 0},
    /* Noise bed: noise low passed at 4 kHz, about -30 dB under the rest, its note moving the band */
    {S_(O_NOISE, F_LP), .cut = 3800.0f, .res = 0.1f, .att = 1.2f, .dec = 2.0f, .sus = 1.0f, .rel = 2.0f, .verb = 0.5f,
     .gain = 0.16f, .reg = 0},
    /* Glitch: noise bursts, crushed hard, short */
    {S_(O_NOISE, F_BP), .cut = 2500.0f, .res = 0.5f, .att = 0.001f, .dec = 0.06f, .sus = 0.0f, .rel = 0.04f, .crush = 0.75f,
     .verb = 0.2f, .echo = 0.5f, .gain = 0.45f, .reg = 0},
    /* Static: a radio's hiss in a band at the note */
    {S_(O_NOISE, F_BP), .cut = 1800.0f, .res = 0.7f, .fenv = -1.0f, .ftime = 1.0f, .att = 0.3f, .dec = 1.5f, .sus = 0.6f,
     .rel = 1.0f, .crush = 0.3f, .verb = 0.6f, .gain = 0.35f, .reg = 0},
    /* Wind: noise through a slow resonant band */
    {S_(O_NOISE, F_BP), .cut = 700.0f, .res = 0.75f, .fenv = 1.5f, .ftime = 2.5f, .att = 2.0f, .dec = 3.0f, .sus = 0.8f,
     .rel = 3.0f, .verb = 0.8f, .gain = 0.4f, .reg = 0},
    /* Bugs: chirps, an FM blip falling fast, crushed */
    {S_(O_FM, F_HP), .ratio = 1.5f, .idx = 3.0f, .itime = 0.05f, .cut = 300.0f, .att = 0.001f, .dec = 0.09f, .sus = 0.0f,
     .rel = 0.06f, .penv = 19.0f, .ptime = 0.05f, .crush = 0.5f, .verb = 0.3f, .echo = 0.5f, .gain = 0.35f, .reg = 12},
    /* Rumble: a low saw cluster through a dark low pass, drive */
    {S_(O_SUPER, F_LP), .det = 18.0f, .sub = 0.6f, .noise = 0.05f, .cut = 160.0f, .res = 0.4f, .att = 1.0f, .dec = 3.0f,
     .sus = 1.0f, .rel = 2.0f, .drive = 2.5f, .verb = 0.4f, .gain = 0.5f, .reg = -12},
};
/* the kits: the album's drums are quiet under the drones; a soft sine kick at 50 Hz falling from about 120,
 * muffled ticks, toms and metal rather than a bright snare */
static const snd_t KITS[3][EN_NDRUM] = {
    {   /* Evol kit */
        {S_(O_SINE, F_LP), .hz = 50.0f, .noise = 0.02f, .cut = 700.0f, .att = 0.001f, .dec = 0.38f, .rel = 0.1f,
         .penv = 15.0f, .ptime = 0.07f, .drive = 1.6f, .verb = 0.05f, .gain = 1.1f},                       /* KIK */
        {S_(O_NOISE, F_BP), .cut = 3200.0f, .res = 0.4f, .att = 0.0005f, .dec = 0.035f, .rel = 0.02f, .verb = 0.3f,
         .echo = 0.2f, .gain = 0.5f},                                                                       /* TIK */
        {S_(O_TRI, F_LP), .hz = 185.0f, .noise = 0.55f, .cut = 2600.0f, .att = 0.001f, .dec = 0.18f, .rel = 0.05f,
         .penv = 5.0f, .ptime = 0.03f, .verb = 0.5f, .gain = 0.5f},                                         /* SNR */
        {S_(O_SINE, F_LP), .hz = 92.0f, .cut = 900.0f, .att = 0.001f, .dec = 0.45f, .rel = 0.1f, .penv = 7.0f,
         .ptime = 0.1f, .drive = 1.3f, .verb = 0.5f, .gain = 0.75f},                                        /* TOM */
        {S_(O_FM, F_BP), .hz = 410.0f, .ratio = 3.7f, .idx = 3.0f, .itime = 0.3f, .cut = 2400.0f, .res = 0.3f,
         .att = 0.001f, .dec = 0.5f, .rel = 0.2f, .verb = 0.6f, .echo = 0.3f, .gain = 0.4f},                /* MTL */
        {S_(O_NOISE, F_HP), .cut = 7500.0f, .att = 0.0005f, .dec = 0.05f, .rel = 0.02f, .verb = 0.2f, .gain = 0.28f},   /* HAT */
        {S_(O_NOISE, F_BP), .cut = 1150.0f, .res = 0.35f, .att = 0.001f, .dec = 0.14f, .rel = 0.05f, .verb = 0.7f,
         .gain = 0.55f},                                                                                    /* CLP */
        {S_(O_SINE, F_LP), .hz = 44.0f, .cut = 600.0f, .att = 0.002f, .dec = 0.9f, .rel = 0.3f, .penv = 12.0f,
         .ptime = 0.35f, .drive = 2.0f, .verb = 0.2f, .gain = 1.0f},                                        /* DRP */
    },
    {   /* Ritual kit: skin, wood and breath */
        {S_(O_SINE, F_LP), .hz = 62.0f, .noise = 0.12f, .cut = 500.0f, .att = 0.001f, .dec = 0.45f, .rel = 0.1f,
         .penv = 9.0f, .ptime = 0.1f, .drive = 1.4f, .verb = 0.2f, .gain = 1.0f},                          /* FRM */
        {S_(O_TRI, F_BP), .hz = 240.0f, .cut = 480.0f, .res = 0.5f, .att = 0.001f, .dec = 0.3f, .rel = 0.1f,
         .penv = 3.0f, .ptime = 0.05f, .verb = 0.5f, .gain = 0.8f},                                         /* LOG */
        {S_(O_SINE, F_BP), .hz = 1150.0f, .cut = 1200.0f, .res = 0.5f, .att = 0.0005f, .dec = 0.05f, .rel = 0.02f,
         .verb = 0.4f, .echo = 0.3f, .gain = 0.5f},                                                         /* BLK */
        {S_(O_NOISE, F_HP), .cut = 5500.0f, .att = 0.012f, .dec = 0.07f, .rel = 0.03f, .verb = 0.3f, .gain = 0.3f},     /* SHK */
        {S_(O_SINE, F_BP), .hz = 2500.0f, .cut = 2500.0f, .res = 0.6f, .att = 0.0005f, .dec = 0.04f, .rel = 0.02f,
         .verb = 0.5f, .gain = 0.4f},                                                                       /* CLV */
        {S_(O_SINE, F_LP), .hz = 74.0f, .noise = 0.06f, .cut = 400.0f, .att = 0.001f, .dec = 0.7f, .rel = 0.2f,
         .penv = 4.0f, .ptime = 0.2f, .verb = 0.6f, .gain = 0.85f},                                         /* TOM */
        {S_(O_NOISE, F_BP), .cut = 900.0f, .res = 0.2f, .att = 0.08f, .dec = 0.4f, .rel = 0.2f, .verb = 0.7f, .gain = 0.4f},   /* BRE */
        {S_(O_FM, F_LP), .hz = 98.0f, .ratio = 1.41f, .idx = 3.0f, .itime = 1.2f, .cut = 2000.0f, .att = 0.003f,
         .dec = 4.0f, .rel = 2.0f, .verb = 0.9f, .gain = 0.55f},                                            /* GNG */
    },
    {   /* Glitch kit: bugs' clicks and bursts */
        {S_(O_SINE, F_LP), .hz = 48.0f, .noise = 0.05f, .cut = 1200.0f, .att = 0.001f, .dec = 0.25f, .rel = 0.05f,
         .penv = 24.0f, .ptime = 0.03f, .drive = 3.0f, .crush = 0.45f, .verb = 0.05f, .gain = 0.9f},        /* KIK */
        {S_(O_PULSE, F_HP), .hz = 3000.0f, .pw = 0.5f, .cut = 1000.0f, .att = 0.0003f, .dec = 0.008f, .rel = 0.005f,
         .gain = 0.35f},                                                                                    /* CLK */
        {S_(O_NOISE, F_BP), .cut = 1800.0f, .res = 0.3f, .att = 0.0005f, .dec = 0.09f, .rel = 0.03f, .crush = 0.8f,
         .verb = 0.3f, .gain = 0.5f},                                                                       /* BIT */
        {S_(O_FM, F_LP), .hz = 600.0f, .ratio = 2.5f, .idx = 4.0f, .itime = 0.04f, .cut = 6000.0f, .att = 0.0005f,
         .dec = 0.08f, .rel = 0.03f, .penv = -24.0f, .ptime = 0.06f, .echo = 0.6f, .gain = 0.35f},          /* ZAP */
        {S_(O_NOISE, F_HP), .cut = 9000.0f, .att = 0.0005f, .dec = 0.03f, .rel = 0.01f, .crush = 0.4f, .gain = 0.3f},    /* NZE */
        {S_(O_SINE, F_LP), .hz = 1800.0f, .cut = 6000.0f, .att = 0.0005f, .dec = 0.04f, .rel = 0.01f, .penv = -12.0f,
         .ptime = 0.02f, .echo = 0.5f, .gain = 0.3f},                                                       /* BLP */
        {S_(O_NOISE, F_LP), .cut = 4000.0f, .att = 0.0005f, .dec = 0.012f, .rel = 0.005f, .crush = 0.9f, .gain = 0.5f}, /* CRK */
        {S_(O_SINE, F_LP), .hz = 41.0f, .cut = 300.0f, .att = 0.003f, .dec = 1.2f, .rel = 0.3f, .penv = 5.0f,
         .ptime = 0.2f, .drive = 2.2f, .gain = 1.0f},                                                       /* SUB */
    },
};
#undef S_
static const char *const DRUM_NAME[3][EN_NDRUM] = {
    {"KIK", "TIK", "SNR", "TOM", "MTL", "HAT", "CLP", "DRP"},
    {"FRM", "LOG", "BLK", "SHK", "CLV", "TOM", "BRE", "GNG"},
    {"KIK", "CLK", "BIT", "ZAP", "NZE", "BLP", "CRK", "SUB"},
};
int en_sound_is_kit(int s) { return s >= 0 && s < 3; }
static int drum_of(int note) { return (note + 4) % EN_NDRUM; }   /* C2 (36) the first: 36..43 the eight */
const char *en_drum_name(int sound, int note)
{
    return DRUM_NAME[en_sound_is_kit(sound) ? sound : 0][drum_of(note < 0 ? 0 : note)];
}
static const snd_t *sound_of(int s, int note)
{
    if (en_sound_is_kit(s))
        return &KITS[s][drum_of(note)];
    return &MEL[(unsigned)(s - 3) < EN_NSOUND - 3 ? s - 3 : 0];
}

/* ---------------------------------------------------------- the mix --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], rsend[BLK], esend[BLK];
#ifdef EN_HOST
static float busb[EN_NBUS][BLK];
static int bus_on;
#define BUS(b, j, x) do { if (bus_on) busb[b][j] += (x); } while (0)
#else
#define BUS(b, j, x) do { } while (0)
#endif

/* ----------------------------------------------------------- the tracks --- */
typedef struct {
    float gl, gr, tone, decay, vol, send_r, send_e, drive, crush;   /* from its parameters, smoothed where it matters */
    float sweep_ph, motion;                     /* Motion: a slow sweep of the filter (eight bars) */
    float pulse, pump;                          /* Pulse: the tremolo's depth, its gain now */
    float tf, tf0;                              /* level x pulse x duck: this chunk's, and the last (a ramp between) */
    float duck;                                 /* the kick's duck, 1 none */
    float peak;
    int8_t drift[EN_NROW];                      /* Evolve: each row's drift, in degrees of the scale */
    uint8_t last, group, midi_note;             /* its last note; the note group playing; its MIDI note sounding */
    int8_t glide_from;
} track_t;
static track_t tk[EN_NTRK];

/* ----------------------------------------------------------- the voices --- */
#define NV 16
typedef struct {
    const snd_t *s;
    float ph, ph2, ph3, sph, mph, inc, inc_t, pk, idx, idec;
    float env, ak, dk, rk, sus;
    float fe, fek, cut0, tone;                  /* the filter: its envelope (octaves), its base (Hz), the note's tone */
    float ic1, ic2, g0, a0;                     /* the SVF's state, and last chunk's coefficients */
    float bendk;                                /* Bend: the pitch's fall a sample */
    float vel, crush, chold, cph, send_e, send_r;
    int32_t wait, gate;                         /* samples before it starts; until it lets go (-1 held) */
    uint8_t on, trk, note, group, stage, started;
} voice_t;
static voice_t vc[NV];
static uint32_t vc_age[NV], vc_clock;

static void voice_release(voice_t *v)
{
    if (v->on && v->stage < 3)
        v->stage = 3;
}

static int voice_alloc(void)                    /* a free voice; else the oldest letting go; else the oldest */
{
    int k, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (k = 0; k < NV; k++)
        if (!vc[k].on)
            return k;
    for (k = 0; k < NV; k++)
        if (vc[k].stage == 3 && vc_age[k] < oldest)
            oldest = vc_age[k], best = k;
    if (best >= 0)
        return best;
    for (k = 0; k < NV; k++)
        if (vc_age[k] < oldest)
            oldest = vc_age[k], best = k;
    return best < 0 ? 0 : best;
}

typedef struct {
    float vel, tone, glide, bend, crush, echo, swell;   /* the cell's effect, as a voice takes it */
    int32_t gate;                               /* samples held; -1 until the next note */
} hit_t;

/* a note on track t: wait samples from now */
static void voice_start(int t, int nn, int32_t wait, const hit_t *h)
{
    int k = voice_alloc(), snd = par[P_T(t, TP_SOUND)];
    voice_t *v = &vc[k];
    const snd_t *s = sound_of(snd, nn);
    track_t *T = &tk[t];
    float f, dec = T->decay, att;
    if (en_sound_is_kit(snd))
        f = s->hz * fm_exp2f((float)par[P_T(t, TP_TONE)] * (1.0f / 100.0f));   /* Tone tunes a kit, half an octave */
    else
        f = mtof((float)(nn + s->reg));
    f = fm_clampf(f, 8.0f, 16000.0f);
    v->s = s;
    v->trk = (uint8_t)t;
    v->note = (uint8_t)nn;
    v->group = T->group;
    v->inc_t = f * (1.0f / EN_SR);
    if (h->glide > 0.0f && T->glide_from >= 0 && !en_sound_is_kit(snd)) {
        v->inc = mtof((float)(T->glide_from + s->reg)) * (1.0f / EN_SR);
        v->pk = coef(h->glide * 0.5f);
    } else {
        v->inc = s->penv != 0.0f ? v->inc_t * fm_exp2f(s->penv * (1.0f / 12.0f)) : v->inc_t;
        v->pk = s->ptime > 0.0f ? coef(s->ptime) : 0.0f;
    }
    v->bendk = h->bend > 0.0f ? fm_exp2f(-h->bend / (12.0f * fm_maxf((float)h->gate, 2000.0f))) : 1.0f;
    v->ph = 0.0f, v->ph2 = 0.31f, v->ph3 = 0.67f, v->sph = 0.0f, v->mph = 0.0f;
    v->idx = s->idx;
    v->idec = s->itime > 0.0f ? coef(s->itime * dec) : 1.0f;
    v->env = 0.0f;
    v->stage = 0;
    att = s->att + h->swell;
    v->ak = 1.0f - fm_expf(-1.0f / (fm_maxf(att, 0.0003f) * 0.35f * EN_SR));
    v->dk = coef(s->dec * dec);
    v->rk = coef(fm_maxf(s->rel * dec, 0.003f));
    v->sus = s->sus;
    v->fe = s->fenv;
    v->fek = coef(fm_maxf(s->ftime, 0.005f) * dec);
    v->cut0 = s->cut;
    v->tone = h->tone;
    v->ic1 = v->ic2 = 0.0f;
    v->g0 = -1.0f;
    v->vel = h->vel * s->gain;
    v->crush = fm_clampf(s->crush + h->crush + T->crush, 0.0f, 0.95f);
    v->chold = 0.0f;
    v->cph = 1.0f;
    v->send_e = s->echo + h->echo;
    v->send_r = s->verb;
    v->wait = wait;
    v->gate = h->gate;
    v->started = 0;
    v->on = 1;
    vc_age[k] = ++vc_clock;
}

/* the voice's first sample: an earlier note on its track lets go (a kit's drums ring on) */
static void voice_begins(voice_t *v)
{
    int k;
    v->started = 1;
    if (en_sound_is_kit(par[P_T(v->trk, TP_SOUND)]))
        return;
    for (k = 0; k < NV; k++) {
        voice_t *o = &vc[k];
        if (o != v && o->on && o->started && o->trk == v->trk && o->group != v->group)
            voice_release(o);
    }
}

static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float saw(float t, float dt) { return 2.0f * t - 1.0f - blep(t, dt); }
static inline float wrap1(float t) { return t >= 1.0f ? t - (float)(int)t : t; }

static void render_voices(uint32_t n)
{
    int k;
    for (k = 0; k < NV; k++) {
        voice_t *v = &vc[k];
        const snd_t *s = v->s;
        track_t *T;
        uint32_t j = 0;
        float ph, ph2, ph3, sph, mph, inc, idx, env, fe, ic1, ic2, chold, cph, cut, g1, a1, dg, da, kres, gl, gr, sr, se, amp;
        float pk1, det, peak = 0.0f;
        int b;
        if (!v->on)
            continue;
        if (v->wait > 0) {
            if ((uint32_t)v->wait >= n) {
                v->wait -= (int32_t)n;
                continue;
            }
            j = (uint32_t)v->wait;
            v->wait = 0;
        }
        if (!v->started)
            voice_begins(v);
        T = &tk[v->trk];
        b = v->trk < 2 ? 0 : v->trk - 1;
        (void)b;                                 /* the buses: the host's only */
        /* the filter's cutoff this chunk: its base, the track's Tone and Motion, the note's tone, its envelope */
        cut = v->cut0 * T->tone * v->tone * fm_exp2f(v->fe + T->motion);
        if (s->fmode != F_HP)                    /* the pitch's keytracking, a little (half) */
            cut *= fm_clampf(fm_sqrtf(v->inc_t * EN_SR * (1.0f / 220.0f)), 0.35f, 4.0f);
        cut = fm_clampf(cut, 25.0f, 16000.0f);
        g1 = fm_tanf(FM_PI * cut * (1.0f / EN_SR));
        kres = 2.0f - 1.96f * s->res;
        a1 = 1.0f / (1.0f + g1 * (g1 + kres));
        if (v->g0 < 0.0f)
            v->g0 = g1, v->a0 = a1;
        dg = (g1 - v->g0) / (float)n;
        da = (a1 - v->a0) / (float)n;
        amp = v->vel;
        gl = T->gl * amp, gr = T->gr * amp;
        sr = v->send_r * T->send_r * amp;
        se = (v->send_e + 0.0f) * T->send_e * amp;
        ph = v->ph, ph2 = v->ph2, ph3 = v->ph3, sph = v->sph, mph = v->mph, inc = v->inc, idx = v->idx, env = v->env;
        fe = v->fe, ic1 = v->ic1, ic2 = v->ic2, chold = v->chold, cph = v->cph;
        pk1 = 1.0f - v->pk;
        det = fm_exp2f(s->det * (1.0f / 1200.0f));
        {
            float g = v->g0 + dg * (float)j, a = v->a0 + da * (float)j;
            const float drive = s->drive * T->drive, dk = v->dk, ak = v->ak, rk = v->rk, sus = v->sus, crush = v->crush;
            const float crate = 1.0f - crush * 0.92f, cq = crush > 0.0f ? 4.0f + (1.0f - crush) * 120.0f : 0.0f;
            const float dtf = (T->tf - T->tf0) / (float)n;
            float tf = T->tf0 + dtf * (float)j;
            int32_t gate = v->gate;
            uint8_t stage = v->stage;
            for (; j < n; j++) {
                float x, hp, v1, v2;
                inc += (v->inc_t - inc) * pk1;
                switch (s->osc) {
                case O_SINE: x = sine(ph); break;
                case O_TRI: x = 4.0f * fm_fabsf(ph - 0.5f) - 1.0f; break;
                case O_SAW: x = saw(ph, inc); break;
                case O_PULSE: {
                    float t2 = ph + 1.0f - s->pw;
                    t2 = wrap1(t2);
                    x = (ph < s->pw ? 1.0f : -1.0f) + blep(ph, inc) - blep(t2, inc);
                    break;
                }
                case O_SUPER:
                    x = 0.45f * (saw(ph, inc) + saw(ph2, inc * det) + saw(ph3, inc / det));
                    ph2 = wrap1(ph2 + inc * det);
                    ph3 = wrap1(ph3 + inc / det);
                    break;
                case O_FM:
                    x = sine(ph + 4.0f + idx * sine(mph));
                    mph = wrap1(mph + inc * s->ratio);
                    idx *= v->idec;
                    break;
                default: x = wnoise(&noise_s); break;
                }
                if (s->osc == O_PULSE && s->det > 0.0f) {   /* a second pulse a few cents up */
                    float t1 = ph2, t2 = wrap1(ph2 + 1.0f - s->pw);
                    x = 0.6f * (x + (t1 < s->pw ? 1.0f : -1.0f) + blep(t1, inc * det) - blep(t2, inc * det));
                    ph2 = wrap1(ph2 + inc * det);
                }
                if (s->sub > 0.0f) {
                    x += s->sub * sine(sph);
                    sph = wrap1(sph + inc * 0.5f);
                }
                if (s->noise > 0.0f)
                    x += s->noise * wnoise(&noise_t);
                ph = wrap1(ph + inc);
                /* the amplitude */
                if (stage == 0) {
                    env += (1.15f - env) * ak;
                    if (env >= 1.0f)
                        env = 1.0f, stage = 1;
                } else if (stage == 1) {
                    env = sus + (env - sus) * dk;
                } else {
                    env *= rk;
                }
                if (gate > 0 && --gate == 0)
                    stage = 3;
                /* drive, the filter (TPT SVF), the crush */
                if (drive > 1.0f) {
                    x *= drive;
                    x = x / (1.0f + fm_fabsf(x)) * 1.4f;
                }
                v1 = (ic1 + g * (x - ic2)) * a;
                v2 = ic2 + g * v1;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                g += dg, a += da;
                hp = x - kres * v1 - v2;
                x = s->fmode == F_LP ? v2 : s->fmode == F_BP ? v1 * kres : hp;
                if (crush > 0.0f) {
                    cph += crate * crate;
                    if (cph >= 1.0f) {
                        cph -= 1.0f;
                        chold = (float)(int)(x * cq) / cq;
                    }
                    x = chold;
                }
                x *= env * tf;
                tf += dtf;
                {
                    float ax = fm_fabsf(x);
                    if (ax > peak)
                        peak = ax;
                }
                mixl[j] += x * gl;
                mixr[j] += x * gr;
                rsend[j] += x * sr;
                esend[j] += x * se;
                BUS(b, j, x * (gl + gr) * 0.7f);
            }
            v->gate = gate;
            v->stage = stage;
        }
        v->inc_t *= v->bendk == 1.0f ? 1.0f : fm_powf(v->bendk, (float)n);
        v->ph = ph, v->ph2 = ph2, v->ph3 = ph3, v->sph = sph, v->mph = mph, v->inc = inc, v->idx = idx;
        v->env = env, v->ic1 = fm_flush(ic1), v->ic2 = fm_flush(ic2), v->chold = chold, v->cph = cph;
        v->g0 = g1, v->a0 = a1;
        /* the filter's envelope falls by the chunk */
        v->fe = fe * fm_powf(v->fek, (float)n);
        if (peak * amp > T->peak)
            T->peak = peak * amp;
        if ((v->stage == 3 || (v->stage == 1 && v->sus <= 0.0f)) && env < 0.0002f)
            v->on = 0;
    }
}

/* ------------------------------------------------------------ the echo --- */
/* ping-pong: one line at half the rate, the input at its start, the left tap at T and the right at 2T, the
 * right fed back into the start through a dark low pass. Up to a second a side. */
#define ECHO_N 44100
static int16_t echo_buf[ECHO_N] EN_POOL;
static int echo_w, echo_odd;
static float echo_d = 8000.0f, echo_d_t = 8000.0f, echo_fb, echo_lp, echo_hp, echo_in;
static float echo_yl, echo_yr, echo_ol, echo_or;
static inline float echo_tap(float d)
{
    float rp = (float)echo_w - d, y0, y1, f;
    int i0;
    if (rp < 0.0f)
        rp += (float)ECHO_N;
    i0 = (int)rp;
    f = rp - (float)i0;
    y0 = (float)echo_buf[i0] * (1.0f / 32768.0f);
    y1 = (float)echo_buf[i0 + 1 < ECHO_N ? i0 + 1 : 0] * (1.0f / 32768.0f);
    return y0 + (y1 - y0) * f;
}
static inline void echo_step(float in, float *l, float *r)
{
    if (echo_odd ^= 1) {                         /* every other sample: one in, two out */
        float w;
        echo_d += (echo_d_t - echo_d) * 0.0005f;
        echo_yl = echo_tap(echo_d);
        echo_yr = echo_tap(2.0f * echo_d);
        echo_lp += 0.3f * (echo_yr - echo_lp);   /* dark repeats */
        echo_hp += 0.004f * (echo_lp - echo_hp);
        w = fm_clampf((echo_in + in) * 0.5f + echo_fb * (echo_lp - echo_hp), -0.999f, 0.999f);
        echo_buf[echo_w] = (int16_t)(w * 32767.0f);
        if (++echo_w >= ECHO_N)
            echo_w = 0;
    } else {
        echo_in = in;
    }
    echo_ol += 0.5f * (echo_yl - echo_ol);
    echo_or += 0.5f * (echo_yr - echo_or);
    *l = echo_ol;
    *r = echo_or;
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in AMB-1, after FoMni); darker here: the album's hall */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 1323                                   /* 30 ms */
#define EXC DS(16)
#define EXC_MAX (2 * EXC)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC_MAX + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC_MAX + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC_MAX + 2))
static float pl_buf[DL_TOTAL] EN_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.7f, pl_decay_t = 0.7f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.45f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.3f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, 0.5f);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.3f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, 0.5f);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------------------ parameters --- */
static float tape_hiss, tape_lp = 1.0f, duck_amt, duck_env;
static float tape_l, tape_r;
static float echo_beats(void)
{
    static const float B[6] = {0.25f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f};
    return B[(unsigned)par[P_ECHO_T] < 6u ? par[P_ECHO_T] : 2];
}
static void echo_time(void)
{
    float d = echo_beats() * 60.0f / fm_maxf(en_bpm, 30.0f) * (EN_SR * 0.5f);
    echo_d_t = fm_clampf(d, 200.0f, (float)(ECHO_N / 2 - 4));
}

static void track_apply(int t)
{
    track_t *T = &tk[t];
    float pn = (float)par[P_T(t, TP_PAN)] * 0.02f, g;
    pan_gains(pn, &T->gl, &T->gr);
    T->tone = fm_exp2f((float)par[P_T(t, TP_TONE)] * (1.0f / 25.0f));
    T->decay = pct(par[P_T(t, TP_DECAY)]);
    T->vol = lvl(par[P_T(t, TP_LEVEL)]) * 1.3f;
    T->send_r = pct(par[P_T(t, TP_VERB)]) * 1.4f;
    T->send_e = pct(par[P_T(t, TP_ECHO)]) * 1.4f;
    g = pct(par[P_T(t, TP_GRIT)]);
    T->drive = 1.0f + g * 3.0f;
    T->crush = g > 0.5f ? (g - 0.5f) * 1.0f : 0.0f;
    T->pulse = pct(par[P_T(t, TP_PULSE)]);
    if (!par[P_T(t, TP_EVOLVE)]) {               /* Evolve off: the notes as written, at once */
        int r;
        for (r = 0; r < EN_NROW; r++)
            T->drift[r] = 0;
    }
}

static void apply(int p, int v)
{
    const en_param_t *pi;
    if (p < 0 || p >= P_NPARAMS)
        return;
    pi = en_param_info(p);
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;                         /* the render's copy (the app keeps en_song.par) */
    if (p >= P_TRK && p < P_NSONG) {
        track_apply((p - P_TRK) / EN_TP);
        return;
    }
    switch (p) {
    case P_ROOT: case P_MODE:
        pool = EN_MODE_POOL[(unsigned)par[P_MODE] < EN_NMODES ? par[P_MODE] : 0];
        break;
    case P_ECHO_T: case P_TEMPO: echo_time(); break;
    case P_ECHO_FB: echo_fb = pct(v) * 1.05f; break;
    case P_VERB: pl_decay_t = 0.45f + pct(v) * 0.47f; break;
    case P_TAPE:
        tape_hiss = pct(v) * pct(v) * 0.012f;
        tape_lp = onepole_k(18000.0f - pct(v) * 12500.0f);
        break;
    case P_DUCK: duck_amt = pct(v) * 0.7f; break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    default: break;
    }
}

/* ------------------------------------------------------------ the tracker --- */
static int seq_pos, seq_row, jump_to = -1;
static float row_frac, beat_ph, clk_acc;
static int32_t duck_at = -1;
static int8_t duck_trk = -1;
/* MIDI clock in */
static float ext_steps, tick_gap;
static uint32_t since_tick, ext_age;

static uint32_t song_pool(void) { return EN_MODE_POOL[(unsigned)par[P_MODE] < EN_NMODES ? par[P_MODE] : 0]; }

/* a note moved d degrees through the scale (snapped into it first) */
static int deg_move(int note, int root, uint32_t p, int d)
{
    int k;
    note = en_snap(note, root, p);
    for (k = 0; k < (d < 0 ? -d : d) && k < 40; k++) {
        int s = 0;
        do {
            note += d < 0 ? -1 : 1;
        } while (!(p >> (((note - root) % 12 + 12) % 12) & 1u) && ++s < 12);
    }
    return note < NOTE_LO ? NOTE_LO : note > NOTE_HI ? NOTE_HI : note;
}
static int deg_shift(int note, int d) { return deg_move(note, par[P_ROOT], song_pool(), d); }

static void midi_note(int t, int on, int n, int vel)
{
    if (muted || !(par[P_MIDI] == EN_MIDI_NOTES || par[P_MIDI] == EN_MIDI_BOTH))
        return;
    en_midi_out((on ? 0x90u : 0x80u) | (uint32_t)((par[P_MCH] + t) & 15), (uint32_t)n & 127u, on ? (uint32_t)vel : 0u);
}
static void midi_rt(uint32_t st)
{
    if (!muted && (par[P_MIDI] == EN_MIDI_CLOCK || par[P_MIDI] == EN_MIDI_BOTH))
        en_midi_out(st, 0, 0);
}
static void track_midi_off(int t)
{
    if (tk[t].midi_note) {
        midi_note(t, 0, tk[t].midi_note, 0);
        tk[t].midi_note = 0;
    }
}

static void track_release(int t, int32_t at)    /* its notes let go, at samples from now */
{
    int k;
    for (k = 0; k < NV; k++) {
        voice_t *v = &vc[k];
        if (!v->on || v->trk != t || v->stage == 3)
            continue;
        if (v->wait > 0)                         /* not begun: never will */
            v->on = 0;
        else if (at <= 0)
            v->stage = 3;
        else
            v->gate = at;
    }
    track_midi_off(t);
}

static int track_holding(int t, int note)        /* a pad or drone already holding this note */
{
    int k;
    for (k = 0; k < NV; k++)
        if (vc[k].on && vc[k].trk == t && vc[k].note == note && vc[k].stage < 3 && vc[k].gate < 0)
            return 1;
    return 0;
}

static float row_samples(void) { return 15.0f / fm_maxf(en_bpm, 20.0f) * EN_SR; }

/* a chord's shape, in degrees of the scale: fifth, triad, sus4, seventh, add9, octave, fourths, triad + octave */
static const int8_t CHORD[8][4] = {{0, 4, 99, 99}, {0, 2, 4, 99}, {0, 3, 4, 99}, {0, 2, 4, 6},
                                   {0, 4, 8, 99}, {0, 7, 99, 99}, {0, 3, 6, 99}, {0, 2, 4, 7}};

static void play_cell(int t, const en_cell_t *c, int32_t at, int row)
{
    track_t *T = &tk[t];
    const int snd = par[P_T(t, TP_SOUND)], kit = en_sound_is_kit(snd), fx = c->fx >> 4, v = c->fx & 15;
    const float rl = row_samples();
    int note = c->note, k;
    hit_t h;
    if (fx == FX_JUMP)
        jump_to = v;
    if (note == NOTE_EMPTY) {                    /* Evolve's ghosts: a drum where the kit's drift has grown one */
        if (kit && T->drift[row] >= 2 && T->last >= NOTE_LO && frand() < 0.5f) {
            note = T->last;
            h.vel = 0.35f;
        } else {
            return;
        }
    } else if (note == NOTE_OFF) {
        track_release(t, at);
        return;
    } else {
        h.vel = 0.8f;
    }
    if (fx == FX_CHANCE && frand() >= (float)(v + 1) * (1.0f / 16.0f))
        return;
    if (fx == FX_DELAY)
        at += (int32_t)(rl * (float)v * (1.0f / 16.0f));
    if (!kit) {
        if (fx == FX_MUTATE && v)
            note = deg_shift(note, irand(2 * v + 1) - v);
        if (T->drift[row]) {
            note = deg_shift(note, T->drift[row]);
            en_evolved++;
        }
    } else if (T->drift[row] < 0 && c->note >= NOTE_LO && frand() < 0.5f) {
        return;                                  /* Evolve drops a drum */
    }
    h.tone = 1.0f, h.glide = 0.0f, h.bend = 0.0f, h.crush = 0.0f, h.echo = 0.0f, h.swell = 0.0f, h.gate = -1;
    switch (fx) {
    case FX_GLIDE: h.glide = 0.02f + 0.03f * (float)v; break;
    case FX_TONE: h.tone = fm_exp2f((float)(v - 7) * 0.5f); break;
    case FX_VOLUME: h.vel = (float)v * (1.0f / 15.0f); break;
    case FX_BEND: h.bend = v ? (float)v : 12.0f; break;
    case FX_ECHO: h.echo = 0.4f + (float)v * 0.2f; break;
    case FX_CRUSH: h.crush = (float)v * (0.9f / 15.0f); break;
    case FX_SWELL: h.swell = (float)(v + 1) * 0.25f * rl * (1.0f / EN_SR); break;
    case FX_LENGTH: h.gate = (int32_t)fm_maxf(rl * (float)(v + 1) * (1.0f / 16.0f), 64.0f); break;
    default: break;
    }
    /* a pad or a drone given the note it holds: it holds on (no breath in again) */
    if (!kit && fx == FX_NONE && sound_of(snd, note)->att >= 0.1f && track_holding(t, note)) {
        en_trk_glow[t] = fm_maxf(en_trk_glow[t], 0.5f);
        return;
    }
    track_midi_off(t);
    T->group++;
    if (fx == FX_CHORD && !kit) {
        for (k = 0; k < 4; k++) {
            int d = CHORD[v & 7][k];
            if (d == 99)
                break;
            if (v >= 8 && k > 0)
                d += 7;
            if (k)
                h.vel *= 0.8f;
            voice_start(t, deg_shift(note, d), at, &h);
        }
    } else if (fx == FX_RATCHET || fx == FX_STUTTER) {
        int cnt;
        float gap;
        if (fx == FX_RATCHET) {
            cnt = v < 1 ? 2 : v + 1;
            gap = rl / (float)cnt;
        } else {                                 /* the album's glitches: 30-60 ms repeats, crushed, falling away */
            gap = (0.03f + 0.002f * (float)v) * EN_SR;
            cnt = (int)fm_clampf(rl * 1.5f / gap, 3.0f, 14.0f);
            h.crush = fm_maxf(h.crush, 0.45f);
        }
        h.gate = (int32_t)(gap * 0.7f);
        for (k = 0; k < cnt; k++) {
            voice_start(t, note, at + (int32_t)(gap * (float)k), &h);
            T->group++;
            h.vel *= fx == FX_STUTTER ? 0.82f : 0.94f;
        }
    } else {
        voice_start(t, note, at, &h);
    }
    if (kit && drum_of(note) == 0 && duck_amt > 0.0f) {   /* the kick ducks everything else */
        duck_at = at;
        duck_trk = (int8_t)t;
    }
    T->glide_from = (int8_t)note;
    T->last = (uint8_t)note;
    T->midi_note = (uint8_t)note;
    midi_note(t, 1, note, (int)(fm_clampf(h.vel, 0.05f, 1.0f) * 127.0f));
    en_trk_glow[t] = 1.0f;
    en_trk_note[t] = (uint8_t)note;
    en_notes++;
}

/* Evolve, after each pass: each row's drift walks a step now and then (in the scale, three at most), and
 * leans back home */
static void evolve_pass(void)
{
    int t, r;
    for (t = 0; t < EN_NTRK; t++) {
        float e = pct(par[P_T(t, TP_EVOLVE)]);
        int8_t *d = tk[t].drift;
        if (e <= 0.0f) {
            for (r = 0; r < EN_NROW; r++)
                d[r] = 0;
            continue;
        }
        for (r = 0; r < EN_NROW; r++) {
            if (frand() < e * 0.18f) {
                int s = d[r] + (frand() < 0.5f ? -1 : 1);
                d[r] = (int8_t)(s < -3 ? -3 : s > 3 ? 3 : s);
            } else if (d[r] && frand() < 0.12f) {
                d[r] = (int8_t)(d[r] > 0 ? d[r] - 1 : d[r] + 1);
            }
        }
    }
}

static int song_len(void) { return par[P_LEN] < 1 ? 1 : par[P_LEN] > EN_NORD ? EN_NORD : par[P_LEN]; }

static void row_fire(int32_t at)
{
    int t, pat;
    int32_t sw = 0;
    if (seq_pos >= song_len())
        seq_pos = 0;
    pat = en_song.order[seq_pos] % EN_NPAT;
    en_pos = (uint8_t)seq_pos;
    en_row = (uint8_t)seq_row;
    en_pat_now = (uint8_t)pat;
    if (seq_row & 1)                             /* the swing: the off sixteenths late */
        sw = (int32_t)(row_samples() * pct(par[P_SWING]) * 0.5f);
    for (t = 0; t < EN_NTRK; t++)
        if (!(tmute >> t & 1u))
            play_cell(t, &en_song.cell[pat][seq_row][t], at + sw, seq_row);
    en_rows++;
    if (jump_to >= 0) {                          /* J: on to an order position */
        seq_pos = jump_to < song_len() ? jump_to : 0;
        seq_row = 0;
        event(EV_JUMP, seq_pos);
        jump_to = -1;
        en_passes++;
        evolve_pass();
        return;
    }
    if (++seq_row >= EN_NROW) {
        seq_row = 0;
        en_passes++;
        evolve_pass();
        if (par[P_LOOP] != LOOP_PATTERN && ++seq_pos >= song_len()) {
            seq_pos = 0;
            event(EV_LOOP, 0);
        }
    }
}

/* ------------------------------------------------------------ transport --- */
static void all_release(void)
{
    int t;
    for (t = 0; t < EN_NTRK; t++)
        track_release(t, 0);
}
static void play_start(int pos, int row)
{
    seq_pos = pos < 0 ? 0 : pos >= song_len() ? 0 : pos;
    seq_row = row < 0 || row >= EN_NROW ? 0 : row;
    row_frac = 0.9999f;                          /* the first row now */
    clk_acc = 0.0f;
    jump_to = -1;
    if (!en_playing)
        midi_rt(seq_pos || seq_row ? 0xFBu : 0xFAu);
    en_playing = 1;
}
static void play_stop(void)
{
    if (en_playing)
        midi_rt(0xFCu);
    en_playing = 0;
    all_release();
}
static void quiet(void)
{
    int k, t;
    for (k = 0; k < NV; k++)
        vc[k].on = 0;
    for (t = 0; t < EN_NTRK; t++)
        track_midi_off(t);
}

static void control(uint32_t n)
{
    const float dt = (float)n * (1.0f / EN_SR);
    float dsteps = 0.0f, bpm0 = en_bpm;
    int t;
    if (par[P_SYNC] && en_ext) {
        dsteps = en_playing ? ext_steps : 0.0f;
        ext_steps = 0.0f;
    } else {
        en_bpm = (float)par[P_TEMPO];
        if (en_playing)
            dsteps = (float)n * en_bpm * (4.0f / 60.0f) * (1.0f / EN_SR);
    }
    if (en_bpm != bpm0)
        echo_time();
    if (duck_at >= 0) {                          /* the kick's duck, at its sample */
        if ((uint32_t)duck_at < n) {
            duck_env = 1.0f;
            duck_at = -1;
        } else {
            duck_at -= (int32_t)n;
        }
    }
    if (dsteps > 0.0f) {                         /* each row this chunk, at its sample */
        float rate = dsteps / (float)n, left = dsteps, at = 0.0f;
        while (row_frac + left >= 1.0f) {
            float need = 1.0f - row_frac;
            at += need / rate;
            left -= need;
            row_frac = 0.0f;
            row_fire((int32_t)fm_minf(at, (float)(n - 1)));
        }
        row_frac += left;
        if (!en_ext) {                           /* our clock out: six a row */
            clk_acc += dsteps * 6.0f;
            while (clk_acc >= 1.0f) {
                clk_acc -= 1.0f;
                midi_rt(0xF8u);
            }
        }
        beat_ph += dsteps * 0.25f;
        if (beat_ph >= 96.0f)
            beat_ph -= 96.0f;
    }
    {   /* each track's movement: the Pulse (locked to the beat), the Motion (eight bars), the duck */
        static const float DIV[5] = {0.25f, 0.5f, 0.75f, 1.0f / 3.0f, 1.0f};
        const float div = DIV[(unsigned)par[P_PULSE_T] < 5u ? par[P_PULSE_T] : 1];
        float ph = beat_ph / div, s;
        ph -= (float)(int)ph;
        if (ph < 0.08f) {                        /* a quick dip in, then back up over the rest */
            s = ph * (1.0f / 0.08f);
        } else {
            s = 1.0f - (ph - 0.08f) * (1.0f / 0.92f);
            s *= s;
        }
        duck_env *= fm_expf(-dt * fm_maxf(en_bpm, 30.0f) * (1.0f / 60.0f) * 11.0f);   /* gone in about an eighth */
        for (t = 0; t < EN_NTRK; t++) {
            track_t *T = &tk[t];
            float sw = beat_ph * (1.0f / 32.0f) + (float)t * 0.17f;
            T->pump = en_playing ? 1.0f - T->pulse * s : 1.0f;
            T->motion = pct(par[P_T(t, TP_MOTION)]) * 2.0f * fm_sin_turns(sw - (float)(int)sw);
            T->duck = t == duck_trk ? 1.0f : 1.0f - duck_amt * duck_env;
            T->tf0 = T->tf;
            T->tf = T->vol * T->pump * T->duck;
            en_trk_glow[t] *= 1.0f - fm_minf(dt * 5.0f, 1.0f);
            en_level[t] = fm_maxf(T->peak * 2.5f, en_level[t] * (1.0f - fm_minf(dt * 6.0f, 1.0f)));
            T->peak = 0.0f;
        }
    }
    pl_decay += (pl_decay_t - pl_decay) * fm_minf(dt * 2.0f, 1.0f);
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu), p;
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_PLAY: play_start(a, b); break;
        case C_STOP: play_stop(); break;
        case C_AUD_ON:
            if (a < EN_NTRK && b >= NOTE_LO && b <= NOTE_HI) {
                hit_t h = {0.8f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1};
                track_t *T = &tk[a];
                if (!en_sound_is_kit(par[P_T(a, TP_SOUND)]))
                    track_release(a, 0);
                T->group++;
                voice_start(a, b, 0, &h);
                T->glide_from = (int8_t)b;
                T->last = (uint8_t)b;
                track_midi_off(a);
                T->midi_note = (uint8_t)b;
                midi_note(a, 1, b, 100);
                en_trk_glow[a] = 1.0f;
                en_trk_note[a] = (uint8_t)b;
            }
            break;
        case C_AUD_OFF:
            if (a < EN_NTRK) {
                int k;
                for (k = 0; k < NV; k++)
                    if (vc[k].on && vc[k].trk == a && vc[k].note == b && vc[k].gate < 0)
                        voice_release(&vc[k]);
                if (tk[a].midi_note == b)
                    track_midi_off(a);
            }
            break;
        case C_RELOAD:
            for (p = 0; p < P_NSONG; p++)
                apply(p, en_song.par[p]);
            break;
        case C_SEED:
            rng ^= (uint32_t)(b & 0xFFFF) * 2654435761u;
            if (!rng)
                rng = 1u;
            break;
        case C_PANIC: quiet(); break;
        case C_MUTE:
            muted = (uint8_t)(a != 0);
            if (muted)
                quiet();
            break;
        case C_TMUTE: {
            int t;
            uint32_t newly = (uint32_t)a & ~tmute;
            tmute = (uint32_t)a;
            for (t = 0; t < EN_NTRK; t++)
                if (newly >> t & 1u)
                    track_release(t, 0);
            break;
        }
        case C_CLOCK:
            if (!par[P_SYNC])
                break;
            ext_age = 0;
            if (a == EN_CLK_TICK) {
                if (since_tick > 0u && since_tick < (uint32_t)EN_SR) {
                    tick_gap = tick_gap > 0.0f ? tick_gap + ((float)since_tick - tick_gap) * 0.02f : (float)since_tick;
                    en_bpm = EN_SR * 60.0f / (24.0f * tick_gap);
                }
                since_tick = 0;
                en_ext = 1;
                if (en_playing)
                    ext_steps += 1.0f / 6.0f;
            } else if (a == EN_CLK_START) {
                en_ext = 1;
                ext_steps = 0.0f;
                play_start(0, 0);
            } else if (a == EN_CLK_CONTINUE) {
                en_ext = 1;
                if (!en_playing)
                    play_start(seq_pos, seq_row);
            } else {
                play_stop();
            }
            break;
        default: break;
        }
    }
}

/* --------------------------------------------------------------- render --- */
static void render_chunk(int32_t *out, uint32_t n, float gain, uint32_t at)
{
    uint32_t j;
#ifdef EN_HOST
    int b;
#endif
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = rsend[j] = esend[j] = 0.0f;
#ifdef EN_HOST
    bus_on = en_bus_on && at + n <= 1024u;
    if (bus_on)
        for (b = 0; b < EN_NBUS; b++)
            for (j = 0; j < n; j++)
                busb[b][j] = 0.0f;
#else
    (void)at;
#endif
    render_voices(n);
    for (j = 0; j < n; j++) {
        float el, er, wl, wr, l, r;
        const float rduck = 1.0f - duck_amt * duck_env * 0.5f;
        echo_step(esend[j], &el, &er);
        reverb((rsend[j] + (el + er) * 0.2f) * 0.55f, &wl, &wr);
        mute_g += ((muted ? 0.0f : 1.0f) - mute_g) * 0.002f;
        l = mixl[j] + (el * 1.1f + wl * 1.25f) * rduck;
        r = mixr[j] + (er * 1.1f + wr * 1.25f) * rduck;
        if (tape_hiss > 0.0f) {
            l += wnoise(&noise_t) * tape_hiss;
            r += wnoise(&noise_t) * tape_hiss;
        }
        tape_l += tape_lp * (l - tape_l);
        tape_r += tape_lp * (r - tape_r);
        l = tape_l * 1.3f * gain * mute_g;
        r = tape_r * 1.3f * gain * mute_g;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
#ifdef EN_HOST
        if (bus_on)
            busb[5][j] = 0.6f * (wl + wr + el + er);
#endif
    }
    tape_l = fm_flush(tape_l), tape_r = fm_flush(tape_r);
#ifdef EN_HOST
    if (bus_on)
        for (j = 0; j < n; j++)
            for (b = 0; b < EN_NBUS; b++)
                en_bus[(at + j) * EN_NBUS + (uint32_t)b] = fm_tanhf(busb[b][j] * 1.3f * gain);
#endif
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    uint32_t at = 0;
    drain();
    if (en_ext && ++ext_age > 100u)              /* the clock stopped coming (~0.6 s): our own time */
        en_ext = 0;
    since_tick += n;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        control(k);
        render_chunk(out, k, gain, at);
        out += 2 * k;
        at += k;
        n -= k;
    }
}

void en_init(void)
{
    int i;
    for (i = 0; i <= SN; i++)
        sint[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SN));
    plate_init();
    for (i = 0; i < ECHO_N; i++)
        echo_buf[i] = 0;
    echo_w = 0;
    q_r = q_w = 0;
    for (i = 0; i < NV; i++)
        vc[i].on = 0;
    for (i = 0; i < EN_NTRK; i++) {
        int r;
        tk[i].glide_from = -1;
        tk[i].pump = tk[i].duck = 1.0f;
        tk[i].midi_note = 0;
        for (r = 0; r < EN_NROW; r++)
            tk[i].drift[r] = 0;
    }
    en_song_recipe(0, &en_song);
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, i < P_NSONG ? en_song.par[i] : PARAMS[i].def);
    for (i = 0; i < EN_NTRK; i++)
        tk[i].tf = tk[i].tf0 = tk[i].vol;
    pl_decay = pl_decay_t;
    echo_d = echo_d_t;
    en_playing = 0;
    seq_pos = seq_row = 0;
}

/* --------------------------------------------------------------- GROW --- */
/* writes one track of one pattern in one of the album's ways: the half-time pulse and its dotted bass, a
 * drone that changes once a bar if at all, the fourths-and-fifths ostinato (The Garden), the phrygian
 * semitone (Ritual, Deep Mutation), the 3:2 of its tempo pairs, a drift of far notes, bugs' swarm, and the
 * near-silence of Thanatosis. Pure (its own generator): the tests and the screen call it too. */
static uint32_t g_rng;
static uint32_t g_rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static int g_chance(int pct_) { return (int)(g_rnd() % 100u) < pct_; }
static int g_irand(int n) { return n > 0 ? (int)(g_rnd() % (uint32_t)n) : 0; }
static void g_put(en_song_t *s, int pat, int row, int t, int note, int fx, int v)
{
    en_cell_t *c = &s->cell[pat][row & 15][t];
    c->note = (uint8_t)note;
    c->fx = (uint8_t)(fx << 4 | (v & 15));
}

void en_grow(en_song_t *s, int pat, int t, int style, int density, int range, uint32_t seed)
{
    const int snd = s->par[P_T(t, TP_SOUND)], root = s->par[P_ROOT];
    const uint32_t p = EN_MODE_POOL[(unsigned)s->par[P_MODE] < EN_NMODES ? s->par[P_MODE] : 0];
    const int high = snd >= 13 && snd <= 17, sub = snd >= 3 && snd <= 6;
    const int base = (high ? 60 : sub ? 36 : 48) + root;
    const int rg = 1 + range * 6 / 100;
    int r, k;
#define GN(d) deg_move(base, root, p, (d))
    g_rng = seed * 2654435761u + 0x9E3779B9u + (uint32_t)(pat * 131 + t * 17 + style);
    if (!g_rng)
        g_rng = 1u;
    pat &= 15;
    for (r = 0; r < EN_NROW; r++)
        s->cell[pat][r][t].note = 0, s->cell[pat][r][t].fx = 0;
    if (en_sound_is_kit(snd)) {                  /* drums: 36 + the drum */
        static const uint16_t KICK[EN_NGROW] = {0x0401, 0x0001, 0x1111, 0x0481, 0x0841, 0x0201, 0x0909, 0x0001};
        static const uint16_t BACK[EN_NGROW] = {0x0100, 0x0000, 0x0000, 0x1000, 0x0000, 0x0100, 0x0404, 0x0000};
        for (r = 0; r < EN_NROW; r++) {
            if (KICK[style & 7] >> r & 1u)
                g_put(s, pat, r, t, 36, style == 6 && g_chance(30) ? FX_RATCHET : 0, 1);
            else if (BACK[style & 7] >> r & 1u)
                g_put(s, pat, r, t, 36 + (g_chance(50) ? 2 : 6), 0, 0);
            else if ((r & 3) == 2 && g_chance(density * 3 / 4))
                g_put(s, pat, r, t, 36 + 1, g_chance(25) ? FX_VOLUME : 0, 6 + g_irand(6));
            else if (style == 4 && r % 3 == 0 && g_chance(density))
                g_put(s, pat, r, t, 36 + 3, 0, 0);
            else if (style == 6 && g_chance(density / 2))
                g_put(s, pat, r, t, 36 + 1 + g_irand(7), g_chance(40) ? FX_STUTTER : FX_CHANCE, g_irand(16));
            else if (g_chance(density / 8))
                g_put(s, pat, r, t, 36 + (g_chance(50) ? 4 : 5), FX_CHANCE, 7 + g_irand(8));
        }
        if (style == 1 || style == 7)            /* the drone's and the silence's kits: a far metal now and then */
            g_put(s, pat, 8 + g_irand(8), t, 36 + 4, FX_CHANCE, 5);
        return;
    }
    switch (style & 7) {
    case 0: {                                    /* Pulse: roots and fifths on the dotted grid */
        static const uint8_t ROW[6] = {0, 3, 6, 8, 11, 14};
        for (k = 0; k < 6; k++)
            if (k == 0 || g_chance(density))
                g_put(s, pat, ROW[k], t, GN(k == 3 && rg > 1 ? 4 : g_chance(20) ? 7 : 0), 0, 0);
        break;
    }
    case 1:                                      /* Drone: a held note; perhaps a move at the half bar */
        g_put(s, pat, 0, t, GN(0), 0, 0);
        if (g_chance(density / 3))
            g_put(s, pat, 8, t, GN(g_chance(50) ? 3 : 4), 0, 0);
        break;
    case 2: {                                    /* Ostinato: a figure of fourths and fifths, round and round */
        int fig[4] = {0, 0, 0, 0}, len = 3 + g_irand(2), step = density > 70 ? 1 : 2;
        static const int8_t MOVE[4] = {0, 3, 4, -3};
        for (k = 1; k < len; k++) {
            fig[k] = MOVE[g_irand(4)] + (k > 1 && g_chance(30) ? fig[k - 1] : 0);
            if (fig[k] > rg + 3)
                fig[k] -= 7;
        }
        for (r = 0, k = 0; r < EN_NROW; r += step, k++)
            g_put(s, pat, r, t, GN(fig[k % len]), 0, 0);
        break;
    }
    case 3:                                      /* Phrygian: the semitone, gliding */
        g_put(s, pat, 0, t, GN(0), 0, 0);
        g_put(s, pat, 4, t, GN(1), FX_GLIDE, 4);
        if (g_chance(density))
            g_put(s, pat, 6, t, GN(0), FX_GLIDE, 2);
        g_put(s, pat, 10, t, GN(g_chance(50) ? 1 : -1), FX_GLIDE, 5);
        if (g_chance(density))
            g_put(s, pat, 13, t, GN(g_chance(50) ? 4 : 0), 0, 0);
        break;
    case 4:                                      /* Triplet: every three rows, against the bar */
        for (r = 0, k = 0; r < EN_NROW; r += 3, k++)
            if (r == 0 || g_chance(density))
                g_put(s, pat, r, t, GN((k * (rg > 2 ? 2 : 1)) % (rg + 3)), 0, 0);
        break;
    case 5:                                      /* Drift: a few far notes, swelling in */
        for (k = 0; k < 1 + density / 30; k++)
            g_put(s, pat, g_irand(16), t, GN(g_irand(2 * rg + 1) - rg + (g_chance(30) ? 7 : 0)),
                  g_chance(50) ? FX_SWELL : FX_TONE, 3 + g_irand(10));
        break;
    case 6:                                      /* Swarm: bugs, short and crushed */
        for (r = 0; r < EN_NROW; r++)
            if (g_chance(density * 2 / 3)) {
                static const uint8_t FXS[5] = {FX_RATCHET, FX_STUTTER, FX_CRUSH, FX_CHANCE, FX_BEND};
                g_put(s, pat, r, t, GN(g_irand(2 * rg + 1)), FXS[g_irand(5)], 4 + g_irand(12));
            }
        break;
    default:                                     /* Sparse: a note, maybe two, from nothing */
        g_put(s, pat, g_irand(4), t, GN(g_irand(rg + 1)), FX_SWELL, 6 + g_irand(10));
        if (g_chance(density))
            g_put(s, pat, 10 + g_irand(4), t, GN(-g_irand(rg + 1)), FX_LENGTH, 10);
        break;
    }
#undef GN
}

/* --------------------------------------------------------------- the songs --- */
/* five songs, each after a track of the album: its tempo and key as measured, its layers in and out over
 * eight patterns (an intro of drone, the bass, the drums, all of it, a variation, a dip, all again, an
 * outro), the order list playing each twice */
typedef struct {
    const char *name;
    uint8_t tempo, root, mode, echo_t, pulse_t, verb, tape, duck;
    uint8_t snd[EN_NTRK], style[EN_NTRK], dens[EN_NTRK], lvl[EN_NTRK], mask[EN_NTRK];
    int8_t deg[EN_NTRK], motion[EN_NTRK], pulse[EN_NTRK], evolve[EN_NTRK], pan[EN_NTRK], echo[EN_NTRK],
        verb_[EN_NTRK], grit[EN_NTRK], tone[EN_NTRK];
} recipe_t;
#define KIT_M 0x5C                               /* the drums: patterns 3, 4, 5, 7 (of 1-8) */
#define BAS_M 0x5E
#define ALL_M 0xFF
#define TOP_M 0xF8
static const recipe_t SONGS[EN_NSONG] = {
    {"Deep Mutation", 97, 2, MODE_PHRYGIAN, 2, 1, 70, 25, 35,
     {0, 4, 7, 15, 13, 18}, {0, 0, 1, 3, 2, 1}, {60, 45, 30, 60, 40, 0}, {62, 76, 52, 46, 38, 30},
     {KIT_M, BAS_M, ALL_M, TOP_M, 0xD8, ALL_M}, {0, 0, 1, 0, 0, 0}, {0, 10, 30, 15, 0, 20}, {0, 0, 45, 0, 0, 0},
     {20, 0, 0, 30, 35, 0}, {0, 0, -10, 15, -20, 0}, {10, 0, 15, 45, 40, 0}, {15, 5, 55, 40, 60, 30},
     {0, 10, 15, 10, 0, 30}, {0, 0, 0, 0, 0, 0}},
    {"Psychic Weather", 64, 2, MODE_AEOLIAN, 2, 2, 85, 30, 20,
     {0, 3, 11, 9, 17, 20}, {7, 1, 5, 1, 5, 1}, {40, 0, 60, 20, 50, 0}, {55, 58, 52, 50, 42, 32},
     {KIT_M, ALL_M, 0xFE, ALL_M, TOP_M, ALL_M}, {0, 0, 0, 2, 0, 0}, {0, 0, 70, 25, 20, 30}, {0, 0, 0, 50, 0, 0},
     {25, 0, 40, 0, 40, 0}, {0, 0, 20, -15, -25, 0}, {15, 0, 40, 20, 50, 0}, {20, 0, 70, 65, 75, 50},
     {0, 0, 0, 0, 0, 20}, {0, 0, 0, 0, 0, 0}},
    {"Ritual", 62, 2, MODE_PHRYGIAN, 4, 2, 80, 30, 25,
     {1, 10, 7, 23, 12, 21}, {4, 3, 1, 1, 7, 1}, {60, 50, 0, 0, 50, 0}, {60, 58, 50, 55, 42, 32},
     {KIT_M, BAS_M, ALL_M, 0xFE, TOP_M, ALL_M}, {0, 0, 1, 0, 0, 0}, {0, 40, 20, 0, 0, 40}, {0, 0, 40, 0, 0, 0},
     {25, 30, 0, 0, 40, 0}, {0, 0, -15, 0, 20, 0}, {0, 10, 0, 0, 30, 0}, {25, 30, 55, 20, 75, 50},
     {0, 20, 10, 30, 0, 0}, {0, 0, 0, 0, 0, 0}},
    {"The Garden", 125, 5, MODE_MAJOR, 2, 1, 65, 20, 40,
     {0, 5, 8, 14, 13, 18}, {2, 0, 1, 2, 2, 1}, {60, 55, 0, 85, 30, 0}, {64, 64, 46, 52, 36, 26},
     {KIT_M, BAS_M, ALL_M, 0xFE, TOP_M, ALL_M}, {0, 0, 0, 0, 4, 0}, {0, 0, 10, 0, 0, 0}, {0, 0, 60, 0, 0, 0},
     {0, 0, 0, 20, 30, 0}, {0, 0, 0, 15, -20, 0}, {0, 0, 10, 35, 45, 0}, {10, 5, 45, 40, 60, 30},
     {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}},
    {"bugs", 96, 2, MODE_AEOLIAN, 1, 0, 60, 35, 30,
     {2, 4, 20, 22, 19, 14}, {6, 0, 1, 6, 6, 5}, {55, 40, 0, 45, 40, 40}, {62, 56, 46, 46, 42, 50},
     {KIT_M, BAS_M, ALL_M, 0xDC, 0xDC, TOP_M}, {0, 0, 0, 0, 0, 0}, {0, 0, 30, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {30, 0, 0, 50, 40, 25}, {-15, 0, 25, 45, -45, -25}, {20, 0, 0, 50, 40, 40}, {20, 0, 55, 55, 55, 60},
     {20, 15, 30, 30, 40, 0}, {0, 0, 0, 0, 0, 0}},
};

const char *en_song_title(int i) { return SONGS[(unsigned)i < EN_NSONG ? i : 0].name; }

void en_song_recipe(int i, en_song_t *s)
{
    const recipe_t *R = &SONGS[(unsigned)i < EN_NSONG ? i : 0];
    static const uint8_t ORDER[16] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7};
    uint8_t *z = (uint8_t *)s;
    uint32_t k;
    int t, pt;
    for (k = 0; k < sizeof *s; k++)
        z[k] = 0;
    s->magic = EN_SONG_MAGIC;
    s->format = EN_SONG_FORMAT;
    for (k = 0; R->name[k] && k < sizeof s->name - 1; k++)
        s->name[k] = R->name[k];
    for (k = 0; k < P_NSONG; k++)
        s->par[k] = PARAMS[k].def;
    s->par[P_TEMPO] = R->tempo, s->par[P_ROOT] = R->root, s->par[P_MODE] = R->mode, s->par[P_LEN] = 16;
    s->par[P_ECHO_T] = R->echo_t, s->par[P_PULSE_T] = R->pulse_t, s->par[P_VERB] = R->verb, s->par[P_TAPE] = R->tape;
    s->par[P_DUCK] = R->duck;
    for (k = 0; k < 16; k++)
        s->order[k] = ORDER[k];
    for (t = 0; t < EN_NTRK; t++) {
        s->par[P_T(t, TP_SOUND)] = R->snd[t], s->par[P_T(t, TP_LEVEL)] = R->lvl[t];
        s->par[P_T(t, TP_MOTION)] = R->motion[t], s->par[P_T(t, TP_PULSE)] = R->pulse[t];
        s->par[P_T(t, TP_EVOLVE)] = R->evolve[t], s->par[P_T(t, TP_PAN)] = R->pan[t];
        s->par[P_T(t, TP_ECHO)] = R->echo[t], s->par[P_T(t, TP_VERB)] = R->verb_[t];
        s->par[P_T(t, TP_GRIT)] = R->grit[t], s->par[P_T(t, TP_TONE)] = R->tone[t];
        for (pt = 0; pt < 8; pt++) {
            int r;
            if (!(R->mask[t] >> pt & 1u))
                continue;
            en_grow(s, pt, t, R->style[t], R->dens[t], 40, (uint32_t)(i * 977 + t * 101 + (pt >= 4 && pt != 7 ? 31 : 0)));
            if (R->deg[t])                       /* the drone's degree (the Eb over D, the B under G) */
                for (r = 0; r < EN_NROW; r++) {
                    en_cell_t *c = &s->cell[pt][r][t];
                    if (c->note >= NOTE_LO)
                        c->note = (uint8_t)deg_move(c->note, R->root, EN_MODE_POOL[R->mode], R->deg[t]);
                }
        }
    }
    for (t = 0; t < EN_NTRK; t++)                /* the outro and the dip: what goes quiet lets go at their top */
        if (!en_sound_is_kit(R->snd[t])) {
            if (!(R->mask[t] >> 7 & 1u))
                s->cell[7][0][t].note = NOTE_OFF;
            if (!(R->mask[t] >> 5 & 1u) && (R->mask[t] >> 4 & 1u))
                s->cell[5][0][t].note = NOTE_OFF;
        }
}

int en_song_valid(const en_song_t *s)
{
    int i, p, r, t;
    if (s->magic != EN_SONG_MAGIC || s->format != EN_SONG_FORMAT)
        return 0;
    for (i = 0; i < P_NSONG; i++)
        if (s->par[i] < PARAMS[i].lo || s->par[i] > PARAMS[i].hi)
            return 0;
    for (i = 0; i < EN_NORD; i++)
        if (s->order[i] >= EN_NPAT)
            return 0;
    for (p = 0; p < EN_NPAT; p++)
        for (r = 0; r < EN_NROW; r++)
            for (t = 0; t < EN_NTRK; t++) {
                int n = s->cell[p][r][t].note;
                if (n != NOTE_EMPTY && n != NOTE_OFF && (n < NOTE_LO || n > NOTE_HI))
                    return 0;
            }
    if (s->name[sizeof s->name - 1])
        return 0;
    return 1;
}
