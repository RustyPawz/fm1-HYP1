/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's chiptune (see amb.h): five chip channels and a lead, a city bed, the city's events on the grid, the
 * weather and the night in the colour of the sound. A RedtailWorks fork of AMB-1 by way of HYP-1, ECO-1 and
 * GRAV-1 (the command queue, the MIDI clock and the echo are theirs). */
#include "amb.h"
#include "fastmath.h"

#ifdef EN_HOST
#define EN_POOL
#else
#define EN_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ------------------------------------------------------------ the names --- */
const char *const EN_CH_NAME[EN_NCH] = {"Pulse A", "Pulse B", "Triangle", "Noise", "Wave", "Lead"};
const char *const EN_NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *const EN_MODE_NAME[EN_NMODES] = {"Lydian", "Ionian", "Mixolydian", "Dorian", "Aeolian"};
const char *const EN_FLAVOR_NAME[EN_NFLAVOR] = {"NES", "GB", "SID"};
static const uint8_t MODES[EN_NMODES][7] = {{0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10},
                                            {0, 2, 3, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 10}};
static const int8_t PROGS[6][4] = {{0, 5, 3, 4}, {0, 3, 4, 3}, {0, 4, 5, 3}, {5, 3, 0, 4}, {0, 2, 3, 4}, {0, 5, 1, 4}};

static const en_param_t PARAMS[P_NPARAMS] = {
    {"Speed", 0, 8, 2}, {"Start as", 0, 3, 0}, {"Traffic", 0, 20, 10}, {"People", 0, 20, 10},
    {"Tempo", 60, 160, 104}, {"Density", 0, 100, 60}, {"Swing", 0, 60, 12}, {"City bed", 0, 100, 60},
    {"Flavor", 0, EN_NFLAVOR - 1, 0}, {"Lead", 0, 3, 0}, {"Echo", 0, 150, 100}, {"Crush", 0, 100, 0},
    {"Day", 0, 4, 1}, {"Season", 0, 5, 0}, {"Days each", 1, 8, 2}, {"Lights", 0, 1, 1},
    {"Weather", 0, 6, 0}, {"Change", 1, 12, 4}, {"Birds", 0, 30, 10}, {"Chats", 0, 1, 1},
    {"Bells", 0, 150, 100}, {"Arps", 0, 150, 100}, {"Bass", 0, 150, 100}, {"Drums", 0, 150, 100},
    {"Pad", 0, 150, 100}, {"Lead", 0, 150, 100}, {"Effects", 0, 150, 100}, {"Sirens", 0, 150, 100},
    {"Lightning", 0, 20, 9}, {"Quakes", 0, 30, 6}, {"Robbery", 0, 60, 12}, {"Crashes", 0, 60, 10},
    {"UFOs", 0, 100, 3}, {"Follow", 0, 1, 1}, {"Disasters", 0, 1, 1}, {"Idle", 0, 3, 1},
    {"Key", 0, 12, 0}, {"Tune", -50, 50, 0}, {"Sync", 0, 1, 1}, {"Mood", 0, 2, 0},
    {"Theme", 0, EN_NTHEMES - 1, 0}, {"Zoom", 0, 2, 0}, {"Map", 0, 2, 0}, {"Labels", 0, 1, 1},
    {"Sends", 0, 3, 1}, {"Channels", 0, 9, 0},
    {"Seed", -32768, 32767, 0}, {"Seed", -32768, 32767, 0},
};
const en_param_t *en_param_info(int i) { return &PARAMS[(unsigned)i < P_NPARAMS ? i : 0]; }

static char *cpy(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *itoa_s(int v, char *b)
{
    char t[8];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
void en_param_text(int i, int v, char *b)
{
    static const char *const SPEED[9] = {"x1/4", "x1/2", "x1", "x2", "x4", "x8", "x16", "x32", "x64"};
    static const char *const START[4] = {"Town", "Suburb", "City", "Huge"};
    static const char *const DAY[5] = {"2 min", "4 min", "8 min", "16 min", "Noon"};
    static const char *const SEASON[6] = {"Cycle", "Calendar", "Spring", "Summer", "Autumn", "Winter"};
    static const char *const WEATHER[7] = {"Auto", "Clear", "Cloudy", "Rain", "Storm", "Fog", "Snow"};
    static const char *const LEAD[4] = {"Square", "Thin", "Wave", "Saw"};
    static const char *const IDLE[4] = {"Off", "1 min", "2 min", "5 min"};
    static const char *const MOOD[3] = {"Auto", "Bright", "Dark"};
    static const char *const THEME[EN_NTHEMES] = {"Asphalt", "Neon", "Blueprint", "Sunset", "Terminal", "Mono"};
    static const char *const ZOOM[3] = {"Detail", "Close", "Whole"};
    static const char *const MAP[3] = {"City", "Zones", "Value"};
    static const char *const SENDS[4] = {"Off", "Notes", "Both", "CCs"};
    switch (i) {
    case P_SPEED: cpy(b, SPEED[(unsigned)v < 9u ? v : 2]); return;
    case P_START: cpy(b, START[v & 3]); return;
    case P_TRAFFIC: case P_PEOPLE: case P_BIRDS: b = itoa_s(v * 10, b); cpy(b, "%"); return;
    case P_TEMPO: itoa_s(v, b); return;
    case P_SWING: b = itoa_s(v / 2, b); cpy(b, "%"); return;
    case P_FLAVOR: cpy(b, EN_FLAVOR_NAME[(unsigned)v < EN_NFLAVOR ? v : 0]); return;
    case P_LEAD: cpy(b, LEAD[v & 3]); return;
    case P_DAYLEN: cpy(b, DAY[(unsigned)v < 5u ? v : 1]); return;
    case P_SEASON: cpy(b, SEASON[(unsigned)v < 6u ? v : 0]); return;
    case P_SEASONDAYS: b = itoa_s(v, b); cpy(b, v == 1 ? " day" : " days"); return;
    case P_NIGHT: case P_CHATS: case P_FOLLOW: case P_DISASTER: case P_LABELS: cpy(b, v ? "On" : "Off"); return;
    case P_SYNC: cpy(b, v ? "Clock" : "Off"); return;
    case P_WEATHER: cpy(b, WEATHER[(unsigned)v < 7u ? v : 0]); return;
    case P_WPACE: {
        int q = v * 25;                          /* x0.25 .. x3 */
        b = cpy(b, "x");
        b = itoa_s(q / 100, b);
        if (q % 100) {
            *b++ = '.';
            b = itoa_s(q % 100 == 50 ? 5 : q % 100, b);
        }
        return;
    }
    case P_LIGHTNING: b = itoa_s(v * 5, b); cpy(b, "%"); return;
    case P_QUAKES: case P_ROBBERY: case P_CRASH:
        if (!v) { cpy(b, "Off"); return; }
        b = itoa_s(v, b);
        cpy(b, "/hr");
        return;
    case P_UFO:
        if (!v) { cpy(b, "Off"); return; }
        b = itoa_s(v / 10, b);
        *b++ = '.';
        b = itoa_s(v % 10, b);
        cpy(b, "/hr");
        return;
    case P_IDLE: cpy(b, IDLE[v & 3]); return;
    case P_KEY: cpy(b, v ? EN_NOTE_NAME[(v - 1) % 12] : "Seed"); return;
    case P_TUNE: if (v > 0) *b++ = '+'; b = itoa_s(v, b); cpy(b, "c"); return;
    case P_MOOD: cpy(b, MOOD[(unsigned)v < 3u ? v : 0]); return;
    case P_THEME: cpy(b, THEME[(unsigned)v < EN_NTHEMES ? v : 0]); return;
    case P_ZOOM: cpy(b, ZOOM[(unsigned)v < 3u ? v : 0]); return;
    case P_MAP: cpy(b, MAP[(unsigned)v < 3u ? v : 0]); return;
    case P_MIDI: cpy(b, SENDS[v & 3]); return;
    case P_MCH: b = itoa_s(v + 1, b); *b++ = '-'; itoa_s(v + 7, b); return;
    default: break;
    }
    b = itoa_s(v, b);
    if (i != P_SEED_HI && i != P_SEED_LO)
        cpy(b, "%");
}

/* ------------------------------------------------------- the pure parts --- */
int en_snap(int m, uint16_t pcs)
{
    static const int8_t D[5] = {0, 1, -1, 2, -2};
    int k;
    for (k = 0; k < 5; k++)
        if (pcs >> (((m + D[k]) % 12 + 12) % 12) & 1u)
            return m + D[k];
    return m;
}
uint32_t en_hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLAY, C_RESET, C_PANIC, C_MUTE, C_CLOCK, C_EVENT, C_LEAD, C_LEADOFF, C_OCT };
#define QN 256u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void en_set(int p, int v) { post(C_SET, p, v); }
void en_play(int on) { post(C_PLAY, on, 0); }
void en_reset(void) { post(C_RESET, 0, 0); }
void en_panic(void) { post(C_PANIC, 0, 0); }
void en_mute(int on) { post(C_MUTE, on, 0); }
void en_clock(int msg) { post(C_CLOCK, msg, 0); }
void en_event(int type, int a, int b) { post(C_EVENT, type, (a & 0xFF) | (b & 0xFF) << 8); }
void en_lead(int id, int semi) { post(C_LEAD, id, semi); }
void en_lead_off(int id) { post(C_LEADOFF, id, 0); }
void en_lead_oct(int oct) { post(C_OCT, oct + 2, 0); }

/* -------------------------------------------------------------- state --- */
en_city_t en_city;
volatile uint8_t en_playing, en_ext, en_step, en_root, en_mode = 1, en_chord;
volatile uint32_t en_bars, en_notes, en_hits, en_events, en_dropped;
volatile uint16_t en_pcs = 0xAB5u;
volatile float en_glow[EN_NCH];
volatile uint8_t en_last[EN_NCH];
volatile float en_bpm = 104.0f;
volatile uint8_t en_bus_on;
#ifdef EN_HOST
float en_bus[EN_NBUS * 1024];
#else
float en_bus[EN_NBUS];
#endif

static int16_t par[P_NPARAMS];
static uint8_t muted;
static float mute_g = 1.0f, tunefac = 1.0f;
static uint32_t rng = 0x9E3779B9u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static float frand(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
static int irand(int n) { return n > 0 ? (int)((rnd() >> 8) % (uint32_t)n) : 0; }
static float mtof(float n) { return 440.0f * tunefac * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static float lvl(int v) { return (float)v * 0.01f; }

/* the music's place: key, mode, the progression and its chord, the stage */
static int key = 0, transpose = 0, mode = 1, prog = 0, chord = 0, stage = 0, pending = -1, lead_oct = 0;
static uint32_t now_s;                           /* samples since boot (wraps; compared by differences) */
static int root_pc(void) { return ((par[P_KEY] ? par[P_KEY] - 1 : key) + transpose) % 12; }
static int note_of(int deg, int oct)
{
    int d = ((deg % 7) + 7) % 7, o = (deg - d) / 7;
    return 48 + root_pc() + MODES[mode][d] + 12 * (o + oct);
}
static int chord_tone(int k, int oct) { return note_of(PROGS[prog][chord] + k, oct); }
static uint16_t scale_pcs(void)
{
    uint16_t m = 0;
    int k;
    for (k = 0; k < 7; k++)
        m |= (uint16_t)(1u << ((MODES[mode][k] + root_pc()) % 12));
    return m;
}

/* ------------------------------------------------------------ voices --- */
enum { WV_PULSE, WV_TRI, WV_WAVE, WV_SAW, WV_NOISE, WV_SINE };
#define NV 32
typedef struct {
    uint8_t on, bus, wave, midi_ch, midi_n, held;
    int8_t lead_id;
    float duty, ph, inc, inc_to, slide_k, vel, g, a_inc, r_dec, pan, vib, vib_ph;
    uint32_t wait, t, hold_at, end_at;           /* samples: before it starts; since; when the release begins; the end */
    uint16_t lfsr;
    uint8_t lshort;
    float nacc, nrate, nval, hp_a, hp_z, hp_x, lp_a, lp_z;
} voice_t;
static voice_t vo[NV];
static const uint8_t CAPS[EN_NCH] = {6, 6, 4, 8, 8, 4};
static float bus_level[EN_NCH] = {1, 1, 1, 1, 1, 1};

static voice_t *voice_get(int bus)
{
    int k, n = 0, old = -1;
    uint32_t oldt = 0;
    for (k = 0; k < NV; k++)
        if (vo[k].on && vo[k].bus == bus) {
            n++;
            if (vo[k].lead_id < 0 && (old < 0 || vo[k].t > oldt)) {
                old = k;
                oldt = vo[k].t;
            }
        }
    if (n >= CAPS[bus] && old >= 0) {            /* the channel's full: its oldest goes */
        if (vo[old].midi_n)
            en_midi_out(0x80u | vo[old].midi_ch, vo[old].midi_n, 0);
        vo[old].on = 0;
        en_dropped++;
    }
    for (k = 0; k < NV; k++)
        if (!vo[k].on)
            return &vo[k];
    return 0;
}
static float coef(float fc) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(fc, 18000.0f) * (1.0f / EN_SR)); }
/* MIDI: each channel on MCH + its number */
static int sends_notes(void) { return !muted && (par[P_MIDI] == EN_MIDI_NOTES || par[P_MIDI] == EN_MIDI_BOTH); }
static void note_midi(voice_t *v, int ch, int n, float vel)
{
    if (!sends_notes() || n <= 0 || n > 127)
        return;
    v->midi_ch = (uint8_t)((par[P_MCH] + ch) & 15);
    v->midi_n = (uint8_t)n;
    en_midi_out(0x90u | v->midi_ch, (uint32_t)n, (uint32_t)fm_clampf(vel * 500.0f + 30.0f, 1.0f, 127.0f));
}
static void vzero(voice_t *v)
{
    uint32_t *p = (uint32_t *)v, k;
    for (k = 0; k < sizeof *v / 4u; k++)
        p[k] = 0;
}
/* a tone: at `at` samples from now, `dur` seconds, its attack, and the time held before the release */
static voice_t *tone(int bus, int wave, float duty, int at, float midi, float dur, float vel, float a, float hold, float slide_to)
{
    voice_t *v;
    uint32_t len;
    if (vel <= 0.002f || dur <= 0.0f)
        return 0;
    v = voice_get(bus);
    if (!v)
        return 0;
    vzero(v);
    v->on = 1;
    v->bus = (uint8_t)bus;
    v->wave = (uint8_t)wave;
    v->duty = duty;
    v->lead_id = -1;
    v->wait = (uint32_t)(at < 0 ? 0 : at);
    v->inc = mtof(midi) * (1.0f / EN_SR);
    v->inc_to = slide_to > 0.0f ? mtof(slide_to) * (1.0f / EN_SR) : v->inc;
    len = (uint32_t)(dur * EN_SR);
    v->slide_k = slide_to > 0.0f ? fm_powf(v->inc_to / v->inc, 1.0f / (float)(len * 9u / 10u + 1u)) : 1.0f;
    v->vel = vel;
    if (a < 0.003f)
        a = 0.003f;
    v->a_inc = vel / (a * EN_SR);
    v->end_at = len;
    v->hold_at = hold > 0.0f ? (uint32_t)(fm_maxf(a, dur - hold) * EN_SR) : (uint32_t)(a * EN_SR);
    v->r_dec = vel / (float)(len > v->hold_at ? len - v->hold_at : 1u);
    v->pan = bus == CH_PA ? 0.22f : bus == CH_PB ? -0.22f : 0.0f;
    v->lfsr = (uint16_t)(rnd() | 1u);
    en_glow[bus] = 1.0f;
    en_last[bus] = (uint8_t)midi;
    en_notes++;
    if (wave != WV_NOISE)
        note_midi(v, bus, (int)(midi + 0.5f), vel);
    return v;
}
static voice_t *noise(int at, float dur, float vel, float rate, int shortl, float hp, float lp, float a, int drum)
{
    voice_t *v = tone(CH_NOI, WV_NOISE, 0.0f, at, 60.0f, dur, vel, a, 0.0f, 0.0f);
    if (!v)
        return 0;
    v->nrate = rate * 0.5f;
    v->lshort = (uint8_t)shortl;
    v->hp_a = hp > 0.0f ? coef(hp) : 0.0f;
    v->lp_a = lp > 0.0f ? coef(lp) : 0.0f;
    v->pan = (frand() - 0.5f) * 0.5f;
    en_hits++;
    if (drum)
        note_midi(v, CH_NOI, drum, vel);
    return v;
}
static float s16(void) { return 60.0f / fm_maxf(en_bpm, 20.0f) * 0.25f; }
static int samp(float sec) { return (int)(sec * EN_SR); }
static float duty_of(void) { return en_city.bright > 0.6f ? 0.125f : en_city.bright > 0.3f ? 0.25f : 0.5f; }
static int pad_wave(void) { return par[P_FLAVOR] == 1 ? WV_WAVE : par[P_FLAVOR] == 2 ? WV_SAW : WV_PULSE; }
static int bass_wave(void) { return par[P_FLAVOR] == 2 ? WV_PULSE : WV_TRI; }

static void all_off(void)
{
    int k;
    for (k = 0; k < NV; k++) {
        if (vo[k].on && vo[k].midi_n)
            en_midi_out(0x80u | vo[k].midi_ch, vo[k].midi_n, 0);
        vo[k].on = 0;
    }
}

/* ---------------------------------------------------------------- the lead --- */
static int lead_note(int semi) { return en_snap(53 + 12 * lead_oct + semi, scale_pcs()); }
static void lead_on(int id, int semi)
{
    voice_t *v;
    int k, w = par[P_LEAD];
    float lv = lvl(par[P_VLEAD]) * 0.16f;
    for (k = 0; k < NV; k++)
        if (vo[k].on && vo[k].lead_id == id) {
            vo[k].held = 0;
            vo[k].hold_at = vo[k].t;
            vo[k].end_at = vo[k].t + samp(0.08f);
            vo[k].r_dec = vo[k].g / (float)samp(0.08f);
            vo[k].lead_id = -1;
        }
    v = tone(CH_LEAD, w == 2 ? WV_WAVE : w == 3 ? WV_SAW : WV_PULSE, w == 1 ? 0.125f : 0.5f, 0, (float)lead_note(semi), 60.0f, lv, 0.005f, 0.0f, 0.0f);
    if (!v)
        return;
    v->lead_id = (int8_t)id;
    v->held = 1;
    v->hold_at = 0xFFFFFFFFu;
    v->end_at = 0xFFFFFFFFu;
    v->vib = 0.0f;
    v->pan = 0.0f;
}
static void lead_off(int id)
{
    int k;
    for (k = 0; k < NV; k++)
        if (vo[k].on && vo[k].lead_id == id) {
            uint32_t r = (uint32_t)samp(0.08f);
            vo[k].held = 0;
            vo[k].lead_id = -1;
            vo[k].hold_at = vo[k].t;
            vo[k].end_at = vo[k].t + r;
            vo[k].r_dec = vo[k].g / (float)r;
        }
}

/* ----------------------------------------------------------- the motifs --- */
/* the landmarks' motifs (city.h CV_*: town hall, gas, fire, police, school, hospital, station, stadium) */
static const struct { int8_t n[4][2]; uint8_t len; float d; } MOTIF[8] = {
    {{{0, 0}, {2, 1}, {4, 2}, {7, 4}}, 4, 0.12f},
    {{{7, 0}, {9, 1}}, 2, 0.06f},
    {{{7, 0}, {4, 1}, {7, 2}, {4, 3}}, 4, 0.08f},
    {{{0, 0}, {3, 2}, {0, 4}, {3, 6}}, 4, 0.1f},
    {{{4, 0}, {2, 2}, {4, 4}, {2, 6}}, 4, 0.18f},
    {{{5, 0}, {3, 4}}, 2, 0.34f},
    {{{4, 0}, {2, 2}, {0, 4}}, 3, 0.22f},
    {{{0, 0}, {4, 2}, {7, 4}}, 3, 0.1f},
};
static void motif(int kind, int at)
{
    int k;
    float sx = s16(), lv = lvl(par[P_VBELLS]);
    if ((unsigned)kind >= 8u)
        return;
    for (k = 0; k < MOTIF[kind].len; k++)
        tone(CH_PA, WV_PULSE, 0.25f, at + samp((float)MOTIF[kind].n[k][1] * sx), (float)note_of(MOTIF[kind].n[k][0], 2), MOTIF[kind].d,
             0.13f * lv, 0, 0, 0);
    if (kind == 7)                               /* the stadium: the crowd */
        noise(at, 1.4f, 0.14f * lvl(par[P_VFX]), 0.5f, 0, 400.0f, 0.0f, 0.4f, 0);
}
/* a moodlet's two notes: its interval */
static const int8_t CHIRP[13][3] = {{0, 4, 99}, {0, 7, 99}, {0, 2, 4}, {0, 12, 99}, {0, 2, 99}, {0, -1, 99}, {7, 99, 99},
                                    {0, 2, 99}, {0, -5, 99}, {0, 7, 12}, {0, -2, 99}, {0, 5, 99}, {0, 4, 7}};

/* ---------------------------------------------------------------- events --- */
#define NEVQ 32
static struct { uint8_t type, a, b; uint32_t at; } evq[NEVQ];
static uint32_t evq_w, evq_r;
static void ev_push(int type, int a, int b)
{
    if (evq_w - evq_r >= NEVQ)
        evq_r++;
    evq[evq_w % NEVQ].type = (uint8_t)type;
    evq[evq_w % NEVQ].a = (uint8_t)a;
    evq[evq_w % NEVQ].b = (uint8_t)b;
    evq[evq_w % NEVQ].at = now_s;
    evq_w++;
}
static void play_event(int type, int a, int b, int at, int s)
{
    float sx = s16(), fx = lvl(par[P_VFX]);
    int k;
    switch (type) {
    case E_RES: {
        static const int8_t T[5] = {0, 2, 4, 2, 4};
        tone(CH_PB, WV_PULSE, duty_of(), at, (float)chord_tone(T[(a - 1) % 5], a >= 4 ? 2 : 1), 0.13f, 0.14f * lvl(par[P_VARPS]), 0, 0, 0);
        break;
    }
    case E_COM: {
        static const int8_t T[3] = {0, 2, 4};
        int deg = b & 7, big = b >> 7;
        if (s % 4 == 0)
            deg = T[deg % 3];
        tone(CH_PA, WV_PULSE, duty_of(), at, (float)note_of(PROGS[prog][chord] + deg, big || a >= 4 ? 3 : 2), 0.07f, 0.12f * lvl(par[P_VBELLS]), 0, 0, 0);
        break;
    }
    case E_IND:
        noise(at, 0.14f, 0.2f * lvl(par[P_VDRUMS]), 0.35f, 0, 0, 0, 0, 0);
        tone(CH_TRI, bass_wave(), 0.5f, at, (float)chord_tone(0, -1), 0.09f, 0.25f * lvl(par[P_VBASS]), 0, 0, 0);
        break;
    case E_GREEN: {
        static const int8_t T[3] = {0, 2, 4};
        tone(CH_WAV, pad_wave(), 0.25f, at, (float)chord_tone(T[a % 3], 1), 0.9f, 0.08f * lvl(par[P_VPAD]), 0.18f, 0.5f, 0);
        break;
    }
    case E_CIVIC: motif(a, at); break;
    case E_HAT: noise(at, 0.025f, 0.1f * lvl(par[P_VDRUMS]), 2.5f, 1, 7000.0f, 0, 0, 42); break;
    case E_SNARE: noise(at, 0.1f, 0.2f * lvl(par[P_VDRUMS]), 1.0f, 0, 1500.0f, 0, 0, 38); break;
    case E_TOM:
        for (k = 0; k < 3; k++)
            tone(CH_TRI, WV_TRI, 0.5f, at + samp((float)k * sx * 0.5f), (float)(47 - k * 3), 0.1f, 0.35f * lvl(par[P_VDRUMS]), 0, 0, (float)(40 - k * 3));
        break;
    case E_TUNK: tone(CH_TRI, WV_TRI, 0.5f, at, (float)chord_tone(0, 1), 0.06f, 0.3f * lvl(par[P_VBASS]), 0, 0, 0); break;
    case E_COIN:
        tone(CH_PA, WV_PULSE, 0.25f, at, (float)chord_tone(4, 2), 0.05f, 0.1f * lvl(par[P_VBELLS]), 0, 0, 0);
        tone(CH_PA, WV_PULSE, 0.25f, at + samp(sx * 0.5f), (float)chord_tone(0, 3), 0.08f, 0.1f * lvl(par[P_VBELLS]), 0, 0, 0);
        break;
    case E_RISE: {
        static const int8_t T[5] = {0, 2, 4, 7, 9};
        for (k = 0; k < 5; k++)
            tone(CH_PA, WV_PULSE, 0.125f, at + samp((float)k * sx * 0.5f), (float)chord_tone(T[k], 2), 0.07f, 0.11f * lvl(par[P_VBELLS]), 0, 0, 0);
        break;
    }
    case E_JINGLE: {
        static const int8_t T[5] = {0, 2, 4, 6, 7};
        for (k = 0; k < 5; k++)
            tone(CH_PA, WV_PULSE, 0.25f, at + samp((float)k * sx), (float)note_of(PROGS[prog][chord] + T[k], 2), 0.12f, 0.12f * lvl(par[P_VBELLS]), 0, 0, 0);
        break;
    }
    case E_CHIRP: {
        int base = chord_tone(0, 2) + 12;
        for (k = 0; k < 3 && (unsigned)a < 13u && CHIRP[a][k] != 99; k++)
            tone(CH_PB, WV_PULSE, 0.25f, at + samp((float)k * sx * 0.5f), (float)(base + CHIRP[a][k]), 0.06f, 0.06f * lvl(par[P_VARPS]), 0, 0, 0);
        break;
    }
    case E_SOLO: {
        int w = par[P_LEAD];
        tone(CH_LEAD, w == 2 ? WV_WAVE : w == 3 ? WV_SAW : WV_PULSE, w == 1 ? 0.125f : 0.5f, at, (float)note_of(a - 3, 2), sx * 1.8f,
             0.1f * lvl(par[P_VLEAD]), 0.006f, sx * 0.6f, 0);
        break;
    }
    default:
        (void)fx;
        break;
    }
}

/* the cues: now, not on the grid */
static float gate_depth, gate_to, gate_until;    /* the quake's tremolo */
static struct { uint8_t on; float ph, inc, inc_to, g, g_to, vph; uint32_t next; float base; } ufo_v;
static void cue(int type, int a)
{
    float fx = lvl(par[P_VFX]), sx = s16();
    int k;
    switch (type) {
    case E_LIGHTNING: noise(0, 0.35f, 0.32f * fx, 1.6f, 0, 600.0f, 0, 0, 0); break;
    case E_THUNDER: noise(0, 2.6f, 0.5f * fx, 0.22f, 0, 0, 320.0f, 0.25f, 0); break;
    case E_QUAKE: {
        float d = (float)a * 0.1f;
        noise(0, d, 0.55f * fx, 0.18f, 0, 0, 180.0f, 0.4f, 0);
        tone(CH_TRI, WV_TRI, 0.5f, 0, 28.0f, d, 0.4f * fx, 0.3f, 0, 0);
        gate_to = 0.5f;
        gate_until = (float)d;
        break;
    }
    case E_CRUMBLE: noise(0, 0.7f, 0.25f * fx, 0.45f, 0, 0, 900.0f, 0, 0); break;
    case E_SWEEP: noise(0, 0.6f, 0.05f * fx, 1.5f, 0, 4000.0f, 0, 0.3f, 0); break;
    case E_DUSK: {
        static const int8_t T[4] = {7, 4, 2, 0};
        for (k = 0; k < 4; k++)
            tone(CH_PA, WV_PULSE, 0.25f, samp((float)k * sx * 2.0f), (float)note_of(T[k], 2), 0.25f, 0.09f * lvl(par[P_VBELLS]), 0, 0.1f, 0);
        break;
    }
    case E_CRASH:
        noise(0, 0.25f, 0.4f * fx, 0.7f, 0, 0, 0, 0, 49);
        noise(samp(0.02f), 0.4f, 0.2f * fx, 3.0f, 1, 3000.0f, 0, 0, 0);
        tone(CH_TRI, WV_TRI, 0.5f, 0, 40.0f, 0.2f, 0.4f * fx, 0, 0, 26.0f);
        break;
    case E_BEAM:
        for (k = 0; k < 12; k++)
            tone(CH_PA, WV_PULSE, 0.125f, samp((float)k * 0.09f), (float)note_of(k * 2, 2), 0.08f, 0.06f * fx, 0, 0, 0);
        noise(0, 3.5f, 0.06f * fx, 0.6f, 0, 2000.0f, 0, 1.5f, 0);
        break;
    case E_UFO_ON:
        ufo_v.on = 1;
        ufo_v.base = (float)chord_tone(0, 3);
        ufo_v.inc = ufo_v.inc_to = mtof(ufo_v.base) * (1.0f / EN_SR);
        ufo_v.g_to = 0.1f * fx;
        ufo_v.next = now_s;
        break;
    case E_UFO_OFF: ufo_v.g_to = 0.0f; break;
    case E_STAGE: pending = a; break;
    default: break;
    }
}

/* ---------------------------------------------------------------- the bed --- */
static int euclid(int k, int n, int s, int rot) { return k > 0 && (((s + rot) % n) * k) % n < k; }
static void new_bar(int at)
{
    float ms = en_city.mood;
    int every, k, n = 0;
    int tones[6];
    en_bars++;
    if (pending >= 0) {                          /* a new stage: up a fourth */
        stage = pending;
        pending = -1;
        transpose = (transpose + 5) % 12;
        noise(at, 0.9f, 0.18f * lvl(par[P_VFX]), 0.8f, 0, 2500.0f, 0, 0, 49);
    }
    if (en_bars % 2u == 0u)
        mode = ms > 0.5f ? 0 : ms > 0.12f ? 1 : ms > -0.12f ? 2 : ms > -0.45f ? 3 : 4;
    every = stage == 0 ? 2 : 1;
    if (en_bars % (uint32_t)every == 0u)
        chord = (chord + 1) % 4;
    en_mode = (uint8_t)mode;
    en_chord = (uint8_t)chord;
    en_root = (uint8_t)root_pc();
    en_pcs = scale_pcs();
    if (en_bars % (uint32_t)every == 0u) {       /* the pad: the chord */
        float dur = s16() * 16.0f * (float)every * 0.98f, v;
        tones[n++] = 0;
        if (stage > 0)
            tones[n++] = 2;
        tones[n++] = 4;
        if (en_city.tension)
            tones[n++] = 1;
        if (stage == 3)
            tones[n++] = 6;
        v = (0.06f + 0.025f * (float)stage) * lvl(par[P_BED]) * lvl(par[P_VPAD]) * (par[P_FLAVOR] == 2 ? 0.6f : 1.0f) *
            (1.0f + 0.5f * (1.0f - en_city.day) + 0.4f * en_city.fog);
        for (k = 0; k < n; k++)
            tone(CH_WAV, pad_wave(), 0.25f, at, (float)chord_tone(tones[k], 0), dur, v, 0.3f, 0.5f, 0);
    }
}

static void env_step(int at, int s)
{
    float sx = s16(), fx = lvl(par[P_VFX]), sir = lvl(par[P_VSIREN]);
    if (en_city.birds && frand() < fm_minf(0.45f, 0.05f * (float)en_city.birds)) {
        static const int8_t T[4] = {0, 2, 4, 7};
        int b = note_of(PROGS[prog][chord] + T[irand(4)], 3) + 12;
        if (en_city.gulls && frand() < 0.5f)
            tone(CH_PB, WV_PULSE, 0.25f, at, (float)(b - 5), 0.14f, 0.05f * fx, 0, 0, (float)(b - 12));
        else {
            tone(CH_PB, WV_PULSE, 0.125f, at, (float)b, 0.04f, 0.05f * fx, 0, 0, 0);
            tone(CH_PB, WV_PULSE, 0.125f, at + samp(sx * 0.33f), (float)(b + 2), 0.04f, 0.045f * fx, 0, 0, 0);
        }
    }
    if (en_city.snow > 0.3f && frand() < 0.06f) {
        static const int8_t T[3] = {0, 2, 4};
        tone(CH_PA, WV_PULSE, 0.125f, at, (float)(chord_tone(T[irand(3)], 3) + 12), 0.2f, 0.045f * fx, 0, 0.1f, 0);
    }
    if (en_city.fires && frand() < 0.5f)
        noise(at, 0.015f + frand() * 0.03f, (0.05f + 0.03f * fm_minf(3.0f, (float)en_city.fires)) * fx, 1.0f + frand() * 2.0f, frand() < 0.5f, 1800.0f, 0, 0, 0);
    if (en_city.siren_fire && s % 8 == 0) {
        int hi = s % 16 == 0;
        tone(CH_PA, WV_PULSE, 0.25f, at, (float)chord_tone(hi ? 4 : 2, 3), sx * 7.5f, 0.07f * sir, 0, 0.05f, (float)chord_tone(hi ? 2 : 4, 3));
    }
    if (en_city.siren_police && s % 16 == 0) {
        tone(CH_PA, WV_PULSE, 0.5f, at, (float)chord_tone(0, 3), sx * 8.0f, 0.06f * sir, 0, 0, (float)chord_tone(4, 3));
        tone(CH_PA, WV_PULSE, 0.5f, at + samp(sx * 8.0f), (float)chord_tone(4, 3), sx * 8.0f, 0.06f * sir, 0, 0, (float)chord_tone(0, 3));
    }
    if (en_city.siren_amb && s % 4 == 0) {
        int hi = s % 8 == 0;
        tone(CH_PB, WV_PULSE, 0.25f, at, (float)(chord_tone(hi ? 4 : 2, 3) + 12), sx * 3.6f, 0.05f * sir, 0, 0.03f, 0);
    }
    if (en_city.alarm)
        tone(CH_PB, WV_PULSE, 0.125f, at, (float)(chord_tone(s % 2 ? 4 : 5, 3) + 12), sx * 0.9f, 0.04f * sir, 0, 0, 0);
}

static void step(int at, int s)
{
    float bed = lvl(par[P_BED]), sx = s16(), inten = fm_minf(1.0f, en_city.rush);
    static const uint8_t KICK[4] = {1, 2, 4, 4}, HAT[4] = {0, 4, 6, 9};
    static const uint8_t BASS_N[4] = {1, 2, 4, 8};
    static const uint8_t BASS[4][8] = {{0}, {0, 8}, {0, 6, 8, 14}, {0, 2, 4, 6, 8, 10, 12, 14}};
    int st = stage, hatk, k, used = 0, played = 0;
    en_step = (uint8_t)s;
    if (s == 0)
        new_bar(at);
    if (euclid(KICK[st], 16, s, 0))
        tone(CH_TRI, WV_TRI, 0.5f, at, 50.0f, 0.13f, 0.5f * bed * lvl(par[P_VDRUMS]), 0, 0, 30.0f);
    hatk = st == 0 ? 0 : (int)((float)HAT[st] * (0.6f + 0.4f * inten) + 0.5f);
    if (euclid(hatk, 16, s, 2))
        noise(at, 0.03f, 0.12f * bed * lvl(par[P_VDRUMS]), 2.0f, 1, 6000.0f, 0, 0, 42);
    if (st >= 2 && (s == 4 || s == 12))
        noise(at, 0.12f, 0.26f * bed * lvl(par[P_VDRUMS]), 1.0f, 0, 1400.0f, 0, 0, 38);
    if (st >= 3 && s == 14 && frand() < 0.4f)
        noise(at, 0.06f, 0.12f * bed * lvl(par[P_VDRUMS]), 1.0f, 0, 1800.0f, 0, 0, 38);
    env_step(at, s);
    for (k = 0; k < BASS_N[st]; k++)
        if (BASS[st][k] == s) {
            int nx = k + 1 < BASS_N[st] ? BASS[st][k + 1] : 16;
            float len = (float)(nx - s) * sx * (st == 0 ? 2.0f : 1.0f);
            int oct = st == 3 && s % 4 == 2 ? 0 : -1;
            tone(CH_TRI, bass_wave(), 0.5f, at, (float)chord_tone(0, oct), len * 0.92f, 0.42f * bed * lvl(par[P_VBASS]), 0, 0.03f, 0);
        }
    if (pending >= 0 && s >= 12) {               /* the bar before a new stage: the fill */
        static const int8_t T[4] = {0, 2, 4, 7};
        noise(at, 0.07f, (0.1f + 0.05f * (float)(s - 12)) * lvl(par[P_VDRUMS]), 1.0f, 0, 1200.0f, 0, 0, 38);
        tone(CH_PA, WV_PULSE, 0.25f, at, (float)chord_tone(T[s - 12], 2), 0.08f, 0.12f * lvl(par[P_VBELLS]), 0, 0, 0);
    }
    while (evq_r != evq_w && played < 2 + st) {  /* the city's events: a few a step, one of a kind (the landmarks all) */
        int i = (int)(evq_r % NEVQ), t = evq[i].type;
        evq_r++;
        if (now_s - evq[i].at > (uint32_t)(0.9f * EN_SR))
            continue;
        if ((used >> t & 1) && t != E_CIVIC)
            continue;
        used |= 1 << t;
        played++;
        en_events++;
        play_event(t, evq[i].a, evq[i].b, at, s);
    }
}

/* ------------------------------------------------------------- the clock --- */
static float step_frac;
static uint32_t step_count;
static float ext_steps, tick_gap;
static uint32_t ext_age, since_tick;
static void play_start(void)
{
    en_playing = 1;
    step_count = 0xFFFFFFFFu;
    step_frac = 1.0f - (float)par[P_SWING] * 0.005f - 0.001f;   /* step one at once */
}
static void play_stop(void)
{
    int k;
    en_playing = 0;
    for (k = 0; k < NV; k++)                     /* the tails ring on; held notes let go */
        if (vo[k].on && vo[k].lead_id < 0 && vo[k].end_at > vo[k].t + (uint32_t)samp(0.4f)) {
            vo[k].hold_at = vo[k].t;
            vo[k].end_at = vo[k].t + (uint32_t)samp(0.4f);
            vo[k].r_dec = vo[k].g / (float)samp(0.4f);
        }
    evq_r = evq_w;
}

/* ---------------------------------------------------------------- the mix --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], esend[BLK];
#ifdef EN_HOST
static float busb[EN_NBUS][BLK];
static uint8_t bus_on;
#define BUS(b, j, x) do { if (bus_on) busb[b][j] += (x); } while (0)
#else
#define BUS(b, j, x) do { (void)(b); } while (0)
#endif
static const float WAVE_GB[32] = {8, 11, 13, 14, 15, 15, 14, 13, 11, 8, 6, 4, 3, 2, 1, 1, 2, 3, 5, 7, 9, 10, 10, 9, 8, 7, 6, 6, 7, 8, 8, 8};
static float blep(float t, float dt)
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
static const float BUS_G[EN_NCH] = {0.55f, 0.5f, 0.8f, 0.5f, 0.45f, 0.6f};
static void render_voices(uint32_t n)
{
    int k;
    for (k = 0; k < NV; k++) {
        voice_t *v = &vo[k];
        uint32_t j;
        float bl, gl, gr;
        if (!v->on)
            continue;
        bl = BUS_G[v->bus] * bus_level[v->bus];
        gl = bl * (1.0f - v->pan);
        gr = bl * (1.0f + v->pan);
        for (j = 0; j < n; j++) {
            float x, ph;
            if (v->wait) {
                v->wait--;
                continue;
            }
            if (v->t < v->hold_at) {             /* attack, then held */
                v->g += v->a_inc;
                if (v->g > v->vel)
                    v->g = v->vel;
            } else {
                v->g -= v->r_dec;
                if (v->t >= v->end_at || v->g <= 0.0f) {
                    v->on = 0;
                    if (v->midi_n)
                        en_midi_out(0x80u | v->midi_ch, v->midi_n, 0);
                    break;
                }
            }
            v->t++;
            ph = v->ph;
            switch (v->wave) {
            case WV_PULSE: {
                float d = v->duty, t2 = ph - d;
                if (t2 < 0.0f)
                    t2 += 1.0f;
                x = (ph < d ? 1.0f : -1.0f) + blep(ph, v->inc) - blep(t2, v->inc);
                x *= 0.6f;
                break;
            }
            case WV_TRI: {
                int i = (int)(ph * 32.0f) & 31;
                x = (i < 16 ? (float)(15 - i) : (float)(i - 16)) * (1.0f / 7.5f) - 1.0f;
                x *= 0.85f;
                break;
            }
            case WV_WAVE: x = (WAVE_GB[(int)(ph * 32.0f) & 31] - 7.6f) * (1.0f / 7.5f) * 0.75f; break;
            case WV_SAW: x = (2.0f * ph - 1.0f - blep(ph, v->inc)) * 0.5f; break;
            case WV_SINE: x = fm_sin_turns(ph); break;
            default: {                           /* the LFSR */
                v->nacc += v->nrate;
                while (v->nacc >= 1.0f) {
                    uint16_t r = v->lfsr, bit = (uint16_t)((r ^ (r >> 1)) & 1u);
                    v->nacc -= 1.0f;
                    r = (uint16_t)((r >> 1) | (bit << 14));
                    if (v->lshort)
                        r = (uint16_t)((r & ~0x40u) | (bit << 6));
                    v->lfsr = r;
                    v->nval = (r & 1u) ? 0.7f : -0.7f;
                }
                x = v->nval;
                if (v->lp_a > 0.0f) {
                    v->lp_z += v->lp_a * (x - v->lp_z);
                    x = v->lp_z * 1.6f;
                }
                if (v->hp_a > 0.0f) {
                    v->hp_z += v->hp_a * (x - v->hp_z);
                    x -= v->hp_z;
                }
                break;
            }
            }
            if (v->wave != WV_NOISE) {
                if (v->lead_id >= 0) {           /* the lead's vibrato, coming in */
                    v->vib = fm_minf(1.0f, v->vib + 1.0f / (0.5f * EN_SR));
                    v->vib_ph += 5.5f / EN_SR;
                    if (v->vib_ph >= 1.0f)
                        v->vib_ph -= 1.0f;
                    ph += v->inc * (1.0f + 0.0081f * v->vib * fm_sin_turns(v->vib_ph));
                } else {
                    ph += v->inc;
                }
                if (v->slide_k != 1.0f)
                    v->inc *= v->slide_k;
                if (ph >= 1.0f)
                    ph -= 1.0f;
                v->ph = ph;
            }
            x *= v->g;
            mixl[j] += x * gl;
            mixr[j] += x * gr;
            esend[j] += x * bl * (v->bus == CH_TRI ? 0.15f : 0.5f);
            BUS(v->bus == CH_LEAD ? 0 : v->bus, j, x * bl);
        }
    }
}

/* the space: a lowpass (the night, the fog), grit, the weather's bed, the beltway's hiss, the UFO, the echo */
static float svf_lp_l, svf_bp_l, svf_lp_r, svf_bp_r, lp_fc = 18000.0f, lp_q = 0.7f;
static float crush_g, wet_g, fb_g = 0.28f, wbed_g, wbed_f = 3000.0f, hiss_g;
static float wb_lp, wb_bp, hs_lp, hs_bp;
static uint16_t wn_lfsr = 0x2A5Bu;
static float wn_acc, wn_val;
#define ECHO_N 16537                            /* 0.75 s at half the rate */
static int16_t echo_buf[ECHO_N] EN_POOL;
static uint32_t echo_w;
static float echo_d = 9000.0f, echo_lp, echo_hold, echo_out, echo_in;
static uint8_t echo_odd;
static inline float echo_step(float in)
{
    if (echo_odd ^= 1) {
        float rp = (float)echo_w - echo_d, y0, y1, f;
        int i0;
        if (rp < 0.0f)
            rp += (float)ECHO_N;
        i0 = (int)rp;
        f = rp - (float)i0;
        y0 = (float)echo_buf[i0] * (1.0f / 32768.0f);
        y1 = (float)echo_buf[i0 + 1 < ECHO_N ? i0 + 1 : 0] * (1.0f / 32768.0f);
        echo_hold = y0 + (y1 - y0) * f;
        echo_lp += 0.35f * (echo_hold - echo_lp);
        {
            float w = fm_clampf((echo_in + in) * 0.5f + fb_g * echo_lp, -0.999f, 0.999f);
            echo_buf[echo_w] = (int16_t)(w * 32767.0f);
        }
        if (++echo_w >= ECHO_N)
            echo_w = 0;
    } else {
        echo_in = in;
    }
    echo_out += 0.5f * (echo_hold - echo_out);
    return echo_out;
}
static inline float wnoise(void)
{
    wn_acc += 1.0f;
    if (wn_acc >= 2.0f) {
        uint16_t r = wn_lfsr, bit = (uint16_t)((r ^ (r >> 1)) & 1u);
        wn_acc -= 2.0f;
        wn_lfsr = (uint16_t)((r >> 1) | (bit << 14));
        wn_val = (wn_lfsr & 1u) ? 0.7f : -0.7f;
    }
    return wn_val;
}
static float gate_ph;

static void control(uint32_t n)
{
    const float dt = (float)n / EN_SR;
    float day = en_city.day, fog = en_city.fog, wv = fm_minf(1.0f, en_city.rain + en_city.storm), snow = en_city.snow, k;
    int b;
    for (b = 0; b < EN_NCH; b++)
        en_glow[b] *= 1.0f - fm_minf(dt * 6.0f, 1.0f);
    if (par[P_FLAVOR] == 2) {
        lp_fc += (700.0f + en_city.bright * 3200.0f - lp_fc) * fm_minf(dt * 3.0f, 1.0f);
        lp_q = 0.6f;
    } else {
        float to = fm_minf(18000.0f, (2600.0f + 15400.0f * day * day) * (1.0f - 0.55f * fog) * (1.0f - 0.35f * wv));
        lp_fc += (to - lp_fc) * fm_minf(dt * 1.25f, 1.0f);
        lp_q = 1.2f;
    }
    k = fm_minf(0.7f, (0.06f + 0.22f * wv + 0.3f * fog + 0.22f * snow + 0.15f * (1.0f - day) + 0.22f * fm_minf(1.0f, (float)en_city.birds / 5.0f) +
                       (ufo_v.on ? 0.25f : 0.0f)) * lvl(par[P_ECHO]));
    wet_g += (k - wet_g) * fm_minf(dt * 2.0f, 1.0f);
    fb_g += (0.28f + 0.25f * fog + 0.1f * snow - fb_g) * fm_minf(dt * 2.0f, 1.0f);
    {
        float d = (en_city.birds ? 3.0f : 6.0f) * s16() * EN_SR * 0.5f;
        if (d > (float)(ECHO_N - 4))
            d = (float)(ECHO_N - 4);
        echo_d += (d - echo_d) * fm_minf(dt * 3.0f, 1.0f);
    }
    k = (0.035f * en_city.rain + 0.08f * en_city.storm + 0.012f * snow) * lvl(par[P_VFX]);
    wbed_g += (k - wbed_g) * fm_minf(dt * 1.25f, 1.0f);
    wbed_f += ((snow > 0.5f ? 6500.0f : 3000.0f) - wbed_f) * fm_minf(dt, 1.0f);
    k = fm_minf(0.1f, (float)en_city.hwy * 0.018f) * lvl(par[P_VDRUMS]);
    hiss_g += (k - hiss_g) * fm_minf(dt * 3.0f, 1.0f);
    k = fm_minf(0.35f, en_city.grit * 0.9f) + lvl(par[P_CRUSH]) * 0.5f;
    crush_g += (k - crush_g) * fm_minf(dt * 2.0f, 1.0f);
    if (gate_until > 0.0f) {                     /* the quake's tremolo, in time */
        gate_until -= dt;
        gate_depth += (gate_to - gate_depth) * fm_minf(dt * 2.5f, 1.0f);
    } else {
        gate_depth += (0.0f - gate_depth) * fm_minf(dt * 2.5f, 1.0f);
    }
    if (ufo_v.on) {                              /* the UFO: a wobbling voice, wandering in whole tones */
        if ((int32_t)(now_s - ufo_v.next) >= 0) {
            ufo_v.next = now_s + (uint32_t)((0.6f + frand() * 0.5f) * EN_SR);
            ufo_v.inc_to = mtof(ufo_v.base + (float)(2 * irand(6))) * (1.0f / EN_SR);
        }
        ufo_v.g += (ufo_v.g_to - ufo_v.g) * fm_minf(dt / 1.2f, 1.0f);
        if (ufo_v.g_to <= 0.0f && ufo_v.g < 0.001f)
            ufo_v.on = 0;
    }
    for (b = 0; b < EN_NCH; b++)
        bus_level[b] = 1.0f;
}

static void render_chunk(int32_t *out, uint32_t n, float gain, uint32_t at)
{
    uint32_t j;
    /* the lowpass runs twice a sample (a Chamberlin filter is stable only well below the rate), and opens to
     * the dry sound above 9 kHz */
    float f = 2.0f * fm_sinf(FM_PI * fm_minf(lp_fc, 12000.0f) / (2.0f * EN_SR)), qd = 1.0f / lp_q;
    float open;
    if (f * (f + 2.0f * qd) > 3.2f)              /* past the filter's stability: the most it can take */
        f = fm_sqrtf(qd * qd + 3.2f) - qd;
    open = fm_clampf((lp_fc - 9000.0f) * (1.0f / 3000.0f), 0.0f, 1.0f);
    float wbf = fm_minf(0.62f, 2.0f * fm_sinf(FM_PI * wbed_f / EN_SR)), hsf = 2.0f * fm_sinf(FM_PI * 900.0f / EN_SR);
    float gf = en_bpm / 60.0f * 2.0f / EN_SR;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = esend[j] = 0.0f;
#ifdef EN_HOST
    {
        int b;
        bus_on = en_bus_on && at + n <= 1024u;
        if (bus_on)
            for (b = 0; b < EN_NBUS; b++)
                for (j = 0; j < n; j++)
                    busb[b][j] = 0.0f;
    }
#else
    (void)at;
#endif
    render_voices(n);
    for (j = 0; j < n; j++) {
        float l = mixl[j], r = mixr[j], e, wn, cl, cr, g;
        wn = wnoise();
        if (hiss_g > 0.0005f) {                  /* the beltway */
            hs_lp += hsf * hs_bp;
            hs_bp += hsf * (wn - hs_lp - 1.2f * hs_bp);
            l += hs_bp * hiss_g;
            r += hs_bp * hiss_g;
            BUS(3, j, hs_bp * hiss_g);
        }
        if (ufo_v.on) {
            ufo_v.inc += (ufo_v.inc_to - ufo_v.inc) * 0.00008f;
            ufo_v.vph += 6.5f / EN_SR;
            if (ufo_v.vph >= 1.0f)
                ufo_v.vph -= 1.0f;
            ufo_v.ph += ufo_v.inc * (1.0f + 0.0175f * fm_sin_turns(ufo_v.vph));
            if (ufo_v.ph >= 1.0f)
                ufo_v.ph -= 1.0f;
            e = fm_sin_turns(ufo_v.ph) * ufo_v.g * 0.45f;
            l += e;
            r += e;
            esend[j] += e;
            BUS(4, j, e);
        }
        /* the lowpass: a state-variable filter, each side */
        svf_lp_l += f * svf_bp_l;
        svf_bp_l += f * (l - svf_lp_l - qd * svf_bp_l);
        svf_lp_r += f * svf_bp_r;
        svf_bp_r += f * (r - svf_lp_r - qd * svf_bp_r);
        svf_lp_l += f * svf_bp_l;
        svf_bp_l += f * (l - svf_lp_l - qd * svf_bp_l);
        svf_lp_r += f * svf_bp_r;
        svf_bp_r += f * (r - svf_lp_r - qd * svf_bp_r);
        l = svf_lp_l + (l - svf_lp_l) * open;
        r = svf_lp_r + (r - svf_lp_r) * open;
        if (crush_g > 0.001f) {                  /* grit: the levels stepped */
            cl = fm_floorf(l * 6.0f + 0.5f) * (1.0f / 6.0f);
            cr = fm_floorf(r * 6.0f + 0.5f) * (1.0f / 6.0f);
            l += (cl - l) * crush_g;
            r += (cr - r) * crush_g;
        }
        if (wbed_g > 0.0005f) {                  /* the rain */
            wb_lp += wbf * wb_bp;
            wb_bp += wbf * (wnoise() * 0.8f + wn * 0.2f - wb_lp - 2.0f * wb_bp);
            l += wb_bp * wbed_g * 1.3f;
            r += wb_bp * wbed_g * 1.3f;
            BUS(5, j, wb_bp * wbed_g);
        }
        e = echo_step(esend[j]) * wet_g * 2.2f;
        mute_g += ((muted ? 0.0f : 1.0f) - mute_g) * 0.002f;
        gate_ph += gf;
        if (gate_ph >= 1.0f)
            gate_ph -= 1.0f;
        g = gain * mute_g * (1.0f - gate_depth * (gate_ph < 0.5f ? 1.0f : 0.0f));
        l = (l + e) * 1.4f * g;
        r = (r + e) * 1.4f * g;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
#ifdef EN_HOST
        if (bus_on)
            busb[5][j] += e;
#endif
    }
#ifdef EN_HOST
    if (bus_on) {
        int b;
        for (j = 0; j < n; j++)
            for (b = 0; b < EN_NBUS; b++)
                en_bus[(at + j) * EN_NBUS + (uint32_t)b] = fm_tanhf(busb[b][j] * 1.4f * gain * 0.99f);
    }
#endif
}

/* the city as CCs, when they change */
static void cc_out(void)
{
    static uint8_t last[6] = {255, 255, 255, 255, 255, 255};
    uint8_t v[6];
    int i;
    if (muted || (par[P_MIDI] != EN_MIDI_BOTH && par[P_MIDI] != EN_MIDI_CC))
        return;
    v[0] = (uint8_t)(fm_clampf(en_city.mood * 0.5f + 0.5f, 0.0f, 1.0f) * 127.0f + 0.5f);
    v[1] = (uint8_t)(fm_clampf(en_city.bright, 0.0f, 1.0f) * 127.0f + 0.5f);
    v[2] = (uint8_t)(fm_clampf(en_city.rush, 0.0f, 1.0f) * 127.0f + 0.5f);
    v[3] = (uint8_t)(fm_clampf(en_city.day, 0.0f, 1.0f) * 127.0f + 0.5f);
    v[4] = (uint8_t)(fm_clampf(en_city.rain + en_city.storm + en_city.snow, 0.0f, 1.0f) * 127.0f + 0.5f);
    v[5] = (uint8_t)(stage * 42);
    for (i = 0; i < 6; i++)
        if (v[i] != last[i]) {
            last[i] = v[i];
            en_midi_out(0xB0u | (uint32_t)((par[P_MCH] + 6) & 15), (uint32_t)(EN_CC_MOOD + i), v[i]);
        }
}

/* ---------------------------------------------------------------- drain --- */
static void apply(int p, int v)
{
    par[p] = (int16_t)v;
    if (p == P_TUNE)
        tunefac = fm_exp2f((float)v * (1.0f / 1200.0f));
    if (p == P_KEY) {
        en_root = (uint8_t)root_pc();
        en_pcs = scale_pcs();
    }
}
static void reset_music(void)
{
    key = en_city.key % 12;
    prog = en_city.prog % 6;
    transpose = 0;
    chord = 0;
    stage = en_city.stage & 3;
    pending = -1;
    mode = 1;
    evq_r = evq_w;
    en_root = (uint8_t)root_pc();
    en_mode = (uint8_t)mode;
    en_pcs = scale_pcs();
}
static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_PLAY:
            if (a && !en_playing)
                play_start();
            else if (!a && en_playing)
                play_stop();
            break;
        case C_RESET: reset_music(); break;
        case C_PANIC: all_off(); break;
        case C_MUTE: muted = (uint8_t)(a != 0); break;
        case C_EVENT: {
            int ea = b & 0xFF, eb = (b >> 8) & 0xFF;
            if (a >= E_LIGHTNING)
                cue(a, ea);
            else if (en_playing)
                ev_push(a, ea, eb);
            break;
        }
        case C_LEAD: lead_on(a, b); break;
        case C_LEADOFF: lead_off(a); break;
        case C_OCT: lead_oct = a - 2; break;
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
                play_start();
            } else if (a == EN_CLK_CONTINUE) {
                en_ext = 1;
                if (!en_playing)
                    play_start();
            } else if (en_playing) {
                play_stop();
            }
            break;
        }
    }
}

void en_init(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        par[i] = PARAMS[i].def;
    for (i = 0; i < NV; i++)
        vo[i].on = 0;
    for (i = 0; i < ECHO_N; i++)
        echo_buf[i] = 0;
    q_r = q_w = 0;
    evq_r = evq_w = 0;
    en_playing = en_ext = 0;
    reset_music();
}

void en_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    uint32_t at = 0;
    static uint32_t cc_left;
    drain();
    if (en_ext && ++ext_age > 100u)
        en_ext = 0;
    since_tick += n;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        float dsteps = 0.0f;
        control(k);
        if (par[P_SYNC] && en_ext) {
            dsteps = en_playing ? ext_steps : 0.0f;
            ext_steps = 0.0f;
        } else {
            en_bpm = (float)par[P_TEMPO];
            if (en_playing)
                dsteps = (float)k * en_bpm * (4.0f / 60.0f) * (1.0f / EN_SR);
        }
        if (dsteps > 0.0f) {                     /* each sixteenth this chunk, at its sample; the swing: the on-beats
                                                    long, the off-beats short (so they land late) */
            float rate = dsteps / (float)k, left = dsteps, pos = 0.0f, sw = (float)par[P_SWING] * 0.005f;
            for (;;) {
                float len = (step_count & 1u) ? 1.0f - sw : 1.0f + sw, need = len - step_frac;
                if (left < need) {
                    step_frac += left;
                    break;
                }
                pos += need / rate;
                left -= need;
                step_frac = 0.0f;
                step_count++;
                step((int)fm_minf(pos, (float)(k - 1)), (int)(step_count & 15u));
            }
        }
        render_chunk(out, k, gain, at);
        now_s += k;
        out += 2 * k;
        at += k;
        n -= k;
    }
    if (cc_left <= at) {
        cc_out();
        cc_left = 2048u;
    } else {
        cc_left -= at;
    }
}
