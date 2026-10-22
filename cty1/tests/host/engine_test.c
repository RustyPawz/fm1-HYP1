/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's engine on its own: it plays, the output stays finite and in range, the city's events and cues
 * all sound, the lead snaps to the scale, MIDI notes go out, and a reset is quiet.
 *   cc -DEN_HOST -Ifirmware/src/dsp tests/host/engine_test.c firmware/src/dsp/amb.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "amb.h"

static unsigned midi_notes;
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1;
    if ((st & 0xF0u) == 0x90u && d2)
        midi_notes++;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static float run(float secs, float *peak)
{
    static int32_t buf[512];
    double ms = 0.0;
    long n = 0, k, blocks = (long)(secs * EN_SR / 256.0f);
    *peak = 0.0f;
    for (k = 0; k < blocks; k++) {
        int i;
        en_render(buf, 256, 2048);
        for (i = 0; i < 512; i++) {
            float x = (float)buf[i] / 8388608.0f;
            if (!(x == x)) {
                printf("FAIL NaN at block %ld\n", k);
                fails++;
                return -999.0f;
            }
            if (fabsf(x) > *peak)
                *peak = fabsf(x);
            ms += (double)x * (double)x;
        }
        n += 512;
    }
    return (float)(10.0 * log10(ms / (double)n + 1e-20));
}

int main(void)
{
    float pk, db;
    int p, e;
    en_init();
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, en_param_info(p)->def);
    en_city.day = 1.0f;
    en_city.bright = 0.6f;
    en_city.key = 3;
    en_play(1);
    db = run(8.0f, &pk);
    printf("  bed: %.1f dB rms, peak %.2f, %u notes, %u hits\n", (double)db, (double)pk, en_notes, en_hits);
    CHECK(db > -40.0f && db < -6.0f, "the bed's level %.1f dB", db);
    CHECK(pk < 0.99f, "the bed clips (%.2f)", pk);
    CHECK(en_notes > 8, "the bed plays notes (%u)", en_notes);
    
    for (e = E_RES; e < E_NEV; e++) {             /* every event and cue */
        unsigned n0 = en_events;
        en_event(e, 1, 0);
        db = run(0.4f, &pk);
        CHECK(pk < 0.999f && db > -80.0f, "event %d: %.1f dB, peak %.2f", e, db, pk);
        (void)n0;
    }
    {   /* every biome: its scales, rhythm and bass, a few bars each, at every stage */
        int b, st;
        for (b = 0; b < EN_NBIOME; b++)
            for (st = 0; st < 4; st++) {
                en_city.biome = (uint8_t)b;
                en_city.stage = (uint8_t)st;
                en_reset();
                db = run(2.5f, &pk);
                CHECK(db > -45.0f && db < -6.0f && pk < 0.99f, "%s at stage %d: %.1f dB, peak %.2f", EN_BIOME_NAME[b], st, (double)db, (double)pk);
            }
        en_city.biome = 0;
        en_city.wind = 1.0f; en_city.rumble = 1.0f; en_city.alert = 1; en_city.festival = 1; en_city.blackout = 1;
        db = run(4.0f, &pk);
        printf("  wind, rumble, siren, festival, blackout: %.1f dB, peak %.2f\n", (double)db, (double)pk);
        CHECK(pk < 0.999f, "the regional sounds stay under full scale (%.2f)", (double)pk);
    }
    en_city.storm = 1.0f; en_city.rain = 1.0f; en_city.fog = 1.0f; en_city.siren_fire = 1; en_city.siren_police = 1;
    en_city.alarm = 1; en_city.birds = 6; en_city.hwy = 8; en_city.stage = 3; en_city.ufo = 1; en_city.grit = 0.5f;
    en_set(P_FLAVOR, 2);
    en_set(P_CRUSH, 150);
    en_set(P_ECHO, 150);
    db = run(10.0f, &pk);
    printf("  everything at once: %.1f dB, peak %.2f\n", db, pk);
    CHECK(db > -40.0f && pk < 0.999f, "everything at once: %.1f dB, peak %.2f", db, pk);
    en_lead(3, 4);
    db = run(0.5f, &pk);
    en_lead_off(3);
    CHECK(db > -60.0f, "the lead sounds");
    CHECK(en_snap(61, 0xAB5) == 60 || en_snap(61, 0xAB5) == 62, "snap");
    CHECK(en_snap(60, 0xAB5) == 60, "snap in scale");
    CHECK(midi_notes > 10, "MIDI out notes (%u)", midi_notes);
    memset((void *)&en_city, 0, sizeof en_city);
    en_city.day = 1.0f;
    en_event(E_UFO_OFF, 0, 0);
    en_event(E_SIREN, 0, 0);
    en_play(0);
    en_panic();
    run(6.0f, &pk);
    db = run(1.0f, &pk);
    printf("  resting: %.1f dB\n", db);
    CHECK(db < -50.0f, "resting is quiet (%.1f dB)", db);
    printf(fails ? "engine: %d FAILED\n" : "engine: all ok\n", fails);
    return fails ? 1 : 0;
}
