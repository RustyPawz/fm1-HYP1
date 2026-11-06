/* SPDX-License-Identifier: GPL-3.0-only */
/* BIT-1's engine on the host: the songs as they come (valid, small enough for their flash object, every one
 * sounding and none past full scale), the scale, GROW, the tracker's timing (tempo, loop, jump, note-offs,
 * mutes), the effects, Evolve, MIDI out and clock, clock in, a key into a track, every sound, and Arp.
 *   tests/run_tests.sh (or: cc -DEN_HOST -Ifirmware/src/dsp tests/host/bit_test.c firmware/src/dsp/amb.c -lm) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t midi_n, midi_notes[16], midi_vel[16], midi_clock, midi_start, midi_stop;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1;
    midi_n++;
    if ((st & 0xF0u) == 0x90u && d2)
        midi_notes[st & 15u]++, midi_vel[st & 15u] = d2;
    if (st == 0xF8u)
        midi_clock++;
    if (st == 0xFAu)
        midi_start++;
    if (st == 0xFCu)
        midi_stop++;
}
static void midi_reset(void)
{
    midi_n = midi_clock = midi_start = midi_stop = 0;
    memset(midi_notes, 0, sizeof midi_notes);
}

static int32_t blk[512];
static double peak, sumsq;
static long frames;
static int bad;
static void run(double secs)
{
    long b, n = (long)(secs * 44100.0 / 256.0);
    for (b = 0; b < n; b++) {
        int i;
        en_render(blk, 256, 2048);
        for (i = 0; i < 512; i++) {
            double x = (double)blk[i] / 8388608.0;
            if (x != x)
                bad = 1;
            sumsq += x * x;
            if (fabs(x) > peak)
                peak = fabs(x);
        }
        frames += 256;
    }
}
static void meter_reset(void) { peak = sumsq = 0.0; frames = 0; }
static double rms_db(void) { return 10.0 * log10(sumsq / (double)(frames * 2 > 0 ? frames * 2 : 1) + 1e-12); }

static void set(int p, int v)
{
    if (p < P_NSONG)
        en_song.par[p] = (int16_t)v;
    en_set(p, v);
}
static void load(int i)
{
    en_stop();
    en_song_recipe(i, &en_song);
    en_reload();
    run(0.05);
}
static void blank(void)                         /* an empty song: every track silent, one pattern */
{
    int t;
    en_stop();
    memset(en_song.cell, 0, sizeof en_song.cell);
    memset(en_song.order, 0, sizeof en_song.order);
    for (t = 0; t < EN_NTRK; t++)
        en_song.par[P_T(t, TP_EVOLVE)] = 0, en_song.par[P_T(t, TP_FOLLOW)] = FR_OFF;
    en_song.par[P_LEAD] = 0;
    en_song.par[P_LEN] = 1;
    en_song.par[P_LOOP] = LOOP_SONG;
    en_reload();
    run(3.0);
}

int main(void)
{
    int i, t, p, r, s;
    char txt[64];
    en_init();

    /* the songs as they come */
    for (i = 0; i < EN_NSONG; i++) {
        static en_song_t s0, s1;
        static uint8_t pk[3840];
        int n;
        en_song_recipe(i, &s0);
        en_song_recipe(i, &s1);
        CHECK(en_song_valid(&s0), "song %d as it comes is not valid", i);
        /* saved packed: it fits its flash object (with room to write in), and comes back the same */
        n = en_song_pack(&s0, pk, (int)sizeof pk);
        CHECK(n > 0 && n == en_song_size(&s0) && n < 3000, "song %d packs to %d bytes", i, n);
        CHECK(n > 0 && en_song_unpack(pk, n, &s1) == 0 && !memcmp(&s0, &s1, sizeof s0), "song %d unpacked is not the same", i);
        CHECK(n > 0 && en_song_unpack(pk, n - 1, &s1) != 0, "song %d: a short pack passes", i);
        printf("  %-16s packs to %d bytes\n", s0.name, n);
        en_song_recipe(i, &s1);
        CHECK(!memcmp(&s0, &s1, sizeof s0), "song %d is not the same twice", i);
        CHECK(!strcmp(s0.name, en_song_title(i)), "song %d's name", i);
        s0.par[P_TEMPO] = 999;
        CHECK(!en_song_valid(&s0), "a tempo out of range passes");
        en_song_recipe(i, &s0);
        s0.cell[3][4][2].note = 5;
        CHECK(!en_song_valid(&s0), "a note of 5 passes");
    }
    for (i = 0; i < P_NPARAMS; i++) {           /* every value's text fits the screen's 16 bytes */
        const en_param_t *pi = en_param_info(i);
        int v;
        CHECK(pi->lo <= pi->def && pi->def <= pi->hi, "param %d's default out of range", i);
        for (v = pi->lo; v <= pi->hi; v += (pi->hi - pi->lo) / 7 + 1) {
            memset(txt, 0, sizeof txt);
            en_param_text(i, v, txt);
            CHECK(strlen(txt) > 0 && strlen(txt) < 16, "param %d value %d: \"%s\"", i, v, txt);
        }
    }

    /* the scale */
    {
        int st[12], n = en_scale(EN_MODE_POOL[MODE_PHRYGIAN], st);
        CHECK(n == 7 && st[1] == 1 && st[2] == 3, "phrygian's steps");
        CHECK(en_snap(63, 2, EN_MODE_POOL[MODE_PHRYGIAN]) == 63, "Eb is in D phrygian");
        CHECK(en_snap(64, 2, EN_MODE_POOL[MODE_PHRYGIAN]) == 63, "E snaps to Eb in D phrygian: %d", en_snap(64, 2, EN_MODE_POOL[MODE_PHRYGIAN]));
        CHECK(en_snap(61, 2, EN_MODE_POOL[MODE_AEOLIAN]) == 60, "C# snaps down to C in D aeolian");
        CHECK(en_snap(61, 0, EN_MODE_POOL[MODE_CHROMATIC]) == 61, "chromatic keeps every note");
    }

    /* GROW: every style on a kit and on a melodic track, in the scale, the same from the same seed */
    {
        static en_song_t g, h;
        en_song_recipe(0, &g);
        for (s = 0; s < EN_NGROW; s++)
            for (t = 0; t < EN_NTRK; t++) {
                int notes = 0;
                h = g;                           /* the same from the same seed (a tool on the same cells) */
                en_grow(&g, 9, t, s, 70, 60, 1234u);
                en_grow(&h, 9, t, s, 70, 60, 1234u);
                CHECK(!memcmp(g.cell[9], h.cell[9], sizeof g.cell[9]), "grow style %d track %d not repeatable", s, t);
                for (r = 0; r < EN_NROW; r++) {
                    int n = g.cell[9][r][t].note;
                    if (n < NOTE_LO)
                        continue;
                    notes++;
                    if (en_sound_is_kit(g.par[P_T(t, TP_SOUND)]))
                        CHECK(n >= 36 && n <= 43, "grow: a drum %d outside the kit", n);
                    else
                        CHECK(en_snap(n, g.par[P_ROOT], EN_MODE_POOL[g.par[P_MODE]]) == n, "grow: %d outside the scale", n);
                }
                CHECK(notes > 0, "grow style %d track %d wrote nothing", s, t);
                CHECK(en_song_valid(&g), "grow made the song invalid");
            }
        en_grow(&h, 9, 4, 6, 70, 60, 99u);
        en_grow(&g, 9, 4, 6, 70, 60, 100u);
        CHECK(memcmp(g.cell[9], h.cell[9], sizeof g.cell[9]), "grow gives the same from two seeds");
    }

    /* SONG: grown to 32 positions around what's written: the user's positions and their patterns untouched,
     * every position playing a pattern with notes, the same from the same seed; from nothing, a song too */
    {
        static en_song_t g, h, before;
        int n, used[EN_NPAT] = {0}, distinct = 0;
        en_song_recipe(1, &g);
        g.par[P_LEN] = 4;                        /* the user's: positions 1-4 (patterns 1, 1, 2, 2) */
        g.order[1] = EN_ORD(0, 3);
        before = g;
        h = g;
        n = en_grow_song(&g, 32, 60, 77u);
        en_grow_song(&h, 32, 60, 77u);
        CHECK(n == 28 && g.par[P_LEN] == 32, "song: %d positions filled, length %d", n, g.par[P_LEN]);
        CHECK(!memcmp(&g, &h, sizeof g), "song: not the same from the same seed");
        CHECK(!memcmp(g.order, before.order, 4) && !memcmp(g.cell[0], before.cell[0], sizeof g.cell[0]) &&
              !memcmp(g.cell[1], before.cell[1], sizeof g.cell[1]), "song: the user's positions or patterns changed");
        for (i = 0; i < 32; i++) {
            int p2 = EN_ORD_PAT(g.order[i]), r2, t2, any = 0;
            for (r2 = 0; r2 < EN_NROW; r2++)
                for (t2 = 0; t2 < EN_NTRK; t2++)
                    any |= g.cell[p2][r2][t2].note != 0;
            CHECK(any, "song: position %d plays an empty pattern", i + 1);
            if (!used[p2]++)
                distinct++;
        }
        CHECK(distinct >= 4 && distinct <= EN_NPAT, "song: %d patterns in it", distinct);
        CHECK(en_song_valid(&g), "song: not valid");
        printf("  song grown: %d positions, %d patterns\n", n, distinct);
        memset(g.cell, 0, sizeof g.cell);        /* nothing written: grown from nothing */
        g.par[P_LEN] = 1;
        n = en_grow_song(&g, 16, 50, 5u);
        CHECK(n == 16 && g.par[P_LEN] == 16, "song from nothing: %d positions", n);
        for (i = 0; i < 16; i++) {
            int p2 = EN_ORD_PAT(g.order[i]), r2, t2, any = 0;
            for (r2 = 0; r2 < EN_NROW; r2++)
                for (t2 = 0; t2 < EN_NTRK; t2++)
                    any |= g.cell[p2][r2][t2].note != 0;
            CHECK(any, "song from nothing: position %d plays an empty pattern", i + 1);
        }
    }

    /* NEW SONG: from nothing, for every seed tried: valid, the same from the same seed, its parts and steps as
     * asked, every position playing a part with notes, a kit on track 1, it packs; it plays */
    {
        static en_song_t g, h;
        static uint8_t pk[3840];
        int seed, ways[4] = {0, 0, 0, 0};
        for (seed = 0; seed < 40; seed++) {
            const int parts = 2 + seed % 15, steps = 4 + seed % 29, pairs = steps > EN_NROW;
            int n = en_new_song(&g, parts, steps, 55, (uint32_t)seed * 101u), ok = 1;
            en_new_song(&h, parts, steps, 55, (uint32_t)seed * 101u);
            CHECK(!memcmp(&g, &h, sizeof g), "new song %d: not the same from the same seed", seed);
            CHECK(en_song_valid(&g), "new song %d: not valid", seed);
            CHECK(n >= 4 && n <= EN_NORD && g.par[P_LEN] == n, "new song %d: %d positions", seed, n);
            CHECK(en_sound_is_kit(g.par[P_T(0, TP_SOUND)]), "new song %d: track 1 isn't a kit", seed);
            for (i = 0; i < n; i++) {
                int p2 = EN_ORD_PAT(g.order[i]), r2, t2, any = 0;
                ok &= (pairs ? (p2 & 1) == 0 && p2 < 2 * (parts > 8 ? 8 : parts) : p2 < parts) && g.len[p2] == steps;
                for (r2 = 0; r2 < steps; r2++)
                    for (t2 = 0; t2 < EN_NTRK; t2++)
                        any |= en_ccell(&g, p2, r2, t2)->note != 0;
                ok &= any || en_follow_role(&g, 0);
            }
            CHECK(ok, "new song %d: a position outside its parts, a part's steps not %d, or an empty one", seed, steps);
            CHECK(en_song_pack(&g, pk, (int)sizeof pk) > 0, "new song %d: doesn't pack", seed);
            ways[g.name[0] == 'F' ? 0 : g.name[0] == 'G' ? 1 : g.name[0] == 'E' ? 2 : 3]++;
        }
        CHECK(ways[0] && ways[1] && ways[2] && ways[3], "new songs: the ways %d %d %d %d", ways[0], ways[1], ways[2], ways[3]);
        printf("  new songs: %d Follow, %d Grow, %d Euclid, %d Minimal\n", ways[0], ways[1], ways[2], ways[3]);
        en_new_song(&en_song, 6, 16, 55, 4242u); /* and one plays */
        en_reload();
        meter_reset();
        bad = 0;
        en_play(0, 0);
        run(8.0);
        CHECK(!bad && rms_db() > -40.0 && peak < 0.98, "a new song plays: %.1f dB peak %.2f", rms_db(), peak);
        en_stop();
        run(2.0);
    }

    /* every song plays: notes, rows at its tempo, a sensible level, nothing past full scale */
    for (i = 0; i < EN_NSONG; i++) {
        uint32_t n0, r0;
        double secs = 24.0, want;
        load(i);
        n0 = en_notes, r0 = en_rows;
        meter_reset();
        bad = 0;
        en_play(0, 0);
        run(secs);
        want = secs * en_song.par[P_TEMPO] * 4.0 / 60.0;
        CHECK(fabs((double)(en_rows - r0) - want) <= 2.0, "%s: %u rows in %.0f s, wanted %.0f", en_song.name, en_rows - r0, secs, want);
        CHECK(en_notes - n0 > 10, "%s: only %u notes", en_song.name, en_notes - n0);
        CHECK(!bad, "%s: NaN out", en_song.name);
        CHECK(peak < 0.98, "%s: peak %.3f", en_song.name, peak);
        CHECK(rms_db() > -36.0 && rms_db() < -6.0, "%s: rms %.1f dB", en_song.name, rms_db());
        printf("  %-16s %3u notes  rms %5.1f dB  peak %.2f\n", en_song.name, en_notes - n0, rms_db(), peak);
        en_stop();
        meter_reset();
        run(9.0);
        meter_reset();
        run(1.0);
        CHECK(rms_db() < -50.0, "%s: still %.1f dB nine seconds after stopping", en_song.name, rms_db());
    }

    /* the tracker: a note, its row, a note-off; tempo; Loop; Jump; mutes */
    load(0);
    blank();
    for (t = 1; t < EN_NTRK; t++)
        set(P_T(t, TP_SOUND), 3 + t);
    set(P_T(0, TP_SOUND), 0);
    set(P_TEMPO, 120);
    en_song.cell[0][0][1].note = 50;             /* a pulse bass, held, let go at row 8 */
    en_song.cell[0][8][1].note = NOTE_OFF;
    midi_reset();
    meter_reset();
    en_play(0, 0);
    run(0.9);                                    /* rows 0-7 at 120: 0.125 s each */
    CHECK(rms_db() > -40.0, "the held bass is silent: %.1f dB", rms_db());
    CHECK(midi_notes[1] >= 1, "no MIDI note on track 2's channel");
    run(0.6);                                    /* the off at 1.0 s; the pattern comes round again at 2.0 */
    meter_reset();
    run(0.4);
    CHECK(rms_db() < -40.0, "after the note-off, still %.1f dB", rms_db());
    run(0.1);
    {
        uint32_t r0 = en_rows;
        run(4.0);
        CHECK(en_rows - r0 >= 31 && en_rows - r0 <= 33, "120 BPM: %u rows in 4 s", en_rows - r0);
    }
    en_stop();
    run(0.1);
    /* Loop: Song moves on; Pattern stays */
    en_song.par[P_LEN] = 4;
    for (p = 0; p < 4; p++)
        en_song.order[p] = (uint8_t)p;
    set(P_LEN, 4);
    set(P_LOOP, LOOP_PATTERN);
    en_play(2, 0);
    run(5.0);
    CHECK(en_pos == 2, "Loop Pattern: at position %d", en_pos);
    set(P_LOOP, LOOP_SONG);
    run(2.1);
    CHECK(en_pos == 3, "Loop Song: at position %d", en_pos);
    run(2.1);
    CHECK(en_pos == 0, "the song wraps: at position %d", en_pos);
    /* Jump: J2 on row 3 of pattern 0 */
    en_song.cell[0][3][5].fx = FX_JUMP << 4 | 2;
    en_stop();
    en_play(0, 0);
    run(0.55);                                   /* rows 0-4 */
    CHECK(en_pos == 2, "J2: at position %d", en_pos);
    en_song.cell[0][3][5].fx = 0;
    en_stop();
    run(0.1);
    /* mutes: track 2 muted plays no notes */
    en_song.par[P_LEN] = 1;
    set(P_LEN, 1);
    for (r = 0; r < EN_NROW; r += 2)
        en_song.cell[0][r][1].note = 45;
    en_track_mute(1u << 1);
    midi_reset();
    en_play(0, 0);
    run(2.0);
    CHECK(midi_notes[1] == 0, "a muted track played %u notes", midi_notes[1]);
    en_track_mute(0);
    run(2.0);
    CHECK(midi_notes[1] > 4, "unmuted, it played %u", midi_notes[1]);
    en_stop();
    run(0.1);

    /* KAWA's cell: a song too full to pack is refused; the volume column; the second effect */
    {
        static en_song_t d;
        static uint8_t pk[3840];
        en_song_recipe(0, &d);
        for (p = 0; p < EN_NPAT; p++)
            for (r = 0; r < EN_NROW; r++)
                for (t = 0; t < EN_NTRK; t++)
                    d.cell[p][r][t].note = 60, d.cell[p][r][t].vol = 9, d.cell[p][r][t].fx2 = FX_GLIDE << 4 | 1;
        CHECK(en_song_pack(&d, pk, (int)sizeof pk) < 0 && en_song_size(&d) > 3840, "a full song packs: %d", en_song_size(&d));
    }
    blank();
    set(P_TEMPO, 120);
    en_song.cell[0][0][1].note = 50, en_song.cell[0][0][1].vol = 16;   /* F: full */
    en_song.cell[0][8][1].note = 50, en_song.cell[0][8][1].vol = 2;    /* 1: near silent */
    midi_reset();
    en_play(0, 0);
    run(0.5);
    CHECK(midi_vel[1] > 110, "volume F: velocity %u", midi_vel[1]);
    run(0.6);
    CHECK(midi_vel[1] < 20, "volume 1: velocity %u", midi_vel[1]);
    en_stop();
    run(0.1);
    blank();
    set(P_TEMPO, 120);
    for (r = 0; r < EN_NROW; r++)                /* Chance 0 as the second effect, a glide the first */
        en_song.cell[0][r][2].note = 50, en_song.cell[0][r][2].fx = FX_GLIDE << 4 | 1, en_song.cell[0][r][2].fx2 = FX_CHANCE << 4;
    midi_reset();
    en_play(0, 0);
    run(16.0);
    CHECK(midi_notes[2] >= 1 && midi_notes[2] <= 22, "Chance 0 second: %u of 128", midi_notes[2]);
    en_stop();
    run(0.1);

    /* a pattern's length (KAWA's END), a position's repeats, and Hop (H: on to a row, once a pass) */
    blank();
    set(P_TEMPO, 120);
    en_song.len[0] = 6;                          /* six rows: 0.75 s a pass at 120 */
    {
        uint32_t p0;
        en_play(0, 0);
        run(0.05);
        p0 = en_passes;
        run(3.0);
        CHECK(en_passes - p0 == 4, "six rows: %u passes in 3 s, wanted 4", en_passes - p0);
        en_stop();
        run(0.1);
    }
    en_song.len[0] = 16;
    en_song.order[0] = EN_ORD(0, 3), en_song.order[1] = EN_ORD(1, 1);   /* pattern 1 three times, then 2 */
    en_song.par[P_LEN] = 2;
    set(P_LEN, 2);
    en_play(0, 0);
    run(5.9);                                    /* two passes of 2 s and most of a third: still position 1 */
    CHECK(en_pos == 0, "x3: at position %d after 5.9 s", en_pos);
    run(0.3);
    CHECK(en_pos == 1, "x3: at position %d after 6.2 s", en_pos);
    en_stop();
    run(0.1);
    en_song.order[0] = EN_ORD(0, 1);
    en_song.cell[0][7][5].fx2 = FX_HOP << 4 | 0; /* row 7: back to row 0, once */
    {
        uint32_t r0;
        en_play(0, 0);
        run(0.05);
        r0 = en_rows;
        run(4.95);                               /* 40 rows: a pass of 16 + 8 again is 24, then the next */
        CHECK(en_pos == 1 || en_rows - r0 >= 38, "hop: %u rows", en_rows - r0);
        CHECK(en_passes > 0, "hop: no pass");
        en_stop();
        run(0.1);
    }
    {
        uint32_t p0;
        en_song.par[P_LEN] = 1;
        set(P_LEN, 1);
        en_play(0, 0);
        run(0.05);
        p0 = en_passes;
        run(6.0);                                /* 48 rows: a pass is 24 rows with the hop, so 2 */
        CHECK(en_passes - p0 == 2, "hop once a pass: %u passes in 6 s, wanted 2", en_passes - p0);
        en_stop();
        run(0.1);
    }
    en_song.cell[0][7][5].fx2 = 0;

    /* 32 steps: a pattern of 32 rows plays its 16 and then the next pattern's (KAWA's link), 32 rows a pass;
     * a note in the linked half plays; Shift goes round all 32 */
    blank();
    set(P_TEMPO, 120);
    en_song.len[0] = 32;
    en_song.cell[1][4][2].note = 55;             /* row 20 of pattern 1: pattern 2's row 4 */
    {
        uint32_t p0;
        midi_reset();
        en_play(0, 0);
        run(0.05);
        p0 = en_passes;
        run(8.0);                                /* 64 rows at 120: two passes of 32 */
        CHECK(en_passes - p0 == 2, "32 rows: %u passes in 8 s, wanted 2", en_passes - p0);
        CHECK(midi_notes[2] == 2, "the linked half's note: played %u times in two passes", midi_notes[2]);
        en_stop();
        run(0.1);
    }
    {
        static en_song_t g;
        g = en_song;
        en_cell(&g, 0, 31, 3)->note = 60;
        en_grow(&g, 0, 3, G_SHIFT, 50, 40, 1u);
        CHECK(en_ccell(&g, 0, 0, 3)->note == 60 && en_ccell(&g, 0, 31, 3)->note == 0, "Shift round 32 rows");
        CHECK(en_song_valid(&g), "a 32-row pattern: not valid");
    }
    en_song.len[0] = 16;
    en_song.cell[1][4][2].note = 0;

    /* effects: Chance 0 plays about one in sixteen; Ratchet 3 plays four; Chord 1 a triad */
    blank();
    set(P_TEMPO, 120);
    for (r = 0; r < EN_NROW; r++)
        en_song.cell[0][r][2].note = 50, en_song.cell[0][r][2].fx = FX_CHANCE << 4 | 0;
    midi_reset();
    en_play(0, 0);
    run(16.0);                                   /* 128 rows */
    CHECK(midi_notes[2] >= 1 && midi_notes[2] <= 22, "Chance 0: %u of 128", midi_notes[2]);
    en_stop();
    run(0.1);
    blank();
    {
        uint32_t n0 = en_notes;
        en_song.cell[0][0][3].note = 60, en_song.cell[0][0][3].fx = FX_CHORD << 4 | 1;
        en_song.cell[0][8][3].note = 60, en_song.cell[0][8][3].fx = FX_RATCHET << 4 | 3;
        meter_reset();
        en_play(0, 0);
        run(2.0);
        CHECK(en_notes - n0 == 2, "a chord and a ratchet count as %u notes", en_notes - n0);
        CHECK(rms_db() > -50.0, "a chord and a ratchet: silent");
    }
    en_stop();
    run(0.1);

    /* Evolve: a melodic track with it on drifts; with it off, never */
    blank();
    for (r = 0; r < EN_NROW; r += 2)
        en_song.cell[0][r][4].note = 62;
    set(P_T(4, TP_EVOLVE), 100);
    {
        uint32_t e0 = en_evolved;
        en_play(0, 0);
        run(40.0);
        CHECK(en_evolved > e0, "Evolve 100: nothing moved in 20 passes");
        set(P_T(4, TP_EVOLVE), 0);
        en_stop();
        run(0.1);
        en_play(0, 0);
        run(0.1);
        e0 = en_evolved;
        run(20.0);
        CHECK(en_evolved == e0, "Evolve 0: %u notes moved", en_evolved - e0);
    }
    en_stop();
    run(0.1);

    /* MIDI clock out (24 a beat) and START / STOP, when asked */
    set(P_MIDI, EN_MIDI_BOTH);
    set(P_TEMPO, 120);
    midi_reset();
    en_play(0, 0);
    run(2.0);                                    /* four beats */
    en_stop();
    run(0.1);
    CHECK(midi_start == 1 && midi_stop == 1, "START %u STOP %u", midi_start, midi_stop);
    CHECK(midi_clock >= 94 && midi_clock <= 98, "%u clocks in four beats", midi_clock);
    set(P_MIDI, EN_MIDI_NOTES);
    midi_reset();
    en_play(0, 0);
    run(1.0);
    en_stop();
    run(0.1);
    CHECK(midi_clock == 0 && midi_start == 0, "Notes only: clock went out");

    /* MIDI clock in: START plays, six ticks a row */
    set(P_SYNC, 1);
    {
        uint32_t r0;
        en_clock(EN_CLK_START);
        r0 = en_rows;
        for (i = 0; i < 24 * 4; i++) {           /* four beats of ticks, three blocks apart (768 samples: 143.6 BPM) */
            en_clock(EN_CLK_TICK);
            run(0.02);
        }
        CHECK(en_playing, "clock START did not play");
        CHECK(en_rows - r0 >= 15 && en_rows - r0 <= 17, "96 ticks: %u rows", en_rows - r0);
        CHECK(fabs((double)en_bpm - 143.55) < 3.0, "the clock's tempo: %.1f", (double)en_bpm);
        en_clock(EN_CLK_STOP);
        run(0.1);
        CHECK(!en_playing, "clock STOP did not stop");
        run(1.0);
    }

    /* a key into a track, stopped: it sounds, and lets go */
    blank();
    set(P_T(3, TP_SOUND), 8);
    meter_reset();
    en_audition(3, 57, 1);
    run(1.0);
    CHECK(rms_db() > -45.0, "a key into the pad: %.1f dB", rms_db());
    en_audition(3, 57, 0);
    run(4.0);
    meter_reset();
    run(0.5);
    CHECK(rms_db() < -50.0, "the pad let go: still %.1f dB", rms_db());

    /* FOLLOW, the pure part: the chords heard in a lead; each role's take in the scale (a kit's in the kit),
     * the same from the same seed, nothing without a lead */
    {
        static en_song_t f;
        en_cell_t o[EN_NROW], o2[EN_NROW];
        int deg[2], role;
        en_song_recipe(1, &f);                   /* a song without Follow: blank its lead track */
        f.par[P_ROOT] = 0, f.par[P_MODE] = MODE_MAJOR;
        memset(f.cell[2], 0, sizeof f.cell[2]);
        f.cell[2][0][3].note = 60, f.cell[2][2][3].note = 64, f.cell[2][4][3].note = 67, f.cell[2][6][3].note = 64;
        f.cell[2][8][3].note = 65, f.cell[2][10][3].note = 69, f.cell[2][12][3].note = 72, f.cell[2][14][3].note = 69;
        CHECK(!en_follow_chords(&f, 2, deg), "chords heard with no lead");
        f.par[P_LEAD] = 4;
        CHECK(en_follow_chords(&f, 2, deg) && deg[0] == 0 && deg[1] == 3, "C-E-G then F-A-C: chords %d %d", deg[0], deg[1]);
        for (t = 0; t < EN_NTRK; t++)
            f.par[P_T(t, TP_SOUND)] = t == 0 ? 0 : t == 1 ? 3 : t == 2 ? 14 : 8 + t;
        for (role = FR_AUTO; role < EN_NROLE; role++)
            for (t = 0; t < EN_NTRK; t++) {
                int notes = 0, kit = en_sound_is_kit(f.par[P_T(t, TP_SOUND)]);
                f.par[P_T(t, TP_FOLLOW)] = (int16_t)role;
                en_follow(&f, 2, t, 77u, o);
                en_follow(&f, 2, t, 77u, o2);
                CHECK(!memcmp(o, o2, sizeof o), "follow role %d track %d not repeatable", role, t);
                if (t == 3) {
                    CHECK(en_follow_role(&f, t) == FR_OFF, "the lead follows itself");
                    continue;
                }
                for (r = 0; r < EN_NROW; r++) {
                    int n = o[r].note;
                    if (n < NOTE_LO)
                        continue;
                    notes++;
                    if (en_follow_role(&f, t) == FR_DRUMS)
                        CHECK(n >= 36 && n <= 43, "follow: drum %d outside the kit", n);
                    else
                        CHECK(en_snap(n, 0, EN_MODE_POOL[MODE_MAJOR]) == n, "follow role %d: %d outside the scale", role, n);
                }
                CHECK(notes > 0, "follow role %d track %d (kit %d) wrote nothing", role, t, kit);
            }
        f.par[P_T(1, TP_FOLLOW)] = FR_BASS;      /* the bass: the chord's roots on the beats */
        en_follow(&f, 2, 1, 5u, o);
        CHECK(o[0].note % 12 == 0 && o[8].note % 12 == 5, "the bass under C then F: %d %d", o[0].note, o[8].note);
        f.par[P_T(4, TP_FOLLOW)] = FR_HARMONY;   /* the harmony: under the lead, on its rhythm */
        en_follow(&f, 2, 4, 5u, o);
        for (r = 0; r < EN_NROW; r++) {
            int ln = f.cell[2][r][3].note;
            CHECK((o[r].note >= NOTE_LO) == (ln >= NOTE_LO), "harmony row %d: %d against the lead's %d", r, o[r].note, ln);
            if (ln >= NOTE_LO)
                CHECK(o[r].note != ln && o[r].note - ln <= 5 && ln - o[r].note <= 9, "harmony row %d: %d for %d", r, o[r].note, ln);
        }
        f.par[P_LEAD] = 0;
        en_follow(&f, 2, 1, 5u, o);
        for (r = 0; r < EN_NROW; r++)
            CHECK(o[r].note == 0, "Follow off still writes");
    }

    /* FOLLOW in the render: an empty track given a role plays from the lead; Evolve 0 keeps the take, 100 new ones;
     * the lead off, it is silent again */
    blank();
    set(P_TEMPO, 120);
    for (r = 0; r < EN_NROW; r += 2)
        en_song.cell[0][r][3].note = (uint8_t)(60 + (r % 8));
    set(P_T(3, TP_SOUND), 8);
    set(P_T(1, TP_SOUND), 4);
    set(P_T(0, TP_SOUND), 0);
    set(P_T(1, TP_FOLLOW), FR_AUTO);
    set(P_T(0, TP_FOLLOW), FR_AUTO);
    set(P_LEAD, 4);
    {
        uint32_t k0;
        midi_reset();
        k0 = en_takes;
        en_play(0, 0);
        run(8.0);                                /* four passes */
        CHECK(midi_notes[1] >= 4, "a following bass played %u notes", midi_notes[1]);
        CHECK(midi_notes[0] >= 8, "following drums played %u notes", midi_notes[0]);
        CHECK(en_takes == k0, "Evolve 0: %u new takes", en_takes - k0);
        set(P_T(0, TP_EVOLVE), 100);
        run(8.0);
        CHECK(en_takes - k0 >= 3, "Evolve 100: %u new takes in four passes", en_takes - k0);
        set(P_T(0, TP_EVOLVE), 0);               /* (a kit's Evolve, the lead off, would ghost its drums in) */
        set(P_LEAD, 0);
        run(0.2);
        midi_reset();
        run(4.0);
        CHECK(midi_notes[0] == 0 && midi_notes[1] == 0, "the lead off, its followers still play: %u %u", midi_notes[0], midi_notes[1]);
        en_stop();
        run(0.1);
    }

    /* every sound in the bank: a key into it sounds, stays under full scale, and lets go */
    for (s = 0; s < EN_NBUILTIN; s++) {
        blank();
        set(P_T(3, TP_SOUND), s);
        set(P_T(3, TP_LEVEL), 80);
        meter_reset();
        bad = 0;
        if (en_sound_is_kit(s)) {
            int d;
            for (d = 0; d < 8; d++) {
                en_audition(3, 36 + d, 1);
                run(0.12);
                en_audition(3, 36 + d, 0);
            }
        } else {
            en_audition(3, 60, 1);
            run(0.6);
            en_audition(3, 60, 0);
        }
        CHECK(!bad, "%s: NaN", EN_SOUND_NAME[s]);
        CHECK(rms_db() > -50.0, "%s: silent (%.1f dB)", EN_SOUND_NAME[s], rms_db());
        CHECK(peak < 0.98, "%s: peak %.3f", EN_SOUND_NAME[s], peak);
        printf("  %-12s rms %5.1f dB  peak %.2f\n", EN_SOUND_NAME[s], rms_db(), peak);
        run(4.0);
        meter_reset();
        run(0.5);
        CHECK(rms_db() < -50.0, "%s: still %.1f dB after letting go", EN_SOUND_NAME[s], rms_db());
    }

    /* Arp: a square's note played as a triad crosses zero more often than the note alone */
    {
        int a, zc[2];
        for (a = 0; a < 2; a++) {
            int prev = 0, b2, j2;
            blank();
            set(P_T(3, TP_SOUND), 8);
            set(P_T(3, TP_ARP), a ? 1 : 0);
            set(P_T(3, TP_ECHO), 0);
            set(P_T(3, TP_VERB), 0);
            set(P_ARP_T, 3);
            en_audition(3, 57, 1);
            run(0.1);
            zc[a] = 0;
            for (b2 = 0; b2 < 80; b2++) {
                en_render(blk, 256, 2048);
                for (j2 = 0; j2 < 256; j2++) {
                    int sg = blk[2 * j2] > 0;
                    if (sg != prev)
                        zc[a]++;
                    prev = sg;
                }
            }
            en_audition(3, 57, 0);
            run(1.0);
        }
        CHECK(zc[1] > zc[0] + zc[0] / 10, "Arp Triad: %d crossings, off %d", zc[1], zc[0]);
        set(P_T(3, TP_ARP), 0);
    }

    /* THE VOICE BANK: a bank built here (a looped sine at 8 kHz, a one-shot drum, a square, a wave, a noise) is
     * taken, a broken one isn't; each voice sounds; the sample plays at its pitch (an octave up, twice the
     * crossings); the one-shot ends by itself; no bank, the ROM sounds are silent */
    {
        static uint8_t bk[EN_BANK_HDR + 4096];
        uint32_t size = sizeof bk, k;
        int zc[2], a;
#define W16(o, v) (bk[o] = (uint8_t)(v), bk[(o) + 1] = (uint8_t)((v) >> 8))
#define W32(o, v) (W16(o, (v) & 0xFFFFu), W16((o) + 2, (uint32_t)(v) >> 16))
        memset(bk, 0, sizeof bk);
        W32(0, EN_BANK_MAGIC), W16(4, 1), W16(6, 5), W32(8, size);
        for (k = 0; k < 2048; k++)               /* the sine: 64 samples a cycle, at 8000 Hz: 125 Hz at its root */
            bk[EN_BANK_HDR + k] = (uint8_t)(int8_t)(100.0 * sin(6.283185307 * (double)k / 64.0));
        for (k = 0; k < 2048; k++)               /* the drum: noise falling away */
            bk[EN_BANK_HDR + 2048 + k] = (uint8_t)(int8_t)((int)((k * 2654435761u) >> 25) - 64) * (2048 - (int)k) / 2048;
        {
            static const char *const NM[5] = {"Sine", "Drum", "Square", "Wave", "Noise"};
            for (k = 0; k < 5; k++) {
                uint32_t e = 16 + 64 * k;
                memcpy(bk + e, NM[k], strlen(NM[k]));
                bk[e + 12] = (uint8_t)(k == 0 || k == 1 ? RV_SAMPLE : k == 2 ? RV_SQUARE : k == 3 ? RV_WAVE : RV_NOISE);
                bk[e + 13] = (uint8_t)(k == 0 ? 1 : k == 1 ? 2 : 0);
                bk[e + 14] = 60;
                bk[e + 15] = (uint8_t)(k == 2 ? 2 : 0);
                W16(e + 16, 2), W16(e + 18, 300), bk[e + 20] = 200, W16(e + 22, 80);
                W32(e + 24, 8000u * 1024u);
                W32(e + 28, EN_BANK_HDR + (k == 1 ? 2048 : 0)), W32(e + 32, 2048), W32(e + 36, k == 0 ? 1024 : 0);
                for (a = 0; a < 16; a++)
                    bk[e + 40 + a] = (uint8_t)(a < 8 ? 0xFF : 0x00);   /* the wave: a square of 4 bits */
                bk[e + 56] = 128;
            }
        }
        W32(12, en_crc32(bk + 16, size - 16));
        CHECK(en_bank_check(bk, size) == 5, "the bank: %d voices", en_bank_check(bk, size));
        bk[EN_BANK_HDR + 5] ^= 1;
        CHECK(en_bank_check(bk, size) < 0, "a bank with a bad CRC passes");
        bk[EN_BANK_HDR + 5] ^= 1;
        W32(16 + 28, size - 100);                /* the sine running past the end */
        W32(12, en_crc32(bk + 16, size - 16));
        CHECK(en_bank_check(bk, size) < 0, "a sample past the bank's end passes");
        W32(16 + 28, EN_BANK_HDR);
        W32(12, en_crc32(bk + 16, size - 16));
        en_bank(bk, size);
        run(0.05);
        CHECK(en_rom_count() == 5, "the bank in use: %d voices", en_rom_count());
        CHECK(!strcmp(en_sound_name(EN_NBUILTIN + 2), "Square") && !strcmp(en_sound_name(EN_NBUILTIN + 9), "ROM 10"),
              "the ROM sounds' names: %s, %s", en_sound_name(EN_NBUILTIN + 2), en_sound_name(EN_NBUILTIN + 9));
        for (k = 0; k < 5; k++) {
            blank();
            set(P_T(3, TP_SOUND), EN_NBUILTIN + (int)k);
            set(P_T(3, TP_LEVEL), 80);
            meter_reset();
            bad = 0;
            en_audition(3, 60, 1);
            run(0.3);
            en_audition(3, 60, 0);
            CHECK(!bad && rms_db() > -45.0 && peak < 0.98, "ROM voice %u: rms %.1f dB peak %.2f", k, rms_db(), peak);
            run(2.0);
        }
        for (a = 0; a < 2; a++) {                /* the sine at its root, and an octave up */
            int prev = 0, b2, j2;
            blank();
            set(P_T(3, TP_SOUND), EN_NBUILTIN);
            set(P_T(3, TP_ECHO), 0);
            set(P_T(3, TP_VERB), 0);
            en_audition(3, a ? 72 : 60, 1);
            run(0.1);
            zc[a] = 0;
            for (b2 = 0; b2 < 86; b2++) {        /* half a second */
                en_render(blk, 256, 2048);
                for (j2 = 0; j2 < 256; j2++) {
                    int sg = blk[2 * j2] > 0;
                    if (sg != prev)
                        zc[a]++;
                    prev = sg;
                }
            }
            en_audition(3, a ? 72 : 60, 0);
            run(1.0);
        }
        CHECK(zc[0] > 110 && zc[0] < 140, "the sine at its root: %d crossings in 0.5 s, wanted 125", zc[0]);
        CHECK(zc[1] > 2 * zc[0] - 20 && zc[1] < 2 * zc[0] + 20, "an octave up: %d crossings, at the root %d", zc[1], zc[0]);
        blank();                                 /* the one-shot drum ends by itself, held or not */
        set(P_T(3, TP_SOUND), EN_NBUILTIN + 1);
        en_audition(3, 60, 1);
        run(0.6);                                /* 2048 samples at 8 kHz: a quarter of a second */
        meter_reset();
        run(0.3);
        CHECK(rms_db() < -50.0, "the one-shot still sounds: %.1f dB", rms_db());
        en_audition(3, 60, 0);
        {                                        /* a bank in: new songs take its voices now and then */
            static en_song_t g;
            int seed, roms = 0, t2;
            for (seed = 0; seed < 20; seed++) {
                en_new_song(&g, 4, 16, 50, (uint32_t)seed);
                for (t2 = 0; t2 < EN_NTRK; t2++)
                    roms += g.par[P_T(t2, TP_SOUND)] >= EN_NBUILTIN && g.par[P_T(t2, TP_SOUND)] < EN_NBUILTIN + 5;
            }
            CHECK(roms >= 10, "new songs with a bank: %d ROM voices in 20 songs", roms);
        }
        en_bank(0, 0);                           /* no bank: the ROM sounds silent */
        run(0.05);
        CHECK(en_rom_count() == 0, "no bank: %d voices", en_rom_count());
        set(P_T(3, TP_SOUND), EN_NBUILTIN);
        meter_reset();
        en_audition(3, 60, 1);
        run(0.3);
        en_audition(3, 60, 0);
        CHECK(rms_db() < -60.0, "no bank, a ROM sound plays: %.1f dB", rms_db());
        set(P_T(3, TP_SOUND), 8);
#undef W16
#undef W32
    }

    printf(fails ? "bit: %d FAILED\n" : "bit: all passed\n", fails);
    return fails != 0;
}
