/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's secret: UFO PILOT. Hold SEL and press EDIT (again, or HOME, to leave). Not the two OCT buttons:
 * held, they count down to the update mode.
 *
 * You fly the saucer over the city, the whole screen. SELECT turns it, KNOB 1 its speed; hold any key to
 * beam: whoever walks under the beam is taken up, a chirp a person. After five the police come looking; a
 * police car under you while you beam costs a shield. Ninety seconds a flight; the best lasts until the FM-1
 * is switched off. The city grows and plays on throughout.
 *
 * Included by ui.c: it draws with ui.c's helpers on the whole screen, two 240x120 halves a frame. */

#define G_TIME_MS 90000u

static struct {
    uint8_t on, over, shields, back_view, beaming;
    float x, y, ang, speed, lift;
    int victim;
    uint32_t score, best, last_ms, start_ms, over_ms, hurt_until, banner_until, cops_ms;
    char banner[20];
    uint32_t taken;                              /* for the tests */
} gm;

static int game_on(void) { return gm.on; }
static void g_banner(const char *s, uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < sizeof gm.banner - 1u && s[i]; i++)
        gm.banner[i] = s[i];
    gm.banner[i] = 0;
    gm.banner_until = plat_ms() + ms;
}
static void g_reset(void)
{
    gm.over = 0;
    gm.shields = 3;
    gm.score = 0;
    gm.victim = -1;
    gm.lift = 0.0f;
    gm.x = (float)C.cx + 0.5f;
    gm.y = (float)C.cy + 0.5f;
    gm.ang = 0.0f;
    gm.speed = 3.0f;
    gm.start_ms = gm.last_ms = plat_ms();
    gm.cops_ms = 0;
    g_banner("BEAM THEM UP", 2000u);
}
static void game_start(void)
{
    gm.on = 1;
    gm.back_view = ui.view;
    ui.full = 0;
    g_reset();
    en_event(E_UFO_ON, 0, 0);
    lcd_fill(0, 0, 240, 240, K_BG);
}
static void game_stop(void)
{
    gm.on = 0;
    en_event(E_UFO_OFF, 0, 0);
    ui.view = gm.back_view;
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
    ui.act_t = plat_ms();
    cam.ts = 0;
    lcd_fill(0, 0, 240, 240, K_BG);
}
static void game_pos(float *x, float *y)
{
    *x = gm.x;
    *y = gm.y;
}

static int g_cop_near(void)
{
    int k;
    for (k = 0; k < NCAR; k++) {
        float x, y;
        if (!cars[k].on || cars[k].kind != VK_POLICE)
            continue;
        car_pos(&cars[k], &x, &y, 0, 0);
        if (lhyp(x - gm.x, y - gm.y) < 1.2f)
            return 1;
    }
    return 0;
}

static void game_update(void)
{
    uint32_t now = plat_ms();
    float dt = (float)(now - gm.last_ms) * 0.001f;
    gm.last_ms = now;
    if (dt > 0.2f)
        dt = 0.2f;
    if (gm.over)
        return;
    if (now - gm.start_ms >= G_TIME_MS || !gm.shields) {
        gm.over = 1;
        gm.over_ms = now;
        gm.beaming = 0;
        if (gm.score > gm.best)
            gm.best = gm.score;
        g_banner(gm.shields ? "TIME" : "SHOT DOWN", 4000u);
        en_event(E_UFO_OFF, 0, 0);
        return;
    }
    if (!gm.beaming) {                           /* flying */
        gm.x += fm_sin_turns(gm.ang) * gm.speed * dt;
        gm.y -= fm_sin_turns(gm.ang + 0.25f) * gm.speed * dt;
        gm.x = fm_clampf(gm.x, 1.0f, (float)MW - 1.0f);
        gm.y = fm_clampf(gm.y, 1.0f, (float)MH - 1.0f);
        gm.victim = -1;
        gm.lift = 0.0f;
        return;
    }
    if (gm.victim < 0) {                         /* the beam finds someone */
        int k;
        float bd = 1.0f;
        for (k = 0; k < NPED; k++) {
            float x, y, d;
            if (!peds[k].on)
                continue;
            ped_pos(&peds[k], &x, &y);
            d = lhyp(x - gm.x, y - gm.y);
            if (d < bd) {
                bd = d;
                gm.victim = k;
            }
        }
        gm.lift = 0.0f;
    }
    if (gm.victim >= 0) {
        if (!peds[gm.victim].on) {
            gm.victim = -1;
        } else if ((gm.lift += dt * 1.6f) >= 1.0f) {
            peds[gm.victim].on = 0;
            gm.victim = -1;
            gm.score++;
            gm.taken++;
            en_event(E_CHIRP, I_STAR, 0);
            en_event(E_BEAM, 0, 0);
            if (gm.score % 5u == 0u) {           /* the police come looking */
                uint32_t q;
                dispatch(VK_POLICE, CV_POLICE, gm.x, gm.y, (int)gm.y * MW + (int)gm.x, &q);
                g_banner("POLICE!", 1500u);
            }
        }
    }
    if (now > gm.hurt_until && g_cop_near()) {
        gm.shields--;
        gm.hurt_until = now + 2000u;
        en_event(E_CRASH, 0, 0);
        g_banner("HIT!", 900u);
    }
}

static void game_draw(int half)
{
    uint32_t now = plat_ms();
    float cx = (float)VOX + gm.x * (float)VS, cy = (float)VOY + gm.y * (float)VS - (float)half, R = 13.0f;
    float hover = fm_sinf((float)now / 300.0f) * 1.5f;
    int k;
    cv_oy = 0;
    if (gm.beaming) {
        ellipse_mix(cx, cy + 6.0f, 9.0f, 5.0f, H(0x96ffe6), 70);
        rect_mix((int)cx - 6, (int)cy - 4, 12, 10, H(0x96ffe6), 40);
        if (gm.victim >= 0 && peds[gm.victim].on) {
            float x, y;
            ped_pos(&peds[gm.victim], &x, &y);
            person((int)((float)VOX + x * (float)VS), (int)((float)VOY + y * (float)VS - (float)half - gm.lift * 10.0f), peds[gm.victim].shirt,
                   peds[gm.victim].skin, VS);
        }
    }
    cy += hover - 10.0f;
    ellipse_mix(cx + 6.0f, cy + 14.0f, R * 0.9f, R * 0.4f, H(0x0a0f14), 64);
    ellipse_mix(cx, cy, R, R * 0.45f, (now < gm.hurt_until && (now / 100u) & 1u) ? H(0xff5a4a) : H(0x7f8993), 256);
    ellipse_mix(cx, cy - 1.0f, R * 0.84f, R * 0.3f, H(0xc3cbd2), 256);
    ellipse_mix(cx, cy - 2.0f, R * 0.36f, R * 0.22f, H(0x82f0d7), 242);
    for (k = 0; k < 8; k++) {
        float a = (float)k / 8.0f * FM_TWO_PI + (float)now / 400.0f;
        cv_rect((int)(cx + fm_cosf(a) * R * 0.8f), (int)(cy + fm_sinf(a) * R * 0.33f), 2, 2, (k + (int)(now / 150u)) % 2 ? H(0xffe066) : H(0xff6a8a));
    }
    {                                            /* the heading */
        float hx = cx + fm_sin_turns(gm.ang) * 20.0f, hy = cy - fm_sin_turns(gm.ang + 0.25f) * 20.0f;
        cv_rect((int)hx - 1, (int)hy - 1, 3, 3, H(0xffe066));
    }
    if (half == 0) {                             /* the score, the time, the shields */
        char t[24], u[12];
        uint32_t left = gm.over ? 0u : (G_TIME_MS - (now - gm.start_ms)) / 1000u;
        rect_mix(0, 0, 240, 20, H(0x101215), 170);
        s_cpy(t, "TAKEN ");
        itoa_u(gm.score, t + 6);
        cv_text(6, 1, &FONT_S, t, H(0x96ffe6));
        itoa_u(left, u);
        s_cat(u, " S");
        text_c(120, 1, &FONT_S, u, H(0xeef0f2));
        for (k = 0; k < 3; k++)
            cv_round(212 + k * 9, 6, 7, 7, 3, k < gm.shields ? H(0x82f0d7) : H(0x3a3d44));
    }
    if (now < gm.banner_until || gm.over) {
        int by = 52 - half;
        if (by > -40 && by < 120) {
            text_c(120, by, &FONT_L, gm.banner, H(0xffe066));
            if (gm.over) {
                char t[24];
                s_cpy(t, "BEST ");
                itoa_u(gm.best, t + 5);
                text_c(120, by + 36, &FONT_S, t, H(0xeef0f2));
                if (now - gm.over_ms > 1500u)
                    text_c(120, by + 56, &FONT_XS, "A KEY: AGAIN   HOME: BACK", H(0x96a0aa));
            }
        }
    }
}

static void game_input(uint32_t btn, uint32_t keys)
{
    uint32_t pressed = btn & ~ui.btn;
    int32_t e;
    if (((pressed >> B_EDIT & 1u) && (btn >> B_SEL & 1u)) || (pressed >> B_HOME & 1u)) {
        ui.btn = btn;
        ui.keys = keys;
        ui.btn_used |= btn;
        game_stop();
        return;
    }
    if (gm.over && keys && !ui.keys && plat_ms() - gm.over_ms > 1500u) {
        g_reset();
        en_event(E_UFO_ON, 0, 0);
    }
    gm.beaming = !gm.over && keys != 0;
    ui.btn = btn;
    ui.keys = keys;
    if ((e = plat_enc(EN_SELECT)) != 0)
        gm.ang += (float)e / 32.0f;
    if ((e = plat_enc(EN_K1)) != 0)
        gm.speed = fm_clampf(gm.speed + (float)e * 0.25f, 0.5f, 8.0f);
    (void)plat_enc(EN_ALGO);
    (void)plat_enc(EN_PRESET);
    (void)plat_enc(EN_K2);
    (void)plat_enc(EN_K3);
    (void)plat_enc(EN_K4);
    ui.act_t = plat_ms();
}
