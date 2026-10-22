/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's city alone: every biome, two seeds each: a seed grows the same town every time; it reaches each stage;
 * the zones keep the mix; nothing is built on water or rock; the names.
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
    static const char *const SEEDS[] = {"redtail", "maple"};
    int si, k, verbose = argc > 1, bio;
    for (bio = 0; bio < NBIOME; bio++)
    for (si = 0; si < 2; si++) {
        uint32_t seed = fnv(SEEDS[si]) + (uint32_t)bio * 7919u, sig = 0;
        int bad = 0;
        int at[4] = {0, 0, 0, 0}, cnt[5] = {0}, farms = 0, big = 0, civ[CV_N] = {0};
        city_new(seed, bio);
        CHECK(C.hall && c_st[C.hall].kind == CV_TOWNHALL && c_st[C.hall].size == 4, "%s: a town hall at the founding", C.name);
        for (k = 0; k < 2400; k++) {
            if (k == 40) {
                int f = 0, p = 0, h = 0, j;
                for (j = 0; j < C.nciv; j++) {
                    f += c_st[c_civ[j]].kind == CV_FIRE;
                    p += c_st[c_civ[j]].kind == CV_POLICE;
                    h += c_st[c_civ[j]].kind == CV_HOSPITAL;
                }
                CHECK(f && p && h, "%s: a fire station, police and a hospital in its first weeks", C.name);
            }
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
        for (k = 0; k < MN; k++) {
            sig = sig * 31u + c_bt[k] * 7u + c_road[k];
            if ((c_bt[k] && BLOCKED(k)) || (c_road[k] == 1 && BLOCKED(k)) || (c_road[k] && TERR(k) == T_ROCK))
                bad++;
        }
        CHECK(!bad, "%s: nothing on water or rock (%d)", C.name, bad);
        CHECK(C.unlocked == 3 && civ[CV_UNLOCK3] == 1, "%s: its three specials (%d), the last: %s", C.name, C.unlocked, UNLOCK_NAME[bio][2]);
        CHECK(c_st[C.hall].L == 4, "%s: the town hall grown with it (%d)", C.name, c_st[C.hall].L);
        printf("%-8s %s: %s  coast %d river %d  stages %d %d %d  pop %d  homes %d shops %d ind %d farms %d civic %d big %d roads %d\n",
               BIOME_SHORT[bio], SEEDS[si], C.name, C.has_coast, C.has_river, at[1], at[2], at[3], (int)C.pop, cnt[1], cnt[2], cnt[3], farms, cnt[4], big, (int)C.roads);
        if (verbose) {
            for (k = 0; k < CV_N; k++) printf("%s %d, ", CIVIC_NAME[k], civ[k]);
            printf("\n");
        }
        CHECK(at[1] > 250 && at[1] < 600, "%s: suburb at week %d", C.name, at[1]);
        CHECK(at[2] > 600 && at[2] < 1400, "%s: city at week %d", C.name, at[2]);
        CHECK(at[3] > (bio == B_ALPINE ? 900 : 1200) && at[3] < 2400, "%s: huge city at week %d", C.name, at[3]);
        CHECK(cnt[1] > (bio == B_ALPINE ? 150 : 300) && cnt[2] > (bio == B_ALPINE ? 40 : 60) && cnt[3] > (bio == B_ALPINE ? 12 : 20), "%s: the mix (homes %d, shops %d, industry %d)", C.name, cnt[1], cnt[2], cnt[3]);
        CHECK(civ[CV_POLICE] >= 1 && civ[CV_HOSPITAL] >= 1 && civ[CV_FIRE] >= 1 && civ[CV_POLICE] + civ[CV_HOSPITAL] + civ[CV_FIRE] >= (bio == B_ALPINE ? 4 : 6), "%s: police, fire, hospitals", C.name);
        {   /* again: the same town */
            uint32_t sig2 = 0;
            city_new(seed, bio);
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
        int seen[NBIOME] = {0};
        for (k = 1; k < 2000; k++)
            seen[city_biome_of((uint32_t)k * 2654435761u)]++;
        for (k = 0; k < NBIOME; k++)
            CHECK(seen[k] > 200, "the seeds pick %s (%d of 2000)", BIOME_NAME[k], seen[k]);
        city_name(fnv("redtail"), -1, n);
        CHECK(!strcmp(n, "Brighton"), "redtail is Brighton (%s)", n);
    }
    printf(fails ? "CITY TESTS FAILED\n" : "city: all passed\n");
    return fails != 0;
}
