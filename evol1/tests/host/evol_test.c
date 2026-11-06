/* SPDX-License-Identifier: GPL-3.0-only */
/* EVOL-1's engine on the host: the songs as they come (valid, small enough for their flash object, every one
 * sounding and none past full scale), the scale, GROW, the tracker's timing (tempo, loop, jump, note-offs,
 * mutes), the effects, Evolve, MIDI out (notes and clock) and MIDI clock in, and playing a key into a track.
 *   tests/run_tests.sh (or: cc -DEN_HOST -Ifirmware/src/dsp tests/host/evol_test.c firmware/src/dsp/amb.c -lm) */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "amb.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t midi_n, midi_notes[16], midi_clock, midi_start, midi_stop;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1;
    midi_n++;
    if ((st & 0xF0u) == 0x90u && d2)
        midi_notes[st & 15u]++;
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
        en_song.par[P_T(t, TP_EVOLVE)] = 0;
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
    CHECK(sizeof(en_song_t) <= 3840u, "a song is %u bytes: more than its flash object", (unsigned)sizeof(en_song_t));
    for (i = 0; i < EN_NSONG; i++) {
        static en_song_t s0, s1;
        en_song_recipe(i, &s0);
        en_song_recipe(i, &s1);
        CHECK(en_song_valid(&s0), "song %d as it comes is not valid", i);
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
                en_grow(&g, 9, t, s, 70, 60, 1234u);
                h = g;
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

    /* every song plays: notes, rows at its tempo, a level like the album's, nothing past full scale */
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
    en_song.cell[0][0][1].note = 50;             /* a drop bass, held, let go at row 8 */
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

    printf(fails ? "evol: %d FAILED\n" : "evol: all passed\n", fails);
    return fails != 0;
}
