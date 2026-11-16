/* SPDX-License-Identifier: GPL-3.0-only */
/* HYP-1's own exports for its browser emulator's page (app/web/emu/), beside fm1-base's (base/web/emu/emu_web.c,
 * which includes this: app.h APP_WEB). From web/emu/amb_web.c, HYP-1's emulator glue before the move. */

/* the power-on seed, before web_boot (the page gives a new one each load) */
__attribute__((used, visibility("default"))) void web_seed(uint32_t s) { host_seed = s; }

static float bg_out[12];                         /* for the stream view's background: what the screen shows */
static void bg_rgb(int at, uint16_t c)
{
    bg_out[at] = (float)(c >> 11) / 31.0f;
    bg_out[at + 1] = (float)((c >> 5) & 63u) / 63.0f;
    bg_out[at + 2] = (float)(c & 31u) / 31.0f;
}
/* the stream view's background: the screen theme's primary, accent and ground */
__attribute__((used, visibility("default"))) float *web_bg(void)
{
    bg_rgb(0, th->pad);
    bg_rgb(3, th->acc);
    bg_rgb(6, th->bg);
    bg_rgb(9, th->acid);
    return bg_out;
}
