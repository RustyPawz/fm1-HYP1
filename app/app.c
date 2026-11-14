/* SPDX-License-Identifier: GPL-3.0-only */
/* HYP-1 on fm1-base: the app's unity build (compiled into fm1-base's, after the platform) and the glue between
 * fm1-base's contract (app.h) and HYP-1's parts: the project (project.c), the UI (ui.c, with runner.c) and the engine
 * (dsp/amb.c, its own -O2 unit). The glue follows AMB-1's (Charles Vestal, fm1-amb's app/app.c). */

/* the power-on seed (a piece at random, a little changed) */
#ifdef PLAT_HOST
static uint32_t host_seed;               /* the script's "seed N": the next power-on's (0: the first piece, exact) */
uint32_t plat_seed(void) { return host_seed; }

/* the last MIDI out but the clock, for the script's "expect lastout": status << 16 | d1 << 8 | d2 */
static uint32_t host_last_out;
static void host_midi_out(uint32_t pkt)
{
    if (((pkt >> 8) & 0xFFu) != 0xF8u)
        host_last_out = (pkt >> 8 & 0xFFu) << 16 | (pkt >> 16 & 0xFFu) << 8 | pkt >> 24;
    plat_midi_out(pkt);
}
#define plat_midi_out host_midi_out
#else
/* the timer as the boot left it and the MASTER pot, with what the last boot left (RAM that survives a reset; after
 * a power cut, whatever it powered up as) */
static uint32_t seed_carry __attribute__((section(".noinit")));
uint32_t plat_seed(void)
{
    uint32_t h = 2166136261u ^ seed_carry, i;
    for (i = 0; i < 24u; i++) {
        h = (h ^ plat_master()) * 16777619u;
        h = (h ^ plat_ticks24()) * 16777619u;
    }
    h ^= h >> 15;
    seed_carry = h * 2654435761u + 1u;
    return h ? h : 1u;
}
#endif

#include "project.c"
#include "ui.c"

/* the engine's MIDI out (clock, notes when MIDI out is on): to USB */
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { plat_midi_out(st >> 4 | st << 8 | d1 << 16 | d2 << 24); }

/* ---- the contract */
void app_splash(void) { lcd_fill(0, 0, 240, 240, RGB(22, 21, 27)); }   /* charcoal, until the UI's RedtailWorks card */
void app_init(void)
{
#ifdef PLAT_HOST
    ui_splash_ms = 0u;                   /* the tests start at the UI */
#endif
    en_init();
    project_start(plat_seed());          /* a piece at random, a little changed */
    project_apply();
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { en_render(lr, n, gain_q12); }
void app_panic(void)
{
    en_play(0);
    en_panic();
}
void app_ui_init(void) { ui_init(); }
void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }

#ifdef PLAT_HOST
#include "host.c"                        /* the host simulator's part: checks and commands */
#endif
