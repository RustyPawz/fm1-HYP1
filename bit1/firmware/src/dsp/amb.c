/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (amb.h): BIT-1, a generative 8-bit tracker. One compilation unit, -O2. The plate reverb, the
 * command queue and the fast maths are AMB-1's (Charles Vestal), by way of GRAV-1; the tracker is EVOL-1's; the
 * chip voices follow CTY-1's (an NES-style pulse and stepped triangle, Game Boy-style 4-bit waves, an LFSR noise
 * with a long and a metal mode), with SID-style saws and pulses through the voice's resonant filter. Chip voices
 * step their volume in sixteen levels; their arpeggios step at frame rate (60 Hz) or on the tempo. */
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
const char EN_FX_CHAR[EN_NFX + 1] = ".G?RDTVBHAMLXSWJ";
const char *const EN_FX_NAME[EN_NFX] = {"None", "Glide", "Chance", "Ratchet", "Delay", "Tone", "Vibrato", "Bend",
                                        "Hop", "Chord", "Mutate", "Length", "Crush", "Stutter", "Swell", "Jump"};
const char *const EN_SOUND_NAME[EN_NSOUND] = {
    "NES kit", "GB kit", "SID kit",
    "Tri bass", "Pulse bass", "SID bass", "Wave bass", "Octave bass",
    "Square", "Thin lead", "Duty lead", "SID lead", "Wave lead",
    "Chip arp", "Wave pad", "SID pad", "Organ", "Ring bell",
    "Blip", "Laser", "Power-up", "Pickup", "Metal", "Noise sweep"};
const char *const EN_GROW_NAME[EN_NGROW] = {"Driver", "Hold", "Ostinato", "Melody", "Triplet", "Arp run", "Bleeps", "Sparse",
                                            "Euclid", "Vary", "Rolls", "Accents", "Shift", "Up a step"};
const char *const EN_ARP_NAME[EN_NARP] = {"Off", "Triad", "Down", "Up-down", "Seventh", "Octave", "Power", "Trill"};
const char *const EN_ROLE_NAME[EN_NROLE] = {"Off", "Auto", "Bass", "Chords", "Arp", "Harmony", "Counter", "Drums"};
static const char *const THEME_NAME[EN_NTHEMES] = {"Arcade", "Pocket", "Home", "Neon", "Mono"};

/* ---------------------------------------------------------- parameters --- */
#define TRK(n, s, lv, ev) {"Sound", 0, EN_NSOUND - 1, s}, {"Tone", -50, 50, 0}, {"Decay", 10, 400, 100}, \
    {"Level", 0, 100, lv}, {"Vibrato", 0, 100, 0}, {"Arp", 0, EN_NARP - 1, 0}, {"Evolve", 0, 100, ev}, \
    {"Pan", -50, 50, 0}, {"Echo", 0, 100, 0}, {"Reverb", 0, 100, 15}, {"Crush", 0, 100, 0}, \
    {"Role", 0, EN_NROLE - 1, 0}
static const en_param_t PARAMS[P_NPARAMS] = {
    {"Tempo", 40, 240, 128},  {"Swing", 0, 75, 0},     {"Root", 0, 11, 0},       {"Mode", 0, EN_NMODES - 1, MODE_MAJOR},
    {"Length", 1, EN_NORD, 8}, {"Loop", 0, 1, 0},      {"Echo time", 0, 5, 2},   {"Feedback", 0, 95, 30},
    {"Reverb", 0, 100, 35},   {"Lo-fi", 0, 100, 15},   {"Duck", 0, 100, 0},      {"Arp rate", 0, 4, 1},
    {"Lead", 0, EN_NTRK, 0},  {"Density", 0, 100, 50}, {"Take", 0, 99, 0},
    TRK(1, 0, 70, 0), TRK(2, 3, 80, 0), TRK(3, 14, 45, 0), TRK(4, 8, 60, 0), TRK(5, 10, 45, 0), TRK(6, 18, 30, 0),
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
    static const char *const ARPT_NAME[5] = {"1 frame", "2 frames", "3 frames", "1/32", "1/16"};
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
    else if (i == P_ARP_T)
        cpy(b, ARPT_NAME[(unsigned)v < 5u ? v : 0]);
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
    else if (k == TP_ARP)
        cpy(b, EN_ARP_NAME[(unsigned)v < EN_NARP ? v : 0]);
    else if (k == TP_FOLLOW)
        cpy(b, EN_ROLE_NAME[(unsigned)v < EN_NROLE ? v : 0]);
    else if (i == P_LEAD) {
        if (!v)
            cpy(b, "Off");
        else
            cpy(b, "Track "), b[6] = (char)('0' + v), b[7] = 0;
    } else if (i == P_TAKE) {
        b[0] = '#';
        itoa_s(v, b + 1);
    } else if (k == TP_PAN) {
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
en_song_t en_song EN_POOL;
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
volatile uint32_t en_notes, en_rows, en_passes, en_evolved, en_takes;
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
/* One voice for everything: an oscillator (sine, triangle, saw, pulse, three detuned saws, two-op FM, white
 * noise; and the chip's: a stepped 4-bit triangle, a 4-bit wave of 32 steps, an LFSR noise), a sub an octave
 * down, white noise mixed in, a pitch that falls (or glides) to the note, a TPT state-variable filter (low,
 * band or high pass) with its own envelope, an ADSR, a drive and a crush. A chip voice (steps) quantises its
 * volume to sixteen levels, and its pulse's duty moves in steps (12.5, 25, 50%) with the track's Tone. Macros:
 * an arpeggio (a shape in the scale, stepping at the song's Arp rate), vibrato (coming in after 0.12 s), a sweep
 * (semitones a second, on and on), a pulse width that moves (PWM). A kit is eight of these, each with its own
 * fixed pitch. */
enum { O_SINE, O_TRI, O_SAW, O_PULSE, O_SUPER, O_FM, O_NOISE, O_TRI4, O_WAVE, O_LFSR };
enum { F_LP, F_BP, F_HP };
enum { W_SAW, W_GB, W_ORGAN, W_HOLLOW, NWAVES };
typedef struct {
    uint8_t osc, fmode, wt, steps, lshort, arp;
    float hz;                                   /* a fixed pitch (drums), 0: the note's */
    float det, pw, sub, ratio, idx, itime, noise;
    float cut, res, fenv, ftime;                /* Hz; 0..1; octaves; s */
    float att, dec, sus, rel;                   /* s, s, 0..1, s */
    float penv, ptime;                          /* semitones above, falling to the note in ptime */
    float vib, sweep, pwm;                      /* semitones deep; semitones a second; Hz */
    float drive, crush, verb, echo, gain;
    int8_t reg;                                 /* register: semitones */
} snd_t;
#define S_(o, f) .osc = o, .fmode = f
#define OPEN .cut = 16000.0f                    /* a chip voice: the filter all but open (Tone still darkens it) */
static const snd_t MEL[EN_NSOUND - 3] = {
    /* Tri bass: the NES triangle, stepped, no volume of its own (it rings until let go) */
    {S_(O_TRI4, F_LP), OPEN, .att = 0.001f, .dec = 2.0f, .sus = 1.0f, .rel = 0.03f, .verb = 0.05f, .gain = 0.7f, .reg = -12},
    /* Pulse bass: a 50% pulse, sixteen steps of volume */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.5f, .cut = 9000.0f, .att = 0.001f, .dec = 0.5f, .sus = 0.7f, .rel = 0.04f,
     .verb = 0.05f, .gain = 0.5f, .reg = -12},
    /* SID bass: a saw into a resonant low pass, the envelope plucking it */
    {S_(O_SAW, F_LP), .cut = 280.0f, .res = 0.62f, .fenv = 3.2f, .ftime = 0.15f, .att = 0.001f, .dec = 0.35f, .sus = 0.5f,
     .rel = 0.08f, .drive = 1.4f, .verb = 0.05f, .gain = 0.5f, .reg = -12},
    /* Wave bass: the Game Boy's wave channel, a stepped saw */
    {S_(O_WAVE, F_LP), .wt = W_SAW, .steps = 1, .cut = 7000.0f, .att = 0.001f, .dec = 0.8f, .sus = 0.8f, .rel = 0.04f,
     .verb = 0.05f, .gain = 0.75f, .reg = -12},
    /* Octave bass: a 25% pulse bouncing the octave at the Arp rate */
    {S_(O_PULSE, F_LP), .steps = 1, .arp = 5, .pw = 0.25f, .cut = 7000.0f, .att = 0.001f, .dec = 0.5f, .sus = 0.6f,
     .rel = 0.04f, .verb = 0.05f, .gain = 0.45f, .reg = -12},
    /* Square: 50%, a little vibrato */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.5f, OPEN, .att = 0.002f, .dec = 0.6f, .sus = 0.7f, .rel = 0.08f, .vib = 0.2f,
     .verb = 0.2f, .echo = 0.15f, .gain = 0.4f},
    /* Thin lead: 12.5%, nasal */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.125f, OPEN, .att = 0.002f, .dec = 0.8f, .sus = 0.75f, .rel = 0.08f, .vib = 0.3f,
     .verb = 0.2f, .echo = 0.2f, .gain = 0.42f},
    /* Duty lead: 25%, the echo's */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.25f, OPEN, .att = 0.002f, .dec = 0.7f, .sus = 0.7f, .rel = 0.1f, .vib = 0.25f,
     .verb = 0.25f, .echo = 0.35f, .gain = 0.4f},
    /* SID lead: a pulse whose width moves, a resonant low pass opening with each note */
    {S_(O_PULSE, F_LP), .pw = 0.35f, .pwm = 1.3f, .cut = 2200.0f, .res = 0.5f, .fenv = 1.5f, .ftime = 0.3f, .att = 0.004f,
     .dec = 0.8f, .sus = 0.7f, .rel = 0.2f, .vib = 0.2f, .verb = 0.25f, .echo = 0.25f, .gain = 0.45f},
    /* Wave lead: the Game Boy's wave, vibrato */
    {S_(O_WAVE, F_LP), .wt = W_GB, .steps = 1, OPEN, .att = 0.006f, .dec = 1.0f, .sus = 0.8f, .rel = 0.12f, .vib = 0.35f,
     .verb = 0.25f, .echo = 0.25f, .gain = 0.7f},
    /* Chip arp: a 25% pulse that plays its note as a triad, fast */
    {S_(O_PULSE, F_LP), .steps = 1, .arp = 1, .pw = 0.25f, OPEN, .att = 0.001f, .dec = 0.45f, .sus = 0.6f, .rel = 0.08f,
     .verb = 0.2f, .echo = 0.2f, .gain = 0.36f},
    /* Wave pad: an organ wave breathing in, stepped */
    {S_(O_WAVE, F_LP), .wt = W_ORGAN, .steps = 1, .cut = 9000.0f, .att = 0.35f, .dec = 2.0f, .sus = 0.8f, .rel = 0.7f,
     .vib = 0.1f, .verb = 0.5f, .echo = 0.1f, .gain = 0.55f},
    /* SID pad: two pulses a few cents apart, their width moving slowly, a soft low pass */
    {S_(O_PULSE, F_LP), .det = 7.0f, .pw = 0.4f, .pwm = 0.45f, .cut = 1500.0f, .res = 0.3f, .att = 0.5f, .dec = 2.0f,
     .sus = 0.85f, .rel = 1.1f, .verb = 0.6f, .echo = 0.1f, .gain = 0.42f},
    /* Organ: a hollow wave, square and quick */
    {S_(O_WAVE, F_LP), .wt = W_HOLLOW, .steps = 1, OPEN, .att = 0.004f, .dec = 3.0f, .sus = 0.9f, .rel = 0.06f, .vib = 0.12f,
     .verb = 0.3f, .gain = 0.55f},
    /* Ring bell: two-op FM at 1:3.5, falling, crushed a little */
    {S_(O_FM, F_LP), .ratio = 3.5f, .idx = 2.0f, .itime = 0.3f, .cut = 9000.0f, .att = 0.001f, .dec = 1.2f, .sus = 0.0f,
     .rel = 0.8f, .crush = 0.2f, .verb = 0.4f, .echo = 0.3f, .gain = 0.5f},
    /* Blip: a 50% pulse dropping an octave in a flash */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.5f, OPEN, .att = 0.001f, .dec = 0.08f, .sus = 0.0f, .rel = 0.04f, .penv = 12.0f,
     .ptime = 0.02f, .echo = 0.2f, .gain = 0.4f},
    /* Laser: a 25% pulse sweeping down */
    {S_(O_PULSE, F_LP), .steps = 1, .pw = 0.25f, OPEN, .att = 0.001f, .dec = 0.28f, .sus = 0.0f, .rel = 0.1f, .penv = 24.0f,
     .ptime = 0.01f, .sweep = -60.0f, .echo = 0.3f, .gain = 0.38f},
    /* Power-up: a 12.5% pulse arpeggiating and sweeping up */
    {S_(O_PULSE, F_LP), .steps = 1, .arp = 1, .pw = 0.125f, OPEN, .att = 0.001f, .dec = 0.6f, .sus = 0.0f, .rel = 0.2f,
     .sweep = 24.0f, .echo = 0.2f, .gain = 0.36f},
    /* Pickup: a 50% pulse leaping to the fifth and the octave */
    {S_(O_PULSE, F_LP), .steps = 1, .arp = 6, .pw = 0.5f, OPEN, .att = 0.001f, .dec = 0.35f, .sus = 0.0f, .rel = 0.1f,
     .echo = 0.2f, .gain = 0.36f},
    /* Metal: the LFSR's short mode, pitched and ringing */
    {S_(O_LFSR, F_LP), .lshort = 1, .steps = 1, OPEN, .att = 0.001f, .dec = 0.5f, .sus = 0.3f, .rel = 0.1f, .verb = 0.2f,
     .echo = 0.2f, .gain = 0.3f},
    /* Noise sweep: the long LFSR through a band, falling */
    {S_(O_LFSR, F_BP), .cut = 3000.0f, .res = 0.3f, .fenv = -2.0f, .ftime = 1.0f, .att = 0.3f, .dec = 2.0f, .sus = 0.6f,
     .rel = 1.0f, .sweep = -12.0f, .verb = 0.5f, .gain = 0.45f, .reg = 12},
};
/* the kits: KIK SNR HAT OHT TOM and three of each chip's own (C2..G2, 36..43) */
static const snd_t KITS[3][EN_NDRUM] = {
    {   /* NES kit: the triangle's kick and toms, the noise channel's snare and hats */
        {S_(O_TRI4, F_LP), .hz = 55.0f, OPEN, .att = 0.0005f, .dec = 0.25f, .rel = 0.05f, .penv = 24.0f, .ptime = 0.04f,
         .gain = 1.1f},                                                                                     /* KIK */
        {S_(O_LFSR, F_LP), .hz = 1800.0f, .steps = 1, .cut = 9000.0f, .att = 0.0005f, .dec = 0.14f, .rel = 0.05f,
         .penv = 7.0f, .ptime = 0.05f, .verb = 0.15f, .gain = 0.55f},                                       /* SNR */
        {S_(O_LFSR, F_HP), .hz = 6000.0f, .steps = 1, .cut = 6000.0f, .att = 0.0005f, .dec = 0.04f, .rel = 0.02f, .gain = 0.35f},  /* HAT */
        {S_(O_LFSR, F_HP), .hz = 6000.0f, .steps = 1, .cut = 5000.0f, .att = 0.0005f, .dec = 0.25f, .rel = 0.08f, .gain = 0.3f},   /* OHT */
        {S_(O_TRI4, F_LP), .hz = 110.0f, OPEN, .att = 0.0005f, .dec = 0.3f, .rel = 0.05f, .penv = 10.0f, .ptime = 0.12f,
         .verb = 0.15f, .gain = 0.9f},                                                                      /* TOM */
        {S_(O_LFSR, F_BP), .hz = 1200.0f, .cut = 1500.0f, .res = 0.3f, .att = 0.0005f, .dec = 0.1f, .rel = 0.04f,
         .verb = 0.2f, .gain = 0.55f},                                                                      /* CLP */
        {S_(O_LFSR, F_HP), .hz = 3000.0f, .steps = 1, .cut = 3000.0f, .att = 0.0005f, .dec = 1.0f, .rel = 0.3f, .gain = 0.32f},    /* CRS */
        {S_(O_PULSE, F_LP), .hz = 1046.0f, .steps = 1, .pw = 0.5f, OPEN, .att = 0.0005f, .dec = 0.05f, .rel = 0.02f,
         .penv = 12.0f, .ptime = 0.01f, .gain = 0.35f},                                                     /* BLP */
    },
    {   /* GB kit: a pulse kick, the metal noise, the wave's tom */
        {S_(O_PULSE, F_LP), .hz = 70.0f, .steps = 1, .pw = 0.5f, .cut = 2500.0f, .att = 0.0005f, .dec = 0.18f, .rel = 0.04f,
         .penv = 30.0f, .ptime = 0.03f, .gain = 0.75f},                                                    /* KIK */
        {S_(O_LFSR, F_LP), .hz = 2600.0f, .lshort = 1, .steps = 1, OPEN, .att = 0.0005f, .dec = 0.12f, .rel = 0.04f,
         .verb = 0.15f, .gain = 0.45f},                                                                     /* SNR */
        {S_(O_LFSR, F_HP), .hz = 9000.0f, .lshort = 1, .steps = 1, .cut = 5000.0f, .att = 0.0005f, .dec = 0.03f, .rel = 0.02f,
         .gain = 0.3f},                                                                                     /* HAT */
        {S_(O_LFSR, F_HP), .hz = 9000.0f, .lshort = 1, .steps = 1, .cut = 5000.0f, .att = 0.0005f, .dec = 0.2f, .rel = 0.06f,
         .gain = 0.26f},                                                                                    /* OHT */
        {S_(O_WAVE, F_LP), .wt = W_GB, .hz = 140.0f, .steps = 1, OPEN, .att = 0.0005f, .dec = 0.25f, .rel = 0.05f,
         .penv = 8.0f, .ptime = 0.1f, .gain = 0.7f},                                                        /* TOM */
        {S_(O_PULSE, F_LP), .hz = 1700.0f, .steps = 1, .pw = 0.125f, OPEN, .att = 0.0005f, .dec = 0.02f, .rel = 0.01f,
         .gain = 0.3f},                                                                                     /* RIM */
        {S_(O_PULSE, F_BP), .hz = 560.0f, .steps = 1, .pw = 0.5f, .det = 680.0f, .cut = 1200.0f, .res = 0.2f, .att = 0.0005f,
         .dec = 0.15f, .rel = 0.04f, .gain = 0.45f},                                                        /* CWB */
        {S_(O_LFSR, F_LP), .hz = 400.0f, .lshort = 1, .steps = 1, OPEN, .att = 0.0005f, .dec = 0.3f, .rel = 0.05f,
         .sweep = -24.0f, .gain = 0.35f},                                                                   /* NZE */
    },
    {   /* SID kit: triangles and noise through the filter */
        {S_(O_TRI, F_LP), .hz = 50.0f, .cut = 800.0f, .att = 0.0005f, .dec = 0.3f, .rel = 0.05f, .penv = 30.0f, .ptime = 0.05f,
         .drive = 1.5f, .gain = 1.0f},                                                                     /* KIK */
        {S_(O_TRI, F_LP), .hz = 180.0f, .noise = 0.8f, .cut = 5000.0f, .att = 0.0005f, .dec = 0.16f, .rel = 0.05f,
         .penv = 5.0f, .ptime = 0.03f, .verb = 0.15f, .gain = 0.5f},                                        /* SNR */
        {S_(O_NOISE, F_HP), .cut = 8000.0f, .att = 0.0005f, .dec = 0.045f, .rel = 0.02f, .gain = 0.3f},     /* HAT */
        {S_(O_NOISE, F_HP), .cut = 7000.0f, .att = 0.0005f, .dec = 0.3f, .rel = 0.08f, .gain = 0.26f},      /* OHT */
        {S_(O_TRI, F_LP), .hz = 100.0f, .cut = 1500.0f, .att = 0.0005f, .dec = 0.35f, .rel = 0.06f, .penv = 12.0f,
         .ptime = 0.15f, .verb = 0.15f, .gain = 0.8f},                                                      /* TOM */
        {S_(O_PULSE, F_LP), .hz = 1500.0f, .pw = 0.3f, .cut = 8000.0f, .att = 0.0005f, .dec = 0.15f, .rel = 0.04f,
         .penv = 36.0f, .ptime = 0.08f, .echo = 0.3f, .gain = 0.35f},                                       /* ZAP */
        {S_(O_NOISE, F_BP), .cut = 1300.0f, .res = 0.4f, .att = 0.0005f, .dec = 0.12f, .rel = 0.04f, .verb = 0.2f,
         .gain = 0.55f},                                                                                    /* CLP */
        {S_(O_SAW, F_LP), .hz = 220.0f, .cut = 400.0f, .res = 0.8f, .fenv = 4.0f, .ftime = 0.1f, .att = 0.0005f, .dec = 0.2f,
         .rel = 0.05f, .echo = 0.2f, .gain = 0.45f},                                                        /* SYN */
    },
};
#undef S_
#undef OPEN
static const char *const DRUM_NAME[3][EN_NDRUM] = {
    {"KIK", "SNR", "HAT", "OHT", "TOM", "CLP", "CRS", "BLP"},
    {"KIK", "SNR", "HAT", "OHT", "TOM", "RIM", "CWB", "NZE"},
    {"KIK", "SNR", "HAT", "OHT", "TOM", "ZAP", "CLP", "SYN"},
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

/* the waves: 32 steps of 4 bits, centred (built in en_init) */
static float wave_t[NWAVES][32];
static void waves_init(void)
{
    static const uint8_t GB[32] = {8, 11, 13, 14, 15, 15, 14, 13, 11, 8, 6, 4, 3, 2, 1, 1, 2, 3, 5, 7, 9, 10, 10, 9, 8, 7,
                                   6, 6, 7, 8, 8, 8};
    int w, i;
    for (i = 0; i < 32; i++) {
        float t = (float)i * (1.0f / 32.0f), v[NWAVES];
        v[W_SAW] = (float)i * (1.0f / 31.0f);
        v[W_GB] = (float)GB[i] * (1.0f / 15.0f);
        v[W_ORGAN] = 0.5f + 0.5f * (fm_sin_turns(t) + 0.5f * fm_sin_turns(2.0f * t) + 0.25f * fm_sin_turns(4.0f * t)) * (1.0f / 1.6f);
        v[W_HOLLOW] = 0.5f + 0.5f * (fm_sin_turns(t) + 0.6f * fm_sin_turns(3.0f * t)) * (1.0f / 1.45f);
        for (w = 0; w < NWAVES; w++) {
            int q = (int)(fm_clampf(v[w], 0.0f, 1.0f) * 15.0f + 0.5f);
            wave_t[w][i] = ((float)q - 7.5f) * (1.0f / 7.5f);
        }
    }
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
    float gl, gr, tone, decay, vol, send_r, send_e, drive, crush;   /* from its parameters */
    float vib;                                  /* Vibrato: semitones deep */
    float tf, tf0;                              /* level x duck: this chunk's, and the last (a ramp between) */
    float duck;                                 /* the kick's duck, 1 none */
    float peak;
    int8_t duty;                                /* Tone's steps of a chip pulse's duty: -1 wider .. 2 narrower */
    uint8_t arp;                                /* Arp: its shape (0 the sound's own) */
    int8_t drift[EN_NROW];                      /* Evolve: each row's drift, in degrees of the scale */
    uint8_t last, group, midi_note;             /* its last note; the note group playing; its MIDI note sounding */
    int8_t glide_from;
} track_t;
static track_t tk[EN_NTRK];
static int32_t arp_len = 735;                   /* the arps' step, in samples (the song's Arp rate) */

/* ----------------------------------------------------------- the voices --- */
#define NV 16
typedef struct {
    const snd_t *s;
    float ph, ph2, ph3, sph, mph, inc, inc_t, pk, idx, idec;
    float env, ak, dk, rk, sus;
    float fe, fek, cut0, tone;                  /* the filter: its envelope (octaves), its base (Hz), the note's tone */
    float ic1, ic2, g0, a0;                     /* the SVF's state, and last chunk's coefficients */
    float bendk, swk;                           /* Bend's fall and the sweep: the pitch's change a sample */
    float vel, crush, chold, cph, send_e, send_r;
    float duty, pwph, vdep, vph, vt;            /* the pulse's width, its PWM's phase; vibrato's depth, phase, time */
    float ar[4];                                /* the arpeggio: each step's pitch ratio */
    float nacc, nval;                           /* the LFSR: its clock, its output */
    int32_t wait, gate;                         /* samples before it starts; until it lets go (-1 held) */
    int32_t acnt;                               /* samples to the arpeggio's next step */
    uint16_t lfsr;
    uint8_t on, trk, note, group, stage, started, an, ai;
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
    float vel, tone, glide, bend, crush, vib, swell;    /* the cell's effects, as a voice takes them */
    int32_t gate;                               /* samples held; -1 until the next note */
} hit_t;

/* the arps' shapes: degrees of the scale (a value of 100 + n: n semitones; 99 the end) */
static const int8_t ARP[EN_NARP][4] = {{0, 99, 99, 99}, {0, 2, 4, 99}, {4, 2, 0, 99}, {0, 2, 4, 2},
                                       {0, 2, 4, 6}, {0, 112, 99, 99}, {0, 4, 112, 99}, {0, 1, 99, 99}};
static int deg_shift(int note, int d);
static const float DUTY[4] = {0.75f, 0.5f, 0.25f, 0.125f};

/* a note on track t: wait samples from now */
static void voice_start(int t, int nn, int32_t wait, const hit_t *h)
{
    int k = voice_alloc(), snd = par[P_T(t, TP_SOUND)], kit = en_sound_is_kit(snd), a;
    voice_t *v = &vc[k];
    const snd_t *s = sound_of(snd, nn);
    track_t *T = &tk[t];
    float f, dec = T->decay, att;
    if (kit)
        f = s->hz * fm_exp2f((float)par[P_T(t, TP_TONE)] * (1.0f / 100.0f));   /* Tone tunes a kit, half an octave */
    else
        f = mtof((float)(nn + s->reg));
    f = fm_clampf(f, 8.0f, 16000.0f);
    v->s = s;
    v->trk = (uint8_t)t;
    v->note = (uint8_t)nn;
    v->group = T->group;
    v->inc_t = f * (1.0f / EN_SR);
    if (h->glide > 0.0f && T->glide_from >= 0 && !kit) {
        v->inc = mtof((float)(T->glide_from + s->reg)) * (1.0f / EN_SR);
        v->pk = coef(h->glide * 0.5f);
    } else {
        v->inc = s->penv != 0.0f ? v->inc_t * fm_exp2f(s->penv * (1.0f / 12.0f)) : v->inc_t;
        v->pk = s->ptime > 0.0f ? coef(s->ptime) : 0.0f;
    }
    v->bendk = h->bend > 0.0f ? fm_exp2f(-h->bend / (12.0f * fm_maxf((float)h->gate, 2000.0f))) : 1.0f;
    v->swk = s->sweep != 0.0f ? fm_exp2f(s->sweep * (1.0f / (12.0f * EN_SR))) : 1.0f;
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
    v->send_e = s->echo;
    v->send_r = s->verb;
    /* the chip's pulse: its duty in steps, Tone moving it */
    v->duty = s->pw;
    if (s->osc == O_PULSE && s->steps && !kit) {
        int d = s->pw >= 0.5f ? 1 : s->pw >= 0.25f ? 2 : 3;
        d += T->duty;
        v->duty = DUTY[d < 1 ? 1 : d > 3 ? 3 : d];
    }
    v->pwph = 0.0f;
    v->vdep = kit ? 0.0f : s->vib + T->vib + h->vib;
    v->vph = 0.0f, v->vt = 0.0f;
    /* the arpeggio: the track's shape, else the sound's */
    a = kit ? 0 : T->arp ? T->arp : s->arp;
    v->an = 1, v->ai = 0, v->ar[0] = 1.0f;
    if (a > 0 && a < EN_NARP) {
        int n = 0, i;
        for (i = 0; i < 4 && ARP[a][i] != 99; i++) {
            int d = ARP[a][i], to = d >= 100 ? nn + d - 100 : deg_shift(nn, d);
            v->ar[n++] = fm_exp2f((float)(to - nn) * (1.0f / 12.0f));
        }
        v->an = (uint8_t)n;
    }
    v->acnt = arp_len;
    v->lfsr = (uint16_t)((0x7FFFu ^ ((uint32_t)k * 2654435761u >> 17)) | 1u);
    v->nacc = 0.0f, v->nval = 0.0f;
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
static inline float pulse(float t, float dt, float w)
{
    float t2 = wrap1(t + 1.0f - w);
    return (t < w ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
}

static void render_voices(uint32_t n)
{
    int k;
    const float chunk_s = (float)n * (1.0f / EN_SR);
    for (k = 0; k < NV; k++) {
        voice_t *v = &vc[k];
        const snd_t *s = v->s;
        track_t *T;
        uint32_t j = 0;
        float ph, ph2, ph3, sph, mph, inc, idx, env, fe, ic1, ic2, chold, cph, cut, g1, a1, dg, da, kres, gl, gr, sr, se, amp;
        float pk1, det, vibf, tgt, base, nacc, nval, duty, peak = 0.0f;
        int32_t acnt;
        uint16_t lfsr;
        uint8_t ai;
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
        /* the macros, by the chunk (a chip's are by the frame): vibrato coming in, the pulse's width moving */
        v->vt += chunk_s;
        vibf = 1.0f;
        if (v->vdep > 0.0f) {
            float in = fm_clampf((v->vt - 0.12f) * 5.0f, 0.0f, 1.0f);
            v->vph = wrap1(v->vph + 5.5f * chunk_s);
            vibf = fm_exp2f(v->vdep * in * fm_sin_turns(v->vph) * (1.0f / 12.0f));
        }
        duty = v->duty;
        if (s->pwm > 0.0f) {
            v->pwph = wrap1(v->pwph + s->pwm * chunk_s);
            duty = fm_clampf(v->duty + 0.32f * fm_sin_turns(v->pwph), 0.06f, 0.94f);
        }
        /* the filter's cutoff this chunk: its base, the track's Tone, the note's tone, its envelope */
        cut = v->cut0 * T->tone * v->tone * fm_exp2f(v->fe);
        if (s->fmode != F_HP && cut < 15000.0f)  /* the pitch's keytracking, a little (half) */
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
        nacc = v->nacc, nval = v->nval, lfsr = v->lfsr, acnt = v->acnt, ai = v->ai;
        pk1 = 1.0f - v->pk;
        det = fm_exp2f(s->det * (1.0f / 1200.0f));
        base = v->inc_t * vibf;
        tgt = base * v->ar[ai];
        {
            float g = v->g0 + dg * (float)j, a = v->a0 + da * (float)j;
            const float drive = s->drive * T->drive, dk = v->dk, ak = v->ak, rk = v->rk, sus = v->sus, crush = v->crush;
            const float crate = 1.0f - crush * 0.92f, cq = crush > 0.0f ? 4.0f + (1.0f - crush) * 120.0f : 0.0f;
            const float nk = s->lshort ? 93.0f : 32.0f;   /* the LFSR's clocks a cycle: metal pitched, long a hiss */
            const float dtf = (T->tf - T->tf0) / (float)n;
            const uint8_t an = v->an, steps = s->steps;
            float tf = T->tf0 + dtf * (float)j;
            int32_t gate = v->gate;
            uint8_t stage = v->stage;
            for (; j < n; j++) {
                float x, hp, v1, v2, e;
                if (an > 1 && --acnt <= 0) {     /* the arpeggio's next step */
                    acnt = arp_len;
                    if (++ai >= an)
                        ai = 0;
                    tgt = base * v->ar[ai];
                    if (v->pk <= 0.0f)
                        inc = tgt;
                }
                inc += (tgt - inc) * pk1;
                switch (s->osc) {
                case O_SINE: x = sine(ph); break;
                case O_TRI: x = 4.0f * fm_fabsf(ph - 0.5f) - 1.0f; break;
                case O_SAW: x = saw(ph, inc); break;
                case O_PULSE:
                    x = pulse(ph, inc, duty);
                    if (s->det > 0.0f) {         /* a second pulse a few cents up */
                        x = 0.6f * (x + pulse(ph2, inc * det, duty));
                        ph2 = wrap1(ph2 + inc * det);
                    }
                    x *= 0.6f;
                    break;
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
                case O_TRI4: {
                    int i = (int)(ph * 32.0f) & 31;
                    x = ((i < 16 ? (float)(15 - i) : (float)(i - 16)) * (1.0f / 7.5f) - 1.0f) * 0.9f;
                    break;
                }
                case O_WAVE: x = wave_t[s->wt][(int)(ph * 32.0f) & 31] * 0.8f; break;
                case O_LFSR: {
                    int c = 0;
                    nacc += inc * nk;
                    while (nacc >= 1.0f && c++ < 8) {
                        uint16_t r = lfsr, bit = (uint16_t)((r ^ (r >> 1)) & 1u);
                        nacc -= 1.0f;
                        r = (uint16_t)((r >> 1) | (bit << 14));
                        if (s->lshort)
                            r = (uint16_t)((r & ~0x40u) | (bit << 6));
                        lfsr = r;
                        nval = (r & 1u) ? 0.7f : -0.7f;
                    }
                    if (nacc >= 1.0f)
                        nacc -= (float)(int)nacc;
                    x = nval;
                    break;
                }
                default: x = wnoise(&noise_s); break;
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
                e = steps ? (float)(int)(env * 15.0f + 0.5f) * (1.0f / 15.0f) : env;   /* a chip's sixteen levels */
                x *= e * tf;
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
        {
            float k2 = v->bendk * v->swk;
            if (k2 != 1.0f)
                v->inc_t = fm_clampf(v->inc_t * fm_powf(k2, (float)n), 4.0f / EN_SR, 0.45f);
        }
        v->ph = ph, v->ph2 = ph2, v->ph3 = ph3, v->sph = sph, v->mph = mph, v->inc = inc, v->idx = idx;
        v->env = env, v->ic1 = fm_flush(ic1), v->ic2 = fm_flush(ic2), v->chold = chold, v->cph = cph;
        v->nacc = nacc, v->nval = nval, v->lfsr = lfsr, v->acnt = acnt, v->ai = ai;
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
static float lofi_lp = 1.0f, lofi_q, duck_amt, duck_env;   /* Lo-fi: the top's one-pole, the bits (0 none) */
static float lofi_l, lofi_r;
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

/* the arps' step: a frame (60 Hz), two or three; or a 32nd or a 16th of the tempo */
static void arp_rate(void)
{
    float r = 15.0f / fm_maxf(en_bpm, 20.0f) * EN_SR;
    switch (par[P_ARP_T]) {
    case 0: arp_len = 735; break;
    case 2: arp_len = 2205; break;
    case 3: arp_len = (int32_t)(r * 0.5f); break;
    case 4: arp_len = (int32_t)r; break;
    default: arp_len = 1470; break;
    }
    if (arp_len < 64)
        arp_len = 64;
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
    g = pct(par[P_T(t, TP_CRUSH)]);
    T->drive = 1.0f + g * 0.6f;
    T->crush = g * 0.85f;
    T->vib = pct(par[P_T(t, TP_VIB)]) * 0.6f;
    T->arp = (uint8_t)par[P_T(t, TP_ARP)];
    {
        int tn = par[P_T(t, TP_TONE)];
        T->duty = (int8_t)(tn <= -25 ? -1 : tn < 15 ? 0 : tn < 40 ? 1 : 2);
    }
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
    case P_ECHO_T: case P_TEMPO: echo_time(); arp_rate(); break;
    case P_ECHO_FB: echo_fb = pct(v) * 1.05f; break;
    case P_VERB: pl_decay_t = 0.45f + pct(v) * 0.47f; break;
    case P_LOFI:
        lofi_lp = onepole_k(18000.0f - pct(v) * 12500.0f);
        lofi_q = v > 0 ? fm_exp2f(12.0f - pct(v) * 7.0f) : 0.0f;   /* 12 bits down to 5 */
        break;
    case P_ARP_T: arp_rate(); break;
    case P_DUCK: duck_amt = pct(v) * 0.7f; break;
    case P_TUNE: tunefac = fm_exp2f((float)v * (1.0f / 1200.0f)); break;
    default: break;
    }
}

/* ------------------------------------------------------------ the tracker --- */
static int seq_pos, seq_row, seq_rep, jump_to = -1, hop_to = -1;
static uint32_t hop_used;                       /* the rows whose Hop has gone this pass */
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

/* a cell's effect: its value + 1 when either of its two has the command (the first wins), else 0 */
static int fxv(const en_cell_t *c, int cmd)
{
    if ((c->fx >> 4) == cmd)
        return (c->fx & 15) + 1;
    if ((c->fx2 >> 4) == cmd)
        return (c->fx2 & 15) + 1;
    return 0;
}

static void play_cell(int t, const en_cell_t *c, int32_t at, int row)
{
    track_t *T = &tk[t];
    const int snd = par[P_T(t, TP_SOUND)], kit = en_sound_is_kit(snd);
    const float rl = row_samples();
    int note = c->note, k, v;
    hit_t h;
    if ((v = fxv(c, FX_JUMP)))
        jump_to = v - 1;
    if ((v = fxv(c, FX_HOP)) && !(hop_used >> row & 1u)) {   /* H: on to a row of this pattern, once a pass */
        hop_used |= 1u << row;
        hop_to = v - 1;
    }
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
        h.vel = c->vol ? 0.04f + (float)EN_VOL(c->vol) * (0.96f / 15.0f) : 0.8f;   /* the volume column */
    }
    if ((v = fxv(c, FX_CHANCE)) && frand() >= (float)v * (1.0f / 16.0f))
        return;
    if ((v = fxv(c, FX_DELAY)))
        at += (int32_t)(rl * (float)(v - 1) * (1.0f / 16.0f));
    if (!kit) {
        if ((v = fxv(c, FX_MUTATE)) > 1)
            note = deg_shift(note, irand(2 * (v - 1) + 1) - (v - 1));
        if (T->drift[row]) {
            note = deg_shift(note, T->drift[row]);
            en_evolved++;
        }
    } else if (T->drift[row] < 0 && c->note >= NOTE_LO && frand() < 0.5f) {
        return;                                  /* Evolve drops a drum */
    }
    h.tone = 1.0f, h.glide = 0.0f, h.bend = 0.0f, h.crush = 0.0f, h.vib = 0.0f, h.swell = 0.0f, h.gate = -1;
    if ((v = fxv(c, FX_GLIDE)))
        h.glide = 0.02f + 0.03f * (float)(v - 1);
    if ((v = fxv(c, FX_TONE)))
        h.tone = fm_exp2f((float)(v - 8) * 0.5f);
    if ((v = fxv(c, FX_VIB)))                    /* V: vibrato, a fifteenth of a semitone a step */
        h.vib = (float)(v - 1) * (1.0f / 15.0f) + 0.05f;
    if ((v = fxv(c, FX_BEND)))
        h.bend = v > 1 ? (float)(v - 1) : 12.0f;
    if ((v = fxv(c, FX_CRUSH)))
        h.crush = (float)(v - 1) * (0.9f / 15.0f);
    if ((v = fxv(c, FX_SWELL)))
        h.swell = (float)v * 0.25f * rl * (1.0f / EN_SR);
    if ((v = fxv(c, FX_LENGTH)))
        h.gate = (int32_t)fm_maxf(rl * (float)v * (1.0f / 16.0f), 64.0f);
    /* a pad or a drone given the note it holds, and nothing else: it holds on (no breath in again) */
    if (!kit && !c->fx && !c->fx2 && sound_of(snd, note)->att >= 0.1f && track_holding(t, note)) {
        en_trk_glow[t] = fm_maxf(en_trk_glow[t], 0.5f);
        return;
    }
    track_midi_off(t);
    T->group++;
    if ((v = fxv(c, FX_CHORD)) && !kit) {
        v--;
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
    } else if (fxv(c, FX_RATCHET) || fxv(c, FX_STUTTER)) {
        const int stut = !fxv(c, FX_RATCHET);
        int cnt;
        float gap;
        v = stut ? fxv(c, FX_STUTTER) - 1 : fxv(c, FX_RATCHET) - 1;
        if (!stut) {                             /* R: KAWA's ROL, 2 to 16 in the row */
            cnt = v < 1 ? 2 : v + 1;
            gap = rl / (float)cnt;
        } else {                                 /* S: 30-60 ms repeats, crushed, falling away */
            gap = (0.03f + 0.002f * (float)v) * EN_SR;
            cnt = (int)fm_clampf(rl * 1.5f / gap, 3.0f, 14.0f);
            h.crush = fm_maxf(h.crush, 0.45f);
        }
        h.gate = (int32_t)(gap * 0.7f);
        for (k = 0; k < cnt; k++) {
            voice_start(t, note, at + (int32_t)(gap * (float)k), &h);
            T->group++;
            h.vel *= stut ? 0.82f : 0.94f;
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
        if (e <= 0.0f || en_follow_role(&en_song, t)) {   /* a track that follows: its Evolve is how often a new take */
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

/* FOLLOW's takes: each following track's, for the pattern it was written from; a new one when the track's
 * Evolve says, at the top of a pass */
static en_cell_t take[EN_NTRK][EN_NROW];
static int8_t take_pat[EN_NTRK] = {-1, -1, -1, -1, -1, -1};
static uint16_t take_var[EN_NTRK];
uint32_t en_take_seed(const en_song_t *s, int t, int pat)
{
    (void)pat;
    t = t < 0 ? 0 : t % EN_NTRK;
    return (uint32_t)s->par[P_TAKE] * 7919u + (uint32_t)t * 104729u + (uint32_t)take_var[t] * 2654435761u;
}
static const en_cell_t *cell_of(int t, int pat)
{
    if (!en_follow_role(&en_song, t))
        return &en_song.cell[pat][seq_row][t];
    if (seq_row == 0 || take_pat[t] != pat) {    /* the top of a pass (the lead may have changed), or new here */
        if (seq_row == 0 && take_pat[t] >= 0 && frand() < pct(par[P_T(t, TP_EVOLVE)])) {
            take_var[t]++;
            en_takes++;
        }
        en_follow(&en_song, pat, t, en_take_seed(&en_song, t, pat), take[t]);
        take_pat[t] = (int8_t)pat;
    }
    return &take[t][seq_row];
}

static void row_fire(int32_t at)
{
    int t, pat, plen;
    int32_t sw = 0;
    if (seq_pos >= song_len())
        seq_pos = 0;
    pat = EN_ORD_PAT(en_song.order[seq_pos]);
    plen = en_pat_len(&en_song, pat);
    if (seq_row >= plen)
        seq_row = 0;
    en_pos = (uint8_t)seq_pos;
    en_row = (uint8_t)seq_row;
    en_pat_now = (uint8_t)pat;
    if (seq_row & 1)                             /* the swing: the off sixteenths late */
        sw = (int32_t)(row_samples() * pct(par[P_SWING]) * 0.5f);
    for (t = 0; t < EN_NTRK; t++) {
        const en_cell_t *c = cell_of(t, pat);
        if (!(tmute >> t & 1u))
            play_cell(t, c, at + sw, seq_row);
    }
    en_rows++;
    if (jump_to >= 0) {                          /* J: on to an order position */
        seq_pos = jump_to < song_len() ? jump_to : 0;
        seq_row = 0, seq_rep = 0, hop_used = 0, hop_to = -1;
        event(EV_JUMP, seq_pos);
        jump_to = -1;
        en_passes++;
        evolve_pass();
        return;
    }
    if (hop_to >= 0) {                           /* H: on to a row of this pattern (each Hop once a pass) */
        seq_row = hop_to < plen ? hop_to : 0;
        hop_to = -1;
        return;
    }
    if (++seq_row >= plen) {                     /* the pattern's end (its length: KAWA's END) */
        seq_row = 0;
        hop_used = 0;
        en_passes++;
        evolve_pass();
        if (par[P_LOOP] != LOOP_PATTERN && ++seq_rep >= EN_ORD_REP(en_song.order[seq_pos])) {
            seq_rep = 0;                         /* its repeats done: the next position */
            if (++seq_pos >= song_len()) {
                seq_pos = 0;
                event(EV_LOOP, 0);
            }
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
    jump_to = -1, hop_to = -1, hop_used = 0, seq_rep = 0;
    {
        int t;
        for (t = 0; t < EN_NTRK; t++)            /* the takes written afresh */
            take_pat[t] = -1;
    }
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
        echo_time(), arp_rate();
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
    duck_env *= fm_expf(-dt * fm_maxf(en_bpm, 30.0f) * (1.0f / 60.0f) * 11.0f);   /* gone in about an eighth */
    for (t = 0; t < EN_NTRK; t++) {              /* each track: its level and the duck */
        track_t *T = &tk[t];
        T->duck = t == duck_trk ? 1.0f : 1.0f - duck_amt * duck_env;
        T->tf0 = T->tf;
        T->tf = T->vol * T->duck;
        en_trk_glow[t] *= 1.0f - fm_minf(dt * 5.0f, 1.0f);
        en_level[t] = fm_maxf(T->peak * 2.5f, en_level[t] * (1.0f - fm_minf(dt * 6.0f, 1.0f)));
        T->peak = 0.0f;
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
        lofi_l += lofi_lp * (l - lofi_l);
        lofi_r += lofi_lp * (r - lofi_r);
        l = lofi_l, r = lofi_r;
        if (lofi_q > 0.0f) {                     /* fewer bits: the mix through a small DAC */
            l = (float)(int)(l * lofi_q) / lofi_q;
            r = (float)(int)(r * lofi_q) / lofi_q;
        }
        l *= 1.3f * gain * mute_g;
        r *= 1.3f * gain * mute_g;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
#ifdef EN_HOST
        if (bus_on)
            busb[5][j] = 0.6f * (wl + wr + el + er);
#endif
    }
    lofi_l = fm_flush(lofi_l), lofi_r = fm_flush(lofi_r);
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
    waves_init();
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
        tk[i].duck = 1.0f;
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
/* writes one track of one pattern in one of eight chip styles: Driver (the eighth-note bass bouncing its
 * octave, a four-on-the-floor kit), Hold (a held chord, perhaps a move at the half bar; a half-time kit), the
 * Ostinato (a figure of fourths and fifths, round and round; a broken beat), a Melody (a phrase stepping
 * through the scale with a leap now and then, landing on the chord; a rock beat), the Triplet (every three
 * rows; a shuffle), the Arp run (sixteenths up and down the chord; sixteenth hats), Bleeps (short and crushed;
 * the kit's own sounds) and the Sparse (a note, maybe two). Pure (its own generator): the tests and the screen
 * call it too. */
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
    c->vol = 0, c->fx2 = 0;
    c->fx = (uint8_t)(fx << 4 | (v & 15));
}
static void g_vol(en_song_t *s, int pat, int row, int t, int level)   /* a cell's volume, 0..15 */
{
    s->cell[pat][row & 15][t].vol = (uint8_t)(level + 1);
}
enum { D_KIK = 36, D_SNR, D_HAT, D_OHT, D_TOM, D_X1, D_X2, D_X3 };

static void grow_kit(en_song_t *s, int pat, int t, int style, int density)
{
    /* kick, snare and hat rows for each style (bit r: row r) */
    static const uint16_t KICK[EN_NGROW] = {0x1111, 0x0401, 0x0401, 0x0141, 0x0201, 0x0101, 0x0821, 0x0001};
    static const uint16_t SNARE[EN_NGROW] = {0x1010, 0x0100, 0x1010, 0x1010, 0x1040, 0x1010, 0x0000, 0x0000};
    static const uint16_t HATS[EN_NGROW] = {0x4444, 0x1111, 0x5555, 0x5555, 0x2491, 0xFFFF, 0x0000, 0x0000};
    int r;
    style &= 7;
    for (r = 0; r < EN_NROW; r++) {
        if (KICK[style] >> r & 1u)
            g_put(s, pat, r, t, D_KIK, 0, 0);
        else if (SNARE[style] >> r & 1u)
            g_put(s, pat, r, t, D_SNR, 0, 0);
        else if (HATS[style] >> r & 1u) {
            if (style == 5)                      /* the sixteenths: accents on the beat */
            {
                g_put(s, pat, r, t, D_HAT, 0, 0);
                if (r & 3)
                    g_vol(s, pat, r, t, 6 + g_irand(4));
            }
            else if (style == 3 && r == 14 && g_chance(density))
                g_put(s, pat, r, t, D_OHT, 0, 0);
            else
                g_put(s, pat, r, t, D_HAT, 0, 0);
        } else if (style == 0 && (r & 1) && g_chance(density / 3))
            g_put(s, pat, r, t, D_HAT, 0, 0), g_vol(s, pat, r, t, 5);
        else if (style == 2 && (r == 7 || r == 15) && g_chance(density))
            g_put(s, pat, r, t, D_SNR, 0, 0), g_vol(s, pat, r, t, 5);     /* the ghosts */
        else if (style == 6 && g_chance(density / 2))
            g_put(s, pat, r, t, D_X1 + g_irand(3), g_chance(40) ? FX_RATCHET : FX_CHANCE, 2 + g_irand(10));
        else if (style == 7 && r >= 12 && g_chance(density / 2))
            g_put(s, pat, r, t, D_TOM, 0, 0);
    }
    if (style != 6 && style != 7 && density > 60 && g_chance(density - 40)) {   /* a fill into the next bar */
        g_put(s, pat, 14, t, D_TOM, 0, 0);
        g_put(s, pat, 15, t, D_SNR, FX_RATCHET, 3);
    }
    if (style == 1 || style == 7)                /* the slow kits: a crash or the kit's own on the one, now and then */
        if (g_chance(density / 2))
            g_put(s, pat, 0, t, D_X2, 0, 0);
}

/* ------------------------------------------------------------ the tools --- */
/* KAWA's MAGIC, on the cells already there (Euclid writes afresh) */
static void put_fx(en_cell_t *c, int cmd, int v)  /* into the first free of the two */
{
    if (!c->fx)
        c->fx = (uint8_t)(cmd << 4 | (v & 15));
    else if (!c->fx2)
        c->fx2 = (uint8_t)(cmd << 4 | (v & 15));
}
static int g_euclid(int k, int n, int r) { return k > 0 && (r * k) % n < k; }

static void grow_tool(en_song_t *s, int pat, int t, int tool, int density, int range, int base)
{
    const int root = s->par[P_ROOT], kit = en_sound_is_kit(s->par[P_T(t, TP_SOUND)]), len = en_pat_len(s, pat);
    const uint32_t p = EN_MODE_POOL[(unsigned)s->par[P_MODE] < EN_NMODES ? s->par[P_MODE] : 0];
    en_cell_t col[EN_NROW];
    int r;
    for (r = 0; r < EN_NROW; r++)
        col[r] = s->cell[pat][r][t];
    switch (tool) {
    case G_EUCLID: {                             /* density's hits over the pattern, as evenly as they go */
        int k = 1 + density * (len - 1) / 100, rot = g_irand(len);
        for (r = 0; r < EN_NROW; r++) {
            en_cell_t *c = &s->cell[pat][r][t];
            c->note = 0, c->vol = 0, c->fx = 0, c->fx2 = 0;
            if (r < len && g_euclid(k, len, (r + rot) % len))
                c->note = (uint8_t)(kit ? 36 : deg_move(base, root, p, g_chance(range / 2) ? 4 : 0));
        }
        return;
    }
    case G_VARY:                                 /* a few notes a step in the scale, a drum or two moved over */
        for (r = 0; r < len; r++) {
            en_cell_t *c = &s->cell[pat][r][t];
            if (c->note < NOTE_LO || !g_chance(density / 2 + 5))
                continue;
            if (!kit) {
                c->note = (uint8_t)deg_move(c->note, root, p, g_chance(50) ? 1 : -1);
            } else {
                int to = (r + (g_chance(50) ? 1 : -1) + len) % len;
                if (s->cell[pat][to][t].note == NOTE_EMPTY) {
                    s->cell[pat][to][t] = *c;
                    c->note = 0, c->vol = 0, c->fx = 0, c->fx2 = 0;
                }
            }
        }
        return;
    case G_ROLLS:                                /* ratchets at the ends of beats (KAWA's ROLLS) */
        for (r = 3; r < len; r += 4) {
            en_cell_t *c = &s->cell[pat][r][t];
            if (!g_chance(density + 10))
                continue;
            if (c->note < NOTE_LO) {             /* nothing there: the beat's last note again */
                int q;
                for (q = r - 1; q >= 0 && col[q].note < NOTE_LO; q--)
                    ;
                if (q < 0)
                    continue;
                c->note = col[q].note;
            }
            put_fx(c, FX_RATCHET, 1 + g_irand(3));
        }
        return;
    case G_ACCENTS:                              /* the beats at full, the rest softer as density says */
        for (r = 0; r < len; r++) {
            en_cell_t *c = &s->cell[pat][r][t];
            if (c->note < NOTE_LO)
                continue;
            c->vol = (uint8_t)(1 + (r % 4 == 0 ? 15 : r % 2 == 0 ? 11 - density / 25 : 8 - density / 20));
        }
        return;
    case G_SHIFT:                                /* on a row, round the pattern */
        for (r = 0; r < len; r++)
            s->cell[pat][(r + 1) % len][t] = col[r];
        return;
    case G_UP:                                   /* a step up the scale (a kit: each drum the next) */
        for (r = 0; r < len; r++) {
            en_cell_t *c = &s->cell[pat][r][t];
            if (c->note >= NOTE_LO)
                c->note = (uint8_t)(kit ? 36 + (c->note - 36 + 1) % EN_NDRUM : deg_move(c->note, root, p, 1));
        }
        return;
    default:
        return;
    }
}

void en_grow(en_song_t *s, int pat, int t, int style, int density, int range, uint32_t seed)
{
    const int snd = s->par[P_T(t, TP_SOUND)], root = s->par[P_ROOT];
    const uint32_t p = EN_MODE_POOL[(unsigned)s->par[P_MODE] < EN_NMODES ? s->par[P_MODE] : 0];
    const int high = (snd >= 8 && snd <= 13) || snd >= 17;
    const int base = (high ? 60 : 48) + root;   /* a bass sound is an octave down already */
    const int rg = 1 + range * 6 / 100;
    int r, k;
#define GN(d) deg_move(base, root, p, (d))
    g_rng = seed * 2654435761u + 0x9E3779B9u + (uint32_t)(pat * 131 + t * 17 + style);
    if (!g_rng)
        g_rng = 1u;
    pat &= 15;
    if (style >= G_EUCLID) {                     /* the tools: on what's there */
        grow_tool(s, pat, t, style, density, range, base);
        return;
    }
    for (r = 0; r < EN_NROW; r++)
        s->cell[pat][r][t].note = 0, s->cell[pat][r][t].fx = 0, s->cell[pat][r][t].vol = 0, s->cell[pat][r][t].fx2 = 0;
    if (en_sound_is_kit(snd)) {
        grow_kit(s, pat, t, style, density);
        return;
    }
    switch (style & 7) {
    case 0: {                                    /* Driver: eighths, the root bouncing its octave; the fifth on beat 3 */
        int deg = g_chance(30) ? (g_chance(50) ? 3 : 5) : 0;   /* half the bars on another degree */
        for (r = 0; r < EN_NROW; r += 2) {
            int n = GN(r >= 8 ? deg : 0);
            if (r == 8 && g_chance(40))
                n = GN(4);
            g_put(s, pat, r, t, (r & 2) ? n + 12 : n, 0, 0);
            if (g_chance(density / 4))
                g_put(s, pat, r + 1, t, n + 12, 0, 0), g_vol(s, pat, r + 1, t, 8);
        }
        break;
    }
    case 1:                                      /* Hold: a held note; perhaps a move at the half bar */
        g_put(s, pat, 0, t, GN(0), 0, 0);
        if (g_chance(density / 2))
            g_put(s, pat, 8, t, GN(g_chance(50) ? 3 : g_chance(50) ? 5 : 4), 0, 0);
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
    case 3: {                                    /* Melody: a rhythm, steps through the scale, a leap now and then */
        static const uint16_t RHY[4] = {0x5A5A, 0x4925, 0x2A95, 0x1555};
        uint16_t rh = RHY[g_irand(4)];
        int d = g_chance(50) ? 0 : (g_chance(50) ? 2 : 4), last = -1;
        for (r = 0; r < EN_NROW; r++) {
            if (r != 0 && !(rh >> r & 1u) && !g_chance(density / 6))
                continue;                        /* off the rhythm: now and then, as density allows */
            if ((r & 3) && (rh >> r & 1u) && !g_chance(40 + density * 3 / 5))
                continue;                        /* on it, off the beat: thinned when sparse */
            if (g_chance(22))
                d += (g_chance(50) ? 1 : -1) * (2 + g_irand(rg > 2 ? 3 : 2));
            else
                d += g_chance(50) ? 1 : -1;
            if (d > rg + 4)
                d -= 3;
            if (d < -rg)
                d += 3;
            last = r;
            g_put(s, pat, r, t, GN(d), g_chance(8) ? FX_GLIDE : 0, 3);
        }
        if (last >= 0)                           /* the phrase lands on the chord: the root or the fifth */
            g_put(s, pat, last, t, GN(d > 2 ? 4 : 0), 0, 0);
        break;
    }
    case 4:                                      /* Triplet: every three rows, against the bar */
        for (r = 0, k = 0; r < EN_NROW; r += 3, k++)
            if (r == 0 || g_chance(density))
                g_put(s, pat, r, t, GN((k * (rg > 2 ? 2 : 1)) % (rg + 3)), 0, 0);
        break;
    case 5: {                                    /* Arp run: the chord's degrees, sixteenths up and down */
        static const int8_t UPDN[2][8] = {{0, 2, 4, 7, 9, 7, 4, 2}, {7, 4, 2, 0, 2, 4, 7, 9}};
        const int8_t *u = UPDN[g_irand(2)];
        int shift = g_chance(30) ? 3 : 0;
        for (r = 0; r < EN_NROW; r++)
            if (!(r & 1) || g_chance(density))
                g_put(s, pat, r, t, GN(u[r & 7] + (r >= 8 ? shift : 0)), 0, 0);
        break;
    }
    case 6:                                      /* Bleeps: short and crushed */
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

/* ------------------------------------------------------------- FOLLOW --- */
/* the other tracks written from the lead (amb.h). Pure, on its own generator like GROW: the render calls it
 * at the top of each pass, the screen to show the takes, the tests to check them. */
int en_follow_role(const en_song_t *s, int t)
{
    int lead = s->par[P_LEAD], r, snd;
    if (lead < 1 || lead > EN_NTRK || t < 0 || t >= EN_NTRK || t == lead - 1)
        return FR_OFF;
    r = s->par[P_T(t, TP_FOLLOW)];
    if (r <= FR_OFF || r >= EN_NROLE)
        return FR_OFF;
    if (r != FR_AUTO)
        return r;
    snd = s->par[P_T(t, TP_SOUND)];          /* Auto: what the sound is for */
    if (en_sound_is_kit(snd))
        return FR_DRUMS;
    if (snd >= 3 && snd <= 7)
        return FR_BASS;
    if (snd == 13)
        return FR_ARP;
    if (snd >= 14 && snd <= 16)
        return FR_CHORDS;
    if (snd == 17)
        return FR_HARMONY;
    return FR_COUNTER;
}

/* the lead in a pattern: the note sounding on each row (0: none; a note held over the loop's end counts from
 * the top), and where each begins */
typedef struct {
    uint8_t sound[EN_NROW], onset[EN_NROW];
    int ons, last, prev;                        /* how many notes; the last two begun (their direction) */
} lead_t;
static void lead_read(const en_song_t *s, int pat, int lt, lead_t *L)
{
    int r, cur = 0;
    const int len = en_pat_len(s, pat);
    L->ons = 0, L->last = 0, L->prev = 0;
    for (r = 0; r < len; r++) {                 /* what's held over from the end of the pattern */
        int n = s->cell[pat][r][lt].note;
        if (n >= NOTE_LO)
            cur = n;
        else if (n == NOTE_OFF)
            cur = 0;
    }
    for (r = 0; r < EN_NROW; r++) {
        int n = r < len ? s->cell[pat][r][lt].note : NOTE_EMPTY;
        L->onset[r] = 0;
        if (r >= len)
            cur = 0;
        if (n >= NOTE_LO) {
            cur = n;
            L->onset[r] = 1;
            L->ons++;
            L->prev = L->last;
            L->last = n;
        } else if (n == NOTE_OFF) {
            cur = 0;
        }
        L->sound[r] = (uint8_t)cur;
    }
}

/* the scale for harmony: the song's, or the major for the chromatic */
static uint32_t harm_pool(const en_song_t *s)
{
    int m = (unsigned)s->par[P_MODE] < EN_NMODES ? s->par[P_MODE] : 0;
    return m == MODE_CHROMATIC ? EN_MODE_POOL[MODE_MAJOR] : EN_MODE_POOL[m];
}

/* each half bar's chord: the degree whose triad the lead's notes fit best */
static void lead_chords(const en_song_t *s, const lead_t *L, int ch[2])
{
    const int root = s->par[P_ROOT];
    const uint32_t p = harm_pool(s);
    int st[12], n = en_scale(p, st), h, d, r;
    for (h = 0; h < 2; h++) {
        float best = -1e9f, tot = 0.0f;
        int bd = h ? ch[0] : 0;
        for (d = 0; d < n; d++) {
            int pr = st[d], p3 = st[(d + 2) % n], p5 = st[(d + 4) % n];
            float sc = d == 0 ? 0.4f + (h == 0 ? 0.2f : 0.0f) : 0.0f;
            if (n == 7 && (d == 3 || d == 4 || d == 5))   /* IV, V, vi: the common ones */
                sc += 0.2f;
            if (((p5 - pr + 12) % 12) == 6 && n == 7)   /* a diminished triad: never, in a seven-note scale */
                continue;
            tot = 0.0f;
            for (r = h * 8; r < h * 8 + 8; r++) {
                int pc, rel;
                float w;
                if (!L->sound[r])
                    continue;
                w = 1.0f + (r % 4 == 0 ? 1.0f : 0.0f) + (L->onset[r] ? 1.0f : 0.0f);
                pc = L->sound[r] % 12;
                rel = (pc - root + 12) % 12;
                tot += w;
                sc += w * (rel == pr ? 1.3f : rel == p3 || rel == p5 ? 1.0f : -0.6f);
            }
            if (sc > best)
                best = sc, bd = d;
        }
        if (tot <= 0.0f)                        /* the lead silent here: the chord before (or the tonic) */
            bd = h ? ch[0] : 0;
        ch[h] = bd;
    }
}

int en_follow_chords(const en_song_t *s, int pat, int deg[2])
{
    lead_t L;
    int lead = s->par[P_LEAD];
    deg[0] = deg[1] = 0;
    if (lead < 1 || lead > EN_NTRK)
        return 0;
    lead_read(s, pat & 15, lead - 1, &L);
    lead_chords(s, &L, deg);
    return L.ons > 0;
}

/* a degree from the tonic in a register (base: a C, the root added) */
static int f_deg(const en_song_t *s, int base, int d)
{
    return deg_move(base + s->par[P_ROOT], s->par[P_ROOT], harm_pool(s), d);
}
/* a chord's root in a register: within a fifth above the tonic there (else an octave down) */
static int f_root(const en_song_t *s, int base, int d)
{
    int n = f_deg(s, base, d);
    if (n - (base + s->par[P_ROOT]) > 6)
        n -= 12;
    return n;
}
/* d degrees above a note, in the song's scale */
static int f_up(const en_song_t *s, int note, int d) { return deg_move(note, s->par[P_ROOT], harm_pool(s), d); }
static void f_put(en_cell_t *o, int r, int note, int fx, int v)
{
    o[r & 15].note = (uint8_t)note;
    o[r & 15].fx = (uint8_t)(fx << 4 | (v & 15));
    o[r & 15].vol = 0, o[r & 15].fx2 = 0;
}

/* its own generator, on the stack: the render and the screen both call en_follow */
static uint32_t f_rnd(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}
#define F_CHANCE(pc) ((int)(f_rnd(&fr) % 100u) < (pc))
#define F_IRAND(n) ((int)(f_rnd(&fr) % (uint32_t)(n)))

void en_follow(const en_song_t *s, int pat, int t, uint32_t seed, en_cell_t o[EN_NROW])
{
    uint32_t fr;
    const int role = en_follow_role(s, t), dens = s->par[P_FDENS];
    lead_t L;
    int ch[2], r, busy, lvl;
    for (r = 0; r < EN_NROW; r++)
        o[r].note = 0, o[r].fx = 0, o[r].vol = 0, o[r].fx2 = 0;
    if (role == FR_OFF)
        return;
    pat &= 15;
    lead_read(s, pat, s->par[P_LEAD] - 1, &L);
    lead_chords(s, &L, ch);
    fr = seed * 2654435761u + 0x6A09E667u + (uint32_t)(pat * 131 + t * 17 + role);
    if (!fr)
        fr = 1u;
    busy = L.ons;                               /* the lead's notes in the bar */
    lvl = dens + (8 - busy) * 6;                /* the sparser the lead, the busier the rest */
    switch (role) {
    case FR_BASS: {                             /* the chord's root, in a rhythm that leaves the lead room */
        for (r = 0; r < EN_NROW; r++) {
            int c = ch[r / 8], root = f_root(s, 48, c);
            if (lvl < 35) {
                if (r == 0 || r == 8)
                    f_put(o, r, root, 0, 0);
                else if (r == 12 && ch[1] == ch[0] && F_CHANCE(50))
                    f_put(o, r, f_up(s, root, 4), 0, 0);
            } else if (lvl < 65) {
                if (r % 4 == 0)
                    f_put(o, r, (r % 8 == 4 && F_CHANCE(60)) ? root + 12 : root, 0, 0);
            } else if (!(r & 1)) {
                f_put(o, r, (r & 2) ? root + 12 : root, 0, 0);
            } else if (F_CHANCE(dens / 4)) {
                f_put(o, r, root + 12, 0, 0), o[r].vol = 9;
            }
        }
        if (dens > 40 && F_CHANCE(55)) {        /* a step into the next bar's chord */
            int to = f_root(s, 48, ch[0]), from = f_root(s, 48, ch[1]);
            f_put(o, lvl >= 65 ? 14 : 15, f_up(s, to, to > from ? -1 : 1), 0, 0);
        }
        break;
    }
    case FR_CHORDS: {                           /* the chord on each half bar; stabs off the beat when the lead rests */
        int shape = dens > 65 ? 3 : 1;
        f_put(o, 0, f_root(s, 60, ch[0]), FX_CHORD, shape);
        if (ch[1] != ch[0] || dens > 30)
            f_put(o, 8, f_root(s, 60, ch[1]), FX_CHORD, shape);
        for (r = 2; r < EN_NROW; r += 4)
            if (r != 8 && !L.sound[r] && F_CHANCE(lvl - 30))
                f_put(o, r, f_root(s, 60, ch[r / 8]), FX_CHORD, shape);
        break;
    }
    case FR_ARP: {                              /* sixteenths round the chord: up, or up and down */
        static const int8_t UP[2][4] = {{0, 2, 4, 7}, {0, 4, 2, 4}};
        const int8_t *u = UP[F_IRAND(2)];
        for (r = 0; r < EN_NROW; r++)
            if (!(r & 1) || F_CHANCE(dens + 20))
                f_put(o, r, f_up(s, f_root(s, 60, ch[r / 8]), u[r & 3]), 0, 0);
        break;
    }
    case FR_HARMONY: {                          /* the lead a third under (a chord tone on the strong beats) */
        int iv = F_CHANCE(70) ? -2 : 2;
        const int root = s->par[P_ROOT];
        const uint32_t p = harm_pool(s);
        for (r = 0; r < EN_NROW; r++) {
            int n = s->cell[pat][r][s->par[P_LEAD] - 1].note;
            if (n == NOTE_OFF) {
                f_put(o, r, NOTE_OFF, 0, 0);
            } else if (n >= NOTE_LO) {
                int h = deg_move(n, root, p, iv);
                if (r % 4 == 0) {               /* on the beat: the chord's tone nearest */
                    int c = ch[r / 8], k, best = h, bd = 99;
                    for (k = 0; k < 3; k++) {
                        int pc = f_deg(s, 60, c + 2 * k) % 12, o2;
                        for (o2 = -1; o2 <= 1; o2++) {
                            int cand = (h / 12 + o2) * 12 + pc, dd = cand - h;
                            if (dd < 0)
                                dd = -dd;
                            if (cand < n && dd < bd && cand >= NOTE_LO)
                                bd = dd, best = cand;
                        }
                    }
                    if (bd <= 3)
                        h = best;
                }
                f_put(o, r, h, 0, 0);
            }
        }
        break;
    }
    case FR_COUNTER: {                          /* in the lead's rests and long notes, the chord's tones, against it */
        int avg = L.last ? L.last : 72 + s->par[P_ROOT], reg, cur, dir, sounding = 0;
        if (L.ons) {
            int sum = 0, k = 0;
            for (r = 0; r < EN_NROW; r++)
                if (L.onset[r])
                    sum += L.sound[r], k++;
            avg = sum / k;
        }
        reg = avg - 12 < 52 ? 52 : avg - 12;
        cur = reg;
        dir = L.last >= L.prev ? -1 : 1;        /* against the lead's last move */
        for (r = 0; r < EN_NROW; r++) {
            int held = L.sound[r] && !L.onset[r] && r > 0 && !L.onset[r - 1];
            if (L.onset[r] && sounding) {       /* the lead speaks: the counter stops */
                f_put(o, r, NOTE_OFF, 0, 0);
                sounding = 0;
                continue;
            }
            if ((!L.sound[r] || held) && !(r & 1) && F_CHANCE(dens + 15)) {
                int c = ch[r / 8], k, best = cur, bd = 99;
                for (k = -3; k <= 6; k++) {     /* the chord tone nearest, a step the counter's way */
                    int cand = f_deg(s, 60, c + 2 * k);
                    int oc;
                    for (oc = 0; oc < 3; oc++) {
                        int nn = cand % 12 + 12 * (reg / 12 - 1 + oc), dd = (nn - cur) * dir;
                        if (dd > 0 && dd < bd && nn >= reg - 7 && nn <= reg + 12)
                            bd = dd, best = nn;
                    }
                }
                if (bd == 99)
                    dir = -dir;
                else
                    cur = best;
                f_put(o, r, cur < NOTE_LO ? NOTE_LO : cur, 0, 0);
                sounding = 1;
            }
        }
        if (!sounding) {                        /* no room in the lead: a chord tone held under each half bar */
            int any = 0;
            for (r = 0; r < EN_NROW; r++)
                any |= o[r].note >= NOTE_LO;
            if (!any) {
                f_put(o, 0, f_deg(s, reg - s->par[P_ROOT], ch[0] + 2), 0, 0);
                f_put(o, 8, f_deg(s, reg - s->par[P_ROOT], ch[1] + (ch[1] == ch[0] ? 4 : 2)), 0, 0);
            }
        }
        break;
    }
    case FR_DRUMS: {                            /* a beat that answers the lead: kicks under its strong notes */
        for (r = 0; r < EN_NROW; r++) {
            if (r == 0 || r == 8)
                f_put(o, r, 36, 0, 0);
            else if (r == 4 || r == 12)
                f_put(o, r, 37, 0, 0);
            else if (L.onset[r] && !(r & 1) && F_CHANCE(dens))
                f_put(o, r, 36, 0, 0);
            else if (L.onset[r] && (r & 1) && F_CHANCE(dens))
                f_put(o, r, 39, 0, 0);          /* the lead off the beat: an open hat */
            else if (lvl > 50 ? !(r & 1) : r % 4 == 2)
                f_put(o, r, 38, 0, 0);
            else if ((r & 1) && F_CHANCE(dens / 3))
                f_put(o, r, 38, 0, 0), o[r].vol = 6;
        }
        if (dens > 45 && !L.sound[12] && !L.sound[13] && !L.sound[14] && F_CHANCE(60)) {   /* a fill where it rests */
            f_put(o, 13, 40, 0, 0);
            f_put(o, 14, 40, 0, 0);
            f_put(o, 15, 37, FX_RATCHET, 3);
        }
        break;
    }
    default:
        break;
    }
}
#undef F_CHANCE
#undef F_IRAND

/* --------------------------------------------------------------- the songs --- */
/* five songs, each a chip's: its layers in and out over eight patterns (an intro, the bass, the drums, all of
 * it, a variation, a dip, all again, an outro), the order list playing each twice */
typedef struct {
    const char *name;
    uint8_t tempo, root, mode, echo_t, arp_t, verb, lofi, duck;
    uint8_t snd[EN_NTRK], style[EN_NTRK], dens[EN_NTRK], lvl[EN_NTRK], mask[EN_NTRK];
    int8_t deg[EN_NTRK], vib[EN_NTRK], arp[EN_NTRK], evolve[EN_NTRK], pan[EN_NTRK], echo[EN_NTRK],
        verb_[EN_NTRK], crush[EN_NTRK], tone[EN_NTRK];
    uint8_t lead, fol[EN_NTRK];                 /* Follow: the lead (1-6, 0 none), each track's role */
} recipe_t;
#define KIT_M 0xDC                               /* the drums: patterns 3, 4, 5, 7, 8 (of 1-8) */
#define BAS_M 0x5E
#define ALL_M 0xFF
#define TOP_M 0xF8
static const recipe_t SONGS[EN_NSONG] = {
    /* Overworld: NES, C major, played by Follow: a square lead, and the kit, the triangle, the pad's chords and
     * a duty lead a third under all written from it, the drums and the bass a new take now and then */
    {"Overworld", 140, 0, MODE_MAJOR, 1, 1, 30, 15, 0,
     {0, 3, 14, 8, 10, 18}, {0, 0, 1, 3, 3, 6}, {60, 50, 0, 60, 45, 20}, {70, 80, 40, 60, 40, 28},
     {KIT_M, BAS_M, ALL_M, ALL_M, 0xD0, 0x50}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 30, 20, 0}, {0, 0, 0, 0, 0, 0},
     {25, 15, 0, 0, 0, 0}, {0, 0, -20, 10, 25, -30}, {0, 0, 10, 25, 30, 40}, {5, 5, 25, 20, 25, 20},
     {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 25, 0}, 4, {FR_AUTO, FR_AUTO, FR_CHORDS, FR_OFF, FR_HARMONY, FR_OFF}},
    /* Dungeon: Game Boy, A minor, a broken beat, the wave's bass and organ, a 25% lead */
    {"Dungeon", 108, 9, MODE_AEOLIAN, 2, 1, 40, 25, 0,
     {1, 6, 16, 10, 22, 21}, {2, 2, 1, 3, 4, 6}, {55, 70, 40, 55, 40, 25}, {68, 76, 42, 56, 30, 26},
     {KIT_M, BAS_M, ALL_M, TOP_M, 0xD8, 0x90}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 25, 0, 0}, {0, 0, 4, 0, 0, 0},
     {10, 0, 0, 30, 40, 0}, {0, 0, 20, -10, 30, -25}, {0, 0, 10, 40, 30, 30}, {5, 5, 30, 25, 30, 20},
     {0, 10, 0, 0, 20, 0}, {0, 0, 0, 0, 0, 0}, 0, {0}},
    /* Starfield: SID, D lydian, a saw bass under a moving pad, a lead and its ring bells, lasers far off */
    {"Starfield", 126, 2, MODE_LYDIAN, 2, 3, 55, 10, 20,
     {2, 5, 15, 11, 17, 19}, {0, 0, 1, 3, 4, 7}, {55, 55, 40, 60, 50, 40}, {66, 72, 44, 54, 40, 26},
     {KIT_M, BAS_M, ALL_M, TOP_M, 0xF0, 0x30}, {0, 0, 0, 0, 4, 0}, {0, 0, 0, 25, 0, 0}, {0, 0, 0, 0, 0, 0},
     {0, 15, 0, 25, 40, 0}, {0, 0, -15, 10, 30, -35}, {0, 10, 20, 35, 45, 50}, {5, 5, 50, 35, 45, 40},
     {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, 0, {0}},
    /* Boss Rush: NES, E phrygian, fast: the octave bass, sixteenth arps, a thin lead, the metal */
    {"Boss Rush", 168, 4, MODE_PHRYGIAN, 1, 0, 25, 20, 0,
     {0, 7, 13, 9, 22, 19}, {5, 0, 5, 3, 2, 6}, {65, 60, 70, 65, 50, 30}, {72, 72, 38, 58, 28, 26},
     {KIT_M, BAS_M, 0xFE, TOP_M, 0xD8, 0xD0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 35, 0, 0}, {0, 0, 0, 0, 0, 0},
     {0, 0, 20, 20, 30, 0}, {0, 0, -25, 10, 25, -30}, {0, 0, 15, 25, 20, 40}, {5, 5, 15, 20, 20, 25},
     {0, 0, 0, 0, 15, 0}, {0, 0, 25, 30, 0, 0}, 0, {0}},
    /* Lullaby: Game Boy, F major, slow: a held triangle, a wave pad as a slow arp, the wave lead, the bell */
    {"Lullaby", 84, 5, MODE_MAJOR, 4, 2, 60, 30, 0,
     {1, 3, 14, 12, 17, 23}, {7, 1, 1, 3, 4, 7}, {30, 0, 50, 50, 40, 30}, {50, 72, 46, 56, 38, 22},
     {0xD8, ALL_M, ALL_M, TOP_M, 0xF8, 0x81}, {0, 0, 0, 0, 2, 0}, {0, 0, 0, 30, 0, 0}, {0, 0, 3, 0, 0, 0},
     {0, 0, 0, 35, 40, 0}, {0, 0, -20, 10, 25, 0}, {0, 0, 20, 40, 45, 30}, {10, 10, 50, 40, 55, 60},
     {0, 0, 0, 0, 0, 0}, {0, 0, -10, 0, 0, 0}, 0, {0}},
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
    s->par[P_ECHO_T] = R->echo_t, s->par[P_ARP_T] = R->arp_t, s->par[P_VERB] = R->verb, s->par[P_LOFI] = R->lofi;
    s->par[P_DUCK] = R->duck;
    s->par[P_LEAD] = R->lead;
    for (k = 0; k < 16; k++)
        s->order[k] = ORDER[k];
    for (k = 0; k < EN_NPAT; k++)
        s->len[k] = EN_NROW;
    for (t = 0; t < EN_NTRK; t++) {
        s->par[P_T(t, TP_SOUND)] = R->snd[t], s->par[P_T(t, TP_LEVEL)] = R->lvl[t];
        s->par[P_T(t, TP_VIB)] = R->vib[t], s->par[P_T(t, TP_ARP)] = R->arp[t];
        s->par[P_T(t, TP_EVOLVE)] = R->evolve[t], s->par[P_T(t, TP_PAN)] = R->pan[t];
        s->par[P_T(t, TP_ECHO)] = R->echo[t], s->par[P_T(t, TP_VERB)] = R->verb_[t];
        s->par[P_T(t, TP_CRUSH)] = R->crush[t], s->par[P_T(t, TP_TONE)] = R->tone[t];
        s->par[P_T(t, TP_FOLLOW)] = R->fol[t];
        for (pt = 0; pt < 8; pt++) {
            int r;
            if (!(R->mask[t] >> pt & 1u))
                continue;
            en_grow(s, pt, t, R->style[t], R->dens[t], 40, (uint32_t)(i * 977 + t * 101 + (pt >= 4 && pt != 7 ? 31 : 0)));
            if (R->deg[t])                       /* a harmony: the track a few degrees up */
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

int en_pat_len(const en_song_t *s, int pat)
{
    int n = s->len[pat & 15];
    return n < 1 || n > EN_NROW ? EN_NROW : n;
}

int en_song_valid(const en_song_t *s)
{
    int i, p, r, t;
    if (s->magic != EN_SONG_MAGIC || s->format != EN_SONG_FORMAT)
        return 0;
    for (i = 0; i < P_NSONG; i++)
        if (s->par[i] < PARAMS[i].lo || s->par[i] > PARAMS[i].hi)
            return 0;
    for (p = 0; p < EN_NPAT; p++)
        if (s->len[p] < 1 || s->len[p] > EN_NROW)
            return 0;
    for (p = 0; p < EN_NPAT; p++)
        for (r = 0; r < EN_NROW; r++)
            for (t = 0; t < EN_NTRK; t++) {
                const en_cell_t *c = &s->cell[p][r][t];
                int n = c->note;
                if (n != NOTE_EMPTY && n != NOTE_OFF && (n < NOTE_LO || n > NOTE_HI))
                    return 0;
                if (c->vol > 16)
                    return 0;
            }
    if (s->name[sizeof s->name - 1])
        return 0;
    return 1;
}

/* --------------------------------------------------------------- packing --- */
/* the song as saved: its header, parameters, order list and lengths as they are (little-endian), then a bitmap
 * of the cells in use (pattern, row, track order), then each used cell: its note (bit 7: it has a volume or a
 * second effect) and its effect, and then those two when it has them. 16 patterns of 16 rows of 6 four-byte
 * cells are 6 KB; packed, a song is a third of that or so, in a 3840-byte flash object. */
#define PK_NCELL (EN_NPAT * EN_NROW * EN_NTRK)
#define PK_FIXED (4 + 2 + 2 + 16 + 2 * P_NSONG + EN_NORD + EN_NPAT + PK_NCELL / 8)
static int cell_used(const en_cell_t *c) { return c->note || c->vol || c->fx || c->fx2; }
static int cell_full(const en_cell_t *c) { return c->vol || c->fx2; }

int en_song_pack(const en_song_t *s, uint8_t *b, int max)
{
    const en_cell_t *c = &s->cell[0][0][0];
    int n = 0, k;
    uint8_t *bm;
    if (max < PK_FIXED)
        return -1;
    b[n++] = (uint8_t)s->magic, b[n++] = (uint8_t)(s->magic >> 8), b[n++] = (uint8_t)(s->magic >> 16), b[n++] = (uint8_t)(s->magic >> 24);
    b[n++] = (uint8_t)s->format, b[n++] = (uint8_t)(s->format >> 8), b[n++] = 0, b[n++] = 0;
    for (k = 0; k < 16; k++)
        b[n++] = (uint8_t)s->name[k];
    for (k = 0; k < P_NSONG; k++)
        b[n++] = (uint8_t)s->par[k], b[n++] = (uint8_t)((uint16_t)s->par[k] >> 8);
    for (k = 0; k < EN_NORD; k++)
        b[n++] = s->order[k];
    for (k = 0; k < EN_NPAT; k++)
        b[n++] = s->len[k];
    bm = b + n;
    for (k = 0; k < PK_NCELL / 8; k++)
        bm[k] = 0;
    n += PK_NCELL / 8;
    for (k = 0; k < PK_NCELL; k++) {
        if (!cell_used(&c[k]))
            continue;
        bm[k >> 3] |= (uint8_t)(1u << (k & 7));
        if (n + (cell_full(&c[k]) ? 4 : 2) > max)
            return -1;
        b[n++] = (uint8_t)(c[k].note | (cell_full(&c[k]) ? 0x80u : 0u));
        b[n++] = c[k].fx;
        if (cell_full(&c[k]))
            b[n++] = c[k].vol, b[n++] = c[k].fx2;
    }
    return n;
}

int en_song_size(const en_song_t *s)
{
    const en_cell_t *c = &s->cell[0][0][0];
    int n = PK_FIXED, k;
    for (k = 0; k < PK_NCELL; k++)
        if (cell_used(&c[k]))
            n += cell_full(&c[k]) ? 4 : 2;
    return n;
}

int en_song_unpack(const uint8_t *b, int len, en_song_t *s)
{
    en_cell_t *c = &s->cell[0][0][0];
    const uint8_t *bm;
    int n = 0, k;
    uint8_t *z = (uint8_t *)s;
    for (k = 0; k < (int)sizeof *s; k++)
        z[k] = 0;
    if (len < PK_FIXED)
        return -1;
    s->magic = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    s->format = (uint16_t)(b[4] | b[5] << 8);
    n = 8;
    if (s->magic != EN_SONG_MAGIC || s->format != EN_SONG_FORMAT)
        return -1;
    for (k = 0; k < 16; k++)
        s->name[k] = (char)b[n++];
    for (k = 0; k < P_NSONG; k++, n += 2)
        s->par[k] = (int16_t)(uint16_t)(b[n] | b[n + 1] << 8);
    for (k = 0; k < EN_NORD; k++)
        s->order[k] = b[n++];
    for (k = 0; k < EN_NPAT; k++)
        s->len[k] = b[n++];
    bm = b + n;
    n += PK_NCELL / 8;
    for (k = 0; k < PK_NCELL; k++) {
        int full;
        if (!(bm[k >> 3] >> (k & 7) & 1u))
            continue;
        if (n + 2 > len)
            return -1;
        full = b[n] >> 7;
        c[k].note = (uint8_t)(b[n++] & 0x7Fu);
        c[k].fx = b[n++];
        if (full) {
            if (n + 2 > len)
                return -1;
            c[k].vol = b[n++];
            c[k].fx2 = b[n++];
        }
    }
    return n == len && en_song_valid(s) ? 0 : -1;
}
