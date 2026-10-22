/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. The panel's handling (knobs, help cards, the remote) is AMB-1's, by way of
 * ECO-1 and GRAV-1.
 *
 *   keys           the lead, snapped to the city's scale. On TIME black keys 1-4 jump to dawn, noon, dusk,
 *                  night; on WEATHER black keys 1-7 set the sky (auto, clear .. snow); on EVENTS black keys 1-6
 *                  call lightning, a quake, a robbery, a crash, a UFO, birds
 *   PLAY           the music plays / rests (the city lives on)   REC   HOLD: the city stops growing
 *   EDIT           a new city: a new seed in this slot (SAVE on Setup keeps it)
 *   SEL            ride along: the camera follows a car (again: another; ALGORITHM lets go)
 *   OCT- / OCT+    the lead an octave down / up
 *   SELECT         every page in turn (with HOME held: the tempo)
 *   ALGORITHM      look around: the camera to the next landmark      PRESETS  the city (ten)
 *   HOME ENV ARP LFO SEQ FX GLO   the pages: CITY, MUSIC, SOUND, TIME (again: WEATHER), VOICES (again: VOICES 2),
 *                  EVENTS (again: EVENTS 2), SETUP (again: DISPLAY, then MIDI OUT)
 *   KNOB 1-4       the page's four values
 *   SAVE           on the Setup pages: saves all ten cities; elsewhere: the city again from its founding;
 *                  held 1.5 s: the city back to its recipe
 * Untouched a while (Idle), the screen gives itself to the city: the camera rides with the cars, each car's
 * route sings the lead, and the incidents take the camera when they happen. */
#include "../dsp/fastmath.h"
/* the few string pieces (the device's libc has none) */
static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static char *s_cpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != 0) {} return r; }
static char *s_cat(char *d, const char *s) { s_cpy(d + s_len(d), s); return d; }
#include "city.c"
#include "life.c"

/* the pages; SELECT walks them all */
enum { V_CITY, V_MUSIC, V_SOUND, V_TIME, V_WEATHER, V_VOICES, V_VOICES2, V_EVENTS, V_EVENTS2, V_SETUP, V_DISPLAY, V_OUT, NVIEWS };
#define NWALK NVIEWS
static const char *const VIEW_NAME[NVIEWS] = {"City", "Music", "Sound", "Time", "Weather", "Voices", "Voices 2", "Events",
                                              "Events 2", "Setup", "Display", "MIDI out"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_ENV, B_ARP, B_LFO, B_LFO, B_SEQ, B_SEQ, B_FX, B_FX, B_GLO, B_GLO, B_GLO};
enum { K_LEDS = 200, K_CPU, K_TIME, K_NONE };
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Speed", "Start as", "Traffic", "People"},   {"Tempo", "Density", "Swing", "City bed"},
    {"Flavor", "Lead", "Echo", "Crush"},          {"Day", "Season", "Days each", "Clock"},
    {"Weather", "Change", "Birds", "Chats"},      {"Bells", "Arps", "Bass", "Drums"},
    {"Pad", "Lead", "Effects", "Sirens"},         {"Lightning", "Quakes", "Robbery", "Crashes"},
    {"UFOs", "Follow", "Disasters", "Idle"},      {"Key", "Tune", "Sync", "Mood"},
    {"Theme", "Zoom", "Map", "Labels"},           {"Sends", "Channels", "Lights", "CPU"},
};
static const uint8_t VIEW_PAR[NVIEWS][4] = {
    {P_SPEED, P_START, P_TRAFFIC, P_PEOPLE}, {P_TEMPO, P_DENSITY, P_SWING, P_BED}, {P_FLAVOR, P_LEAD, P_ECHO, P_CRUSH},
    {P_DAYLEN, P_SEASON, P_SEASONDAYS, 0}, {P_WEATHER, P_WPACE, P_BIRDS, P_CHATS}, {P_VBELLS, P_VARPS, P_VBASS, P_VDRUMS},
    {P_VPAD, P_VLEAD, P_VFX, P_VSIREN}, {P_LIGHTNING, P_QUAKES, P_ROBBERY, P_CRASH}, {P_UFO, P_FOLLOW, P_DISASTER, P_IDLE},
    {P_KEY, P_TUNE, P_SYNC, P_MOOD}, {P_THEME, P_ZOOM, P_MAP, P_LABELS}, {P_MIDI, P_MCH, 0, 0}};

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help, reset_done, dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame, last_ms;
    uint32_t sig[3];
    uint32_t boot_t;
    uint8_t splash, splash_step;
    uint8_t full;                      /* the idle view: the city on the whole screen */
    uint8_t hold;                      /* REC: the city stops growing */
    uint8_t ff;                        /* growing back to Start as */
    float acc;                         /* weeks owed */
    uint32_t news_r;
    uint32_t coin_t;
    int ride;                          /* SEL: the car the camera rides with, -1 none */
    uint32_t look_until;               /* ALGORITHM: the camera stays where it was sent */
    int look_i;
    float tour_x, tour_y;
    uint32_t tour_next;
    int8_t lead_oct;
    uint32_t lead_keys;
} ui;
uint32_t ui_splash_ms = 3200u;
static const uint32_t IDLE_MS[4] = {0u, 60000u, 120000u, 300000u};
static const float SPEEDS[9] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};
#define WEEKS_PER_S 0.8f

/* ------------------------------------------------------------- themes --- */
/* the frame's colours (the city keeps its own): Asphalt (lane-paint yellow), Neon, Blueprint, Sunset,
 * Terminal, Mono */
typedef struct {
    uint16_t bg, panel, line, dim, text, acc, cold, sky;
} theme_t;
static const theme_t THEMES[EN_NTHEMES] = {
    {RGB(24, 26, 29), RGB(44, 48, 53), RGB(66, 71, 78), RGB(150, 156, 162), RGB(236, 238, 233), RGB(232, 185, 49), RGB(120, 190, 235), RGB(200, 215, 230)},
    {RGB(16, 10, 26), RGB(38, 24, 56), RGB(64, 44, 92), RGB(160, 140, 190), RGB(244, 232, 255), RGB(255, 92, 196), RGB(70, 230, 255), RGB(230, 200, 255)},
    {RGB(10, 26, 52), RGB(22, 46, 86), RGB(40, 72, 120), RGB(140, 170, 210), RGB(230, 242, 255), RGB(130, 200, 255), RGB(240, 240, 255), RGB(200, 225, 255)},
    {RGB(30, 16, 14), RGB(58, 32, 26), RGB(88, 52, 40), RGB(190, 150, 130), RGB(255, 236, 222), RGB(255, 140, 60), RGB(255, 200, 120), RGB(255, 220, 190)},
    {RGB(2, 8, 4), RGB(10, 30, 14), RGB(24, 64, 32), RGB(63, 154, 82), RGB(168, 255, 181), RGB(198, 255, 207), RGB(63, 191, 95), RGB(125, 255, 154)},
    {RGB(0, 0, 0), RGB(30, 30, 30), RGB(60, 60, 60), RGB(138, 138, 138), RGB(240, 240, 240), RGB(255, 255, 255), RGB(150, 150, 150), RGB(190, 190, 190)},
};
static const theme_t *th(void) { return &THEMES[(unsigned)proj.par[P_THEME] < EN_NTHEMES ? proj.par[P_THEME] : 0]; }
#define K_BG (th()->bg)
#define K_PANEL (th()->panel)
#define K_LINE (th()->line)
#define K_DIM (th()->dim)
#define K_TEXT (th()->text)
#define K_ACC (th()->acc)
#define K_COLD (th()->cold)
#define K_HOLD RGB(255, 138, 76)

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1500u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
static int msg_busy(void) { return plat_ms() < ui.msg_until; }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
/* 12,345 / 123K / 1.2M */
static void pop_text(int32_t v, char *b)
{
    if (v < 0)
        v = 0;
    if (v < 100000) {
        char t[12];
        int n, k = 0, j;
        itoa_u((uint32_t)v, t);
        n = (int)s_len(t);
        for (j = 0; j < n; j++) {
            b[k++] = t[j];
            if ((n - 1 - j) % 3 == 0 && j < n - 1)
                b[k++] = ',';
        }
        b[k] = 0;
    } else if (v < 1000000) {
        itoa_u((uint32_t)(v / 1000), b);
        s_cat(b, "K");
    } else {
        itoa_u((uint32_t)(v / 1000000), b);
        b += s_len(b);
        *b++ = '.';
        itoa_u((uint32_t)(v / 100000 % 10), b);
        s_cat(b, "M");
    }
}

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

/* -------------------------------------------------------------- knobs --- */
static int view_knob(int v, int i)
{
    if (v == V_TIME && i == 3)
        return K_TIME;
    if (v == V_OUT && i >= 2)
        return i == 2 ? K_LEDS : K_CPU;
    return VIEW_PAR[v][i];
}
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_CPU)
        return (int)plat_cpu_pct();
    if (k == K_TIME)
        return (int)(phase() * 24.0f) % 24;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_CPU ? 100 : k == K_TIME ? 23 : 0;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS) {
        en_param_text(k, v, b);
    } else if (k == K_LEDS) {
        s_cpy(b, v == 2 ? "Keys" : v ? "On" : "Off");
    } else if (k == K_CPU) {
        itoa_u((uint32_t)v, b);
        s_cat(b, "%");
    } else if (k == K_TIME) {
        clock_text(b);
    } else {
        b[0] = 0;
    }
}
static void new_city(int keep_view);
static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_CPU || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    if (k == K_TIME)
        v = (v % 24 + 24) % 24;
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k == K_TIME) {
        set_phase((float)v / 24.0f);
        return;
    }
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        en_set(k, v);
        if (k == P_ZOOM)
            cam.ts = 0;
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    }
    mark_dirty();
}
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a)
        return s;
    if (a > 1)
        dt /= a;
    ui.enc_run[role] = (uint8_t)(dt < 60u ? (ui.enc_run[role] + a > 255u ? 255u : ui.enc_run[role] + a) : a);
    if (range <= 24 || accel_off || ui.enc_run[role] < 4u)
        return s;
    m = dt < 12u ? 8u : dt < 25u ? 5u : dt < 45u ? 3u : 1u;
    if (range < 100 && m > 3u)
        m = 3u;
    return s * (int32_t)m;
}
static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE || k == K_CPU)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ the city --- */
static uint32_t seed_now(void) { return (uint32_t)(uint16_t)proj.par[P_SEED_HI] << 16 | (uint16_t)proj.par[P_SEED_LO]; }
/* the city from its founding: the map, the music's key and progression, life cleared, the camera home; Start as
 * grows it back to a stage first (a little a frame) */
static void new_city(int keep_view)
{
    uint32_t seed = seed_now();
    city_new(seed);
    ui.news_r = c_news_w;
    en_city.key = (uint8_t)(seed % 12u);
    en_city.prog = (uint8_t)((seed >> 4) % 6u);
    en_city.stage = 0;
    en_reset();
    life_reset();
    ui.ff = proj.par[P_START] > 0;
    ui.acc = 0.0f;
    ui.ride = -1;
    ui.look_until = 0;
    ui.tour_next = 0;
    cam.x = (float)C.cx + 0.5f;
    cam.y = (float)C.cy + 0.5f;
    cam.ts = 0;
    if (!keep_view)
        ui.touched = -1;
}
static void set_piece(int i)
{
    char t[16];
    project_piece(i);
    project_apply();
    new_city(1);
    t[0] = (char)('0' + (proj.piece + 1) / 10);
    t[1] = (char)('0' + (proj.piece + 1) % 10);
    t[2] = 0;
    {
        char n[16];
        caps(n, C.name, sizeof n);
        say(t, n);
    }
    mark_dirty();
}
static void grow(uint32_t dms)
{
    int n = 0;
    if (!city_ready())
        return;
    if (ui.ff) {                                 /* growing back to Start as: many weeks a frame */
        int target = proj.par[P_START];
        while (C.stage < target && C.tick < 3000u && n < 24) {
            city_step();
            n++;
        }
        if (C.stage >= target || C.tick >= 3000u) {
            ui.ff = 0;
            ui.news_r = c_news_w;                /* what happened on the way isn't news */
            en_city.stage = C.stage;
            en_reset();
        }
        return;
    }
    if (ui.hold)
        return;
    ui.acc += (float)dms * 0.001f * WEEKS_PER_S * SPEEDS[(unsigned)proj.par[P_SPEED] < 9u ? proj.par[P_SPEED] : 2];
    while (ui.acc >= 1.0f && n < 6) {
        city_step();
        ui.acc -= 1.0f;
        n++;
    }
    if (ui.acc > 2.0f)
        ui.acc = 2.0f;
}

/* the city's news: messages, and the music's cues */
static void a_name_caps(int type, int L, char *b)
{
    st_t s;
    char t[24];
    memset(&s, 0, sizeof s);
    s.type = (uint8_t)type;
    s.L = (uint8_t)(L ? L : 1);
    s.size = 1;
    caps(t, st_name(&s), sizeof t);
    s_cpy(b, t);
}
static void city_news(void)
{
    while (ui.news_r != c_news_w) {
        news_t e;
        char a[24], b[24];
        int big = 1;
        if (c_news_w - ui.news_r > NNEWS)
            ui.news_r = c_news_w - NNEWS;
        e = c_news[ui.news_r % NNEWS];
        ui.news_r++;
        a[0] = b[0] = 0;
        switch (e.code) {
        case N_FOUNDED: caps(a, C.name, sizeof a); s_cpy(b, "FOUNDED"); break;
        case N_FIRST: s_cpy(a, "FIRST"); a_name_caps(e.a, e.b, b); big = 0; break;
        case N_FIRSTBIG: {
            st_t s;
            memset(&s, 0, sizeof s);
            s.type = e.a;
            s.L = e.b;
            s.size = 2;
            s_cpy(a, "FIRST");
            caps(b, st_name(&s), sizeof b);
            big = 0;
            break;
        }
        case N_CIVIC:
            caps(a, CIVIC_NAME[e.a], sizeof a);
            s_cpy(b, "OPENS");
            if (e.a < CV_GREEN0)
                en_event(E_CIVIC, e.a, 0);
            break;
        case N_GREEN: caps(a, CIVIC_NAME[e.a], sizeof a); s_cpy(b, "OPENS"); en_event(E_GREEN, e.a, 0); big = 0; break;
        case N_SQUARE: s_cpy(a, "TOWN SQUARE"); s_cpy(b, "LAID OUT"); en_event(E_GREEN, 0, 0); break;
        case N_STAGE:
            s_cpy(a, "NOW A");
            caps(b, STAGE_NAME[e.a & 3], sizeof b);
            en_event(E_STAGE, e.a, 0);
            en_city.stage = e.a;
            break;
        case N_BRIDGE: s_cpy(a, "A BRIDGE"); s_cpy(b, "CROSSES"); en_event(E_JINGLE, 0, 0); break;
        case N_CAUSEWAY: s_cpy(a, "A CAUSEWAY"); s_cpy(b, "OVER THE BAY"); en_event(E_JINGLE, 0, 0); break;
        case N_BELTWAY: s_cpy(a, "THE BELTWAY"); s_cpy(b, "BEGINS"); en_event(E_JINGLE, 0, 0); break;
        case N_PORT: s_cpy(a, "THE PORT"); s_cpy(b, "OPENS"); break;
        case N_FARMS: s_cpy(a, "FARMS"); s_cpy(b, "SPREAD"); big = 0; break;
        case N_FARMGONE: s_cpy(a, "FARMLAND"); s_cpy(b, "BUILT OVER"); big = 0; break;
        case N_REDEV: s_cpy(a, "OLD LOTS"); s_cpy(b, "CLEARED"); big = 0; break;
        case N_BUILTOUT: s_cpy(a, "EVERY LOT"); s_cpy(b, "BUILT"); break;
        case N_BUILD:
            if (plat_ms() - ui.coin_t > 600u) {
                ui.coin_t = plat_ms();
                en_event(E_COIN, 0, 0);
            }
            continue;
        case N_TOWER: en_event(E_RISE, 0, 0); continue;
        case N_FIRE: s_cpy(a, "FIRE!"); a_name_caps(e.a, e.b, b); break;
        case N_BURNT: a_name_caps(e.a, e.b, a); s_cpy(b, "BURNS DOWN"); break;
        case N_SAVED: s_cpy(a, "FIRE"); s_cpy(b, "PUT OUT"); break;
        case N_QUAKE: s_cpy(a, "EARTHQUAKE"); break;
        case N_ROBBERY: s_cpy(a, "ROBBERY!"); a_name_caps(e.a, e.b, b); break;
        case N_CAUGHT: s_cpy(a, "ROBBER"); s_cpy(b, "CAUGHT"); break;
        case N_ESCAPED: s_cpy(a, "ROBBER"); s_cpy(b, "GETS AWAY"); break;
        case N_CRASH: s_cpy(a, "CAR CRASH"); break;
        case N_TREATED: s_cpy(a, "PARAMEDICS"); s_cpy(b, "ALL WELL"); break;
        case N_TOWED: s_cpy(a, "THE WRECK"); s_cpy(b, "TOWED"); break;
        case N_UFO: s_cpy(a, "UFO!"); s_cpy(b, "BEAMED UP"); break;
        default: continue;
        }
        if (ui.ff || ui.full)
            continue;
        if (big || !msg_busy())
            say(a, b[0] ? b : 0);
    }
}

/* ------------------------------------------------------------ the walk --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

/* ------------------------------------------------------------ buttons --- */
#ifndef EN_WEB
static void rm_enter(void);
#endif
static void game_start(void);
static void ride_next(void);
static void button(int b)
{
    int v;
    if (b == B_GLO && (ui.view == V_SETUP || ui.view == V_DISPLAY)) {
        set_view(ui.view + 1);
        return;
    }
    if (b == B_LFO && ui.view == V_TIME) {
        set_view(V_WEATHER);
        return;
    }
    if (b == B_SEQ && ui.view == V_VOICES) {
        set_view(V_VOICES2);
        return;
    }
    if (b == B_FX && ui.view == V_EVENTS) {
        set_view(V_EVENTS2);
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_SEL:
        ride_next();
        break;
    case B_EDIT: {                               /* a new seed here */
        uint32_t s = (plat_ms() * 2654435761u) ^ (lrnd_u() * 40503u) ^ seed_now();
        char n[16];
        if (!s)
            s = 1u;
        knob_set(P_SEED_HI, (int16_t)(uint16_t)(s >> 16));
        knob_set(P_SEED_LO, (int16_t)(uint16_t)s);
        new_city(1);
        caps(n, C.name, sizeof n);
        say("A NEW CITY", n);
        mark_dirty();
        break;
    }
    case B_PLAY:
        if (en_playing) {
            en_play(0);
            say("MUSIC", "RESTS");
        } else {
            en_play(1);
            say("MUSIC", "PLAYS");
        }
        break;
    case B_REC:
        ui.hold = !ui.hold;
        say("GROWTH", ui.hold ? "HOLDS" : "GOES ON");
        break;
    case B_OCTDN: case B_OCTUP: {
        char t[8];
        ui.lead_oct = (int8_t)(ui.lead_oct + (b == B_OCTUP ? 1 : -1));
        if (ui.lead_oct < -2)
            ui.lead_oct = -2;
        if (ui.lead_oct > 2)
            ui.lead_oct = 2;
        en_lead_oct(ui.lead_oct);
        t[0] = ui.lead_oct < 0 ? '-' : '+';
        t[1] = (char)('0' + (ui.lead_oct < 0 ? -ui.lead_oct : ui.lead_oct));
        t[2] = 0;
        say("LEAD OCTAVE", ui.lead_oct ? t : "0");
        break;
    }
    case B_SAVE:
        if (ui.view >= V_SETUP) {
            if (project_save() == 0) {
                ui.dirty = 0;
                say("SAVED", "ALL TEN");
            } else {
                say("NOT SAVED", "FLASH ERROR");
            }
        } else {
            new_city(1);
            say("FROM ITS", "FOUNDING");
        }
        break;
    default: break;
    }
}

/* the keys: the lead; on TIME, WEATHER and the EVENTS pages the black keys do the page's work */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int black_of(int k) { int i; for (i = 0; i < 11; i++) if (BLACK_KEY[i] == k) return i; return -1; }
static void key_down(int k)
{
    int bk = black_of(k);
    if (ui.view == V_TIME && bk >= 0 && bk < 4) {
        static const float PH[4] = {0.26f, 0.5f, 0.76f, 0.95f};
        static const char *const N[4] = {"DAWN", "NOON", "DUSK", "NIGHT"};
        set_phase(PH[bk]);
        say("TIME", N[bk]);
        return;
    }
    if (ui.view == V_WEATHER && bk >= 0 && bk < 7) {
        static const char *const N[7] = {"AUTO", "CLEAR", "CLOUDY", "RAIN", "STORM", "FOG", "SNOW"};
        knob_set(P_WEATHER, bk);
        wx.next = 0;
        say("WEATHER", N[bk]);
        return;
    }
    if ((ui.view == V_EVENTS || ui.view == V_EVENTS2) && bk >= 0 && bk < 6) {
        static const char *const N[6] = {"LIGHTNING", "EARTHQUAKE", "ROBBERY", "CRASH", "UFO", "BIRDS"};
        switch (bk) {
        case 0: strike(1); break;
        case 1: quake_start(); break;
        case 2: start_robbery(); break;
        case 3: start_crash(); break;
        case 4: start_ufo(); break;
        default: spawn_flock(); break;
        }
        say(N[bk], "CALLED");
        return;
    }
    en_lead(k, k);
    ui.lead_keys |= 1u << k;
}
static void key_up(int k)
{
    if (ui.lead_keys >> k & 1u) {
        en_lead_off(k);
        ui.lead_keys &= ~(1u << k);
    }
}

/* MIDI in: notes play the lead; the clock */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        else if ((st & 0xF0u) == 0x90u && v)
            en_lead(64 + (int)(n & 63u), (int)n - 53);
        else if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && !v))
            en_lead_off(64 + (int)(n & 63u));
    }
}

#define HELP_HOLD 900u
#define RESET_HOLD 1500u
static void help_tick(uint32_t btn, uint32_t keys)
{
    uint32_t now = plat_ms();
    int i;
    if (ui.help) {
        uint32_t m = 1u << (ui.help - 1);
        if (btn != m || keys || (int32_t)(ui.turn_t - ui.down_t[ui.help - 1]) > 0)
            ui.help = 0;
        return;
    }
    if (!btn || (btn & (btn - 1u)) || keys)
        return;
    for (i = 0; i < NB; i++)
        if (btn == 1u << i && i != B_PLAY && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

/* REMOTE (SEL + GLO): the panel plays CTY-1 in the browser over USB MIDI, and the FM-1 goes quiet, as on HYP-1,
 * LOF-1, ECO-1 and GRAV-1. On channel 16: the keys are notes 48 + k, the buttons CC 102 + b (127 down, 0 up),
 * the knobs CC 85 + role (64 + detents), MASTER CC 7, and CC 116 says hello every second. The browser answers
 * with its lights the same way and CC 117. SEL + GLO again: back. Not in the browser's own build. */
#define RM_ST_NOTE 0x9Fu
#define RM_ST_OFF 0x8Fu
#define RM_ST_CC 0xBFu
#define RM_KEY0 48u
#define RM_BTN0 102u
#define RM_ENC0 85u
#define RM_MASTER 7u
#define RM_HELLO 116u
#define RM_ACK 117u
static struct {
    uint8_t on, link, was_playing;
    uint32_t btn, keys, ign_b, ign_k;
    uint32_t lit_b, lit_k;
    uint32_t hello_t, rx_t;
    int master;
} rm;
int ui_remote(void) { return rm.on; }

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c);
static void rm_out(uint32_t st, uint32_t d1, uint32_t d2) { plat_midi_out(st >> 4 | st << 8 | d1 << 16 | d2 << 24); }
static void rm_draw(void)
{
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 120, K_BG);
    text_c(120, 4, &FONT_L, "REMOTE", K_ACC);
    text_c(120, 44, &FONT_S, rm.link ? "PLAYING THE BROWSER" : "WAITING FOR THE BROWSER", rm.link ? K_COLD : K_DIM);
    text_c(120, 66, &FONT_XS, "USB MIDI  //  CHANNEL 16", K_DIM);
    text_c(120, 96, &FONT_S, "SEL + GLO: BACK", K_TEXT);
    cv_blit(0, 60);
}
#ifndef EN_WEB
static void rm_enter(void)
{
    int e;
    memset(&rm, 0, sizeof rm);
    rm.on = 1;
    rm.was_playing = en_playing;
    rm.ign_b = plat_buttons();
    rm.ign_k = plat_keys();
    rm.hello_t = plat_ms() - 1000u;
    rm.master = -1;
    for (e = 0; e < NE; e++)
        (void)plat_enc(e);
    ui.full = 0;
    en_play(0);
    en_mute(1);
    plat_leds(0, 0);
    rm_draw();
}
#endif
static void rm_leave(void)
{
    uint32_t i;
    for (i = 0; i < NB; i++)
        if (rm.btn >> i & 1u)
            rm_out(RM_ST_CC, RM_BTN0 + i, 0);
    for (i = 0; i < NKEYS; i++)
        if (rm.keys >> i & 1u)
            rm_out(RM_ST_OFF, RM_KEY0 + i, 0);
    rm_out(RM_ST_CC, RM_HELLO, 0);
    rm.on = 0;
    en_mute(0);
    if (rm.was_playing)
        en_play(1);
    ui.btn = plat_buttons();
    ui.keys = plat_keys();
    ui.btn_used = ui.btn;
    ui.act_t = plat_ms();
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
    say("REMOTE", "OFF");
}
static void rm_tick(void)
{
    uint32_t b = plat_buttons(), k = plat_keys(), now = plat_ms(), ch, i, pkt;
    int e;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == RM_ST_NOTE || st == RM_ST_OFF) {
            if (n >= RM_KEY0 && n < RM_KEY0 + NKEYS) {
                uint32_t m = 1u << (n - RM_KEY0);
                rm.lit_k = st == RM_ST_NOTE && v ? rm.lit_k | m : rm.lit_k & ~m;
            }
        } else if (st == RM_ST_CC) {
            if (n >= RM_BTN0 && n < RM_BTN0 + NB) {
                uint32_t m = 1u << (n - RM_BTN0);
                rm.lit_b = v ? rm.lit_b | m : rm.lit_b & ~m;
            } else if (n == RM_ACK) {
                rm.rx_t = now;
                if (!rm.link) {
                    rm.link = 1;
                    rm_draw();
                }
                if (!v)
                    rm.lit_b = rm.lit_k = 0;
            }
        }
    }
    if (rm.link && now - rm.rx_t > 3000u) {
        rm.link = 0;
        rm.lit_b = rm.lit_k = 0;
        rm_draw();
    }
    if ((b >> B_SEL & 1u) && (b >> B_GLO & 1u) && !((rm.ign_b | rm.btn) >> B_GLO & 1u)) {
        rm_leave();
        return;
    }
    rm.ign_b &= b;
    rm.ign_k &= k;
    b &= ~rm.ign_b;
    k &= ~rm.ign_k;
    if (b >> B_GLO & 1u && b >> B_SEL & 1u)
        b &= ~(1u << B_GLO);
    ch = b ^ rm.btn;
    for (i = 0; i < NB; i++)
        if (ch >> i & 1u)
            rm_out(RM_ST_CC, RM_BTN0 + i, b >> i & 1u ? 127u : 0u);
    rm.btn = b;
    ch = k ^ rm.keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (k >> i & 1u)
                rm_out(RM_ST_NOTE, RM_KEY0 + i, 100u);
            else
                rm_out(RM_ST_OFF, RM_KEY0 + i, 0u);
        }
    rm.keys = k;
    for (e = 0; e < NE; e++) {
        int32_t d = plat_enc(e);
        if (d)
            rm_out(RM_ST_CC, RM_ENC0 + (uint32_t)e, (uint32_t)(64 + (d > 63 ? 63 : d < -63 ? -63 : d)));
    }
    {
        int m = (int)(plat_master() >> 5);
        if (m > 127)
            m = 127;
        if (rm.master < 0 || m - rm.master > 1 || rm.master - m > 1 || ((m == 0 || m == 127) && m != rm.master)) {
            rm.master = m;
            rm_out(RM_ST_CC, RM_MASTER, (uint32_t)m);
        }
    }
    if (now - rm.hello_t >= 1000u) {
        rm.hello_t = now;
        rm_out(RM_ST_CC, RM_HELLO, 127u);
    }
}

static int game_on(void);
static void game_input(uint32_t btn, uint32_t keys);
static void game_pos(float *x, float *y);

/* --------------------------------------------------------- the camera --- */
static const uint8_t TS[7] = {12, 10, 8, 6, 4, 3, 2};
static int fits_at(int s) { return (C.maxx - C.minx + 4) * s <= cam.vw && (C.maxy - C.miny + 4) * s <= cam.vh; }
/* SEL: ride along with a car on screen (another each press) */
static void ride_next(void)
{
    rect_t r = view_rect(0.0f);
    int k, start = ui.ride + 1;
    for (k = 0; k < NCAR; k++) {
        int c = (start + k) % NCAR;
        float x, y;
        if (!cars[c].on || cars[c].duty)
            continue;
        car_pos(&cars[c], &x, &y, 0, 0);
        if (in_rect(&r, x, y)) {
            ui.ride = c;
            cars[c].keep = 1;
            say("RIDING ALONG", cars[c].kind == VK_TRUCK ? "A TRUCK" : cars[c].kind == VK_BUS ? "A BUS" : "A CAR");
            return;
        }
    }
    ui.ride = -1;
    say("NO CARS", "IN VIEW");
}
/* ALGORITHM: look around: the landmarks in turn, then the tall and the big */
static void look_next(int d)
{
    int n = C.nciv, i;
    if (!n)
        return;
    ui.look_i = ((ui.look_i + d) % n + n) % n;
    i = c_civ[ui.look_i];
    ui.look_until = plat_ms() + 9000u;
    ui.tour_x = (float)c_st[i].x + (float)c_st[i].size * 0.5f;
    ui.tour_y = (float)c_st[i].y + (float)c_st[i].size * 0.5f;
    if (ui.ride >= 0 && cars[ui.ride].on)
        cars[ui.ride].keep = 0;
    ui.ride = -1;
    {
        char t[20];
        caps(t, CIVIC_NAME[c_st[i].kind], sizeof t);
        say("LOOKING AT", t);
    }
}
static int notable_pick(float *x, float *y)
{
    int tries;
    for (tries = 0; tries < 12; tries++) {
        int t = 1 + lirnd(3), si;
        const st_t *s;
        if (lrnd() < 0.3f && C.nciv) {
            si = c_civ[lirnd(C.nciv)];
        } else {
            if (!C.nbl[t])
                continue;
            si = c_ref[c_bl[t][lirnd(C.nbl[t])]];
        }
        s = &c_st[si];
        if (s->type == BT_CIV || s->L >= 3 || s->size > 1 || (C.tick - s->born < 40u)) {
            *x = (float)s->x + (float)s->size * 0.5f;
            *y = (float)s->y + (float)s->size * 0.5f;
            return 1;
        }
    }
    return 0;
}
static int solo_pick(void)                       /* the idle view: a car near the middle to ride with */
{
    rect_t r = view_rect(-1.0f);
    int k, best = -1;
    float bd = 1e9f;
    for (k = 0; k < NCAR; k++) {
        float x, y, d;
        if (!cars[k].on || cars[k].duty || cars[k].len - cars[k].k < 8)
            continue;
        car_pos(&cars[k], &x, &y, 0, 0);
        if (!in_rect(&r, x, y))
            continue;
        d = lhyp(x - cam.x, y - cam.y);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}
static void cam_update(float dt)
{
    int mode = proj.par[P_ZOOM], mins, target, k;
    float tx = cam.x, ty = cam.y, rate = 0.03f, fx, fy;
    cam.vw = 240;
    cam.vh = ui.full || game_on() ? 240 : 144;
    mins = ui.full || game_on() ? 8 : mode == 2 ? 2 : mode == 1 ? 12 : 8;
    target = mins;
    if (mode != 1 || ui.full)
        for (k = 0; k < 7; k++) {
            if (TS[k] < mins)
                break;
            if (fits_at(TS[k])) {
                target = TS[k];
                break;
            }
        }
    if (!cam.ts || target < cam.ts || (ui.full && cam.ts != target && !fits_at(cam.ts)))
        cam.ts = target;
    if (ui.full && !game_on()) {                 /* the idle view: ride with the cars, the incidents first */
        if (life_focus(&fx, &fy)) {
            tx = fx;
            ty = fy;
            rate = 0.05f;
            solo_car = -1;
        } else {
            if (solo_car < 0 || !cars[solo_car].on || cars[solo_car].duty || cars[solo_car].k >= cars[solo_car].len - 2) {
                if (solo_car >= 0 && cars[solo_car].on)
                    cars[solo_car].keep = 0;
                solo_car = solo_pick();
                if (solo_car >= 0)
                    cars[solo_car].keep = 1;
            }
            if (solo_car >= 0) {
                car_pos(&cars[solo_car], &tx, &ty, 0, 0);
                rate = 0.004f;
            } else if (fits_at(cam.ts)) {
                tx = (float)(C.minx + C.maxx + 1) * 0.5f;
                ty = (float)(C.miny + C.maxy + 1) * 0.5f;
            }
        }
    } else if (game_on()) {
        game_pos(&tx, &ty);
        rate = 0.0001f;
    } else if (plat_ms() < ui.look_until) {
        tx = ui.tour_x;
        ty = ui.tour_y;
        rate = 0.02f;
    } else if (ui.ride >= 0 && cars[ui.ride].on && !cars[ui.ride].duty) {
        car_pos(&cars[ui.ride], &tx, &ty, 0, 0);
        rate = 0.004f;
        if (cars[ui.ride].k >= cars[ui.ride].len - 2) {   /* its trip's end: the next car near */
            cars[ui.ride].keep = 0;
            ui.ride = solo_pick();
            if (ui.ride >= 0)
                cars[ui.ride].keep = 1;
        }
    } else if (life_focus(&fx, &fy) && !fits_at(cam.ts)) {
        tx = fx;
        ty = fy;
        rate = 0.05f;
    } else if (fits_at(cam.ts)) {
        tx = (float)(C.minx + C.maxx + 1) * 0.5f;
        ty = (float)(C.miny + C.maxy + 1) * 0.5f;
    } else {
        if (plat_ms() > ui.tour_next) {
            if (!notable_pick(&ui.tour_x, &ui.tour_y)) {
                ui.tour_x = (float)C.cx + 0.5f;
                ui.tour_y = (float)C.cy + 0.5f;
            }
            ui.tour_next = plat_ms() + 7000u;
        }
        tx = ui.tour_x;
        ty = ui.tour_y;
        rate = 0.35f;
    }
    if (ui.ride >= 0 && (!cars[ui.ride].on || cars[ui.ride].duty))
        ui.ride = -1;
    {
        float kk = 1.0f - fm_powf(rate, dt);
        float vw = (float)cam.vw / (float)cam.ts, vh = (float)cam.vh / (float)cam.ts;
        cam.x += (tx - cam.x) * kk;
        cam.y += (ty - cam.y) * kk;
        cam.x = vw < (float)MW ? fm_clampf(cam.x, vw * 0.5f, (float)MW - vw * 0.5f) : (float)MW * 0.5f;
        cam.y = vh < (float)MH ? fm_clampf(cam.y, vh * 0.5f, (float)MH - vh * 0.5f) : (float)MH * 0.5f;
    }
}

/* -------------------------------------------------------------- input --- */
static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    if (rm.on)
        return;
    if (game_on()) {
        game_input(btn, keys);
        midi_in();
        return;
    }
    if (ui.full) {                               /* the idle view: any touch only wakes the screen */
        int any = btn != ui.btn || keys != ui.keys, r;
        for (r = 0; r < NE; r++)
            any |= plat_enc(r) != 0;
        if (any) {
            ui.full = 0;
            if (solo_car >= 0 && cars[solo_car].on)
                cars[solo_car].keep = 0;
            solo_car = -1;
            ui.btn_used |= btn;
            ui.btn = btn;
            ui.keys = keys;
            ui.act_t = plat_ms();
            ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
            cam.ts = 0;
            lcd_fill(0, 0, 240, 240, K_BG);
        }
        midi_in();
        return;
    }
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if ((i == B_GLO || i == B_EDIT) && (btn >> B_SEL & 1u)) {   /* SEL + GLO: remote; SEL + EDIT: the secret */
                ui.btn_used |= m | 1u << B_SEL;
                ui.btn = btn;
                ui.keys = keys;
                ui.help = 0;
#ifndef EN_WEB
                if (i == B_GLO) {
                    rm_enter();
                    return;
                }
#endif
                if (i == B_EDIT) {
                    game_start();
                    return;
                }
                continue;
            }
            if (i == B_SAVE)
                ui.reset_done = 0;
            if (i == B_PLAY)
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u)
                key_down((int)i);
            else
                key_up((int)i);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {        /* SELECT: the walk; with HOME held, the tempo */
        if (btn >> B_HOME & 1u) {
            char t[12];
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            en_param_text(P_TEMPO, proj.par[P_TEMPO], t);
            say("TEMPO", t);
        } else {
            int k = (int)ui.view + (int)e;
            set_view(k < 0 ? 0 : k >= NWALK ? NWALK - 1 : k);
        }
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        look_next((int)e);
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set_piece(proj.piece + (int)e);
        ui.act_t = ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        char n[16];
        ui.reset_done = 1;
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        project_piece_reset();
        project_apply();
        new_city(1);
        caps(n, C.name, sizeof n);
        say("RECIPE", n);
        mark_dirty();
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, view_knob(ui.view, (int)i), e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* ------------------------------------------------------------- drawing --- */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68
#define H(x) RGB(((x) >> 16) & 255, ((x) >> 8) & 255, (x) & 255)
static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { cv_text(cx - text_w(f, s) / 2, y, f, s, c); }
/* a pixel blended in: a out of 256 */
static void px_mix(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg, ia;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 256u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    ia = 256u - a;
    *q = swap16((uint32_t)((((c >> 11) * a + (bg >> 11) * ia) >> 8) << 11 | ((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * ia) >> 8) << 5 |
                           (((c & 31u) * a + (bg & 31u) * ia) >> 8)));
}
static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a) { px_mix(x, y, c, a * 16u); }
/* a light: added, saturating (a out of 256) */
static void px_add(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg, r, g, b;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    bg = swap16(*q);
    r = (bg >> 11) + (((c >> 11) * a) >> 8);
    g = ((bg >> 5) & 63u) + ((((c >> 5) & 63u) * a) >> 8);
    b = (bg & 31u) + (((c & 31u) * a) >> 8);
    *q = swap16((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
}
static void rect_mix(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a)
{
    int32_t i, j;
    if (a >= 256u) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            px_mix(i, j, c, a);
}
static void rect_add(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            px_add(i, j, c, a);
}
/* the whole canvas toward c, a out of 256 */
static void tint(uint16_t c, float af)
{
    uint32_t a = (uint32_t)(fm_clampf(af, 0.0f, 1.0f) * 256.0f + 0.5f), ia, ar, ag, ab, i, n = cv_w * cv_h;
    if (a < 3u)
        return;
    ia = 256u - a;
    ar = (c >> 11) * a;
    ag = ((c >> 5) & 63u) * a;
    ab = (c & 31u) * a;
    for (i = 0; i < n; i++) {
        uint32_t v = swap16(cv_px[i]);
        cv_px[i] = swap16(((((v >> 11) * ia + ar) >> 8) << 11) | (((((v >> 5) & 63u) * ia + ag) >> 8) << 5) | (((v & 31u) * ia + ab) >> 8));
    }
}
static void ellipse_mix(float cx, float cy, float rx, float ry, uint16_t c, uint32_t a)
{
    int32_t y0 = (int32_t)fm_floorf(cy - ry), y1 = (int32_t)(cy + ry), x, y;
    if (rx < 0.5f || ry < 0.5f)
        return;
    for (y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry, w;
        int32_t xa, xb;
        if (dy * dy >= 1.0f)
            continue;
        w = rx * fm_sqrtf(1.0f - dy * dy);
        xa = (int32_t)fm_floorf(cx - w + 0.5f);
        xb = (int32_t)fm_floorf(cx + w + 0.5f);
        for (x = xa; x < xb; x++)
            px_mix(x, y, c, a);
    }
}
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 1.0f);
    int32_t y0 = (int32_t)fm_floorf(cy - r - 1.0f), y1 = (int32_t)(cy + r + 1.0f), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            px_blend(x, y, c, (uint32_t)(fm_clampf(r - d + 0.5f, 0.0f, 1.0f) * 16.0f + 0.5f));
        }
}
static float turns(float y, float x)
{
    float r = atan2_t(y, x);
    if (r < 0.0f)
        r += FM_TWO_PI;
    return r * (1.0f / FM_TWO_PI);
}
static void arc(int32_t cx, int32_t cy, int32_t r, float v, uint16_t track, uint16_t fill, uint16_t bg, int bipolar)
{
    const float thk = 3.4f, a0 = 0.375f, sweep = 0.75f;
    float ro = (float)r, ri = ro - thk, rm = ro - thk * 0.5f, fx = (float)cx + 0.5f, fy = (float)cy + 0.5f, lo, hi;
    int32_t x, y;
    v = fm_clampf(v, 0.0f, 1.0f);
    lo = bipolar ? (v < 0.5f ? v : 0.5f) : 0.0f;
    hi = bipolar ? (v < 0.5f ? 0.5f : v) : v;
    for (y = -r - 1; y <= r + 1; y++)
        for (x = -r - 1; x <= r + 1; x++) {
            float dx = (float)x, dy = (float)y, d = fm_sqrtf(dx * dx + dy * dy), t, a;
            a = fm_clampf(ro + 0.5f - d, 0.0f, 1.0f) * fm_clampf(d - ri + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f)
                continue;
            t = turns(dy, dx) - a0;
            if (t < 0.0f)
                t += 1.0f;
            t /= sweep;
            if (t > 1.0f)
                continue;
            px_blend(cx + x, cy + y, (t >= lo && t <= hi) ? fill : track, (uint32_t)(a * 16.0f + 0.5f));
        }
    {
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI, el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, thk * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, thk * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, thk * 0.5f, fill);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, thk * 0.5f + 1.3f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, thk * 0.5f + 0.6f, fill);
    }
}
static uint16_t mix(uint16_t a, uint16_t b, int t)
{
    int r, g, bl;
    if (t <= 0)
        return a;
    if (t >= 16)
        return b;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static int q16(float x) { return x <= 0.0f ? 0 : x >= 1.0f ? 16 : (int)(x * 16.0f + 0.5f); }
/* lighter (a > 0) or darker (a < 0), as the prototype's shade() */
static uint16_t shade(uint16_t c, float a)
{
    float r = (float)((c >> 11) * 255 / 31), g = (float)(((c >> 5) & 63) * 255 / 63), b = (float)((c & 31) * 255 / 31);
    if (a > 0.0f) {
        r += (255.0f - r) * a;
        g += (255.0f - g) * a;
        b += (255.0f - b) * a;
    } else {
        r *= 1.0f + a;
        g *= 1.0f + a;
        b *= 1.0f + a;
    }
    return RGB((uint32_t)r, (uint32_t)g, (uint32_t)b);
}

/* ------------------------------------------ the unit painter: 8 units a tile --- */
static float SXf, SYf, SK;
static int rnd_i(float v) { return (int)fm_floorf(v + 0.5f); }
static void at(int px, int py, int s)
{
    SXf = (float)px;
    SYf = (float)py;
    SK = (float)s * 0.125f;
}
static void P(float x, float y, float w, float h, uint16_t c)
{
    int x0 = rnd_i(SXf + x * SK), y0 = rnd_i(SYf + y * SK), x1 = rnd_i(SXf + (x + w) * SK), y1 = rnd_i(SYf + (y + h) * SK);
    cv_rect(x0, y0, x1 - x0 < 1 ? 1 : x1 - x0, y1 - y0 < 1 ? 1 : y1 - y0, c);
}
static void PA(float x, float y, float w, float h, uint16_t c, uint32_t a)
{
    int x0 = rnd_i(SXf + x * SK), y0 = rnd_i(SYf + y * SK), x1 = rnd_i(SXf + (x + w) * SK), y1 = rnd_i(SYf + (y + h) * SK);
    rect_mix(x0, y0, x1 - x0 < 1 ? 1 : x1 - x0, y1 - y0 < 1 ? 1 : y1 - y0, c, a);
}
static void dot(float x, float y, uint16_t c)
{
    int d = rnd_i(SK) < 1 ? 1 : rnd_i(SK);
    cv_rect(rnd_i(SXf + x * SK), rnd_i(SYf + y * SK), d, d, c);
}
static void dotA(float x, float y, uint16_t c, uint32_t a)
{
    int d = rnd_i(SK) < 1 ? 1 : rnd_i(SK);
    rect_mix(rnd_i(SXf + x * SK), rnd_i(SYf + y * SK), d, d, c, a);
}
static void blob(float x, float y, float w, float h, uint16_t c)
{
    P(x + 1.0f, y, w - 2.0f, h, c);
    P(x, y + 1.0f, w, h - 2.0f, c);
}
static void outline(float x, float y, float w, float h, uint16_t c)
{
    P(x, y, w, 1, c);
    P(x, y + h - 1.0f, w, 1, c);
    P(x, y, 1, h, c);
    P(x + w - 1.0f, y, 1, h, c);
}
static void bevel(float x, float y, float w, float h, uint16_t c)
{
    P(x, y, w, h, c);
    P(x, y, w, 0.8f, shade(c, 0.3f));
    P(x, y, 0.8f, h, shade(c, 0.2f));
    P(x, y + h - 0.8f, w, 0.8f, shade(c, -0.28f));
    P(x + w - 0.8f, y, 0.8f, h, shade(c, -0.2f));
}
static void tree(float x, float y, float r, float sv)
{
    float d = r * 2.0f;
    uint16_t c = SEAS[SC_LEAF], hi = sv > 0.5f ? SEAS[SC_LEAFHI] : SEAS[SC_LEAFMID];
    PA(x - r + 1.0f, y - r + 1.2f, d, d, RGB(20, 30, 15), 72);
    if (season.idx == 3 && sv > 0.38f) {         /* bare branches, a dusting of snow */
        P(x - 0.3f, y - r + 0.4f, 0.6f, d - 0.8f, H(0x6b5640));
        P(x - r + 0.4f, y - 0.3f, d - 0.8f, 0.6f, H(0x6b5640));
        dot(x - r * 0.5f, y - r * 0.5f, H(0x7d6650));
        dot(x + r * 0.4f, y + r * 0.3f, H(0xeef3f5));
        return;
    }
    if (season.idx == 2) {
        static const uint16_t AUT[4] = {H(0xc46a2a), H(0xd99a2e), H(0xb8432a), H(0x8a8a34)};
        c = AUT[(int)(sv * 4.0f) & 3];
        hi = shade(c, 0.25f);
    } else if (season.idx == 0 && sv > 0.72f) {
        hi = sv > 0.86f ? H(0xf6d6e0) : H(0xf2b8c8);
    } else if (season.idx == 3) {
        hi = H(0xeef3f5);
    }
    blob(x - r, y - r, d, d, c);
    P(x - r + 0.6f, y - r + 0.6f, d * 0.5f, d * 0.45f, hi);
}
static uint16_t grass(int i)
{
    int h = c_hgt[i];
    return SEAS[h < 97 ? SC_G0 : h < 128 ? SC_G1 : h < 153 ? SC_G2 : h < 174 ? SC_G3 : SC_G4];
}

/* the view: screen rows r0 .. r0+bh of a cam.vw x cam.vh view, onto the canvas */
static int VOX, VOY, VS;
static int tile_px(float x) { return VOX + (int)fm_floorf(x * (float)VS + 0.5f); }
static int tile_py(float y) { return VOY + (int)fm_floorf(y * (float)VS + 0.5f); }

static int rd(int x, int y) { return in_map(x, y) && c_road[y * MW + x]; }
static void draw_road(int x, int y, int px, int py, int s)
{
    int i = y * MW + x, bridge = c_road[i] == 2, kind = c_plan[i], lane = LANE(i), med = MED(i);
    uint16_t asph = kind == PL_BELTWAY ? H(0x33363c) : bridge ? H(0x62656c) : H(0x3a3d44);
    int L, Rr, U, D;
    if (bridge && s >= 6) {
        int bL = rd(x - 1, y), bR = rd(x + 1, y), bU = rd(x, y - 1), bD = rd(x, y + 1);
        int vert = lane == 1 || (lane == 0 && (bU || bD) && !(bL || bR));
        cv_rect(px, py, s, s, (c_terr[i] & F_OCEAN) ? H(0x2b5b86) : SEAS[SC_RIVER]);
        at(px, py, s);
        if (vert) {
            float a0 = bL ? 0.0f : 1.0f, a1 = bR ? 8.0f : 7.0f;
            P(a0, 0, a1 - a0, 8, H(0x6a6d74));
            if (!bL) P(1, 0, 0.7f, 8, H(0xc9ccd0));
            if (!bR) P(6.3f, 0, 0.7f, 8, H(0xc9ccd0));
            if (y % 3 == 0) {
                if (!bL) P(0, 3, 1, 2, H(0x22344a));
                if (!bR) P(7, 3, 1, 2, H(0x22344a));
            }
            if (y & 1) P(3.7f, 1, 0.6f, 5, kind == PL_AVENUE ? H(0xe7e7e2) : H(0xd8c35e));
        } else {
            float a0 = bU ? 0.0f : 1.0f, a1 = bD ? 8.0f : 7.0f;
            P(0, a0, 8, a1 - a0, H(0x6a6d74));
            if (!bU) P(0, 1, 8, 0.7f, H(0xc9ccd0));
            if (!bD) P(0, 6.3f, 8, 0.7f, H(0xc9ccd0));
            if (x % 3 == 0) {
                if (!bU) P(3, 0, 2, 1, H(0x22344a));
                if (!bD) P(3, 7, 2, 1, H(0x22344a));
            }
            if (x & 1) P(1, 3.7f, 5, 0.6f, kind == PL_AVENUE ? H(0xe7e7e2) : H(0xd8c35e));
        }
        return;
    }
    if (kind == PL_CULDESAC && !bridge) {        /* the turnaround */
        if (s < 6) {
            cv_rect(px, py, s, s, asph);
            return;
        }
        cv_rect(px, py, s, s, grass(i));
        at(px, py, s);
        if (rd(x, y - 1)) P(0, 0, 8, 4, asph);
        if (rd(x, y + 1)) P(0, 4, 8, 4, asph);
        if (rd(x - 1, y)) P(0, 0, 4, 8, asph);
        if (rd(x + 1, y)) P(4, 0, 4, 8, asph);
        blob(0.2f, 0.2f, 7.6f, 7.6f, asph);
        blob(2.6f, 2.6f, 2.8f, 2.8f, SEAS[SC_PARK]);
        dot(3.6f, 3.4f, SEAS[SC_LEAF]);
        return;
    }
    cv_rect(px, py, s, s, asph);
    if (s < 6)
        return;
    at(px, py, s);
    L = rd(x - 1, y);
    Rr = rd(x + 1, y);
    U = rd(x, y - 1);
    D = rd(x, y + 1);
    if (kind == PL_BELTWAY) {                    /* the median barrier, the edge line, the lanes */
        if (lane == 1) {
            if (med & 1) { P(0, 0, 0.8f, 8, H(0xc3c6ca)); P(6.6f, 0, 0.5f, 8, H(0xe7e7e2)); }
            else if (med & 2) { P(7.2f, 0, 0.8f, 8, H(0xc3c6ca)); P(0.9f, 0, 0.5f, 8, H(0xe7e7e2)); }
            if (y & 1) P(3.75f, 1, 0.5f, 5, H(0xe7e7e2));
        } else if (lane == 2) {
            if (med & 4) { P(0, 0, 8, 0.8f, H(0xc3c6ca)); P(0, 6.6f, 8, 0.5f, H(0xe7e7e2)); }
            else if (med & 8) { P(0, 7.2f, 8, 0.8f, H(0xc3c6ca)); P(0, 0.9f, 8, 0.5f, H(0xe7e7e2)); }
            if (x & 1) P(1, 3.75f, 5, 0.5f, H(0xe7e7e2));
        }
        return;
    }
    {
        uint16_t curb = bridge ? H(0xa9acb2) : H(0x8d9096);
        if (!U) P(0, 0, 8, 1, curb);
        if (!D) P(0, 7, 8, 1, curb);
        if (!L) P(0, 0, 1, 8, curb);
        if (!Rr) P(7, 0, 1, 8, curb);
    }
    if (kind == PL_AVENUE && med) {              /* the planted median */
        uint16_t g = bridge ? H(0x8d9096) : SEAS[SC_PARK];
        if (med & 1) P(0, 0, 1.2f, 8, g);
        if (med & 2) P(6.8f, 0, 1.2f, 8, g);
        if (med & 4) P(0, 0, 8, 1.2f, g);
        if (med & 8) P(0, 6.8f, 8, 1.2f, g);
        if (!bridge && s >= 8 && ((x + y) & 1)) {
            if (med & 1) P(0, 3, 1, 1.6f, SEAS[SC_LEAF]);
            if (med & 2) P(7, 3, 1, 1.6f, SEAS[SC_LEAF]);
            if (med & 4) P(3, 0, 1.6f, 1, SEAS[SC_LEAF]);
            if (med & 8) P(3, 7, 1.6f, 1, SEAS[SC_LEAF]);
        }
        if (lane == 1 && (y & 1)) P(3.7f, 1.5f, 0.6f, 4, H(0xe7e7e2));
        if (lane == 2 && (x & 1)) P(1.5f, 3.7f, 4, 0.6f, H(0xe7e7e2));
        return;
    }
    if (lane == 1 && (y & 1)) P(3.6f, 1, 0.8f, 6, H(0xd8c35e));
    if (lane == 2 && (x & 1)) P(1, 3.6f, 6, 0.8f, H(0xd8c35e));
    if (lane == 0 && s >= 8 && (L || Rr) && (U || D)) {
        int k;
        for (k = 1; k < 7; k += 2) {
            if (U && LANE(i - MW) != 0) P((float)k, 0, 1, 1, H(0xc9cbcf));
            if (D && LANE(i + MW) != 0) P((float)k, 7, 1, 1, H(0xc9cbcf));
        }
    }
}
static void draw_rounds(int s)
{
    int k;
    for (k = 0; k < C.nrounds; k++) {
        int a = c_round[k], x = a % MW, y = a / MW, px, py;
        if (!c_road[a] || !c_road[a + 1] || !c_road[a + MW] || !c_road[a + MW + 1])
            continue;
        px = VOX + x * s;
        py = VOY + y * s;
        if (px < -2 * s || py < -2 * s || px > cam.vw || py > cam.vh)
            continue;
        if (s < 6) {
            cv_rect(px + s - 1, py + s - 1, 2, 2, SEAS[SC_PARK]);
            continue;
        }
        at(px, py, s);
        blob(4.6f, 4.6f, 6.8f, 6.8f, H(0xe7e7e2));
        blob(5.2f, 5.2f, 5.6f, 5.6f, H(0x3a3d44));
        blob(5.9f, 5.9f, 4.2f, 4.2f, SEAS[SC_PARK]);
        tree(8, 7.8f, 1.3f, 0.7f);
    }
}
static void draw_rubble(int x, int y, int px, int py, int s)
{
    int n = s * s / 6 < 2 ? 2 : s * s / 6, d = s / 6 < 1 ? 1 : s / 6, k;
    cv_rect(px, py, s, s, H(0x8f877a));
    for (k = 0; k < n; k++) {
        float h = city_hashf(C.seed + (uint32_t)k, x, y);
        cv_rect(px + (int)(city_hashf((uint32_t)k, x, y + 3) * (float)(s - d)), py + (int)(city_hashf((uint32_t)k, y, x + 5) * (float)(s - d)), d, d,
                h < 0.33f ? H(0x6f675c) : h < 0.66f ? H(0xa59d90) : H(0x5a5148));
    }
}
static void draw_ground(int x, int y, int px, int py, int s, uint32_t sh)
{
    int i = y * MW + x;
    if (TERR(i) == T_WATER) {
        cv_rect(px, py, s, s, (c_terr[i] & F_OCEAN) ? H(0x2b5b86) : SEAS[SC_RIVER]);
        if (!(c_terr[i] & F_OCEAN) && season.snow > 0.5f && s >= 4 && city_hashf(C.seed, x, y) < 0.5f) {
            rect_mix(px + 1, py + (s >> 2), s - 2, 1, H(0xecf4fa), 140);
            rect_mix(px + (s >> 1), py + (s >> 1) + 1, s >> 2, 1, H(0xecf4fa), 140);
        }
        if (s >= 3 && city_hashf(C.seed ^ sh, x, y) < 0.06f)
            cv_rect(px + (int)(city_hashf(sh, x, y) * (float)(s >> 1)), py + (s >> 1), (s >> 1) < 1 ? 1 : s >> 1, 1, H(0x86b6da));
        return;
    }
    cv_rect(px, py, s, s, grass(i));
    if (RUBBLE(i)) {
        draw_rubble(x, y, px, py, s);
        return;
    }
    if (c_bt[i])
        return;
    if (TERR(i) == T_FOREST) {
        cv_rect(px, py, s, s, SEAS[SC_FOREST]);
        if (s >= 6) {
            float a = city_hashf(C.seed, x, y), b = city_hashf(C.seed, y, x);
            at(px, py, s);
            tree(2.0f + a * 1.5f, 2.2f + b, 2.2f, a);
            tree(5.5f - b, 4.0f + a * 1.5f, 2.4f, b);
            tree(2.5f + b * 2.0f, 6.0f, 1.8f, a);
        } else if (s >= 3) {
            cv_rect(px + (int)(city_hashf(C.seed, x, y) * (float)(s - 1)), py + (int)(city_hashf(C.seed, y, x) * (float)(s - 1)), s >> 1, s >> 1,
                    SEAS[SC_LEAF2]);
        }
    } else if (c_terr[i] & F_TREE) {
        if (s >= 6) {
            float a = city_hashf(C.seed + 5u, x, y);
            at(px, py, s);
            tree(3.0f + a * 2.0f, 3.0f + city_hashf(C.seed + 6u, x, y) * 2.0f, 2.0f + a * 0.6f, a);
        } else if (s >= 3) {
            cv_rect(px + (s >> 2), py + (s >> 2), s >> 1, s >> 1, SEAS[SC_LEAF]);
        } else {
            cv_rect(px, py, 1, 1, SEAS[SC_LEAFMID]);
        }
    } else if (s >= 8 && city_hashf(C.seed + 9u, x, y) < 0.18f) {   /* wildflowers */
        at(px, py, s);
        dot(1.0f + city_hashf(C.seed, x, y + 9) * 6.0f, 1.0f + city_hashf(C.seed, x + 9, y) * 6.0f,
            city_hashf(C.seed, x, y) < 0.5f ? H(0xf2e27a) : H(0xf0f0ea));
    }
}

/* -------------------------------------------------------- the buildings --- */
#define NSMOKE 24
static struct { float x, y, k; } smoke[NSMOKE];
static int nsmoke;
static void chimney(float x, float y)
{
    blob(x, y, 2, 2, H(0x6b3f36));
    dot(x + 0.6f, y + 0.6f, H(0x1e1e1e));
    if (nsmoke < NSMOKE) {
        smoke[nsmoke].x = SXf + (x + 1.0f) * SK;
        smoke[nsmoke].y = SYf + (y + 0.5f) * SK;
        smoke[nsmoke].k = SK;
        nsmoke++;
    }
}
static void tank(float x, float y, float d)
{
    blob(x, y, d, d, H(0xe4e6e8));
    P(x + 0.6f, y + 0.6f, d * 0.4f, d * 0.3f, H(0xffffff));
    P(x + d * 0.5f, y + d - 1.0f, d * 0.4f, 0.6f, H(0xb9bdc2));
}
static void sprite_r(const st_t *st)
{
    float v = city_hashf(C.seed, st->x, st->y);
    switch (st->L) {
    case 1: {                                    /* a house: yard, driveway, a tree */
        static const uint16_t ROOF[6] = {H(0xc8664c), H(0xa9573f), H(0x8f6d5b), H(0xc47f4e), H(0x7d5a4a), H(0xb0603f)};
        uint16_t roof = ROOF[(int)(v * 6.0f) % 6];
        P(0, 0, 8, 8, SEAS[SC_YARD]);
        P(5, 4.5f, 2, 3.5f, H(0xa2a39f));
        P(1, 1.5f, 5, 2, shade(roof, 0.18f));
        P(1, 3.5f, 5, 2, roof);
        P(1, 3.4f, 5, 0.4f, shade(roof, -0.3f));
        tree(6.5f, 1.8f, 1.4f, v);
        if (v > 0.6f)
            P(1, 6.5f, 3, 1, H(0x5f8f44));
        break;
    }
    case 2: {                                    /* townhouses */
        static const uint16_t ROOF[3] = {H(0xb9785e), H(0xa8705a), H(0x9c6152)};
        uint16_t roof = ROOF[(int)(v * 3.0f) % 3];
        P(0, 0, 8, 8, SEAS[SC_YARD]);
        P(0.5f, 1, 7, 2.2f, shade(roof, 0.15f));
        P(0.5f, 3.2f, 7, 2.2f, roof);
        P(2.8f, 1, 0.5f, 4.4f, shade(roof, -0.4f));
        P(5.1f, 1, 0.5f, 4.4f, shade(roof, -0.4f));
        P(0.5f, 6, 7, 1, H(0x5f8f44));
        P(1.4f, 5.4f, 0.8f, 2.6f, H(0xc6c2b8));
        P(3.7f, 5.4f, 0.8f, 2.6f, H(0xc6c2b8));
        P(6, 5.4f, 0.8f, 2.6f, H(0xc6c2b8));
        break;
    }
    case 3:                                      /* an apartment block */
        P(0, 0, 8, 8, H(0xb8b2a4));
        bevel(1, 1, 6, 5, H(0xd6c29d));
        dot(2, 2, H(0x8d8d8d));
        dot(5, 2.5f, H(0x8d8d8d));
        P(3, 3.5f, 2, 1, H(0xa99878));
        tree(1.5f, 7, 1.1f, v);
        tree(6.5f, 7, 1.1f, 1.0f - v);
        break;
    case 4: {                                    /* a mid-rise */
        static const uint16_t BODY[4] = {H(0xe0d3b8), H(0xd4c3a8), H(0xe6dccb), H(0xcfc7b8)};
        float x, y;
        P(0, 0, 8, 8, H(0xa9a59c));
        bevel(0.5f, 0.5f, 7, 7, BODY[(int)(v * 4.0f) & 3]);
        for (y = 2.0f; y < 7.0f; y += 1.5f)
            for (x = 1.5f; x < 7.0f; x += 1.5f)
                dotA(x, y, H(0x3c2d1e), 77);
        blob(4.5f, 1, 2.5f, 2.5f, H(0x7a6a58));
        break;
    }
    default: {                                   /* a residential tower, four roofs */
        static const uint16_t BODY[4] = {H(0xece6da), H(0xe4d6c2), H(0xd9dde2), H(0xefe2cf)};
        uint16_t body = BODY[(int)(v * 4.0f) & 3];
        int kind = (int)(city_hashf(C.seed + 3u, st->x, st->y) * 4.0f) & 3;
        P(0, 0, 8, 8, H(0xa9a59c));
        bevel(0.5f, 0.5f, 7, 7, body);
        if (kind == 0) {
            bevel(1.8f, 1.8f, 4.4f, 4.4f, shade(body, 0.4f));
            P(2.6f, 2.6f, 2.8f, 2.8f, H(0x6aa65a));
            dot(3.2f, 3.2f, H(0x4f8a40));
            dot(4.4f, 4.2f, H(0x4f8a40));
        } else if (kind == 1) {
            P(1.5f, 1.5f, 5, 3, H(0x5fb8e0));
            P(1.8f, 1.8f, 2, 0.6f, H(0xb9e6f6));
            P(1.5f, 5.2f, 5, 1.4f, H(0xc9c0ae));
        } else if (kind == 2) {
            blob(2, 2, 4, 4, H(0x5a5e64));
            P(3, 2.9f, 0.5f, 2.2f, H(0xf0f0ea));
            P(4.5f, 2.9f, 0.5f, 2.2f, H(0xf0f0ea));
            P(3, 3.8f, 2, 0.45f, H(0xf0f0ea));
        } else {
            P(1.5f, 1.5f, 2.2f, 2.2f, H(0x9aa0a6));
            P(4.5f, 1.8f, 2, 1.4f, H(0x8a9096));
            blob(4.3f, 4.2f, 2.4f, 2.4f, H(0x7a6a58));
        }
    }
    }
}
static void sprite_c(const st_t *st, int n)
{
    float v = city_hashf(C.seed, st->x, st->y);
    int k;
    if (n == 1) {
        switch (st->L) {
        case 1:                                  /* a corner store, its striped awning */
            P(0, 0, 8, 8, H(0xbdb9b0));
            bevel(1, 1, 6, 4.2f, H(0xd8d2c4));
            for (k = 0; k < 6; k++)
                P(1.0f + (float)k, 5.2f, 1, 1.2f, k % 2 ? H(0xf4f1ea) : H(0xd9473f));
            P(5.5f, 2, 1, 1, H(0x9aa0a6));
            break;
        case 2: {                                /* a shop row, three awnings */
            static const uint16_t COLS[4] = {H(0xd9473f), H(0x3f8fd0), H(0xe8b931), H(0x3c9a5f)};
            P(0, 0, 8, 8, H(0xbdb9b0));
            bevel(0, 0.8f, 8, 4.4f, H(0xcfc8b8));
            for (k = 0; k < 3; k++)
                P((float)k * 2.7f + 0.2f, 5.2f, 2.4f, 1.2f, COLS[((int)(v * 4.0f) + k) & 3]);
            P(2.6f, 0.8f, 0.4f, 4.4f, H(0xa7a090));
            P(5.3f, 0.8f, 0.4f, 4.4f, H(0xa7a090));
            break;
        }
        case 3:                                  /* a plaza store and its parking */
            P(0, 0, 8, 8, H(0x4d5056));
            bevel(0.5f, 0.5f, 7, 3.8f, H(0xc9ccd0));
            P(0.5f, 4.3f, 7, 0.7f, H(0x3f8fd0));
            for (k = 1; k < 8; k += 2)
                P((float)k, 5.6f, 0.4f, 2, H(0xe7e7e2));
            dot(2, 6.5f, H(0xd9473f));
            dot(6, 6.2f, H(0xf0f0ea));
            break;
        case 4: {                                /* an office block */
            float y;
            P(0, 0, 8, 8, H(0x8f949b));
            bevel(0.5f, 0.5f, 7, 7, H(0x7f9fb8));
            for (y = 1.5f; y < 7.0f; y += 1.5f)
                P(1.2f, y, 5.6f, 0.6f, H(0xbfe0f2));
            P(5, 1.2f, 1.5f, 1.2f, H(0x5d6b78));
            break;
        }
        default:                                 /* a glass tower */
            P(0, 0, 8, 8, H(0x8f949b));
            bevel(0.5f, 0.5f, 7, 7, H(0x8fcdea));
            P(0.5f, 0.5f, 3.5f, 7, H(0xa9dcf2));
            for (k = 1; k < 7; k++)
                dotA((float)k, 7.0f - (float)k, H(0xffffff), 218);
            bevel(2.8f, 2.8f, 2.4f, 2.4f, H(0xe9f7fd));
        }
        return;
    }
    if (st->L <= 3) {                            /* a shopping center, its parking full */
        static const uint16_t CARS[5] = {H(0xd9473f), H(0xf0f0ea), H(0x3f8fd0), H(0x2f3337), H(0xe8b931)};
        P(0, 0, 16, 16, H(0x4b4e54));
        bevel(1, 1, 14, 7, H(0xcfd2d6));
        for (k = 0; k < 3; k++)
            P(3.0f + (float)k * 4.0f, 3, 2, 2, H(0x9ad0e8));
        P(1, 8, 14, 0.9f, H(0xd9473f));
        P(6, 8, 4, 0.9f, H(0xf4f1ea));
        for (k = 1; k < 16; k += 2) {
            P((float)k, 10, 0.35f, 2, H(0xe7e7e2));
            P((float)k, 13, 0.35f, 2, H(0xe7e7e2));
        }
        for (k = 0; k < 9; k++) {
            float h2 = city_hashf(C.seed + (uint32_t)k, st->x, st->y);
            if (h2 < 0.6f)
                P(1.4f + (float)k * 1.6f, h2 < 0.3f ? 10.5f : 13.5f, 1, 1.2f, CARS[(int)(h2 * 17.0f) % 5]);
        }
    } else if (st->L == 4) {                     /* an office campus */
        float y;
        P(0, 0, 16, 16, H(0x9fa49a));
        P(0, 10, 16, 6, H(0x78a95a));
        bevel(1, 1, 8, 8, H(0x7f9fb8));
        bevel(10, 2, 5, 6, H(0x8aa7bd));
        for (y = 2.5f; y < 8.5f; y += 1.5f)
            P(2, y, 6, 0.6f, H(0xbfe0f2));
        P(7, 9, 2, 7, H(0xd8d0bc));
        tree(3, 13, 1.6f, v);
        tree(12, 12.5f, 1.8f, 1.0f - v);
        tree(14, 14.5f, 1.3f, v);
    } else {                                     /* a skyscraper */
        P(0, 0, 16, 16, H(0x9aa0a6));
        bevel(1, 1, 14, 14, H(0x7fc3e4));
        P(1, 1, 7, 14, H(0x99d6f0));
        for (k = 2; k < 14; k++)
            dotA((float)k, 15.0f - (float)k, H(0xffffff), 218);
        bevel(4, 4, 8, 8, H(0xbfe6f6));
        bevel(6, 6, 4, 4, H(0xecf8fd));
        blob(7, 7, 2, 2, H(0x5a5e64));
    }
}
static void sprite_farm(const st_t *st, int n)
{
    float w = (float)(n * 8), v = city_hashf(C.seed, st->x, st->y), y;
    uint16_t c0, c1;
    static const uint16_t CROPS[4][2] = {{H(0xd9c06a), H(0xc4a94e)}, {H(0x7fae4f), H(0x5f9a40)}, {H(0x9cc76f), H(0x86b85a)}, {H(0xc98e4a), H(0xb07a3c)}};
    if (season.idx == 3) { c0 = H(0xe6ecef); c1 = H(0xd6dde0); }
    else if (season.idx == 0) { c0 = H(0x7fae4f); c1 = H(0x8a6a48); }
    else if (season.idx == 2) { c0 = H(0xd9b24a); c1 = H(0xa8823a); }
    else { c0 = CROPS[(int)(v * 4.0f) & 3][0]; c1 = CROPS[(int)(v * 4.0f) & 3][1]; }
    P(0, 0, w, w, season.idx == 3 ? H(0xd3d9dc) : H(0x8a6a48));
    for (y = 0.5f; y < w - 0.5f; y += 1.4f)
        P(0.5f, y, w - 1.0f, 0.8f, ((int)(y * 10.0f)) % 2 ? c0 : c1);
    if (n > 1) {
        bevel(w - 6.0f, w - 5.5f, 4.4f, 3.4f, H(0xb8463c));
        P(w - 6.0f, w - 4.0f, 4.4f, 0.4f, H(0x8f332b));
        blob(w - 7.8f, w - 3.4f, 2, 2, H(0xc9ccd0));
    } else {
        bevel(5, 5, 2.6f, 2.4f, H(0xb8463c));
    }
}
static void sprite_i(const st_t *st, int n)
{
    int k;
    if (st->flags & SF_FARM) {
        sprite_farm(st, n);
        return;
    }
    if (n == 1) {
        float f;
        switch (st->L) {
        case 1:                                  /* a workshop: a shed and crates */
            P(0, 0, 8, 8, H(0xa99a78));
            P(1, 1, 6, 4, H(0xb5a46c));
            for (f = 1.5f; f < 7.0f; f += 1.0f)
                P(f, 1, 0.4f, 4, H(0x9d8d58));
            P(1, 6, 1.2f, 1.2f, H(0x8a6a42));
            P(2.6f, 6.2f, 1, 1, H(0x9c7a4c));
            break;
        case 2:                                  /* a warehouse, its docks and a truck */
            P(0, 0, 8, 8, H(0x8d9096));
            bevel(0, 0.6f, 8, 5, H(0xa7acb2));
            for (f = 1.6f; f < 5.6f; f += 1.0f)
                P(0.4f, f, 7.2f, 0.35f, H(0x8e949a));
            P(1, 5.6f, 1.5f, 0.8f, H(0x2f3337));
            P(3.5f, 5.6f, 1.5f, 0.8f, H(0x2f3337));
            P(5.4f, 6.4f, 2.2f, 1.1f, H(0xf0f0ee));
            P(7.1f, 6.4f, 0.5f, 1.1f, H(0xd9473f));
            break;
        case 3:                                  /* a small factory: the sawtooth roof, a stack */
            P(0, 0, 8, 8, H(0x8d9096));
            for (k = 0; k < 6; k++)
                P((float)k, 2, 1, 6, k % 2 ? H(0x7d8288) : H(0xb9bdc2));
            chimney(6, 0.5f);
            break;
        case 4:                                  /* a plant: tanks */
            P(0, 0, 8, 8, H(0x8d9096));
            tank(0.5f, 0.5f, 3.5f);
            tank(4, 0.5f, 3.5f);
            P(1, 4.5f, 6, 0.6f, H(0x5d6168));
            P(2.2f, 4, 0.6f, 3, H(0x5d6168));
            chimney(5.5f, 5.5f);
            break;
        default:                                 /* a depot */
            P(0, 0, 8, 8, H(0x4b4e54));
            bevel(0, 0, 8, 4.6f, H(0x9da3aa));
            for (f = 0.8f; f < 4.6f; f += 1.0f)
                P(0.4f, f, 7.2f, 0.35f, H(0x868c93));
            P(0.8f, 5.6f, 2.4f, 1, H(0xf0f0ee));
            P(4.2f, 5.6f, 2.4f, 1, H(0xe8b931));
            P(0.8f, 7, 2.4f, 0.6f, H(0xf0f0ee));
        }
        return;
    }
    if (st->L <= 3) {                            /* a big factory: offices, sawtooth halls, two stacks */
        float f;
        P(0, 0, 16, 16, H(0x868a90));
        bevel(0.5f, 0.5f, 7, 3.5f, H(0xc7c2b6));
        for (f = 1.5f; f < 7.0f; f += 1.5f)
            dot(f, 2, H(0x8fb6cf));
        for (k = 0; k < 12; k++)
            P(0.5f + (float)k, 4.6f, 1, 11, k % 2 ? H(0x7a7f85) : H(0xbcc0c5));
        P(12.8f, 4, 3, 11.5f, H(0x6f747b));
        chimney(13.3f, 5);
        chimney(13.3f, 10.5f);
    } else if (st->L == 4) {                     /* a power plant: a cooling tower, tanks */
        P(0, 0, 16, 16, H(0x7f848a));
        blob(0.8f, 0.8f, 8, 8, H(0xc9ccd0));
        blob(2.6f, 2.6f, 4.4f, 4.4f, H(0x55595f));
        blob(3.4f, 3.4f, 2.8f, 2.8f, H(0x3a3d42));
        if (nsmoke < NSMOKE) {
            smoke[nsmoke].x = SXf + 4.8f * SK;
            smoke[nsmoke].y = SYf + 4.8f * SK;
            smoke[nsmoke].k = SK * 1.6f;
            nsmoke++;
        }
        tank(10, 1, 4);
        tank(10, 6, 4);
        bevel(1, 10, 9, 5, H(0xa7acb2));
        for (k = 2; k < 9; k += 2)
            P((float)k, 10.5f, 0.5f, 4, H(0x7d8288));
        chimney(12, 12);
    } else {                                     /* a logistics hub: a huge roof, the truck yard */
        static const uint16_t CC[4] = {H(0xf0f0ee), H(0xe8b931), H(0xd9473f), H(0x3f8fd0)};
        float f;
        P(0, 0, 16, 16, H(0x4b4e54));
        bevel(0.5f, 0.5f, 15, 8.5f, H(0xa3a9b0));
        for (f = 1.5f; f < 9.0f; f += 1.2f)
            P(1, f, 14, 0.35f, H(0x8a9097));
        for (k = 0; k < 7; k++)
            P(1.0f + (float)k * 2.1f, 9, 1.4f, 0.8f, H(0x2f3337));
        for (k = 0; k < 5; k++)
            P(1.0f + (float)k * 3.0f, 10.8f + (float)(k % 2) * 2.6f, 2.4f, 1.1f, CC[k % 4]);
    }
}
static void sprite_civ(const st_t *st)
{
    float v = city_hashf(C.seed, st->x, st->y), x, y;
    int k;
    switch (st->kind) {
    case CV_TOWNHALL:
        P(0, 0, 16, 16, H(0xcfc6b0));
        bevel(2, 1.5f, 12, 8, H(0xe3d49c));
        blob(5.5f, 3, 5, 5, H(0x7fb8a4));
        P(6.4f, 3.8f, 1.8f, 1.4f, H(0xa8dbc8));
        P(3, 9.5f, 10, 1, H(0xefe6c4));
        P(0, 12, 16, 4, H(0x79ad5a));
        P(7, 10.5f, 2, 5.5f, H(0xcfc6b0));
        P(7.6f, 11.2f, 0.4f, 2.4f, H(0x5d6168));
        P(8, 11.2f, 1.4f, 0.9f, H(0xd9473f));
        tree(2.5f, 13.5f, 1.5f, v);
        tree(13.5f, 13.5f, 1.5f, 1.0f - v);
        break;
    case CV_GAS:
        P(0, 0, 8, 8, H(0x7f838a));
        P(0.8f, 0.8f, 6.4f, 3.4f, H(0xf2f2f0));
        P(0.8f, 3.4f, 6.4f, 0.8f, H(0xd9473f));
        P(1.6f, 4.8f, 0.8f, 0.8f, H(0xd9473f));
        P(3.6f, 4.8f, 0.8f, 0.8f, H(0xd9473f));
        P(5.6f, 4.8f, 0.8f, 0.8f, H(0xd9473f));
        bevel(0.8f, 6, 3.6f, 1.6f, H(0xd8d2c4));
        P(6, 6, 1.2f, 1.6f, H(0xe8b931));
        break;
    case CV_FIRE:
        P(0, 0, 8, 8, H(0x8d9096));
        bevel(0.8f, 0.8f, 6.4f, 4.4f, H(0xb8463c));
        P(1.6f, 2.4f, 4.8f, 0.5f, H(0x8f332b));
        P(1, 5.2f, 2.6f, 0.5f, H(0xf0f0ea));
        P(4.4f, 5.2f, 2.6f, 0.5f, H(0xf0f0ea));
        P(1.2f, 6, 3, 1.5f, H(0xe03a2e));
        P(1.6f, 6.2f, 1.2f, 0.4f, H(0xf0f0ea));
        dot(3.6f, 6.4f, H(0xbfe0f2));
        break;
    case CV_POLICE:
        P(0, 0, 8, 8, H(0x8d9096));
        bevel(0.8f, 0.8f, 6.4f, 4.4f, H(0x3f5fa8));
        P(1.6f, 2.6f, 4.8f, 0.5f, H(0x2c4a8a));
        dot(3.6f, 1.5f, H(0xf2d36b));
        P(0.8f, 5.8f, 2.6f, 1.4f, H(0xf2f2ee));
        P(0.8f, 5.8f, 2.6f, 0.5f, H(0x22262b));
        dot(2, 6.5f, H(0x4fa3ff));
        P(4.6f, 5.8f, 2.6f, 1.4f, H(0xf2f2ee));
        P(4.6f, 5.8f, 2.6f, 0.5f, H(0x22262b));
        dot(5.8f, 6.5f, H(0xff3b30));
        break;
    case CV_SCHOOL:
        P(0, 0, 16, 16, SEAS[SC_YARD]);
        bevel(1, 1, 9, 4, H(0xc97b52));
        bevel(1, 1, 4, 9, H(0xc97b52));
        P(1.8f, 1.8f, 7.4f, 0.6f, H(0xe09a72));
        P(1, 11, 5.5f, 4.2f, H(0xe3cf9a));
        P(1.8f, 11.8f, 1.8f, 1.2f, H(0xd9473f));
        P(4, 12, 1.6f, 0.8f, H(0x3f8fd0));
        P(2.4f, 13.6f, 2.6f, 1, H(0xe8b931));
        blob(8.5f, 6, 7, 9.5f, H(0xc95a45));
        blob(9.8f, 7.3f, 4.4f, 6.9f, H(0x6fae5a));
        P(12, 7.6f, 0.3f, 6.3f, H(0xf0f0ea));
        P(5.8f, 6.2f, 2.4f, 1, H(0xe8b931));
        tree(13.5f, 2.5f, 1.5f, v);
        break;
    case CV_HOSPITAL:
        P(0, 0, 16, 16, H(0xc9cdd1));
        P(0, 0, 3, 16, H(0x79ad5a));
        tree(1.5f, 2, 1.3f, v);
        tree(1.5f, 7, 1.3f, 1.0f - v);
        bevel(3.5f, 1, 11.5f, 5, H(0xf6f6f3));
        bevel(7, 1, 4.5f, 13, H(0xf6f6f3));
        P(8.6f, 3.2f, 1.3f, 5.2f, H(0xd9473f));
        P(6.6f, 5.2f, 5.3f, 1.3f, H(0xd9473f));
        blob(11.8f, 10, 3.8f, 3.8f, H(0x5a5e64));
        P(12.7f, 10.9f, 0.5f, 2, H(0xf0f0ea));
        P(14.2f, 10.9f, 0.5f, 2, H(0xf0f0ea));
        P(12.7f, 11.7f, 2, 0.45f, H(0xf0f0ea));
        P(3.8f, 13.5f, 2, 1.1f, H(0xf0f0ea));
        dot(4.4f, 13.7f, H(0xd9473f));
        break;
    case CV_STATION: {
        static const float TY[2] = {1.5f, 12.0f};
        P(0, 0, 16, 16, H(0x9fa49a));
        for (k = 0; k < 2; k++) {
            float ty = TY[k];
            P(0, ty, 16, 2.5f, H(0x5d5348));
            for (x = 0.5f; x < 16.0f; x += 1.4f)
                P(x, ty, 0.5f, 2.5f, H(0x7a6d5e));
            P(0, ty + 0.5f, 16, 0.3f, H(0xbfc3c8));
            P(0, ty + 1.7f, 16, 0.3f, H(0xbfc3c8));
        }
        P(2, 1.3f, 11, 2.9f, H(0xe8e6ef));
        P(2, 2.5f, 11, 0.5f, H(0x3f8fd0));
        for (k = 3; k < 13; k += 2)
            dot((float)k, 1.8f, H(0x9fc4dc));
        bevel(0.5f, 5, 15, 5.8f, H(0xa374d4));
        P(0.5f, 7.6f, 15, 0.5f, H(0xc9a6ee));
        break;
    }
    case CV_STADIUM: {
        static const uint16_t FL[4] = {H(0xd9473f), H(0x3f8fd0), H(0xe8b931), H(0xf0f0ea)};
        P(0, 0, 24, 24, H(0x9fa49a));
        blob(1, 1, 22, 22, H(0xbfc6cd));
        blob(2, 2, 20, 20, H(0xd6dce1));
        for (k = 0; k < 4; k++)
            P(4.0f + (float)k * 4.5f, 2.4f, 2.5f, 1, FL[k]);
        P(5, 5, 14, 14, H(0x4f9a4a));
        for (k = 5; k < 19; k += 2)
            P((float)k, 5, 1, 14, H(0x5aa654));
        outline(6, 6, 12, 12, H(0xeef2ec));
        P(11.8f, 6, 0.4f, 12, H(0xeef2ec));
        outline(10, 10, 4, 4, H(0xeef2ec));
        break;
    }
    case CV_PARK:
        P(0, 0, 16, 16, SEAS[SC_PARK]);
        outline(2, 2, 12, 12, H(0xd9cfae));
        P(2, 7.6f, 12, 0.8f, H(0xd9cfae));
        tree(4, 4.5f, 1.8f, v);
        tree(11.5f, 4, 2, 1.0f - v);
        tree(5, 11.5f, 2.1f, v);
        tree(12, 11.8f, 1.7f, v);
        tree(8, 4, 1.3f, 1.0f - v);
        tree(0.8f, 14.5f, 1.2f, v);
        tree(15, 1.2f, 1.2f, v);
        dot(7, 9.5f, H(0x8a6a42));
        dot(9.5f, 6.3f, H(0x8a6a42));
        break;
    case CV_POCKET:
        P(0, 0, 8, 8, SEAS[SC_PARK]);
        P(0, 3.6f, 8, 0.8f, H(0xd9cfae));
        tree(2.5f, 2, 1.7f, v);
        tree(5.5f, 6, 1.6f, 1.0f - v);
        dot(6, 2.8f, H(0x8a6a42));
        break;
    case CV_PLAYGROUND:
        P(0, 0, 8, 8, SEAS[SC_PARK]);
        P(0.6f, 0.6f, 6.8f, 6.8f, H(0xe3cf9a));
        P(1.2f, 1.2f, 1.2f, 3, H(0xd9473f));
        P(1.2f, 1.2f, 2.4f, 1, H(0xe8b931));
        P(4.4f, 1.4f, 2.6f, 0.4f, H(0x5d6168));
        dot(4.8f, 2, H(0x3f8fd0));
        dot(6.2f, 2, H(0x3f8fd0));
        blob(4, 4.4f, 3, 2.4f, H(0x3c9a5f));
        dot(1.6f, 5.6f, H(0xd9473f));
        break;
    case CV_GARDEN:
        P(0, 0, 8, 8, SEAS[SC_PARK]);
        for (k = 0; k < 4; k++) {
            P(0.8f, 0.8f + (float)k * 1.7f, 6.4f, 1.1f, H(0x7a5a3e));
            P(1, 1.0f + (float)k * 1.7f, 6, 0.6f, k % 2 ? H(0x7fbf4f) : H(0x5fa04a));
        }
        dot(2, 1.2f, H(0xd9473f));
        dot(4.6f, 4.6f, H(0xe8b931));
        P(5.8f, 6.2f, 1.6f, 1.4f, H(0xc9b48a));
        break;
    case CV_PLAZA:
        P(0, 0, 8, 8, H(0xd8d0bc));
        for (y = 0; y < 8.0f; y += 2.0f)
            for (x = (float)(((int)y / 2) % 2); x < 8.0f; x += 2.0f)
                P(x, y, 1, 1, H(0xcfc6ae));
        blob(2.4f, 2.4f, 3.2f, 3.2f, H(0xb7ad96));
        blob(2.9f, 2.9f, 2.2f, 2.2f, H(0x5fa8d8));
        dot(3.6f, 3.6f, H(0xe9f7fd));
        tree(1, 1, 1, v);
        tree(7, 1, 1, 1.0f - v);
        tree(1, 7, 1, 1.0f - v);
        tree(7, 7, 1, v);
        break;
    case CV_POND:
        P(0, 0, 16, 16, SEAS[SC_PARK]);
        blob(3, 4, 10, 8, H(0x4f8fc0));
        blob(4, 3, 8, 10, H(0x4f8fc0));
        blob(5, 5, 6, 6, H(0x5d9ccc));
        dot(6, 7, H(0xf0f0ea));
        dot(9, 9, H(0xf0f0ea));
        outline(1, 1, 14, 14, H(0xd9cfae));
        tree(2, 2.5f, 1.4f, v);
        tree(14, 13.5f, 1.5f, 1.0f - v);
        tree(13.5f, 2.5f, 1.3f, v);
        tree(2.5f, 13.5f, 1.6f, v);
        break;
    case CV_FIELDS:
        P(0, 0, 16, 16, SEAS[SC_PARK]);
        P(1, 1, 14, 7, H(0x5aa14c));
        for (k = 1; k < 15; k += 2)
            P((float)k, 1, 1, 7, H(0x63ab55));
        outline(1.5f, 1.5f, 13, 6, H(0xeef2ec));
        P(7.8f, 1.5f, 0.4f, 6, H(0xeef2ec));
        P(1.5f, 3.5f, 1.2f, 2, H(0xeef2ec));
        P(13.3f, 3.5f, 1.2f, 2, H(0xeef2ec));
        P(3, 9, 10, 6.5f, H(0x5aa14c));
        blob(5.5f, 10.5f, 5, 4.5f, H(0xc99c6a));
        blob(6.8f, 11.6f, 2.4f, 2.2f, H(0x5aa14c));
        dot(7.7f, 14.2f, H(0xf0f0ea));
        dot(5.8f, 12.2f, H(0xf0f0ea));
        dot(9.6f, 12.2f, H(0xf0f0ea));
        dot(7.7f, 10.4f, H(0xf0f0ea));
        break;
    case CV_CEMETERY:
        P(0, 0, 16, 16, H(0x6f9f5a));
        outline(0.5f, 0.5f, 15, 15, H(0x3f5f34));
        for (y = 2.5f; y < 13.0f; y += 2.2f)
            for (x = 2.5f; x < 11.0f; x += 1.8f)
                P(x, y, 0.8f, 1, H(0xc3c6ca));
        bevel(11.8f, 11.5f, 3, 3, H(0xc9c3b5));
        P(13, 11, 0.6f, 1.2f, H(0x8d8d8d));
        tree(13, 3.5f, 1.8f, v);
        tree(13.2f, 8, 1.4f, 1.0f - v);
        break;
    case CV_MARINA: {
        static const float BOAT[5][2] = {{3.8f, 8}, {3.8f, 12}, {8.9f, 9.5f}, {10.6f, 13}, {13.8f, 8.5f}};
        P(0, 0, 16, 16, H(0x3871a0));
        P(0, 0, 16, 6, H(0xcfc6b0));
        bevel(1, 0.8f, 6, 3.6f, H(0xe7e2d4));
        P(9, 1.5f, 6, 3, H(0x4b4e54));
        P(0, 5.5f, 16, 1.2f, H(0xb08a5a));
        P(2.5f, 6.5f, 0.9f, 9, H(0xb08a5a));
        P(7.5f, 6.5f, 0.9f, 9, H(0xb08a5a));
        P(12.5f, 6.5f, 0.9f, 9, H(0xb08a5a));
        for (k = 0; k < 5; k++) {
            P(BOAT[k][0], BOAT[k][1], 2.2f, 1.2f, H(0xf4f4f2));
            dot(BOAT[k][0] + 0.8f, BOAT[k][1] + 0.3f, H(0x3f8fd0));
        }
        break;
    }
    case CV_GOLF:
        P(0, 0, 24, 24, H(0x6aa04f));
        blob(2, 15, 18, 6, H(0x86c068));
        blob(12, 3, 9, 14, H(0x86c068));
        blob(16, 3.5f, 5, 4.5f, H(0xa4d987));
        P(18.2f, 2, 0.4f, 3.4f, H(0xf4f4f2));
        P(18.6f, 2, 1.4f, 1, H(0xd9473f));
        blob(3, 16.5f, 3.5f, 2.5f, H(0xa4d987));
        blob(10, 15.5f, 3, 2, H(0xecdca8));
        blob(14.5f, 9, 2.6f, 2, H(0xecdca8));
        blob(5, 7, 5, 4, H(0x4f8fc0));
        tree(3, 3, 2, v);
        tree(7, 2.5f, 1.6f, 1.0f - v);
        tree(2.5f, 11.5f, 1.8f, v);
        tree(21.5f, 20.5f, 1.8f, 1.0f - v);
        tree(9.5f, 11.5f, 1.4f, v);
        break;
    default: break;
    }
}
static const uint16_t LOW_R[5] = {H(0xc8664c), H(0xb9785e), H(0xd2b48f), H(0xddd0b9), H(0xece6da)};
static const uint16_t LOW_C[5] = {H(0x6fb0c4), H(0x5a9fcf), H(0xc9ccd0), H(0x7f9fb8), H(0xa9dcf2)};
static const uint16_t LOW_I[5] = {H(0xb5a46c), H(0xa7acb2), H(0x8b9198), H(0x9aa0a6), H(0x7a8088)};
static const uint16_t LOW_CIV[CV_N] = {H(0xd9c98e), H(0xd9473f), H(0xb8463c), H(0x3f5fa8), H(0xd0915a), H(0xf4f4f2), H(0xa374d4), H(0xbfc6cd),
                                       H(0x5ea84b), H(0x5ea84b), H(0xe3cf9a), H(0x7a5a3e), H(0xd8d0bc), H(0x4f8fc0), H(0x5aa14c), H(0x6f9f5a),
                                       H(0x3871a0), H(0x86c068)};
static void draw_struct(const st_t *st, int px, int py, int s)
{
    int n = st->size, w = s * n;
    if (s < 6) {
        uint16_t c = st->type == BT_CIV ? LOW_CIV[st->kind] : (st->flags & SF_FARM) ? H(0xa9b862)
                                                            : (st->type == BT_RES ? LOW_R : st->type == BT_COM ? LOW_C : LOW_I)[st->L - 1];
        cv_rect(px, py, w, w, c);
        if (s >= 3) {
            if (st->kind == CV_HOSPITAL && st->type == BT_CIV) {
                cv_rect(px + s - 1, py + 1, 2, s * 2 - 2, H(0xd9473f));
                cv_rect(px + 1, py + s - 1, s * 2 - 2, 2, H(0xd9473f));
            } else if (st->type == BT_CIV && (st->kind == CV_SCHOOL || st->kind == CV_STADIUM)) {
                cv_rect(px + s, py + s, s, s, st->kind == CV_SCHOOL ? H(0xc95a45) : H(0x4f9a4a));
            } else if (st->type == BT_IND && st->L >= 3 && !(st->flags & SF_FARM)) {
                cv_rect(px + w - 2, py, 2, 2, H(0x5a3a34));
            }
        }
    } else {
        at(px, py, s);
        if (st->type == BT_RES)
            sprite_r(st);
        else if (st->type == BT_COM)
            sprite_c(st, n);
        else if (st->type == BT_IND)
            sprite_i(st, n);
        else
            sprite_civ(st);
    }
    if (season.snow > 0.05f && !(st->flags & SF_FARM) && !(st->type == BT_CIV && civ_green(st->kind)))
        rect_mix(px + 1, py + 1, w - 2, (int)((float)w * 0.55f + 0.5f) < 1 ? 1 : (int)((float)w * 0.55f + 0.5f), H(0xf6f8fa),
                 (uint32_t)(115.0f * season.snow));
    if (C.tick - st->born < 5u)
        rect_mix(px, py, w, w, H(0xfffae4), (uint32_t)(140.0f * (1.0f - (float)(C.tick - st->born) / 5.0f)));
}
static float st_height(const st_t *st)
{
    static const float HR[6] = {0, 1, 1.5f, 3, 4.5f, 6.5f}, HC[6] = {0, 1, 1, 1.5f, 4, 7}, HI[6] = {0, 1, 1.2f, 1.5f, 1.5f, 1.5f};
    static const float CIVH[8] = {2, 0.8f, 1.2f, 1.2f, 1.2f, 3, 1, 2};
    if (st->flags & SF_FARM)
        return 0.6f;
    if (st->type == BT_CIV)
        return st->kind < 8 ? CIVH[st->kind] : 0.0f;
    if (st->type == BT_RES)
        return HR[st->L];
    if (st->type == BT_COM)
        return HC[st->L] * (st->size > 1 && st->L == 5 ? 1.4f : 1.0f);
    return HI[st->L];
}

/* ---------------------------------------------------------- the traffic --- */
static void draw_cars(int s)
{
    rect_t vr = view_rect(1.0f);
    int k;
    for (k = 0; k < NCAR; k++) {
        const car_t *c = &cars[k];
        float x, y, cx, cy, len;
        int dx, dy, horiz, wid, w, h, x0, y0;
        if (!c->on)
            continue;
        car_pos(c, &x, &y, &dx, &dy);
        if (!in_rect(&vr, x, y))
            continue;
        cx = (float)VOX + x * (float)s;
        cy = (float)VOY + y * (float)s;
        if (s < 6) {
            cv_rect((int)(cx - 0.5f), (int)(cy - 0.5f), 1, 1, c->kind == VK_BUS ? H(0xe8a531) : c->kind >= VK_FIRE ? H(0xe03a2e) : CARCOL[c->col]);
            continue;
        }
        len = (c->kind == VK_BUS ? 0.8f : c->kind == VK_TRUCK || c->kind == VK_FIRE ? 0.64f : c->kind == VK_AMB ? 0.6f : 0.5f) * (float)s;
        wid = (int)(0.28f * (float)s + 0.5f);
        if (wid < 2)
            wid = 2;
        horiz = dx != 0;
        w = horiz ? (int)(len + 0.5f) : wid;
        h = horiz ? wid : (int)(len + 0.5f);
        x0 = (int)fm_floorf(cx - (float)w * 0.5f + 0.5f);
        y0 = (int)fm_floorf(cy - (float)h * 0.5f + 0.5f);
        if (c->kind >= VK_FIRE) {
            int blink = (plat_ms() / 140u) & 1u;
            cv_rect(x0, y0, w, h, c->kind == VK_FIRE ? H(0xe03a2e) : H(0xf2f2ee));
            if (horiz)
                cv_rect(x0, y0 + (h >> 1), w, 1, c->kind == VK_FIRE ? H(0xf0f0ea) : c->kind == VK_POLICE ? H(0x2c4a9a) : H(0xd9473f));
            else
                cv_rect(x0 + (w >> 1), y0, 1, h, c->kind == VK_FIRE ? H(0xf0f0ea) : c->kind == VK_POLICE ? H(0x2c4a9a) : H(0xd9473f));
            cv_rect(x0 + (w >> 1), y0 + (h >> 1), 1, 1,
                    c->kind == VK_POLICE ? (blink ? H(0xff3b30) : H(0x4fa3ff)) : c->kind == VK_AMB ? (blink ? H(0xff3b30) : H(0xffffff))
                                                                                     : (blink ? H(0x4fa3ff) : H(0xffffff)));
        } else if (c->kind == VK_BUS) {
            int j;
            cv_rect(x0, y0, w, h, H(0xe8a531));
            for (j = 1; j < (horiz ? w : h) - 1; j += 2)
                cv_rect(horiz ? x0 + j : x0, horiz ? y0 : y0 + j, 1, 1, H(0xf6e7b8));
        } else if (c->kind == VK_TRUCK) {
            int cab = (int)(len * 0.3f) < 1 ? 1 : (int)(len * 0.3f);
            cv_rect(x0, y0, w, h, H(0xeceeea));
            if (horiz)
                cv_rect(dx > 0 ? x0 + w - cab : x0, y0, cab, h, CARCOL[c->col]);
            else
                cv_rect(x0, dy > 0 ? y0 + h - cab : y0, w, cab, CARCOL[c->col]);
        } else {
            cv_rect(x0, y0, w, h, CARCOL[c->col]);
            if (horiz)
                rect_mix(dx > 0 ? x0 + w - 2 : x0 + 1, y0, 1, h, H(0x192332), 190);
            else
                rect_mix(x0, dy > 0 ? y0 + h - 2 : y0 + 1, w, 1, H(0x192332), 190);
        }
        if (k == ui.ride || (ui.full && k == solo_car)) {   /* the one the camera rides with */
            if ((plat_ms() / 400u) & 1u)
                rect_mix(x0 - 1, y0 - 1, w + 2, h + 2, H(0xffffff), 70);
        }
    }
}
static void bubble(int px, int py, int icon)
{
    int bx = px - 3, by = py - 12, r, q;
    if (bx < 1)
        bx = 1;
    if (bx > cam.vw - 8)
        bx = cam.vw - 8;
    if (by < 1)
        by = 1;
    rect_mix(bx, by - 1, 7, 9, H(0x1c2026), 190);
    rect_mix(bx - 1, by, 9, 7, H(0x1c2026), 190);
    rect_mix(px - 1, by + 7, 3, 2, H(0x1c2026), 190);
    cv_rect(bx + 1, by, 5, 7, H(0xfbfaf6));
    cv_rect(bx, by + 1, 7, 5, H(0xfbfaf6));
    cv_rect(px, by + 7, 1, 1, H(0xfbfaf6));
    for (r = 0; r < 5; r++)
        for (q = 0; q < 5; q++)
            if (ICON[icon].rows[r] >> (4 - q) & 1u)
                cv_rect(bx + 1 + q, by + 1 + r, 1, 1, ICON[icon].c);
}
static void person(int px, int py, uint8_t shirt, uint8_t skin, int s)
{
    cv_rect(px, py, 1, s >= 12 ? 2 : 1, SHIRT[shirt % 10]);
    cv_rect(px, py - 1, 1, 1, SKIN[skin % 5]);
    if (s >= 12)
        cv_rect(px, py + 2, 1, 1, H(0x3a3d44));
}
static void draw_peds(int s)
{
    rect_t vr = view_rect(1.0f);
    uint32_t now = plat_ms();
    int k;
    if (s < 6)
        return;
    for (k = 0; k < NPED; k++) {
        const ped_t *p = &peds[k];
        float x, y;
        int px, py;
        if (!p->on)
            continue;
        ped_pos(p, &x, &y);
        if (!in_rect(&vr, x, y))
            continue;
        px = tile_px(x);
        py = tile_py(y);
        if (s >= 8)
            person(px, py, p->shirt, p->skin, s);
        else
            cv_rect(px, py, 1, 1, SHIRT[p->shirt % 10]);
    }
    for (k = 0; k < NPED; k++) {                 /* what they say */
        ped_t *p = &peds[k];
        uint32_t el;
        int idx;
        float x, y;
        if (!p->on || p->c_until <= now || now < p->c_start)
            continue;
        el = now - p->c_start;
        idx = (int)(el / p->c_turn);
        if (idx >= p->nicons || ((idx % 2 == 0) != p->first))
            continue;
        if ((float)(el % p->c_turn) / (float)p->c_turn > 0.82f)
            continue;
        ped_pos(p, &x, &y);
        if (!in_rect(&vr, x, y))
            continue;
        if (p->lastbub != idx) {
            p->lastbub = (uint8_t)idx;
            if (music_on())
                en_event(E_CHIRP, p->icons[idx], 0);
        }
        bubble(tile_px(x), tile_py(y), p->icons[idx]);
    }
}

/* ---------------------------------------------------------- the sky and the night --- */
static void draw_night(int s, const uint16_t *vis, int nvis)
{
    float night = 1.0f - day_lit(), a, p = phase(), res_lit;
    rect_t r = view_rect(1.0f);
    uint32_t slot = plat_ms() / 20000u;
    int x, y, k;
    if (night < 0.02f)
        return;
    tint(H(0x0a102c), 0.58f * night);
    if (!proj.par[P_NIGHT])
        return;
    a = fm_minf(1.0f, night * 1.4f);
    for (y = (int)fm_maxf(0.0f, r.y0); y <= (int)fm_minf((float)(MH - 1), r.y1); y++)   /* the streetlights */
        for (x = (int)fm_maxf(0.0f, r.x0); x <= (int)fm_minf((float)(MW - 1), r.x1); x++) {
            int i = y * MW + x, cx, cy, g;
            if (!c_road[i] || !(LANE(i) == 0 || (c_plan[i] == PL_BELTWAY && (x + y) % 3 == 0)))
                continue;
            cx = VOX + x * s + s / 2;
            cy = VOY + y * s + s / 2;
            g = (int)((float)s * 1.2f) < 2 ? 2 : (int)((float)s * 1.2f);
            rect_add(cx - g / 2, cy - g / 2, g, g, H(0xffb450), (uint32_t)(41.0f * a));
            px_add(cx, cy, H(0xffd78c), (uint32_t)(140.0f * a));
        }
    res_lit = p > 0.74f && p < 0.96f ? 0.65f : p > 0.2f && p < 0.3f ? 0.45f : 0.2f;
    for (k = 0; k < nvis; k++) {                 /* the windows */
        const st_t *st = &c_st[vis[k]];
        float lit;
        int n, j;
        if ((st->flags & SF_FARM) || (st->type == BT_CIV && civ_green(st->kind)))
            continue;
        lit = st->type == BT_RES ? res_lit : st->type == BT_COM ? 0.4f : st->type == BT_IND ? 0.3f : 0.6f;
        n = (int)((float)((st->L + 1) * st->size * st->size) * (s >= 6 ? 1.6f : 0.5f) + 0.5f);
        if (n < 1)
            n = 1;
        for (j = 0; j < n; j++) {
            if (city_hashf(C.seed + (uint32_t)j + slot * 13u, st->x, st->y) > lit)
                continue;
            px_add(VOX + (int)(((float)st->x + 0.15f + city_hashf((uint32_t)j, st->x, st->y + 7) * 0.7f * (float)st->size) * (float)s),
                   VOY + (int)(((float)st->y + 0.15f + city_hashf((uint32_t)j, st->y, st->x + 3) * 0.7f * (float)st->size) * (float)s), H(0xffcd69),
                   (uint32_t)(230.0f * a));
        }
    }
    for (k = 0; k < NCAR; k++) {                 /* headlights, tail lights */
        float cx, cy, xx, yy;
        int dx, dy;
        if (!cars[k].on)
            continue;
        car_pos(&cars[k], &xx, &yy, &dx, &dy);
        if ((!dx && !dy) || !in_rect(&r, xx, yy))
            continue;
        cx = (float)VOX + xx * (float)s;
        cy = (float)VOY + yy * (float)s;
        rect_add((int)(cx + (float)dx * (float)s * 0.45f - 1.0f), (int)(cy + (float)dy * (float)s * 0.45f - 1.0f), 2 + (dx < 0 ? -dx : dx) * 2,
                 2 + (dy < 0 ? -dy : dy) * 2, H(0xfff0be), (uint32_t)(100.0f * a));
        px_add((int)(cx - (float)dx * (float)s * 0.3f), (int)(cy - (float)dy * (float)s * 0.3f), H(0xff2828), (uint32_t)(190.0f * a));
    }
}
static void draw_fires(int s)
{
    uint32_t now = plat_ms();
    int f;
    for (f = 0; f < NFIRE; f++) {
        const st_t *st;
        int n, px, py, w, cnt, k, j;
        if (!fires[f].on)
            continue;
        st = &c_st[fires[f].si];
        n = st->size;
        px = VOX + st->x * s;
        py = VOY + st->y * s;
        w = n * s;
        if (px > cam.vw + s || py > cam.vh + s || px + w < -s || py + w < -s)
            continue;
        cnt = n * n * (s >= 6 ? 6 : 2);
        if (cnt < 3)
            cnt = 3;
        for (k = 0; k < cnt; k++) {
            uint32_t fl = now / 90u + (uint32_t)k;
            int fx = px + (int)(city_hashf((uint32_t)k, st->x, st->y) * (float)(w - 1)), fy = py + (int)(city_hashf((uint32_t)k, st->y, st->x) * (float)(w - 1));
            int h = 1 + (int)(city_hashf(fl, k, 3) * (float)(s / 3 < 2 ? 2 : s / 3));
            cv_rect(fx, fy - h + 1, 1, h, H(0xff5a1f));
            cv_rect(fx, fy, 1, 1, H(0xffd04a));
        }
        for (j = 0; j < 5; j++) {
            float ph = (float)now / 1400.0f + (float)j / 5.0f + (float)st->x * 0.07f, r;
            ph -= fm_floorf(ph);
            r = (float)s / 8.0f * (2.0f + ph * 4.0f);
            if (r < 1.0f)
                r = 1.0f;
            rect_mix((int)((float)px + (float)w * 0.5f + ph * (float)s * 0.8f - r * 0.5f), (int)((float)py - ph * (float)s * 1.4f - r * 0.5f), (int)r,
                     (int)r, H(0x282628), (uint32_t)(140.0f * (1.0f - ph)));
        }
        if (fires[f].truck >= 0 && cars[fires[f].truck].on && cars[fires[f].truck].arrived) {   /* the hose */
            float tx, ty, bx = (float)px + (float)w * 0.5f, by = (float)py + (float)w * 0.5f;
            car_pos(&cars[fires[f].truck], &tx, &ty, 0, 0);
            tx = (float)VOX + tx * (float)s;
            ty = (float)VOY + ty * (float)s;
            for (k = 0; k < 9; k++) {
                float u = (float)now / 400.0f + (float)k / 9.0f;
                u -= fm_floorf(u);
                px_mix((int)(tx + (bx - tx) * u), (int)(ty + (by - ty) * u - fm_sinf(u * FM_PI) * (float)s * 0.6f), H(0x96cdff), 230);
            }
        }
    }
}
static void draw_ufo(int s)
{
    uint32_t now = plat_ms();
    float hover = fm_sinf((float)now / 300.0f) * 0.08f, cx = (float)VOX + ufo.ux * (float)s, cy = (float)VOY + (ufo.uy + hover) * (float)s;
    float R = (float)s * 1.4f, gx = (float)VOX + ufo.tx * (float)s, gy = (float)VOY + ufo.ty * (float)s;
    int k, ls = s >= 10 ? 2 : 1;
    if (ufo.phase == 1) {                        /* the beam */
        float rr = ((float)(now % 500u) / 500.0f) * (float)s * 0.9f;
        ellipse_mix(gx, gy, (float)s * 0.75f, (float)s * 0.5f, H(0x96ffe6), (uint32_t)(56.0f + 20.0f * fm_sinf((float)now / 90.0f)));
        ellipse_mix(gx, gy, rr + 1.0f, (rr + 1.0f) * 0.65f, H(0xbefff0), 40);
        for (k = 0; k < 6; k++) {
            float u = (float)k / 6.0f;
            rect_mix((int)(gx + (cx - gx) * u - (float)s * 0.3f), (int)(gy + (cy - gy) * u), (int)((float)s * 0.6f), (int)((cy - gy) / 6.0f) - 1 > 0 ? 1 : 2,
                     H(0x96ffe6), 40);
        }
    }
    if (s >= 6) {
        if (ufo.phase == 0) {
            person((int)gx, (int)gy, ufo.shirt, ufo.skin, s);
            if (lhyp(ufo.ux - ufo.tx, ufo.uy - ufo.ty) < 4.0f)
                bubble((int)gx, (int)gy, I_EXCLAIM);
        } else if (ufo.phase == 1 && ufo.lift < 0.95f) {
            person((int)(gx + (cx - gx) * ufo.lift), (int)(gy + (cy - gy) * ufo.lift), ufo.shirt, ufo.skin, s);
        }
    }
    ellipse_mix(cx + (float)s * 0.9f, cy + (float)s * 1.3f, R * 0.9f, R * 0.42f, H(0x0a0f14), 64);
    ellipse_mix(cx, cy, R, R * 0.45f, H(0x7f8993), 256);
    ellipse_mix(cx, cy - R * 0.06f, R * 0.84f, R * 0.3f, H(0xc3cbd2), 256);
    ellipse_mix(cx, cy - R * 0.12f, R * 0.36f, R * 0.22f, H(0x82f0d7), 242);
    px_mix((int)(cx - R * 0.15f), (int)(cy - R * 0.2f), H(0xe9fffa), 256);
    for (k = 0; k < 8; k++) {
        float a = (float)k / 8.0f * FM_TWO_PI + (float)now / 400.0f;
        cv_rect((int)(cx + fm_cosf(a) * R * 0.8f), (int)(cy + fm_sinf(a) * R * 0.33f), ls, ls, (k + (int)(now / 150u)) % 2 ? H(0xffe066) : H(0xff6a8a));
    }
}
static void draw_incidents(int s)
{
    uint32_t now = plat_ms();
    if (crash.on) {
        float cx = (float)VOX + crash.x * (float)s, cy = (float)VOY + crash.y * (float)s;
        int L = (int)(0.5f * (float)s + 0.5f), W = (int)(0.28f * (float)s + 0.5f) < 2 ? 2 : (int)(0.28f * (float)s + 0.5f), k;
        cv_rect((int)(cx - 0.2f * (float)s) - L / 2, (int)(cy - 0.12f * (float)s) - W / 2, L, W, CARCOL[crash.col[0]]);
        cv_rect((int)(cx + 0.16f * (float)s) - W / 2, (int)(cy + 0.12f * (float)s) - L / 2, W, L, CARCOL[crash.col[1]]);
        if ((now / 300u) & 1u) {
            cv_rect((int)(cx - (float)s * 0.3f), (int)(cy + (float)s * 0.3f), 1, 1, H(0xffae2a));
            cv_rect((int)(cx + (float)s * 0.3f), (int)(cy - (float)s * 0.3f), 1, 1, H(0xffae2a));
        }
        for (k = 0; k < 5; k++)
            cv_rect((int)(cx + (city_hashf((uint32_t)k, crash.i, 1) - 0.5f) * (float)s * 0.9f), (int)(cy + (city_hashf((uint32_t)k, crash.i, 2) - 0.5f) * (float)s * 0.9f),
                    1, 1, H(0xc9ccd0));
        if (crash.phase == 0)
            for (k = 0; k < 3; k++) {
                float ph = (float)now / 1500.0f + (float)k / 3.0f, r;
                ph -= fm_floorf(ph);
                r = (float)s / 8.0f * (1.0f + ph * 3.0f);
                if (r < 1.0f)
                    r = 1.0f;
                rect_mix((int)(cx + ph * (float)s * 0.5f - r * 0.5f), (int)(cy - ph * (float)s - r * 0.5f), (int)r, (int)r, H(0x6e6e73),
                         (uint32_t)(128.0f * (1.0f - ph)));
            }
    }
    if (rob.on && s >= 6) {
        float x, y;
        int px, py;
        ped_pos(&rob.robber, &x, &y);
        px = tile_px(x);
        py = tile_py(y);
        person(px, py, rob.robber.shirt, rob.robber.skin, s);
        if (rob.phase == 1) {
            const st_t *st = &c_st[rob.si];
            bubble(px, py, (now / 900u) & 1u ? I_MONEY : I_EXCLAIM);
            if ((now / 200u) & 1u) {
                int sx = VOX + st->x * s, sy = VOY + st->y * s, w = st->size * s;
                cv_rect(sx, sy, 1, 1, H(0xff3b30));
                cv_rect(sx + w - 1, sy, 1, 1, H(0xff3b30));
            }
        } else if (rob.phase == 2) {
            bubble(px, py, I_FROWN);
        } else if (rob.phase == 3) {
            bubble(px, py, I_SWEAT);
        }
    }
    if (ufo.on)
        draw_ufo(s);
}
static void draw_birds(int s)
{
    uint32_t now = plat_ms();
    int f, k;
    for (f = 0; f < NFLOCK; f++) {
        if (!flock[f].on)
            continue;
        for (k = 0; k < flock[f].n; k++) {
            float x = (float)VOX + (flock[f].x + flock[f].ox[k]) * (float)s, y = (float)VOY + (flock[f].y + flock[f].oy[k]) * (float)s;
            int X = (int)x, Y = (int)y, up = ((int)((float)now / 160.0f + flock[f].ph[k])) & 1;
            uint16_t c = flock[f].gull ? H(0xf4f4f0) : H(0x2a2d33);
            if (x < -4.0f || y < -4.0f || x > (float)cam.vw + 4.0f || y > (float)cam.vh + 4.0f)
                continue;
            rect_mix((int)(x + (float)s * 0.8f), (int)(y + (float)s * 1.1f), 2, 1, H(0x141914), 56);
            cv_rect(X, Y, 1, 1, c);
            if (up) {
                cv_rect(X - 1, Y - 1, 1, 1, c);
                cv_rect(X + 1, Y - 1, 1, 1, c);
            } else {
                cv_rect(X - 1, Y, 1, 1, c);
                cv_rect(X + 1, Y, 1, 1, c);
            }
            if (s >= 10) {
                cv_rect(X - 2, Y - (up ? 2 : 0), 1, 1, c);
                cv_rect(X + 2, Y - (up ? 2 : 0), 1, 1, c);
            }
        }
    }
}
static void draw_weather(void)
{
    uint32_t now = plat_ms();
    float rain = wlvl(W_RAIN) + wlvl(W_STORM) * 1.6f, snow = wlvl(W_SNOW), fog = wlvl(W_FOG), dk = dusk();
    float dark = 0.08f * wlvl(W_CLOUDY) + 0.14f * wlvl(W_RAIN) + 0.28f * wlvl(W_STORM) + 0.05f * snow;
    float area = (float)(cam.vw * cam.vh) / 20480.0f;
    int k;
    if (dk > 0.02f)
        tint(H(0xff7832), 0.13f * dk);
    tint(H(0x1c2434), dark);
    tint(H(0xf0f4f8), 0.12f * snow);
    if (rain > 0.01f) {
        int n = (int)(rain * 90.0f * area);
        for (k = 0; k < n; k++) {
            float y = fm_floorf(city_hashf(7u, k, 1) * (float)(cam.vh + 12)) + (float)now * 0.22f, x;
            y -= fm_floorf(y / (float)(cam.vh + 12)) * (float)(cam.vh + 12);
            y -= 6.0f;
            x = city_hashf(7u, k, 2) * (float)cam.vw + y * 0.4f + (float)cam.vw;
            x -= fm_floorf(x / (float)cam.vw) * (float)cam.vw;
            px_mix((int)x, (int)y, H(0xbed2f0), 140);
            px_mix((int)(x - 0.4f), (int)(y - 1.0f), H(0xbed2f0), 140);
            px_mix((int)(x - 0.8f), (int)(y - 2.0f), H(0xbed2f0), 140);
        }
    }
    if (snow > 0.01f) {
        int n = (int)(snow * 70.0f * area);
        for (k = 0; k < n; k++) {
            float y = city_hashf(9u, k, 1) * (float)(cam.vh + 4) + (float)now * 0.018f, x;
            y -= fm_floorf(y / (float)(cam.vh + 4)) * (float)(cam.vh + 4);
            y -= 2.0f;
            x = city_hashf(9u, k, 2) * (float)cam.vw + fm_sinf((float)now / 900.0f + (float)k) * 3.0f + (float)cam.vw;
            x -= fm_floorf(x / (float)cam.vw) * (float)cam.vw;
            px_mix((int)x, (int)y, H(0xfafcff), 230);
        }
    }
    if (fog > 0.01f) {
        tint(H(0xd6dce2), 0.38f * fog);
        for (k = 0; k < 4; k++) {
            float y = (float)(k * 37) + (float)now * 0.004f;
            y -= fm_floorf(y / (float)(cam.vh + 30)) * (float)(cam.vh + 30);
            rect_mix(0, (int)y - 15, cam.vw, 10, H(0xe8ecf0), (uint32_t)(36.0f * fog));
        }
    }
}

/* the info on the city: the hour and the sky, the people, what the camera follows */
static void sky_icon(int x, int y, uint16_t fg)
{
    int w = wx.blend > 0.5f ? wx.now : wx.prev, night = day_lit() < 0.3f;
    if (w == W_CLEAR) {
        if (night) {
            cv_rect(x + 2, y, 4, 7, fg);
            cv_rect(x + 1, y + 1, 1, 5, fg);
            cv_rect(x + 4, y + 1, 3, 5, H(0x2a2d33));
        } else {
            cv_rect(x + 2, y + 2, 3, 3, H(0xf2c94b));
            cv_rect(x + 3, y, 1, 7, H(0xf2c94b));
            cv_rect(x, y + 3, 7, 1, H(0xf2c94b));
        }
        return;
    }
    cv_rect(x + 1, y + 2, 5, 3, fg);
    cv_rect(x + 2, y + 1, 3, 1, fg);
    if (w == W_RAIN || w == W_STORM) {
        cv_rect(x + 1, y + 6, 1, 1, H(0x79b8ff));
        cv_rect(x + 3, y + 6, 1, 1, H(0x79b8ff));
        cv_rect(x + 5, y + 6, 1, 1, H(0x79b8ff));
        if (w == W_STORM)
            cv_rect(x + 3, y + 5, 1, 2, H(0xffe066));
    } else if (w == W_SNOW) {
        cv_rect(x + 1, y + 6, 1, 1, H(0xffffff));
        cv_rect(x + 4, y + 6, 1, 1, H(0xffffff));
    } else if (w == W_FOG) {
        cv_rect(x, y + 6, 7, 1, fg);
    }
}
static void pill(int x, int y, int w, int right)
{
    int x0 = right ? x - w : x;
    rect_mix(x0, y, w, 13, H(0x101215), 170);
}
static void draw_info(void)
{
    char t[24], u[24];
    int w;
    if (!proj.par[P_LABELS])
        return;
    clock_text(t);
    w = text_w(&FONT_XS, t) + 18;
    pill(3, 3, w, 0);
    sky_icon(6, 6, H(0xdfe3ea));
    cv_text(15, 1, &FONT_XS, t, H(0xeef0f2));
    s_cpy(t, "POP ");
    pop_text(C.pop, t + 4);
    w = text_w(&FONT_XS, t) + 8;
    pill(3, cam.vh - 16, w, 0);
    cv_text(7, cam.vh - 18, &FONT_XS, t, H(0xeef0f2));
    u[0] = 0;
    {
        float fx, fy;
        if (ui.ff) {
            s_cpy(u, "GROWING");
        } else if (incident_at(&fx, &fy)) {
            if (ufo.on) s_cpy(u, "UFO");
            else if (quake.on) s_cpy(u, "QUAKE");
            else if (rob.on) s_cpy(u, "ROBBERY");
            else if (crash.on) s_cpy(u, "CRASH");
            else s_cpy(u, "FIRE");
        } else if (ui.hold) {
            s_cpy(u, "HOLD");
        } else if (ui.ride >= 0 || (ui.full && solo_car >= 0)) {
            s_cpy(u, "RIDING");
        }
    }
    if (u[0]) {
        w = text_w(&FONT_XS, u) + 8;
        pill(cam.vw - 3, cam.vh - 16, w, 1);
        cv_text(cam.vw - 3 - w + 4, cam.vh - 18, &FONT_XS, u, ui.hold && !ui.ff ? K_HOLD : H(0xffd04a));
    }
}

/* the map's own views: the zones, the land value */
static void draw_map(int s, int x0, int x1, int y0, int y1)
{
    static const uint16_t ZT[6] = {0, H(0xf08a4b), H(0x4aa3e8), H(0xe6c84a), 0, H(0x96c85a)};
    static const uint16_t BTC[5] = {0, H(0xf08a4b), H(0x4aa3e8), H(0xe6c84a), H(0x5bd16a)};
    int x, y, view = proj.par[P_MAP];
    if (!view)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * MW + x, px = VOX + x * s, py = VOY + y * s, z = ZONE(i), b = c_bt[i];
            if (view == 2) {
                float v = fm_minf(1.0f, LVF(i));
                static const uint16_t G[5] = {H(0x3c50dc), H(0x2fc4d8), H(0x46d25a), H(0xf0d23c), H(0xe8463c)};
                int k2 = (int)(v * 4.0f);
                if (TERR(i) == T_WATER || c_road[i])
                    continue;
                rect_mix(px, py, s, s, mix(G[k2 > 3 ? 3 : k2], G[k2 > 3 ? 4 : k2 + 1], (int)((v * 4.0f - (float)k2) * 16.0f)), 150);
            } else {
                if (z && z != Z_X && !c_road[i] && TERR(i) != T_WATER)
                    rect_mix(px, py, s, s, ZT[z], 82);
                if (b) {
                    int m = s >= 4 ? 1 : 0;
                    rect_mix(px + m, py + m, s - 2 * m, s - 2 * m, (c_st[c_ref[i]].flags & SF_FARM) ? ZT[Z_FARM] : BTC[b], 166);
                }
            }
        }
}

/* the whole view, its rows r0 .. r0+bh, onto a canvas of bh rows */
static uint16_t vis_list[1024];
static int shake_dx, shake_dy;
static void draw_view(int r0, int bh)
{
    int s = cam.ts, x0, x1, y0, y1, x, y, k, nvis = 0;
    uint32_t sh = plat_ms() / 380u;
    cv_begin(240, (uint32_t)bh, SEAS[SC_G1]);
    cv_oy = -r0;
    VS = s;
    VOX = (int)fm_floorf((float)cam.vw * 0.5f - cam.x * (float)s + 0.5f) + shake_dx;
    VOY = (int)fm_floorf((float)cam.vh * 0.5f - cam.y * (float)s + 0.5f) + shake_dy;
    x0 = (int)fm_floorf((float)-VOX / (float)s) - 1;
    x1 = (int)((float)(cam.vw - VOX) / (float)s) + 1;
    y0 = (int)fm_floorf((float)(r0 - VOY) / (float)s) - 1;
    y1 = (int)((float)(r0 + bh - VOY) / (float)s) + 1;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > MW - 1 ? MW - 1 : x1;
    y1 = y1 > MH - 1 ? MH - 1 : y1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int i = y * MW + x, px = VOX + x * s, py = VOY + y * s;
            if (c_road[i])
                draw_road(x, y, px, py, s);
            else
                draw_ground(x, y, px, py, s, sh);
        }
    draw_rounds(s);
    draw_cars(s);
    for (y = y0 - 3 < 0 ? 0 : y0 - 3; y <= y1; y++)     /* the buildings here (the big ones may start above or left) */
        for (x = x0 - 3 < 0 ? 0 : x0 - 3; x <= x1; x++) {
            int si = c_ref[y * MW + x];
            if (si && c_st[si].x == x && c_st[si].y == y && !(c_st[si].flags & SF_DEAD) && nvis < 1024)
                vis_list[nvis++] = (uint16_t)si;
        }
    for (k = 0; k < nvis; k++) {                 /* their shadows */
        const st_t *st = &c_st[vis_list[k]];
        int hp = (int)(st_height(st) * (float)s / 8.0f + 0.5f);
        if (hp > s * 2)
            hp = s * 2;
        if (hp && !(st->type == BT_CIV && civ_green(st->kind)))
            rect_mix(VOX + st->x * s + hp, VOY + st->y * s + hp, st->size * s, st->size * s, H(0x10141c), 92);
    }
    nsmoke = 0;
    for (k = 0; k < nvis; k++) {
        const st_t *st = &c_st[vis_list[k]];
        draw_struct(st, VOX + st->x * s, VOY + st->y * s, s);
    }
    draw_peds(s);
    {
        uint32_t now = plat_ms();
        for (k = 0; k < nsmoke; k++) {           /* the stacks' smoke */
            int j;
            for (j = 0; j < 4; j++) {
                float ph = (float)now / 1600.0f + (float)j / 4.0f + smoke[k].x * 0.013f, r;
                ph -= fm_floorf(ph);
                r = smoke[k].k * (1.4f + ph * 2.6f);
                if (r < 1.0f)
                    r = 1.0f;
                rect_mix((int)(smoke[k].x + ph * smoke[k].k * 5.0f - r * 0.5f), (int)(smoke[k].y - ph * smoke[k].k * 7.0f - r * 0.5f), (int)r, (int)r,
                         H(0xeceef1), (uint32_t)(230.0f * (1.0f - ph * 0.85f)));
            }
        }
    }
    if (cloud_amt > 0.02f)                       /* the clouds' shadows */
        for (k = 0; k < NCLOUD; k++) {
            float cx = (float)VOX + cloud[k].x * (float)s, cy = (float)VOY + cloud[k].y * (float)s, r = cloud[k].r * (float)s;
            ellipse_mix(cx, cy, r, r * 0.6f, H(0x141c28), (uint32_t)(41.0f * cloud_amt));
            ellipse_mix(cx + cloud[k].r * 0.8f * (float)s, cy + cloud[k].r * 0.2f * (float)s, r * 0.7f, r * 0.45f, H(0x141c28), (uint32_t)(41.0f * cloud_amt));
        }
    draw_night(s, vis_list, nvis);
    draw_fires(s);
    {
        uint32_t now = plat_ms();
        for (k = 0; k < NDUST; k++) {            /* what fell */
            float a, u, r;
            if (!dust[k].on || now - dust[k].t0 > 1600u)
                continue;
            u = (float)(now - dust[k].t0) / 1000.0f;
            a = 1.0f - u / 1.6f;
            r = (dust[k].r + u * 0.4f) * (float)s;
            if (r < 1.0f)
                r = 1.0f;
            rect_mix((int)((float)VOX + (dust[k].x + dust[k].vx * u) * (float)s - r * 0.5f), (int)((float)VOY + (dust[k].y + dust[k].vy * u) * (float)s - r * 0.5f),
                     (int)r, (int)r, H(0x9e9484), (uint32_t)(180.0f * a));
        }
    }
    draw_incidents(s);
    draw_birds(s);
    draw_weather();
    if (bolt.on && plat_ms() - bolt.t0 < 230u && ((plat_ms() - bolt.t0) / 60u) % 2u == 0u) {   /* the bolt */
        for (k = 0; k < 7; k++) {
            int ax = tile_px(bolt.sx[k]), ay = tile_py(bolt.sy[k]), bx = tile_px(bolt.sx[k + 1]), by = tile_py(bolt.sy[k + 1]);
            cv_line(ax - 1, ay, bx - 1, by, H(0xaabeff));
            cv_line(ax + 1, ay, bx + 1, by, H(0xaabeff));
            cv_line(ax, ay, bx, by, H(0xf4f6ff));
        }
    }
    {
        float fa = 1.0f - (float)(plat_ms() - flash_at) / 260.0f;
        if (fa > 0.0f && fa <= 1.0f)
            tint(H(0xebf0ff), 0.45f * fa);
    }
    draw_map(s, x0, x1, y0, y1);
    if (!game_on())
        draw_info();
}

/* ------------------------------------------------------------- the header --- */
static const char *const STAGE_SHORT[4] = {"TOWN", "SUBURB", "CITY", "HUGE CITY"};
static void draw_header(void)
{
    int msg = msg_busy(), l;
    uint32_t h = hash(hash(2166136261u, proj.piece | (uint32_t)C.stage << 8 | (uint32_t)ui.hold << 12 | (uint32_t)ui.ff << 13),
                      en_playing | ui.dirty << 4 | (uint32_t)msg << 5 | (uint32_t)en_ext << 6 | (uint32_t)proj.par[P_THEME] << 12 |
                          (uint32_t)proj.par[P_TEMPO] << 20);
    if (en_ext)
        h = hash(h, (uint32_t)(en_bpm + 0.5f));
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1] | (uint32_t)ui.msg[1][0] << 16), ui.msg_until);
    for (l = 0; l < EN_NCH; l++)
        h = hash(h, (uint32_t)(q16(en_glow[l]) / 4));
    h = hash(h, (uint32_t)C.seed);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        static const uint16_t CHC[EN_NCH] = {H(0xff6a5a), H(0xffb04a), H(0x5ad2ff), H(0xd8dce2), H(0x9a86ff), H(0x8cff9a)};
        int32_t x = 234, w;
        char c[16], n[16];
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        if (en_ext) {
            s_cpy(c, "EXT ");
            itoa_u((uint32_t)(en_bpm + 0.5f), c + 4);
        } else {
            itoa_u((uint32_t)proj.par[P_TEMPO], c);
        }
        x -= text_w(&FONT_S, c);
        cv_text(x, 6, &FONT_S, c, en_ext ? K_ACC : K_DIM);
        x -= 8;
        if (ui.hold) {
            x -= text_w(&FONT_S, "HOLD");
            cv_text(x, 6, &FONT_S, "HOLD", K_HOLD);
            x -= 6;
        }
        if (en_playing) {
            for (l = EN_NCH - 1; l >= 0; l--) {
                x -= 7;
                cv_round(x, 11, 5, 5, 2, mix(K_PANEL, CHC[l], 4 + q16(en_glow[l]) * 3 / 4));
            }
        } else {
            int32_t j;
            x -= 10;
            for (j = 0; j < 5; j++)
                cv_rect(x + 2 + j, 9 + j, 1, 11 - 2 * j, K_DIM);
        }
        caps(n, C.name, sizeof n);
        w = text_w(&FONT_B, n) + 12;
        cv_round(4, 4, w, 20, 6, K_ACC);
        cv_text(10, 6, &FONT_B, n, K_BG);
        if (w + 10 + text_w(&FONT_S, STAGE_SHORT[C.stage]) < x - 4)
            cv_text(w + 10, 6, &FONT_S, STAGE_SHORT[C.stage], K_DIM);
    }
    cv_blit(0, 0);
}

/* -------------------------------------------------------------- help --- */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE CITY: SPEED, START AS,"); L("TRAFFIC, PEOPLE"); L("+ SELECT: THE TEMPO"); break;
    case B_ENV: L("THE MUSIC: TEMPO,"); L("DENSITY, SWING, CITY BED"); break;
    case B_ARP: L("THE SOUND: FLAVOR (NES,"); L("GB, SID), LEAD, ECHO,"); L("CRUSH"); break;
    case B_LFO: L("TIME: THE DAY, SEASONS"); L("AGAIN: THE WEATHER"); L("BLACK KEYS: THE HOUR,"); L("THE SKY"); break;
    case B_SEQ: L("THE VOICES' LEVELS"); L("AGAIN: PAD, LEAD,"); L("EFFECTS, SIRENS"); break;
    case B_SEL: L("RIDE ALONG: THE"); L("CAMERA FOLLOWS A CAR"); L("AGAIN: ANOTHER"); break;
    case B_FX: L("EVENTS: HOW OFTEN"); L("AGAIN: UFOS, FOLLOW,"); L("DISASTERS, IDLE"); L("BLACK KEYS: CALL ONE"); break;
    case B_GLO: L("SETUP: KEY, TUNE,"); L("SYNC, MOOD"); L("AGAIN: DISPLAY, MIDI OUT"); L("SEL + GLO: REMOTE"); break;
    case B_EDIT: L("A NEW CITY:"); L("A NEW SEED HERE"); L("(SAVE ON SETUP KEEPS IT)"); break;
    case B_REC: L("HOLD: THE CITY STOPS"); L("GROWING; LIFE AND"); L("THE MUSIC GO ON"); break;
    case B_OCTDN: L("THE LEAD AN"); L("OCTAVE DOWN"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE LEAD AN"); L("OCTAVE UP"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("ON SETUP: SAVES ALL TEN"); L("ELSEWHERE: FROM ITS"); L("FOUNDING; HOLD: THE RECIPE"); break;
    default: break;
    }
#undef L
    return n;
}
static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(16, 14, 208, 116, 6, K_PANEL);
    cv_round(16, 14, 208, 26, 6, K_ACC);
    cv_rect(16, 34, 208, 6, K_ACC);
    text_c(120, 18, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 50 + k * 19, &FONT_S, l[k], K_TEXT);
}

#include "game.c"

static void draw_main(void)
{
    if (game_on()) {                             /* the secret: the whole screen */
        if (ui.frame % 3u)
            return;
        game_update();
        cam.vw = 240;
        cam.vh = 240;
        draw_view(0, 120);
        game_draw(0);
        cv_blit(0, 0);
        draw_view(120, 120);
        game_draw(120);
        cv_blit(0, 120);
        return;
    }
    if (ui.help) {
        uint32_t h = hash(0x48454C50u, ui.help);
        if (ui.frame % 3u)
            return;
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            draw_help();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    ui.sig[1] = 0;
    if (ui.full) {                               /* the idle view: the whole screen, fifteen frames a second */
        if (ui.frame % 4u)
            return;
        draw_view(0, 120);
        cv_blit(0, 0);
        draw_view(120, 120);
        cv_blit(0, 120);
        return;
    }
    if (ui.frame % 3u)                           /* twenty frames a second */
        return;
    draw_view(0, MAIN_H);
    cv_blit(0, MAIN_Y);
}

/* the knob strip: a rule with the page's name and the walk's dots at its right end; a cell a knob */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)proj.par[P_THEME] << 20), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(view_knob(ui.view, (int)i)));
    h = hash(h, (uint32_t)now_touch);
    if (ui.view == V_OUT)
        h = hash(h, plat_cpu_pct());
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {
        char t[24];
        int k;
        int32_t xd = 237 - NWALK * 6, xt;
        caps(t, VIEW_NAME[ui.view], sizeof t);
        xt = xd - 5 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 9, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, K_ACC);
        for (k = 0; k < NWALK; k++)
            cv_round(xd + k * 6, 3, 4, 4, 2, k == ui.view ? K_ACC : K_LINE);
    }
    for (i = 0; i < 4; i++) {
        int k = view_knob(ui.view, (int)i), lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[16];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : K_COLD;
        if (k == K_NONE || (k == 0 && ui.view == V_OUT))
            continue;
        if (i)
            cv_rect(x, 18, 1, 44, K_LINE);
        if (hot)
            cv_round(x + 3, 12, 54, 56, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        caps(t, KNOB_LABEL[ui.view][i], sizeof t);
        {
            const felucca_font_t *f = text_w(&FONT_S, t) <= 57 ? &FONT_S : &FONT_XS;
            text_c(cx, f == &FONT_S ? 12 : 13, f, t, hot ? K_TEXT : K_DIM);
        }
        if (k != K_CPU && hi - lo < 6) {
            int nn = hi - lo + 1, j, pw = nn > 4 ? 6 : 9, gap = 3, w0 = nn * (pw + gap) - gap;
            for (j = 0; j < nn; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 37, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else if (k != K_CPU) {
            arc(cx, 40, 12, hi > lo ? (float)(v - lo) / (float)(hi - lo) : 0.0f, K_LINE, fill, bg, lo < 0);
        }
        knob_text(k, t);
        {
            const felucca_font_t *f = text_w(&FONT_B, t) <= 57 ? &FONT_B : text_w(&FONT_S, t) <= 57 ? &FONT_S : &FONT_XS;
            text_c(cx, f == &FONT_XS ? 53 : 52, f, t, hot ? K_ACC : K_TEXT);
        }
    }
    cv_blit(0, KNB_Y);
}

/* ------------------------------------------------------------- the lights --- */
/* the idle view's lights: the music on the panel. Each channel lights a page button as it plays (HOME Pulse A,
 * ENV Pulse B, ARP the triangle, LFO the noise, SEQ the wave, FX the lead) and the key of its note; OCT- and
 * OCT+ take the beats in turn; GLO flashes with the lightning, SAVE while an incident goes on, REC while the
 * city holds */
static void leds_show(void)
{
    static const uint8_t PB[EN_NCH] = {B_HOME, B_ENV, B_ARP, B_LFO, B_SEQ, B_FX};
    uint32_t b = 0, k = 0, i;
    float fx, fy;
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.hold)
        b |= 1u << B_REC;
    if (proj.leds) {
        for (i = 0; i < EN_NCH; i++)
            if (en_glow[i] > 0.4f) {
                int n = en_last[i];
                b |= 1u << PB[i];
                if (i != CH_NOI && n > 0) {
                    while (n < 53)
                        n += 12;
                    while (n > 79)
                        n -= 12;
                    k |= 1u << (n - 53);
                }
            }
        if (en_playing)
            b |= 1u << (en_step / 4u & 1u ? B_OCTUP : B_OCTDN);
        if (plat_ms() - flash_at < 300u)
            b |= 1u << B_GLO;
        if (incident_at(&fx, &fy) && (ui.frame / 20u) % 2u == 0u)
            b |= 1u << B_SAVE;
    }
    plat_glow(0);
    plat_leds(b, k);
}
static void leds(void)
{
    uint32_t b = 0, k = 0, i, pcs = en_pcs;
    if (ui.full || game_on()) {
        leds_show();
        return;
    }
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.hold)
        b |= 1u << B_REC;
    if (ui.ride >= 0)
        b |= 1u << B_SEL;
    if (proj.leds) {
        for (i = 0; i < NKEYS; i++)              /* the lead's scale */
            if (pcs >> ((5u + i) % 12u) & 1u)
                k |= 1u << i;
        k |= ui.lead_keys;
        if (ui.view == V_WEATHER) {              /* the sky chosen */
            for (i = 0; i < 7; i++)
                k &= ~(1u << BLACK_KEY[i]);
            k |= 1u << BLACK_KEY[proj.par[P_WEATHER] % 7];
        } else if (ui.view == V_TIME) {          /* the part of the day */
            float p = phase();
            int part = p < 0.2f || p >= 0.9f ? 3 : p < 0.35f ? 0 : p < 0.7f ? 1 : 2;
            for (i = 0; i < 4; i++)
                k &= ~(1u << BLACK_KEY[i]);
            k |= 1u << BLACK_KEY[part];
        } else if (ui.view == V_EVENTS || ui.view == V_EVENTS2) {   /* what's going on */
            int f, anyfire = 0, birds = 0;
            for (f = 0; f < NFIRE; f++)
                anyfire |= fires[f].on;
            for (f = 0; f < NFLOCK; f++)
                birds |= flock[f].on;
            for (i = 0; i < 6; i++)
                k &= ~(1u << BLACK_KEY[i]);
            if (anyfire || (bolt.on && plat_ms() - bolt.t0 < 600u)) k |= 1u << BLACK_KEY[0];
            if (quake.on) k |= 1u << BLACK_KEY[1];
            if (rob.on) k |= 1u << BLACK_KEY[2];
            if (crash.on) k |= 1u << BLACK_KEY[3];
            if (ufo.on) k |= 1u << BLACK_KEY[4];
            if (birds) k |= 1u << BLACK_KEY[5];
        }
    }
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* ------------------------------------------------------------- the boot cards --- */
/* As HYP-1's, ECO-1's and GRAV-1's. First RedtailWorks: a pixel red panda on the name; then CTY-1 in big
 * pixel letters over a skyline whose windows light up as the town starts. Any button or key: straight in. */
#define PANDA_W 32
#define PANDA_H 25
static const char *const PANDA[PANDA_H] = {
    ".WWW..............WWW...........", ".WBBW............WBBW...........", ".WBBRRRRRRRRRRRRRRBBW...........",
    "..RRRRRRRRRRRRRRRRRR............", ".RRRWWWRRRRRRRRWWWRRR...........", ".RRRRRRRRRRRRRRRRRRRR...........",
    ".RRRRKKRRRRRRRRKKRRRR...........", ".RWWDKKDRRRRRRDKKDWWR...........", ".WWWWDDWWWWWWWWDDWWWW...........",
    ".WWWWWDDWWKKWWDDWWWWW...........", "..WWWWWDDWWWWDDWWWWW........DDD.", "...DWWWWDDWWDDWWWWD........DDDDD",
    "....DDWWWWWWWWWWDD.........TTTTT", "......DDDDDDDDDD...........TTTTT", "......DRRRRRRRRD...........RRRRR",
    ".....BRRRRRRRRRRB..........RRRRR", "....BBRRRRRRRRRRBB.........TTTTT", "....BBBRRRRRRRRBBB........TTTTTT",
    "...BBBBBRRRRRRBBBBB.......RRRRRR", "...BBBBBBBBBBBBBBBB......RRRRRR.", "...BBBBBBBBBBBBBBBBRRTTRRTTTTT..",
    "...BBBBBBBBBBBBBBBBRRTTRRTTTTT..", "....BBBB.BBBB.BBBBRRRTTRRRRRR...", "...KKBBB..BB..BBBKKDDTTDDDD.....",
    "...KKKK...KK...KKKK............."};
static uint16_t panda_col(char c)
{
    switch (c) {
    case 'R': return RGB(200, 84, 32);
    case 'D': return RGB(140, 52, 20);
    case 'T': return RGB(232, 156, 92);
    case 'W': return RGB(246, 236, 222);
    case 'B': return RGB(64, 30, 22);
    case 'K': return RGB(24, 16, 18);
    default: return 0;
    }
}
#define K_RUST RGB(200, 84, 32)
#define K_RINGS RGB(232, 156, 92)
static void draw_splash(void)
{
    int i, x, y;
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 96, K_BG);
    for (y = 0; y < PANDA_H; y++)
        for (x = 0; PANDA[y][x]; x++)
            if (PANDA[y][x] != '.')
                cv_rect(120 - PANDA_W * 3 / 2 + x * 3, 96 - PANDA_H * 3 - 4 + y * 3, 3, 3, panda_col(PANDA[y][x]));
    cv_blit(0, 0);
    cv_begin(240, 144, K_BG);
    text_c(120, 0, &FONT_L, "REDTAIL", K_RUST);
    text_c(120, 36, &FONT_L, "WORKS", K_RINGS);
    for (i = 0; i < 9; i++)
        cv_rect(48 + i * 16, 86, 12, 6, i & 1 ? K_RINGS : K_RUST);
    cv_rect(40, 80, 160, 1, K_LINE);
    cv_rect(40, 97, 160, 1, K_LINE);
    cv_rect(40, 80, 1, 18, K_LINE);
    cv_rect(199, 80, 1, 18, K_LINE);
    text_c(120, 110, &FONT_XS, "PRESENTS", K_DIM);
    cv_blit(0, 96);
}
static const uint8_t BIG_GLYPH[5][7] = {
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  /* C */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  /* T */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},  /* Y */
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},  /* - */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  /* 1 */
};
static const uint8_t SKY_H[16] = {22, 34, 18, 46, 30, 52, 26, 38, 60, 34, 44, 24, 50, 30, 20, 36};
#define SKY_Y 236
static void draw_cty(void)
{
    int g, r, c, pass;
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 120, K_BG);
    for (pass = 0; pass < 2; pass++)
        for (g = 0; g < 5; g++)
            for (r = 0; r < 7; r++)
                for (c = 0; c < 5; c++)
                    if (BIG_GLYPH[g][r] >> (4 - c) & 1u)
                        cv_rect(33 + (g * 6 + c) * 6 + (pass ? 0 : 3), 6 + r * 6 + (pass ? 0 : 3), 6, 6, pass ? RGB(232, 185, 49) : K_RUST);
    text_c(120, 60, &FONT_S, "A CITY GROWS", K_TEXT);
    text_c(120, 78, &FONT_S, "AND PLAYS THE FM-1", K_TEXT);
    text_c(120, 100, &FONT_XS, EN_VERSION[0] == 'D' ? "EXPERIMENTAL" : "EXPERIMENTAL  //  " EN_VERSION, K_RINGS);
    cv_blit(0, 20);
    for (c = 0; c < 16; c++)                     /* the skyline, dark */
        lcd_fill(6 + c * 14, SKY_Y - SKY_H[c], 13, SKY_H[c], K_PANEL);
}
static void splash_tick(void)
{
    uint32_t t = plat_ms() - ui.boot_t, t1 = ui_splash_ms * 17u / 32u, n, k = 0, i;
    if (ui.splash == 1) {
        if (t < t1)
            return;
        ui.splash = 2;
        draw_cty();
    }
    n = (t - t1) * 17u / (ui_splash_ms - t1 + 1u);
    if (n > 16u)
        n = 16u;
    for (; ui.splash_step < n; ui.splash_step++) {   /* a building's windows light */
        int c = ui.splash_step, y;
        for (y = SKY_Y - SKY_H[c] + 4; y < SKY_Y - 3; y += 6) {
            lcd_fill(8 + (uint32_t)c * 14, (uint32_t)y, 3, 3, RGB(255, 205, 105));
            lcd_fill(14 + (uint32_t)c * 14, (uint32_t)y, 3, 3, (c + y) % 3 ? RGB(255, 205, 105) : K_BG);
        }
    }
    for (i = 0; i < n; i++)
        k |= 1u << WHITE_KEY[i];
    plat_glow(1);
    plat_leds(n ? 1u << B_PLAY : 0u, k);
}

/* ------------------------------------------------------------- the frame --- */
void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.ride = -1;
    ui.boot_t = ui.last_ms = plat_ms();
    life_init();
    life_t = plat_ms();
    new_city(0);
    en_lead_oct(0);
    ui.splash = ui_splash_ms > 0u;
    if (ui.splash) {
        draw_splash();
    } else {
        lcd_fill(0, 0, 240, 240, K_BG);
        if (!en_playing)
            en_play(1);
    }
}

void ui_frame(void)
{
    uint32_t now = plat_ms(), dms = now - ui.last_ms;
    ui.last_ms = now;
    if (dms > 200u)
        dms = 200u;
    if (rm.on) {
        rm_tick();
        if (rm.on) {
            plat_glow(1);
            plat_leds(rm.lit_b, rm.lit_k);
            return;
        }
    }
    if (ui.splash) {
        uint32_t held = plat_buttons() | plat_keys();
        grow(dms);                               /* the town starts while the cards show */
        if (plat_ms() - ui.boot_t < ui_splash_ms && !held) {
            splash_tick();
            return;
        }
        ui.splash = 0;
        ui.btn = plat_buttons();
        ui.keys = plat_keys();
        ui.btn_used = ui.btn;
        ui.act_t = plat_ms();
        lcd_fill(0, 0, 240, 240, K_BG);
        if (!en_playing)
            en_play(1);
    }
    input();
    grow(dms);
    city_news();
    life_update();
    shake(&shake_dx, &shake_dy);
    cam_update((float)dms * 0.001f);
    if (!ui.full && !game_on() && !ui.help && proj.par[P_IDLE] && plat_ms() - ui.act_t >= IDLE_MS[proj.par[P_IDLE] & 3]) {
        ui.full = 1;                             /* untouched a while: the city on the whole screen */
        cam.ts = 0;
        lcd_fill(0, 0, 240, 240, K_BG);
    }
    if (!ui.full && !game_on())
        draw_header();
    draw_main();
    if (!ui.full && !game_on())
        draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
