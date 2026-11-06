/* SPDX-License-Identifier: GPL-3.0-only */
/* BIT-1 UI: the panel and the screen, a tracker. Part of the unity build (after gfx.c and project.c); the
 * host simulator includes it too. The panel's handling (knobs, help cards, the remote) is AMB-1's, by way of
 * GRAV-1.
 *
 *   keys           the cursor's track: its note (a kit's: the white keys are its eight drums). With REC on, on
 *                  the Pattern page, each key writes its note into the cell and the cursor steps down; playing,
 *                  it goes into the row playing. OCT- / OCT+ the keys' octave
 *   PLAY           play from the top of the position on screen / stop     (SEL + PLAY: from the cursor's row)
 *   REC            edit: the keys write
 *   EDIT           clear the cell (SEL + EDIT: a note-off). On GROW: grow the track (SEL + EDIT: every track).
 *                  On SONG: the position's pattern copied into a fresh one (SEL + EDIT: cleared)
 *   SEL            follow the song on / off; held, a shift
 *   SELECT         the row (with SEL: four); on SONG the position; elsewhere every page in turn; HOME held: tempo
 *   ALGORITHM      the track            PRESETS  the song (five)
 *   HOME           PATTERN    SEQ SONG    ENV SOUND (again: MOVE, SENDS)    LFO SPACE (again: GROOVE)
 *   ARP            GROW (again: FOLLOW)       GLO SETUP (again: DEVICE)
 *   FX             the cursor's track muted / not; held: black keys 1-6 mute, white keys 1-6 solo
 *   KNOB 1-4       the page's four values (on PATTERN: the cell's note, effect and value, and the pattern; with
 *                  SEL held, its volume, second effect and value, and the pattern's rows. On SONG, SEL held: the
 *                  position's repeats)
 *   SAVE           saves the song (and the device's settings); held 1.5 s on SETUP: the song as it came */
#include "../dsp/fastmath.h"

enum { V_PATTERN, V_SONG, V_SOUND, V_MOVE, V_SENDS, V_SPACE, V_GROOVE, V_GROW, V_FOLLOW, V_SETUP, V_DEVICE, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pattern", "Song", "Sound", "Move", "Sends", "Space", "Groove", "Grow",
                                              "Follow", "Setup", "Device"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, B_ENV, B_ENV, B_ENV, B_LFO, B_LFO, B_ARP, B_ARP, B_GLO, B_GLO};
#define GRID_VIEW(v) ((v) == V_PATTERN || (v) == V_GROW || (v) == V_FOLLOW)   /* the pages that show the grid */
enum { K_NOTE = 200, K_FX, K_VAL, K_PAT, K_POS, K_FOLLOW, K_STYLE, K_DENS, K_RANGE, K_SEED, K_VOL, K_FX2, K_VAL2, K_LEN,
       K_REP, K_NONE };
static const char *const KNOB_LABEL[NVIEWS][4] = {
    {"Note", "Effect", "Value", "Pattern"},
    {"Position", "Pattern", "Length", "Follow"},
    {"Sound", "Tone", "Decay", "Level"},
    {"Vibrato", "Arp", "Evolve", "Pan"},
    {"Echo", "Reverb", "Crush", "Arp rate"},
    {"Echo time", "Feedback", "Reverb", "Lo-fi"},
    {"Tempo", "Swing", "Duck", "Loop"},
    {"Style", "Density", "Range", "Seed"},
    {"Lead", "Role", "Change", "Density"},
    {"Root", "Mode", "Tune", "Sync"},
    {"Theme", "Lights", "MIDI out", "Channel"},
};
static const char *const SHORT[EN_NBUILTIN] = {"NES", "GB", "SID", "TRI", "PBAS", "SBAS", "WBAS", "OBAS", "SQR", "THIN",
                                             "DUTY", "SLED", "WLED", "ARP", "WPAD", "SPAD", "ORGN", "BELL", "BLIP", "LASR",
                                             "PWUP", "PICK", "METL", "NSWP"};
static const char *snd_short(int s)              /* a sound's four letters: built in, or "R1".."R16" for the bank's */
{
    static char r[EN_NROM][5];
    int k = s - EN_NBUILTIN;
    if (s >= 0 && s < EN_NBUILTIN)
        return SHORT[s];
    if (k < 0 || k >= EN_NROM)
        return SHORT[0];
    r[k][0] = 'R';
    if (k + 1 >= 10)
        r[k][1] = '1', r[k][2] = (char)('0' + (k + 1) % 10), r[k][3] = 0;
    else
        r[k][1] = (char)('1' + k), r[k][2] = 0;
    return r[k];
}

static struct {
    uint8_t view, trk, row, pos, rec, follow, mutes, fxwin;
    uint8_t shift;                     /* SEL held: the second set of knobs (Pattern, Song) */
    uint8_t style, dens, range, seed;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t enc_run[NE];
    uint32_t down_t[NB];
    uint32_t turn_t;
    uint8_t help;
    uint8_t reset_done;
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t boot_t;
    uint8_t splash, splash_step;
    uint32_t ev_r;
    uint8_t key_note[NKEYS];           /* the note each held key sounds (to let go of the same) */
    uint8_t midi_held[128];            /* MIDI in notes held: the track they went to + 1 */
    uint32_t discard_until;            /* PRESETS with changes unsaved: a second turn before this drops them */
} ui;
uint32_t ui_splash_ms = 3200u;          /* the boot cards: RedtailWorks, then BIT-1 (the host's tests set 0) */

/* ------------------------------------------------------------- themes --- */
/* Arcade (black, a hot red and yellow), Pocket (the four greens of a handheld's screen), Home (a home
 * computer's blues), Neon, Mono. The tracks keep their own colours under every theme. */
typedef struct {
    uint16_t bg, panel, line, dim, text, acc, hot, cold;
} theme_t;
static const theme_t THEMES[EN_NTHEMES] = {
    {RGB(6, 6, 10), RGB(28, 24, 36), RGB(56, 50, 70), RGB(150, 146, 170), RGB(244, 242, 250), RGB(255, 64, 64), RGB(255, 200, 40), RGB(80, 200, 255)},
    {RGB(15, 56, 15), RGB(30, 82, 30), RGB(48, 98, 48), RGB(110, 150, 40), RGB(200, 228, 140), RGB(155, 188, 15), RGB(230, 250, 170), RGB(139, 172, 15)},
    {RGB(32, 26, 110), RGB(52, 46, 150), RGB(80, 74, 180), RGB(150, 146, 230), RGB(220, 218, 255), RGB(130, 200, 255), RGB(255, 230, 120), RGB(160, 230, 160)},
    {RGB(8, 2, 18), RGB(30, 10, 54), RGB(60, 26, 100), RGB(160, 120, 210), RGB(240, 230, 255), RGB(0, 240, 255), RGB(255, 40, 200), RGB(140, 255, 120)},
    {RGB(0, 0, 0), RGB(30, 30, 30), RGB(60, 60, 60), RGB(138, 138, 138), RGB(240, 240, 240), RGB(255, 255, 255), RGB(200, 200, 200), RGB(170, 170, 170)},
};
static const theme_t *th(void) { return &THEMES[(unsigned)par_get(P_THEME) < EN_NTHEMES ? par_get(P_THEME) : 0]; }
#define K_BG (th()->bg)
#define K_PANEL (th()->panel)
#define K_LINE (th()->line)
#define K_DIM (th()->dim)
#define K_TEXT (th()->text)
#define K_ACC (th()->acc)
#define K_HOT (th()->hot)
#define K_COLD (th()->cold)
static const uint16_t TRK_C[EN_NTRK] = {RGB(255, 70, 172), RGB(255, 142, 44), RGB(176, 118, 255), RGB(156, 212, 72),
                                        RGB(70, 206, 236), RGB(236, 226, 206)};

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
static void two(uint32_t v, char *b)              /* two digits */
{
    b[0] = (char)('0' + v / 10u % 10u);
    b[1] = (char)('0' + v % 10u);
    b[2] = 0;
}

static void mark_dirty(void) { ui.dirty = 1; }
int ui_dirty(void) { return ui.dirty != 0; }

/* ------------------------------------------------------------ the song --- */
static int pat_shown(void) { return EN_ORD_PAT(en_song.order[ui.pos % EN_NORD]); }
static en_cell_t *cur_cell(void) { return &en_song.cell[pat_shown()][ui.row & 15][ui.trk % EN_NTRK]; }
static int trk_sound(int t) { return par_get(P_T(t, TP_SOUND)); }
static int trk_kit(int t) { return en_sound_is_kit(trk_sound(t)); }
static int song_len(void) { return par_get(P_LEN); }
static uint32_t pool_abs(void)                  /* the scale as absolute pitch classes */
{
    uint32_t p = EN_MODE_POOL[(unsigned)par_get(P_MODE) < EN_NMODES ? par_get(P_MODE) : 0], r = (uint32_t)par_get(P_ROOT);
    return ((p << r) | (p >> (12u - r))) & 0xFFFu;
}
static void note_text(int n, int kit, int snd, char *b)   /* "C#3", "KIK", "===", "" */
{
    if (n == NOTE_EMPTY) {
        b[0] = 0;
    } else if (n == NOTE_OFF) {
        b[0] = b[1] = b[2] = '=', b[3] = 0;
    } else if (kit) {
        const char *d = en_drum_name(snd, n);
        b[0] = d[0], b[1] = d[1], b[2] = d[2], b[3] = 0;
    } else {
        const char *nm = EN_NOTE_NAME[n % 12];
        int k = 0;
        while (*nm)
            b[k++] = *nm++;
        b[k++] = (char)('0' + (n / 12 - 1) % 10);
        b[k] = 0;
    }
}

/* ------------------------------------------------------------ values --- */
static int view_knob(int v, int i)
{
    static const uint8_t K[NVIEWS][4] = {
        {0}, {0}, {0}, {0}, {0},
        {P_ECHO_T, P_ECHO_FB, P_VERB, P_LOFI}, {P_TEMPO, P_SWING, P_DUCK, P_LOOP}, {0}, {0},
        {P_ROOT, P_MODE, P_TUNE, P_SYNC}, {P_THEME, P_LEDS, P_MIDI, P_MCH}};
    static const uint8_t TRKP[3][4] = {{TP_SOUND, TP_TONE, TP_DECAY, TP_LEVEL}, {TP_VIB, TP_ARP, TP_EVOLVE, TP_PAN},
                                       {TP_ECHO, TP_VERB, TP_CRUSH, 0}};
    switch (v) {
    case V_PATTERN:
        if (ui.shift)                            /* SEL held: the cell's volume and second effect, the rows */
            return i == 0 ? K_VOL : i == 1 ? K_FX2 : i == 2 ? K_VAL2 : K_LEN;
        return K_NOTE + i;
    case V_SONG: return i == 0 ? K_POS : i == 1 ? (ui.shift ? K_REP : K_PAT) : i == 2 ? P_LEN : K_FOLLOW;
    case V_SOUND: case V_MOVE: case V_SENDS:
        if (v == V_SENDS && i == 3)
            return P_ARP_T;
        return P_T(ui.trk, TRKP[v - V_SOUND][i]);
    case V_GROW: return K_STYLE + i;
    case V_FOLLOW: return i == 0 ? P_LEAD : i == 1 ? P_T(ui.trk, TP_FOLLOW) : i == 2 ? P_T(ui.trk, TP_EVOLVE) : P_FDENS;
    default: return K[v][i];
    }
}
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return par_get(k);
    switch (k) {
    case K_NOTE: return cur_cell()->note;
    case K_FX: return cur_cell()->fx >> 4;
    case K_VAL: return cur_cell()->fx & 15;
    case K_PAT: return pat_shown();
    case K_POS: return ui.pos;
    case K_FOLLOW: return ui.follow;
    case K_STYLE: return ui.style;
    case K_DENS: return ui.dens;
    case K_RANGE: return ui.range;
    case K_SEED: return ui.seed;
    case K_VOL: return cur_cell()->vol;
    case K_FX2: return cur_cell()->fx2 >> 4;
    case K_VAL2: return cur_cell()->fx2 & 15;
    case K_LEN: return en_pat_len(&en_song, pat_shown());
    case K_REP: return EN_ORD_REP(en_song.order[ui.pos % EN_NORD]);
    default: return 0;
    }
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = en_param_info(k)->lo;
        *hi = en_param_info(k)->hi;
        return;
    }
    *lo = 0;
    switch (k) {
    case K_NOTE: *hi = NOTE_HI; break;
    case K_FX: case K_VAL: case K_PAT: *hi = 15; break;
    case K_POS: *hi = song_len() - 1; break;
    case K_FOLLOW: *hi = 1; break;
    case K_STYLE: *hi = EN_NGROW - 1; break;
    case K_DENS: case K_RANGE: *hi = 100; break;
    case K_SEED: *hi = 99; break;
    case K_VOL: *hi = 16; break;
    case K_FX2: case K_VAL2: *hi = 15; break;
    case K_LEN: case K_REP: *lo = 1, *hi = 16; break;
    default: *hi = 0; break;
    }
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    static const char HEX[] = "0123456789ABCDEF";
    if (k < P_NPARAMS) {
        en_param_text(k, v, b);
        return;
    }
    switch (k) {
    case K_NOTE:
        note_text(v, trk_kit(ui.trk), trk_sound(ui.trk), b);
        if (!b[0])
            b[0] = '-', b[1] = '-', b[2] = 0;
        break;
    case K_FX: case K_FX2: {
        const char *n = EN_FX_NAME[v & 15];
        while ((*b++ = *n++))
            ;
        break;
    }
    case K_VAL: case K_VAL2: b[0] = HEX[v & 15], b[1] = 0; break;
    case K_VOL:
        if (!v)
            b[0] = '-', b[1] = '-', b[2] = 0;
        else
            b[0] = HEX[EN_VOL(v) & 15], b[1] = 0;
        break;
    case K_LEN:
        itoa_u((uint32_t)v, b);
        while (*b)
            b++;
        b[0] = ' ', b[1] = 'r', b[2] = 'o', b[3] = 'w', b[4] = 's', b[5] = 0;
        break;
    case K_REP: b[0] = 'x', itoa_u((uint32_t)v, b + 1); break;
    case K_PAT: b[0] = 'P', two((uint32_t)v + 1u, b + 1); break;
    case K_POS:
        two((uint32_t)v + 1u, b);
        b[2] = '/';
        two((uint32_t)song_len(), b + 3);
        break;
    case K_FOLLOW: {
        const char *n = v ? "On" : "Off";
        while ((*b++ = *n++))
            ;
        break;
    }
    case K_STYLE: {
        const char *n = EN_GROW_NAME[v % EN_NGROW];
        while ((*b++ = *n++))
            ;
        break;
    }
    case K_DENS: case K_RANGE:
        itoa_u((uint32_t)v, b);
        while (*b)
            b++;
        *b++ = '%';
        *b = 0;
        break;
    case K_SEED: b[0] = '#', two((uint32_t)v, b + 1); break;
    default: b[0] = 0; break;
    }
}

static void touch_cell(void) { mark_dirty(); }

static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        par_put(k, v);
        if (k == P_LEN && ui.pos >= v)
            ui.pos = (uint8_t)(v - 1);
        if (k < P_NSONG)
            mark_dirty();
        return;
    }
    switch (k) {
    case K_NOTE: cur_cell()->note = (uint8_t)v; touch_cell(); break;
    case K_FX: cur_cell()->fx = (uint8_t)(v << 4 | (cur_cell()->fx & 15)); touch_cell(); break;
    case K_VAL: cur_cell()->fx = (uint8_t)((cur_cell()->fx & 0xF0) | v); touch_cell(); break;
    case K_PAT:
        en_song.order[ui.pos % EN_NORD] = EN_ORD(v, EN_ORD_REP(en_song.order[ui.pos % EN_NORD]));
        mark_dirty();
        break;
    case K_VOL: cur_cell()->vol = (uint8_t)v; touch_cell(); break;
    case K_FX2: cur_cell()->fx2 = (uint8_t)(v << 4 | (cur_cell()->fx2 & 15)); touch_cell(); break;
    case K_VAL2: cur_cell()->fx2 = (uint8_t)((cur_cell()->fx2 & 0xF0) | v); touch_cell(); break;
    case K_LEN: en_song.len[pat_shown()] = (uint8_t)v; mark_dirty(); break;
    case K_REP: en_song.order[ui.pos % EN_NORD] = EN_ORD(pat_shown(), v); mark_dirty(); break;
    case K_POS: ui.pos = (uint8_t)v; ui.follow = 0; break;
    case K_FOLLOW: ui.follow = (uint8_t)v; break;
    case K_STYLE: ui.style = (uint8_t)v; break;
    case K_DENS: ui.dens = (uint8_t)v; break;
    case K_RANGE: ui.range = (uint8_t)v; break;
    case K_SEED: ui.seed = (uint8_t)v; break;
    default: break;
    }
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

static int note_default(void)                    /* a note for an empty cell: the track's last, or the root */
{
    int r;
    if (trk_kit(ui.trk))
        return 36;
    for (r = (int)ui.row; r >= 0; r--) {
        int n = en_song.cell[pat_shown()][r][ui.trk].note;
        if (n >= NOTE_LO)
            return n;
    }
    return 48 + par_get(P_ROOT);
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE)
        return;
    if (k == K_NOTE) {                           /* the note: empty, off, then the notes (a kit: its eight drums) */
        int n = cur_cell()->note, kit = trk_kit(ui.trk), d = e > 0 ? 1 : -1, steps = e > 0 ? e : -e, i;
        for (i = 0; i < steps; i++) {
            if (n == NOTE_EMPTY)
                n = d > 0 ? note_default() : NOTE_EMPTY;
            else if (n == NOTE_OFF)
                n = d > 0 ? NOTE_OFF : NOTE_EMPTY;
            else if (kit)
                n = d > 0 ? (n >= 43 ? 43 : n < 36 ? 36 : n + 1) : (n <= 36 ? NOTE_OFF : n - 1);
            else
                n = d > 0 ? (n < NOTE_HI ? n + 1 : n) : (n > NOTE_LO ? n - 1 : NOTE_OFF);
        }
        knob_set(K_NOTE, n);
        return;
    }
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ the pages --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.help = 0;
    say(VIEW_NAME[v], 0);
    ui.msg_until = plat_ms() + 700u;
}

static void play_toggle(int from_row)
{
    if (en_playing) {
        en_stop();
        say("STOP", 0);
    } else {
        en_seed(plat_ms());
        en_play(ui.pos, from_row ? ui.row : 0);
        say("PLAY", from_row ? "FROM THE ROW" : 0);
    }
}

static void row_step(int d)
{
    ui.row = (uint8_t)(((int)ui.row + d + EN_NROW * 4) % EN_NROW);
}

/* EDIT on SONG: the position's pattern into the first one no position plays, which this position then plays */
static void song_clone(void)
{
    uint8_t used[EN_NPAT] = {0};
    int i, from = pat_shown(), to = -1;
    char t[12] = "INTO P";
    for (i = 0; i < song_len(); i++)
        used[EN_ORD_PAT(en_song.order[i])] = 1;
    for (i = 0; i < EN_NPAT && to < 0; i++)
        if (!used[i])
            to = i;
    if (to < 0) {
        say("NO PATTERN", "FREE");
        return;
    }
    memcpy(en_song.cell[to], en_song.cell[from], sizeof en_song.cell[0]);
    en_song.order[ui.pos] = EN_ORD(to, EN_ORD_REP(en_song.order[ui.pos]));
    en_song.len[to] = en_song.len[from];
    two((uint32_t)to + 1u, t + 6);
    say("COPIED", t);
    mark_dirty();
}

static void grow(int all)
{
    int t;
    char b[12] = "P";
    for (t = 0; t < EN_NTRK; t++)
        if (all || t == ui.trk)
            en_grow(&en_song, pat_shown(), t, ui.style, ui.dens, ui.range,
                    (uint32_t)ui.seed * 7919u + (uint32_t)ui.pos * 13u + plat_ms());
    two((uint32_t)pat_shown() + 1u, b + 1);
    say(all ? "GREW ALL" : "GREW", b);
    ui.seed = (uint8_t)((ui.seed + 1) % 100);
    mark_dirty();
}

/* FOLLOW: EDIT, a new take for every following track; SEL + EDIT, the takes written into their cells (every
 * pattern the song plays) and the tracks back to their own */
static void follow_take(void)
{
    char b[8] = "#";
    int v = (par_get(P_TAKE) + 1) % 100;
    par_put(P_TAKE, v);
    two((uint32_t)v, b + 1);
    say(par_get(P_LEAD) ? "NEW TAKE" : "NO LEAD", par_get(P_LEAD) ? b : "SET ONE");
    mark_dirty();
}
static void follow_write(void)
{
    uint8_t done[EN_NPAT] = {0};
    int i, t, r, n = 0;
    char b[12] = "TRACKS: ";
    uint32_t wrote = 0;
    for (i = 0; i < song_len(); i++) {
        int pat = EN_ORD_PAT(en_song.order[i]);
        en_cell_t o[EN_NROW];
        if (done[pat])
            continue;
        done[pat] = 1;
        for (t = 0; t < EN_NTRK; t++)
            if (en_follow_role(&en_song, t)) {
                en_follow(&en_song, pat, t, en_take_seed(&en_song, t, pat), o);
                for (r = 0; r < EN_NROW; r++)
                    en_song.cell[pat][r][t] = o[r];
                wrote |= 1u << t;
            }
    }
    for (t = 0; t < EN_NTRK; t++)
        if (wrote >> t & 1u) {
            par_put(P_T(t, TP_FOLLOW), FR_OFF);
            n++;
        }
    b[8] = (char)('0' + n), b[9] = 0;
    say(n ? "WRITTEN" : "NOTHING", n ? b : "FOLLOWS");
    if (n)
        mark_dirty();
}

static void set_mutes(uint32_t m)
{
    ui.mutes = (uint8_t)(m & 0x3Fu);
    en_track_mute(ui.mutes);
}

static void rm_enter(void);                     /* REMOTE, below */
static void button(int b, int shift)
{
    int v;
    if (b == B_ENV && (ui.view == V_SOUND || ui.view == V_MOVE)) { set_view(ui.view + 1); return; }
    if (b == B_ENV && ui.view == V_SENDS) { set_view(V_SOUND); return; }
    if (b == B_LFO && ui.view == V_SPACE) { set_view(V_GROOVE); return; }
    if (b == B_LFO && ui.view == V_GROOVE) { set_view(V_SPACE); return; }
    if (b == B_ARP && ui.view == V_GROW) { set_view(V_FOLLOW); return; }
    if (b == B_ARP && ui.view == V_FOLLOW) { set_view(V_GROW); return; }
    if (b == B_GLO && ui.view == V_SETUP) { set_view(V_DEVICE); return; }
    if (b == B_GLO && ui.view == V_DEVICE) { set_view(V_SETUP); return; }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_SEL:
        ui.follow ^= 1u;
        say("FOLLOW", ui.follow ? "ON" : "OFF");
        break;
    case B_PLAY: play_toggle(shift); break;
    case B_REC:
        ui.rec ^= 1u;
        say("EDIT", ui.rec ? "ON: THE KEYS WRITE" : "OFF");
        break;
    case B_EDIT:
        if (ui.view == V_GROW) {
            grow(shift);
        } else if (ui.view == V_FOLLOW) {
            if (shift)
                follow_write();
            else
                follow_take();
        } else if (ui.view == V_SONG) {
            if (shift) {
                memset(en_song.cell[pat_shown()], 0, sizeof en_song.cell[0]);
                say("CLEARED", 0);
                mark_dirty();
            } else {
                song_clone();
            }
        } else {
            en_cell_t *c = cur_cell();
            c->note = shift ? NOTE_OFF : NOTE_EMPTY;
            if (!shift)
                c->fx = 0;
            mark_dirty();
            if (ui.rec)
                row_step(1);
        }
        break;
    case B_FX:
        set_mutes(ui.mutes ^ (1u << ui.trk));
        say(ui.mutes >> ui.trk & 1u ? "MUTED" : "UNMUTED", snd_short(trk_sound(ui.trk)));
        break;
    case B_OCTDN: case B_OCTUP: {
        int d = b == B_OCTUP ? 1 : -1;
        if (shift && ui.view == V_PATTERN) {     /* SEL + OCT: the cell a semitone */
            en_cell_t *c = cur_cell();
            if (c->note >= NOTE_LO) {
                int n = c->note + d;
                c->note = (uint8_t)(n < NOTE_LO ? NOTE_LO : n > NOTE_HI ? NOTE_HI : n);
                mark_dirty();
            }
            break;
        }
        proj.oct = (int8_t)(proj.oct + d < -3 ? -3 : proj.oct + d > 3 ? 3 : proj.oct + d);
        {
            char t[4] = {(char)(proj.oct < 0 ? '-' : '+'), (char)('0' + (proj.oct < 0 ? -proj.oct : proj.oct)), 0};
            say("OCTAVE", proj.oct ? t : "0");
        }
        break;
    }
    case B_SAVE:
        {
            int rc = project_save();
            if (rc == 0) {
                ui.dirty = 0;
                say("SAVED", en_song.name);
            } else {
                say("NOT SAVED", rc == -2 ? "SONG FULL" : "FLASH ERROR");
            }
        }
        break;
    default: break;
    }
}

/* the keys */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int white_of(int k) { int i; for (i = 0; i < 16; i++) if (WHITE_KEY[i] == k) return i; return -1; }
static int black_of(int k) { int i; for (i = 0; i < 11; i++) if (BLACK_KEY[i] == k) return i; return -1; }

static void write_note(int n)                    /* REC: into the row playing, or the cursor's (stepping on) */
{
    en_cell_t *c;
    if (en_follow_role(&en_song, ui.trk)) {      /* it plays its take, not its cells */
        say("IT FOLLOWS", "ROLE OFF TO WRITE");
        return;
    }
    if (en_playing && ui.follow) {
        c = &en_song.cell[en_pat_now % EN_NPAT][en_row % EN_NROW][ui.trk];
        c->note = (uint8_t)n;
    } else {
        c = cur_cell();
        c->note = (uint8_t)n;
        row_step(1);
    }
    mark_dirty();
}

static int key_to_note(int k)                    /* -1: the key plays nothing here */
{
    if (trk_kit(ui.trk)) {
        int w = white_of(k);
        return w < 0 ? -1 : 36 + w % EN_NDRUM;
    }
    {
        int n = 53 + k + 12 * proj.oct;           /* F3 */
        return n < NOTE_LO ? NOTE_LO : n > NOTE_HI ? NOTE_HI : n;
    }
}

static void key_down(int k)
{
    int n;
    if (ui.fxwin) {                              /* FX held: black keys 1-6 mute, white keys 1-6 solo */
        int w = white_of(k), b = black_of(k);
        if (b >= 0 && b < EN_NTRK) {
            set_mutes(ui.mutes ^ (1u << b));
        } else if (w >= 0 && w < EN_NTRK) {
            uint32_t solo = 0x3Fu & ~(1u << w);
            set_mutes(ui.mutes == solo ? 0u : solo);
        }
        return;
    }
    n = key_to_note(k);
    if (n < 0)
        return;
    ui.key_note[k] = (uint8_t)n;
    en_audition(ui.trk, n, 1);
    if (ui.rec && GRID_VIEW(ui.view))
        write_note(n);
}
static void key_up(int k)
{
    if (ui.key_note[k]) {
        en_audition(ui.trk, ui.key_note[k], 0);
        ui.key_note[k] = 0;
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            en_clock(st == 0xF8u ? EN_CLK_TICK : st == 0xFAu ? EN_CLK_START : st == 0xFBu ? EN_CLK_CONTINUE : EN_CLK_STOP);
        } else if ((st & 0xF0u) == 0x90u && v) {  /* notes in: as the keys (a kit: C2.. the drums) */
            int note = (int)n;
            if (note < NOTE_LO || note > NOTE_HI)
                continue;
            ui.midi_held[n] = (uint8_t)(ui.trk + 1);
            en_audition(ui.trk, note, 1);
            if (ui.rec && GRID_VIEW(ui.view))
                write_note(note);
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            if (ui.midi_held[n]) {
                en_audition(ui.midi_held[n] - 1, (int)n, 0);
                ui.midi_held[n] = 0;
            }
        }
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
        if (btn == 1u << i && i != B_PLAY && i != B_FX && i != B_SEL && !(ui.btn_used & btn) && now - ui.down_t[i] >= HELP_HOLD &&
            (int32_t)(ui.turn_t - ui.down_t[i]) <= 0) {
            ui.help = (uint8_t)(i + 1);
            ui.btn_used |= btn;
        }
}

/* REMOTE (SEL + GLO): the panel plays BIT-1 in the browser over USB MIDI, and the FM-1 goes quiet, as on
 * HYP-1 and GRAV-1. On channel 16: the keys are notes 48 + k, the buttons CC 102 + b (127 down, 0 up), the
 * knobs CC 85 + role (64 + detents), MASTER CC 7, and CC 116 says hello every second. The browser answers
 * with its lights the same way (notes and CC 102 + b) and CC 117, so the panel lights as the emulator's
 * does. SEL + GLO again: back. Not in the browser's own build. */
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
    en_stop();
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
        en_play(ui.pos, 0);
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

/* ------------------------------------------------------------- input --- */
static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    const int shift = (int)(btn >> B_SEL & 1u);
    if (rm.on)
        return;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.down_t[i] = plat_ms();
            if (i == B_GLO && shift) {            /* SEL + GLO: remote */
                ui.btn_used |= m | 1u << B_SEL;
                ui.btn = btn;
                ui.keys = keys;
                ui.help = 0;
#ifndef EN_WEB
                rm_enter();
                return;
#else
                continue;
#endif
            }
            if (i == B_SAVE)
                ui.reset_done = 0;
            if (i != B_SEL && shift)              /* SEL as a shift: what it shifts acts now, and SEL does nothing */
                ui.btn_used |= 1u << B_SEL;
            if (i == B_PLAY) {
                button((int)i, shift);
                ui.btn_used |= m;
            }
        } else if (!(ui.btn_used & m)) {
            button((int)i, i != B_SEL && (ui.btn >> B_SEL & 1u));
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++) {
        if ((ch & keys) >> i & 1u) {
            if (btn >> B_FX & 1u)
                ui.btn_used |= 1u << B_FX;
            key_down((int)i);
        } else if ((ch & ~keys) >> i & 1u) {
            key_up((int)i);
        }
    }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((btn >> B_FX & 1u) && !ui.fxwin && plat_ms() - ui.down_t[B_FX] >= 350u && !(btn & ~(1u << B_FX))) {
        ui.fxwin = 1;                            /* FX held: the mutes */
        ui.btn_used |= 1u << B_FX;
    }
    if (!(btn >> B_FX & 1u) && ui.fxwin) {
        ui.fxwin = 0;
        ui.sig[1] = 0;
    }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (btn >> B_HOME & 1u) {                /* HOME held: the tempo */
            char t[12];
            turn(EN_SELECT, P_TEMPO, e);
            ui.btn_used |= 1u << B_HOME;
            en_param_text(P_TEMPO, par_get(P_TEMPO), t);
            say("TEMPO", t);
        } else if (GRID_VIEW(ui.view)) {
            row_step((int)e * (shift ? 4 : 1));
            if (shift)
                ui.btn_used |= 1u << B_SEL;
        } else if (ui.view == V_SONG) {
            int p = (int)ui.pos + (e > 0 ? 1 : -1);
            if (p >= 0 && p < song_len()) {
                ui.pos = (uint8_t)p;
                ui.follow = 0;
            }
        } else {
            int v = (int)ui.view + (e > 0 ? 1 : -1);
            if (v >= 0 && v < NVIEWS)
                set_view(v);
        }
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        int t = (int)ui.trk + (int)e;
        ui.trk = (uint8_t)(t < 0 ? 0 : t >= EN_NTRK ? EN_NTRK - 1 : t);
        ui.turn_t = plat_ms();
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {         /* the song: changes unsaved need a second turn */
        int s = ((int)proj.song + (e > 0 ? 1 : -1) + EN_NSONG) % EN_NSONG;
        if (ui.dirty && plat_ms() > ui.discard_until) {
            ui.discard_until = plat_ms() + 2500u;
            say("NOT SAVED:", "TURN AGAIN TO DROP");
            ui.msg_until = ui.discard_until;
        } else {
            int was = en_playing;
            song_open(s);
            if (was)                             /* playing: the next song from its top */
                en_play(0, 0);
            ui.dirty = 0;
            ui.pos = ui.row = 0;
            ui.discard_until = 0;
            ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
            say(en_song.name, song_saved(s) ? "SAVED" : "AS IT CAME");
        }
        ui.turn_t = plat_ms();
    }
    if ((btn >> B_SAVE & 1u) && !ui.reset_done && (ui.view == V_SETUP || ui.view == V_DEVICE) &&
        plat_ms() - ui.down_t[B_SAVE] > RESET_HOLD) {
        ui.reset_done = 1;
        ui.btn_used |= 1u << B_SAVE;
        ui.help = 0;
        song_recipe();
        ui.pos = ui.row = 0;
        say("AS IT CAME", "(SAVE TO KEEP)");
        mark_dirty();
    }
    ui.shift = (uint8_t)(shift && (ui.view == V_PATTERN || ui.view == V_SONG));
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (ui.shift)                        /* a knob turned under SEL: SEL's let go does nothing */
                ui.btn_used |= 1u << B_SEL;
            turn(EN_K1 + (int)i, view_knob(ui.view, (int)i), e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = ui.turn_t = plat_ms();
        }
    help_tick(btn, keys);
}

/* the engine's news */
static void engine_news(void)
{
    if (ui.follow && en_playing && ui.pos != en_pos)
        ui.pos = en_pos;
    while (ui.ev_r != en_ev_w) {
        uint16_t ev;
        if (en_ev_w - ui.ev_r > EN_NEV)
            ui.ev_r = en_ev_w - EN_NEV;
        ev = en_ev[ui.ev_r % EN_NEV];
        ui.ev_r++;
        if (ev >> 8 == EV_LOOP && plat_ms() >= ui.msg_until)
            say("FROM THE TOP", 0);
    }
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the main (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

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

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *q;
    uint32_t bg;
    y += cv_oy;
    if ((uint32_t)x >= cv_w || y < cv_y0 || y >= cv_y1 || !a)
        return;
    q = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    if (a >= 16u) {
        *q = swap16(c);
        return;
    }
    bg = swap16(*q);
    *q = swap16((uint32_t)(((((c >> 11) * a + (bg >> 11) * (16u - a)) >> 4) << 11) |
                           (((((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (16u - a)) >> 4) << 5) |
                           (((c & 31u) * a + (bg & 31u) * (16u - a)) >> 4)));
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
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a = ax < ay ? ax / (ay + 1e-9f) : ay / (ax + 1e-9f), s = a * a, r;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = FM_PI - r;
    if (y < 0.0f)
        r = FM_TWO_PI - r;
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
        float e0 = a0 * FM_TWO_PI, e1 = (a0 + sweep) * FM_TWO_PI, ev = (a0 + sweep * v) * FM_TWO_PI;
        float el = (a0 + sweep * lo) * FM_TWO_PI;
        disc(fx + fm_cosf(e0) * rm - 0.5f, fy + fm_sinf(e0) * rm - 0.5f, thk * 0.5f, lo <= 0.0f && hi > 0.0f ? fill : track);
        disc(fx + fm_cosf(e1) * rm - 0.5f, fy + fm_sinf(e1) * rm - 0.5f, thk * 0.5f, hi >= 1.0f ? fill : track);
        if (hi > lo)
            disc(fx + fm_cosf(el) * rm - 0.5f, fy + fm_sinf(el) * rm - 0.5f, thk * 0.5f, fill);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, thk * 0.5f + 1.3f, bg);
        disc(fx + fm_cosf(ev) * rm - 0.5f, fy + fm_sinf(ev) * rm - 0.5f, thk * 0.5f + 0.6f, fill);
    }
}

static void caps(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i < n - 1 && s[i]; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
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

static void draw_header(void)
{
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, proj.song | (uint32_t)ui.pos << 4 | (uint32_t)song_len() << 10 | (uint32_t)en_pos << 16),
                      en_playing | ui.dirty << 1 | (uint32_t)msg << 2 | (uint32_t)ui.rec << 3 | (uint32_t)en_ext << 4 |
                          (uint32_t)par_get(P_THEME) << 5 | (uint32_t)par_get(P_TEMPO) << 8 | (uint32_t)ui.follow << 16);
    if (en_ext)
        h = hash(h, (uint32_t)(en_bpm + 0.5f));
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1] | (uint32_t)ui.msg[1][0] << 16), ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 234, w;
        char c[16];
        if (ui.dirty) {
            cv_round(x - 5, 12, 5, 5, 2, K_DIM);
            x -= 11;
        }
        if (en_ext) {
            c[0] = 'E', c[1] = 'X', c[2] = 'T', c[3] = ' ';
            itoa_u((uint32_t)(en_bpm + 0.5f), c + 4);
        } else {
            itoa_u((uint32_t)par_get(P_TEMPO), c);
        }
        x -= text_w(&FONT_S, c);
        cv_text(x, 6, &FONT_S, c, en_ext ? K_ACC : K_DIM);
        x -= 8;
        if (en_playing) {                        /* a play triangle */
            int32_t j;
            x -= 9;
            for (j = 0; j < 5; j++)
                cv_rect(x + j, 9 + j, 1, 11 - 2 * j, K_ACC);
        } else {
            x -= 9;
            cv_rect(x, 10, 8, 8, K_DIM);
        }
        if (ui.rec) {
            x -= 30;
            cv_round(x, 7, 26, 15, 4, K_HOT);
            cv_text(x + 4, 6, &FONT_XS, "REC", K_BG);
        }
        w = text_w(&FONT_B, "8") + 12;
        cv_round(4, 4, w, 20, 6, K_ACC);
        c[0] = (char)('1' + proj.song), c[1] = 0;
        cv_text(10, 6, &FONT_B, c, K_BG);
        caps(c, en_song.name, sizeof c);
        if (text_w(&FONT_S, c) > x - w - 16)
            c[10] = 0;
        cv_text(w + 10, 6, &FONT_S, c, K_TEXT);
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------ the grid --- */
/* the track heads (14 px) and ten rows of 13: a row number, then six cells of 37 px: the note (or drum, or
 * the off's bars) and the effect's letter and value. The cursor's cell is boxed; the row playing lit. */
#define G_HEAD 14
#define G_ROWH 13
#define G_NROWS 10
#define G_X0 18
#define G_CW 37
static void draw_grid(void)
{
    static const char HEX[] = "0123456789ABCDEF";
    const int pat = pat_shown(), playing_here = en_playing && en_pat_now == pat && (ui.follow || en_pos == ui.pos);
    const int lead = par_get(P_LEAD) - 1, plen = en_pat_len(&en_song, pat);
    int top = (int)ui.row - 4, i, t;
    static en_cell_t takes[EN_NTRK][EN_NROW];   /* what a following track plays here: its take, not its cells */
    uint8_t fol[EN_NTRK];
    for (t = 0; t < EN_NTRK; t++) {
        fol[t] = (uint8_t)en_follow_role(&en_song, t);
        if (fol[t])
            en_follow(&en_song, pat, t, en_take_seed(&en_song, t, pat), takes[t]);
    }
    if (top < 0)
        top = 0;
    if (top > EN_NROW - G_NROWS)
        top = EN_NROW - G_NROWS;
    cv_begin(240, MAIN_H, K_BG);
    for (t = 0; t < EN_NTRK; t++) {              /* the heads: the track's sound, its level under it */
        int32_t x = G_X0 + t * G_CW;
        int muted = ui.mutes >> t & 1u, sel = t == ui.trk;
        char s[6];
        uint16_t c = muted ? K_LINE : mix(K_DIM, TRK_C[t], 6 + q16(en_trk_glow[t]) * 10 / 16);
        if (sel)
            cv_round(x, 0, G_CW - 1, G_HEAD - 1, 3, K_PANEL);
        caps(s, snd_short(trk_sound(t)), sizeof s);
        cv_text(x + 3, -2, &FONT_XS, s, sel ? TRK_C[t] : c);
        cv_rect(x + 2, G_HEAD - 2, (int32_t)(fm_clampf(en_level[t], 0.0f, 1.0f) * (float)(G_CW - 5)), 1,
                muted ? K_LINE : TRK_C[t]);
        if (t == lead)                           /* the lead: a hot mark; a track following it: a cold one */
            cv_rect(x + G_CW - 6, 2, 4, 4, K_HOT);
        else if (fol[t])
            cv_rect(x + G_CW - 6, 3, 3, 3, K_COLD);
    }
    {
        char p[4] = {'P', 0, 0, 0};
        two((uint32_t)pat + 1u, p + 1);
        cv_text(0, -2, &FONT_XS, p, K_DIM);
    }
    for (i = 0; i < G_NROWS; i++) {
        int r = top + i;
        int32_t y = G_HEAD + i * G_ROWH;
        char num[3];
        if (playing_here && r == en_row)
            cv_rect(0, y, 240, G_ROWH, mix(K_BG, K_HOT, 5));
        else if (r == ui.row)
            cv_rect(0, y, 240, G_ROWH, K_PANEL);
        else if (r % 4 == 0)
            cv_rect(0, y, 240, G_ROWH, mix(K_BG, K_PANEL, 7));
        two((uint32_t)r, num);
        cv_text(1, y - 2, &FONT_XS, num, r >= plen ? K_LINE : r % 4 == 0 ? K_ACC : K_DIM);
        for (t = 0; t < EN_NTRK; t++) {
            const en_cell_t *c = fol[t] ? &takes[t][r] : &en_song.cell[pat][r][t];
            int32_t x = G_X0 + t * G_CW + 2;
            int fx = c->fx >> 4, muted = ui.mutes >> t & 1u;
            char n[6];
            uint16_t col = muted || r >= plen ? K_DIM : fol[t] ? mix(K_DIM, TRK_C[t], 9) : K_TEXT;
            if (r == ui.row && t == ui.trk) {
                cv_round(x - 2, y, G_CW - 1, G_ROWH, 2, ui.rec ? K_HOT : K_ACC);
                col = K_BG;
            }
            if (c->note == NOTE_EMPTY) {
                cv_rect(x + 2, y + 6, 2, 1, r == ui.row && t == ui.trk ? K_BG : K_LINE);
                cv_rect(x + 8, y + 6, 2, 1, r == ui.row && t == ui.trk ? K_BG : K_LINE);
            } else if (c->note == NOTE_OFF) {
                cv_rect(x + 1, y + 4, 16, 2, col);
                cv_rect(x + 1, y + 8, 16, 2, col);
            } else {
                note_text(c->note, trk_kit(t), trk_sound(t), n);
                cv_text(x, y - 2, &FONT_XS, n, col == K_TEXT ? (trk_kit(t) ? mix(K_TEXT, TRK_C[t], 8) : K_TEXT) : col);   /* a take: in its track's colour */
            }
            if (c->vol) {                        /* the volume: a bar beside the note, as tall as it's loud */
                int32_t hgt = 1 + EN_VOL(c->vol) * 9 / 15;
                cv_rect(x + 18, y + 11 - hgt, 1, hgt, r == ui.row && t == ui.trk ? K_BG : K_COLD);
            }
            if (c->fx2 && fx)                    /* a second effect behind the first: a dot */
                cv_rect(x + 33, y + 2, 2, 2, r == ui.row && t == ui.trk ? K_BG : K_HOT);
            if (!fx && c->fx2) {                 /* only the second: shown in the first's place */
                fx = c->fx2 >> 4;
                {
                    char e[3] = {EN_FX_CHAR[fx], HEX[c->fx2 & 15], 0};
                    cv_text(x + 21, y - 2, &FONT_XS, e, r == ui.row && t == ui.trk ? K_BG : muted ? K_DIM : K_COLD);
                }
            } else if (fx) {
                char e[3] = {EN_FX_CHAR[fx], HEX[c->fx & 15], 0};
                cv_text(x + 21, y - 2, &FONT_XS, e, r == ui.row && t == ui.trk ? K_BG : muted ? K_DIM : K_COLD);
            } else {
                cv_rect(x + 24, y + 6, 2, 1, r == ui.row && t == ui.trk ? K_BG : K_LINE);
            }
        }
    }
    for (t = 1; t < EN_NTRK; t++)
        cv_rect(G_X0 + t * G_CW - 1, G_HEAD, 1, MAIN_H - G_HEAD, mix(K_BG, K_LINE, 8));
}

/* the order list: 32 positions in four rows of eight, each its pattern; the cursor's ringed, the playing lit */
static void draw_song(void)
{
    int i;
    char t[24];
    cv_begin(240, MAIN_H, K_BG);
    caps(t, en_song.name, sizeof t);
    cv_text(4, -1, &FONT_B, t, K_TEXT);
    {
        char u[16] = "LOOP: ", m[12];
        int pc = en_song_size(&en_song) * 100 / (int)PLAT_STORE_MAX, k = 0;
        caps(u + 6, par_get(P_LOOP) ? "Pattern" : "Song", 10);
        cv_text(236 - text_w(&FONT_XS, u), -3, &FONT_XS, u, K_DIM);
        itoa_u((uint32_t)pc, m);                 /* how much of its flash the song would take */
        while (m[k])
            k++;
        m[k++] = '%', m[k++] = ' ', m[k++] = 'F', m[k++] = 'U', m[k++] = 'L', m[k++] = 'L', m[k] = 0;
        cv_text(236 - text_w(&FONT_XS, m), 9, &FONT_XS, m, pc > 100 ? K_HOT : pc > 85 ? K_ACC : K_DIM);
    }
    for (i = 0; i < EN_NORD; i++) {
        int32_t x = 4 + (i % 8) * 29, y = 22 + (i / 8) * 30;
        int in = i < song_len(), cur = i == ui.pos, play = en_playing && i == en_pos;
        char p[4];
        uint16_t bg = play ? K_HOT : cur ? K_ACC : in ? K_PANEL : K_BG;
        cv_round(x, y, 26, 26, 4, bg);
        if (!in) {
            cv_rect(x + 11, y + 12, 4, 1, K_LINE);
            continue;
        }
        two((uint32_t)EN_ORD_PAT(en_song.order[i]) + 1u, p);
        text_c(x + 13, y + 2, &FONT_S, p, play || cur ? K_BG : K_TEXT);
        if (EN_ORD_REP(en_song.order[i]) > 1) {  /* its repeats */
            char q[4] = {'x', 0, 0, 0};
            itoa_u((uint32_t)EN_ORD_REP(en_song.order[i]), q + 1);
            text_c(x + 13, y + 13, &FONT_XS, q, play || cur ? K_BG : K_COLD);
        }
        if (play) {
            int32_t w = (int32_t)en_row * 22 / (en_pat_len(&en_song, EN_ORD_PAT(en_song.order[i])) > 1 ? en_pat_len(&en_song, EN_ORD_PAT(en_song.order[i])) - 1 : 1);
            cv_rect(x + 2, y + 22, w + 1, 2, K_BG);
        }
    }
}

/* the mixer, on the other pages: the six tracks' sounds, levels and state; the cursor's ringed */
static void draw_mixer(void)
{
    int t;
    cv_begin(240, MAIN_H, K_BG);
    for (t = 0; t < EN_NTRK; t++) {
        int32_t x = 4 + t * 39, h;
        int sel = t == ui.trk, muted = ui.mutes >> t & 1u;
        char s[16];
        float lv = fm_clampf(en_level[t], 0.0f, 1.0f);
        if (sel)
            cv_round(x - 2, 0, 38, MAIN_H, 6, K_PANEL);
        s[0] = (char)('1' + t), s[1] = 0;
        text_c(x + 17, -1, &FONT_B, s, sel ? TRK_C[t] : K_DIM);
        caps(s, snd_short(trk_sound(t)), sizeof s);
        text_c(x + 17, 17, &FONT_XS, s, muted ? K_LINE : TRK_C[t]);
        cv_rect(x + 9, 36, 16, 84, mix(K_BG, K_LINE, 10));
        h = (int32_t)(lv * 82.0f);
        if (h > 0)
            cv_rect(x + 10, 119 - h, 14, h, muted ? K_LINE : mix(TRK_C[t], K_TEXT, q16(en_trk_glow[t]) / 3));
        {   /* the level as set: a tick */
            int32_t lt = 119 - par_get(P_T(t, TP_LEVEL)) * 82 / 100;
            cv_rect(x + 6, lt, 22, 1, K_DIM);
        }
        text_c(x + 17, 124, &FONT_XS, muted ? "MUTE" : en_sound_is_kit(trk_sound(t)) ? "KIT" : "", muted ? K_HOT : K_DIM);
    }
}

/* a button's card (help_tick): its name, then what it does */
#define HELP_LINES 4
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static int help_lines(int b, const char *l[HELP_LINES])
{
    int n = 0;
#define L(t) (l[n++] = (t))
    switch (b) {
    case B_HOME: L("THE PATTERN: SELECT THE ROW,"); L("ALGORITHM THE TRACK, KNOBS"); L("NOTE, EFFECT, VALUE, PATTERN"); L("+ SELECT: THE TEMPO"); break;
    case B_SEQ: L("THE SONG: THE ORDER LIST"); L("SELECT THE POSITION"); L("EDIT: A FRESH COPY HERE"); L("SEL + EDIT: CLEAR IT"); break;
    case B_ENV: L("THE TRACK'S SOUND,"); L("TONE, DECAY, LEVEL"); L("AGAIN: MOVE (VIBRATO, ARP,"); L("EVOLVE, PAN), THEN SENDS"); break;
    case B_LFO: L("THE SPACE: ECHO, FEEDBACK,"); L("REVERB, LO-FI"); L("AGAIN: GROOVE (TEMPO,"); L("SWING, DUCK, LOOP)"); break;
    case B_ARP: L("GROW: EDIT WRITES A PATTERN"); L("(SEL: EVERY TRACK). AGAIN:"); L("FOLLOW THE LEAD: EDIT A NEW"); L("TAKE, SEL + EDIT WRITE IT"); break;
    case B_GLO: L("SETUP: ROOT, MODE,"); L("TUNE, SYNC"); L("AGAIN: THE DEVICE"); L("SEL + GLO: REMOTE"); break;
    case B_EDIT: L("CLEAR THE CELL"); L("SEL + EDIT: A NOTE-OFF"); L("(GROW AND SONG:"); L("SEE THEIR CARDS)"); break;
    case B_REC: L("EDIT ON / OFF:"); L("THE KEYS WRITE NOTES,"); L("THE CURSOR STEPS DOWN"); L("PLAYING: INTO THE ROW"); break;
    case B_OCTDN: L("THE KEYS AN"); L("OCTAVE DOWN"); L("SEL: THE CELL A SEMITONE"); L("WITH OCT+, 5 S: UPDATE MODE"); break;
    case B_OCTUP: L("THE KEYS AN"); L("OCTAVE UP"); L("SEL: THE CELL A SEMITONE"); L("WITH OCT-, 5 S: UPDATE MODE"); break;
    case B_SAVE: L("SAVES THE SONG"); L("AND THE DEVICE"); L("HOLD ON SETUP:"); L("THE SONG AS IT CAME"); break;
    default: break;
    }
#undef L
    return n;
}

static void draw_help(void)
{
    const char *l[HELP_LINES];
    int n = help_lines(ui.help - 1, l), k;
    cv_round(12, 10, 216, 124, 6, K_PANEL);
    cv_round(12, 10, 216, 26, 6, K_ACC);
    cv_rect(12, 30, 216, 6, K_ACC);
    text_c(120, 14, &FONT_B, BTN_LABEL[ui.help - 1], K_BG);
    for (k = 0; k < n; k++)
        text_c(120, 48 + k * 20, &FONT_S, l[k], K_TEXT);
}

/* FX held: the six tracks, mute (black keys) and solo (white keys) */
static void draw_fxwin(void)
{
    int t;
    cv_text(8, 0, &FONT_B, "MUTES", K_TEXT);
    cv_text(70, 2, &FONT_XS, "BLACK 1-6 MUTE  //  WHITE 1-6 SOLO", K_DIM);
    for (t = 0; t < EN_NTRK; t++) {
        int32_t x = 6 + t * 38;
        int muted = ui.mutes >> t & 1u;
        char s[8];
        cv_round(x, 26, 34, 100, 6, muted ? K_PANEL : TRK_C[t]);
        s[0] = (char)('1' + t), s[1] = 0;
        text_c(x + 17, 34, &FONT_L, s, muted ? K_DIM : K_BG);
        caps(s, snd_short(trk_sound(t)), sizeof s);
        text_c(x + 17, 74, &FONT_XS, s, muted ? K_DIM : K_BG);
        text_c(x + 17, 100, &FONT_XS, muted ? "MUTE" : "ON", muted ? K_HOT : K_BG);
    }
}

static void draw_main(void)
{
    uint32_t h;
    int t;
    if (ui.frame % 3u)                           /* 20 frames a second */
        return;
    if (ui.fxwin) {
        h = hash(0x4D555445u, ui.mutes | (uint32_t)par_get(P_THEME) << 8);
        for (t = 0; t < EN_NTRK; t++)
            h = hash(h, (uint32_t)trk_sound(t));
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            draw_fxwin();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    if (ui.help) {
        h = hash(0x48454C50u, ui.help);
        if (h != ui.sig[1]) {
            ui.sig[1] = h;
            cv_begin(240, MAIN_H, K_BG);
            draw_help();
            cv_blit(0, MAIN_Y);
        }
        return;
    }
    /* what the main shows depends on: the view, the cursor, the song's cells shown, the playing row, the
     * levels and glows: hashed, drawn only when it changed */
    h = hash(2166136261u, ui.view | (uint32_t)ui.trk << 4 | (uint32_t)ui.row << 8 | (uint32_t)ui.pos << 12 |
                              (uint32_t)ui.rec << 18 | (uint32_t)ui.mutes << 20 | (uint32_t)par_get(P_THEME) << 27);
    h = hash(h, en_playing | (uint32_t)en_row << 1 | (uint32_t)en_pos << 5 | (uint32_t)en_pat_now << 10 |
                    (uint32_t)song_len() << 14 | (uint32_t)ui.follow << 20 | (uint32_t)par_get(P_LOOP) << 21);
    for (t = 0; t < EN_NTRK; t++)
        h = hash(h, (uint32_t)q16(en_level[t]) | (uint32_t)q16(en_trk_glow[t]) << 5 | (uint32_t)trk_sound(t) << 10 |
                        (uint32_t)par_get(P_T(t, TP_LEVEL)) << 16);
    if (GRID_VIEW(ui.view)) {
        const en_cell_t *c = &en_song.cell[pat_shown()][0][0];
        uint32_t k;
        for (k = 0; k < (uint32_t)(EN_NROW * EN_NTRK); k++)
            h = hash(h, (uint32_t)c[k].note | (uint32_t)c[k].fx << 8 | (uint32_t)c[k].vol << 16 | (uint32_t)c[k].fx2 << 24);
        h = hash(h, (uint32_t)en_pat_len(&en_song, pat_shown()));
        h = hash(h, (uint32_t)par_get(P_LEAD) | (uint32_t)par_get(P_TAKE) << 4 | (uint32_t)par_get(P_FDENS) << 12 |
                        (uint32_t)par_get(P_ROOT) << 20 | (uint32_t)par_get(P_MODE) << 24);
        for (t = 0; t < EN_NTRK; t++)
            h = hash(h, (uint32_t)par_get(P_T(t, TP_FOLLOW)) | en_take_seed(&en_song, t, pat_shown()) << 4);
    } else if (ui.view == V_SONG) {
        uint32_t k;
        for (k = 0; k < EN_NORD; k++)
            h = hash(h, en_song.order[k]);
        h = hash(h, (uint32_t)en_song_size(&en_song));
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    if (GRID_VIEW(ui.view))
        draw_grid();
    else if (ui.view == V_SONG)
        draw_song();
    else
        draw_mixer();
    cv_blit(0, MAIN_Y);
}

/* the knob strip: a rule with the page's name (and the track's), a cell a knob */
static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)ui.trk << 12 | (uint32_t)par_get(P_THEME) << 20 |
                                       (uint32_t)ui.shift << 28), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(view_knob(ui.view, (int)i)) | (uint32_t)trk_sound(ui.trk) << 16);
    h = hash(h, (uint32_t)now_touch | (uint32_t)song_len() << 1);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    {
        char t[32], u[24];
        int m;
        int32_t xt;
        caps(t, VIEW_NAME[ui.view], 16);
        if (ui.view == V_SOUND || ui.view == V_MOVE || ui.view == V_SENDS || GRID_VIEW(ui.view)) {
            for (m = 0; t[m]; m++)
                ;
            t[m++] = ' ', t[m++] = '-', t[m++] = ' ', t[m++] = 'T', t[m++] = (char)('1' + ui.trk), t[m++] = ' ';
            caps(u, en_sound_name(trk_sound(ui.trk)), sizeof u);
            for (i = 0; u[i] && m < 30; i++)
                t[m++] = u[i];
            t[m] = 0;
        }
        xt = 236 - text_w(&FONT_XS, t);
        cv_rect(4, 5, xt - 10, 1, K_LINE);
        cv_text(xt, -1, &FONT_XS, t, ui.view == V_SOUND || ui.view == V_MOVE || ui.view == V_SENDS ? TRK_C[ui.trk] : K_ACC);
    }
    for (i = 0; i < 4; i++) {
        int k = view_knob(ui.view, (int)i), lo, hi, v, hot = now_touch && ui.touched == (int)i;
        int32_t x = (int32_t)i * 60, cx = x + 30;
        char t[24];
        uint16_t bg = hot ? K_PANEL : K_BG, fill = hot ? K_ACC : K_COLD;
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 18, 1, 44, K_LINE);
        if (hot)
            cv_round(x + 3, 12, 54, 56, 6, K_PANEL);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        {
            static const char *const SHIFTED[4] = {"Volume", "Effect 2", "Value 2", "Rows"};
            caps(t, ui.shift && ui.view == V_PATTERN ? SHIFTED[i] : ui.shift && ui.view == V_SONG && i == 1 ? "Repeat"
                                                                                                             : KNOB_LABEL[ui.view][i], sizeof t);
        }
        {
            const felucca_font_t *f = text_w(&FONT_S, t) <= 57 ? &FONT_S : &FONT_XS;
            text_c(cx, f == &FONT_S ? 12 : 13, f, t, hot ? K_TEXT : K_DIM);
        }
        if (k == K_NOTE) {                       /* the cell's note, big */
            knob_text(k, t);
            text_c(cx, 32, &FONT_M, t, hot ? K_ACC : K_TEXT);
            continue;
        }
        if (k == K_VAL || k == K_FX) {           /* hex value / the effect's letter, big */
            if (k == K_FX) {
                char e[2] = {v ? EN_FX_CHAR[v & 15] : '-', 0};
                text_c(cx, 28, &FONT_M, e, hot ? K_ACC : K_COLD);
            } else {
                knob_text(k, t);
                text_c(cx, 28, &FONT_M, t, hot ? K_ACC : K_COLD);
            }
        } else if (hi - lo < 6) {
            int nn = hi - lo + 1, j, pw = nn > 4 ? 6 : 9, gap = 3, w0 = nn * (pw + gap) - gap;
            for (j = 0; j < nn; j++)
                cv_round(cx - w0 / 2 + j * (pw + gap), 37, pw, 8, 2, j == v - lo ? fill : K_LINE);
        } else {
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

/* the lights: the page's button, PLAY (red while writing as it plays), REC in edit, FX with a track muted;
 * the keys: a kit's eight drums, or the scale; FX held: black keys the tracks playing */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    if (ui.fxwin) {
        for (i = 0; i < EN_NTRK; i++)
            if (!(ui.mutes >> i & 1u))
                k |= 1u << BLACK_KEY[i];
        plat_glow(1);
        plat_leds(1u << B_FX, k);
        return;
    }
    b |= 1u << VIEW_BTN[ui.view];
    if (en_playing)
        b |= 1u << B_PLAY;
    if (ui.rec)
        b |= 1u << B_REC;
    if (ui.mutes)
        b |= 1u << B_FX;
    if (ui.follow)
        b |= 1u << B_SEL;
    if (par_get(P_LEDS)) {
        if (trk_kit(ui.trk)) {
            for (i = 0; i < EN_NDRUM; i++)
                k |= 1u << WHITE_KEY[i];
        } else {
            uint32_t ap = pool_abs();
            for (i = 0; i < NKEYS; i++)
                if (ap >> ((5u + i) % 12u) & 1u)
                    k |= 1u << i;
        }
        if (en_playing && en_trk_glow[ui.trk] > 0.6f)   /* the track's note: its key flickers off */
            for (i = 0; i < NKEYS; i++)
                if ((int)((53 + (int)i + 12 * proj.oct) % 12) == en_trk_note[ui.trk] % 12)
                    k &= ~(1u << i);
    }
    plat_play_red(en_playing && ui.rec);
    plat_glow(par_get(P_LEDS) == 1);
    plat_leds(b, k);
}

/* THE BOOT CARDS (as HYP-1's and GRAV-1's). First RedtailWorks: a pixel red panda on the name; then BIT-1 in
 * big pixel letters, magenta over an orange shadow, broken by a glitch as the cover is, the rows lighting
 * in until the song is ready. Any button or key: straight in. */
#define PANDA_W 32
#define PANDA_H 25
static const char *const PANDA[PANDA_H] = {
    ".WWW..............WWW...........",
    ".WBBW............WBBW...........",
    ".WBBRRRRRRRRRRRRRRBBW...........",
    "..RRRRRRRRRRRRRRRRRR............",
    ".RRRWWWRRRRRRRRWWWRRR...........",
    ".RRRRRRRRRRRRRRRRRRRR...........",
    ".RRRRKKRRRRRRRRKKRRRR...........",
    ".RWWDKKDRRRRRRDKKDWWR...........",
    ".WWWWDDWWWWWWWWDDWWWW...........",
    ".WWWWWDDWWKKWWDDWWWWW...........",
    "..WWWWWDDWWWWDDWWWWW........DDD.",
    "...DWWWWDDWWDDWWWWD........DDDDD",
    "....DDWWWWWWWWWWDD.........TTTTT",
    "......DDDDDDDDDD...........TTTTT",
    "......DRRRRRRRRD...........RRRRR",
    ".....BRRRRRRRRRRB..........RRRRR",
    "....BBRRRRRRRRRRBB.........TTTTT",
    "....BBBRRRRRRRRBBB........TTTTTT",
    "...BBBBBRRRRRRBBBBB.......RRRRRR",
    "...BBBBBBBBBBBBBBBB......RRRRRR.",
    "...BBBBBBBBBBBBBBBBRRTTRRTTTTT..",
    "...BBBBBBBBBBBBBBBBRRTTRRTTTTT..",
    "....BBBB.BBBB.BBBBRRRTTRRRRRR...",
    "...KKBBB..BB..BBBKKDDTTDDDD.....",
    "...KKKK...KK...KKKK............."
};
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

/* the BIT-1 card: big 5 x 7 pixel letters (6 px a pixel) */
#define BIG_N 5
static const uint8_t BIG_GLYPH[BIG_N][7] = {
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  /* B */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},  /* I */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  /* T */
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},  /* - */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  /* 1 */
};
#define SPLASH_STEP_Y 184
#define K_MAG RGB(255, 64, 64)
#define K_ORG RGB(255, 200, 40)
static void draw_bit(void)
{
    int g, r, c, pass;
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 144, K_BG);
    for (pass = 0; pass < 2; pass++)
        for (g = 0; g < BIG_N; g++)
            for (r = 0; r < 7; r++)
                for (c = 0; c < 5; c++)
                    if (BIG_GLYPH[g][r] >> (4 - c) & 1u) {
                        int32_t gx = 33 + (g * 6 + c) * 6 + (pass ? 0 : 3), gy = 10 + r * 6 + (pass ? 0 : 3);
                        cv_rect(gx, gy, 6, 6, pass ? (r < 2 ? RGB(255, 140, 140) : K_MAG) : K_ORG);   /* lit from the top */
                    }
    text_c(120, 70, &FONT_S, "A GENERATIVE", K_TEXT);
    text_c(120, 88, &FONT_S, "8-BIT TRACKER", K_TEXT);
    text_c(120, 112, &FONT_XS, EN_VERSION[0] == 'D' ? "EXPERIMENTAL" : "EXPERIMENTAL  //  " EN_VERSION, K_ORG);
    cv_blit(0, 24);
    for (c = 0; c < 16; c++)
        lcd_fill(8 + c * 14, SPLASH_STEP_Y, 11, 11, K_PANEL);
}

static void splash_tick(void)
{
    uint32_t t = plat_ms() - ui.boot_t, t1 = ui_splash_ms * 17u / 32u, n, k = 0, i;
    if (ui.splash == 1) {
        if (t < t1)
            return;
        ui.splash = 2;
        draw_bit();
    }
    n = (t - t1) * 17u / (ui_splash_ms - t1 + 1u);
    if (n > 16u)
        n = 16u;
    for (; ui.splash_step < n; ui.splash_step++)
        lcd_fill(8 + ui.splash_step * 14, SPLASH_STEP_Y, 11, 11, TRK_C[ui.splash_step * 6u / 16u]);
    for (i = 0; i < n; i++)
        k |= 1u << WHITE_KEY[i];
    plat_glow(1);
    plat_leds(n ? 1u << B_PLAY : 0u, k);
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.boot_t = plat_ms();
    ui.ev_r = en_ev_w;
    ui.follow = 1;
    ui.style = 0;
    ui.dens = 50;
    ui.range = 40;
    ui.splash = ui_splash_ms > 0u;
    if (ui.splash) {
        draw_splash();
    } else {
        lcd_fill(0, 0, 240, 240, K_BG);
        if (!en_playing)                         /* it plays from the moment it's on, as every RedtailWorks firmware */
            en_play(0, 0);
    }
}

void ui_frame(void)
{
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
        if (plat_ms() - ui.boot_t < ui_splash_ms && !held) {
            splash_tick();
            return;
        }
        ui.splash = 0;
        ui.btn = plat_buttons();
        ui.keys = plat_keys();
        ui.btn_used = ui.btn;
        lcd_fill(0, 0, 240, 240, K_BG);
        if (!en_playing)
            en_play(0, 0);
    }
    input();
    engine_news();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
