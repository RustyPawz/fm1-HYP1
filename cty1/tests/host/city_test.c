/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's city alone: a seed grows the same town every time; it reaches each stage; the names; the zones
 * keep the mix; the rubble clears.
 *   cc -O2 -ffp-contract=off -Ifirmware/src/dsp -o city_test tests/host/city_test.c && ./city_test */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../firmware/src/app/city.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } else { printf("  ok "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t fnv(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

static int run(int argc);
int main(int argc, char **argv)
{
    (void)argv;
    return run(argc);
}
static int run(int argc)
{
    static const char *const SEEDS[] = {"redtail", "maple", "42", "test", "zz"};
    int si, k, verbose = argc > 1;
    for (si = 0; si < 5; si++) {
        uint32_t seed = fnv(SEEDS[si]), sig = 0;
        int at[4] = {0, 0, 0, 0}, cnt[5] = {0}, farms = 0, big = 0, civ[CV_N] = {0};
        city_new(seed);
        for (k = 0; k < 2400; k++) {
            city_step();
            if (k % 64 == 0)
                city_sweep();
            if (!at[C.stage])
                at[C.stage] = (int)C.tick;
        }
        for (k = 1; k < NST; k++) {
            const st_t *s = &c_st[k];
            if (!s->type || (s->flags & SF_DEAD))
                continue;
            if (s->flags & SF_FARM) farms++;
            else cnt[s->type]++;
            if (s->size > 1 && s->type != BT_CIV) big++;
            if (s->type == BT_CIV) civ[s->kind]++;
        }
        for (k = 0; k < MN; k++)
            sig = sig * 31u + c_bt[k] * 7u + c_road[k];
        printf("%s: %s  coast %d river %d  stages %d %d %d  pop %d  homes %d shops %d ind %d farms %d civic %d big %d roads %d\n",
               SEEDS[si], C.name, C.has_coast, C.has_river, at[1], at[2], at[3], (int)C.pop, cnt[1], cnt[2], cnt[3], farms, cnt[4], big, (int)C.roads);
        if (verbose) {
            for (k = 0; k < CV_N; k++) printf("%s %d, ", CIVIC_NAME[k], civ[k]);
            printf("\n");
        }
        CHECK(at[1] > 250 && at[1] < 500, "%s: suburb at week %d", C.name, at[1]);
        CHECK(at[2] > 600 && at[2] < 1100, "%s: city at week %d", C.name, at[2]);
        CHECK(at[3] > 1200 && at[3] < 2000, "%s: huge city at week %d", C.name, at[3]);
        CHECK(cnt[1] > 400 && cnt[2] > 80 && cnt[3] > 30, "%s: the mix (homes %d, shops %d, industry %d)", C.name, cnt[1], cnt[2], cnt[3]);
        CHECK(civ[CV_POLICE] >= 2 && civ[CV_HOSPITAL] >= 2 && civ[CV_FIRE] >= 2, "%s: police, fire, hospitals", C.name);
        {   /* again: the same town */
            uint32_t sig2 = 0;
            city_new(seed);
            for (k = 0; k < 2400; k++) {
                city_step();
                if (k % 64 == 0)
                    city_sweep();
            }
            for (k = 0; k < MN; k++)
                sig2 = sig2 * 31u + c_bt[k] * 7u + c_road[k];
            CHECK(sig == sig2, "%s: the seed grows the same town", C.name);
        }
    }
    {
        char n[16];
        city_name(fnv("redtail"), n);
        CHECK(!strcmp(n, "Brighton"), "redtail is Brighton (%s)", n);
        city_name(fnv("maple"), n);
        CHECK(!strcmp(n, "Faircrest"), "maple is Faircrest (%s)", n);
    }
    printf(fails ? "CITY TESTS FAILED\n" : "city: all passed\n");
    return fails != 0;
}
