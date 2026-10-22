/* SPDX-License-Identifier: GPL-3.0-only */
/* CTY-1's secret: FIRST RESPONDER. Hold SEL and press EDIT (again, or HOME, to leave). Not the two OCT buttons:
 * held, they count down to the update mode.
 *
 * Pick a ride (white key 1 the fire truck, 2 the police car, 3 the ambulance) and it rolls out of its station on
 * its own, close in over the roofs, siren going. It drives itself: straight on where it can, round the bends,
 * turned back at a dead end. White key 1 turns left at the next junction, key 2 right (it holds the turn until
 * there's a road that way). The calls come one after another, your kind only (nobody else is sent to them now):
 * a fire to put out, a robbery to stop, a crash to treat. The yellow arrow points the way (as the crow flies, the
 * roads are yours to find) and says how far. Get there before the building burns down, the robber gets away or
 * the wreck's towed: the quicker the more points; three missed and the shift's over. The best lasts until the
 * FM-1 is switched off. The city grows and plays on throughout, and its other calls still go out as ever.
 *
 * The vehicle is one of the city's cars (life.c game_car): drawn with the rest, its lights flashing; game.c
 * steers it a tile at a time. Included by ui.c: it draws with ui.c's helpers on the whole screen, two 240x120
 * halves a frame. */

#define G_SPEED 4.2f                             /* tiles a second */
#define G_WORK_MS 2500u                          /* at the scene */

enum { GS_PICK, GS_DRIVE, GS_SCENE, GS_OVER };
enum { GC_NONE, GC_FIRE, GC_ROB, GC_CRASH };

static struct {
    uint8_t on, st, kind, strikes, call, back_view;
    int8_t want;                                 /* the turn held for the next junction: -1 left, 1 right */
    int dx, dy;                                  /* the heading */
    int target;                                  /* the call's road tile */
    int fi;                                      /* the fire's slot */
    uint16_t csi;                                /* the call's building */
    uint32_t cborn, ct0, climit;                 /* the call: who, since when, how long it has */
    uint32_t score, best, calls, last_ms, scene_ms, over_ms, next_ms, banner_until;
    char banner[24];
    char sub[24];
} gm;

static int game_on(void) { return gm.on; }
static void g_banner(const char *s, const char *sub, uint32_t ms)
{
    s_cpy(gm.banner, s);
    s_cpy(gm.sub, sub ? sub : "");
    gm.banner_until = plat_ms() + ms;
}
static int g_road(int x, int y) { return in_map(x, y) && c_road[y * MW + x] != 0; }
static car_t *g_car(void) { return game_car >= 0 ? &cars[game_car] : 0; }
static void g_pos(float *x, float *y)
{
    car_t *c = g_car();
    if (c && c->on) {
        car_pos(c, x, y, 0, 0);
    } else {
        *x = (float)C.cx + 0.5f;
        *y = (float)C.cy + 0.5f;
    }
}
static void game_pos(float *x, float *y)        /* the camera: a little ahead of the vehicle */
{
    g_pos(x, y);
    if (gm.st == GS_DRIVE) {
        *x += (float)gm.dx * 0.8f;
        *y += (float)gm.dy * 0.8f;
    }
}

/* ------------------------------------------------------------ the driving --- */
static void g_target_xy(float *x, float *y)
{
    *x = (float)(gm.target % MW) + 0.5f;
    *y = (float)(gm.target / MW) + 0.5f;
}
/* the next tile from `cur` heading dx,dy: the held turn if there's a road that way, else straight on, else the
 * bend (at a T, the side nearer the call), else back the way it came */
static int g_next(int cur)
{
    int x = cur % MW, y = cur / MW, lx = gm.dy, ly = -gm.dx, rx = -gm.dy, ry = gm.dx;
    int sl = g_road(x + lx, y + ly), sr = g_road(x + rx, y + ry), sf = g_road(x + gm.dx, y + gm.dy);
    int nx, ny;
    if (gm.want < 0 && sl) {
        nx = lx, ny = ly;
        gm.want = 0;
    } else if (gm.want > 0 && sr) {
        nx = rx, ny = ry;
        gm.want = 0;
    } else if (sf) {
        nx = gm.dx, ny = gm.dy;
    } else if (sl && sr) {
        float tx, ty;
        g_target_xy(&tx, &ty);
        if (gm.call && (tx - (float)x) * (float)lx + (ty - (float)y) * (float)ly < (tx - (float)x) * (float)rx + (ty - (float)y) * (float)ry)
            nx = rx, ny = ry;
        else
            nx = lx, ny = ly;
    } else if (sl) {
        nx = lx, ny = ly;
    } else if (sr) {
        nx = rx, ny = ry;
    } else {                                     /* a dead end: round we go */
        nx = -gm.dx, ny = -gm.dy;
        if (!g_road(x + nx, y + ny))
            nx = 0, ny = 0;
    }
    gm.dx = nx;
    gm.dy = ny;
    return (y + ny) * MW + x + nx;
}
static void g_drive(float dt)
{
    car_t *c = g_car();
    if (!c || !c->on)
        return;
    c->d += G_SPEED * dt;
    while (c->d >= 1.0f) {
        c->d -= 1.0f;
        c->path[0] = c->path[1];
        c->path[1] = c->path[2];
        c->path[2] = (uint16_t)g_next(c->path[1]);
    }
}
static int g_place(int kind)                    /* out of the station (or the town hall) onto the road */
{
    int civ = kind == VK_FIRE ? CV_FIRE : kind == VK_POLICE ? CV_POLICE : CV_HOSPITAL, k, from = -1, j, best = 0;
    float bd = 1e9f;
    car_t *c = 0;
    for (k = 0; k < C.nciv; k++) {
        const st_t *s = &c_st[c_civ[k]];
        float d = lhyp((float)s->x - (float)C.cx, (float)s->y - (float)C.cy);
        if (s->kind == civ && !(s->flags & SF_DEAD) && d < bd) {
            bd = d;
            best = c_civ[k];
        }
    }
    if (best)
        from = road_near(&c_st[best], 1);
    if (from < 0 && C.hall)
        from = road_near(&c_st[C.hall], 1);
    for (k = 0; from < 0 && k < MN; k++)        /* any road at all */
        if (c_road[k])
            from = k;
    if (from < 0)
        return 0;
    for (k = 0; k < NCAR && !c; k++)            /* a car: a free one, or one of the traffic's */
        if (!cars[k].on)
            c = &cars[k];
    for (k = 0; k < NCAR && !c; k++)
        if (!cars[k].keep && !cars[k].duty && k != solo_car)
            c = &cars[k];
    if (!c)
        return 0;
    memset(c, 0, sizeof *c);
    c->on = 1;
    c->keep = 1;
    c->kind = (uint8_t)kind;
    c->v = G_SPEED;
    c->len = 3;
    c->k = 1;
    gm.dx = 1;
    gm.dy = 0;
    for (j = 0; j < 4; j++) {                    /* head off down whichever road there is */
        static const int8_t D[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        if (g_road(from % MW + D[j][0], from / MW + D[j][1])) {
            gm.dx = D[j][0];
            gm.dy = D[j][1];
            break;
        }
    }
    c->path[0] = c->path[1] = (uint16_t)from;
    game_car = (int)(c - cars);
    game_kind = kind;
    c->path[2] = (uint16_t)g_next(from);
    return 1;
}

/* --------------------------------------------------------------- the calls --- */
static int g_fire_new(void)                     /* a building alight, a fair drive away */
{
    float px, py;
    int tries;
    g_pos(&px, &py);
    for (tries = 0; tries < 60; tries++) {
        int t = lirnd(3), si, n = C.nbl[t];
        float d;
        if (!n)
            continue;
        si = c_ref[c_bl[t][lirnd(n)]];
        if (!si || (c_st[si].flags & (SF_DEAD | SF_BURN)) || road_near(&c_st[si], 1) < 0)
            continue;
        d = lhyp((float)c_st[si].x - px, (float)c_st[si].y - py);
        if ((d < 7.0f && tries < 40) || d < 3.0f || (d > 34.0f && tries < 40))
            continue;
        ignite(si);
        return 1;
    }
    return 0;
}
static int g_pick_call(void)                    /* the oldest call of our kind going on; else a new one */
{
    uint32_t now = plat_ms();
    int f, best = -1;
    gm.call = GC_NONE;
    if (gm.kind == VK_FIRE) {
        for (f = 0; f < NFIRE; f++)
            if (fires[f].on && (best < 0 || fires[f].t0 < fires[best].t0))
                best = f;
        if (best < 0 && !g_fire_new())
            return 0;
        if (best < 0)                            /* the one just lit */
            for (f = 0; f < NFIRE; f++)
                if (fires[f].on && (best < 0 || fires[f].t0 > fires[best].t0))
                    best = f;
        if (best < 0)
            return 0;
        gm.call = GC_FIRE;
        gm.fi = best;
        gm.csi = fires[best].si;
        gm.cborn = fires[best].born;
        gm.target = road_near(&c_st[gm.csi], 1);
        gm.ct0 = fires[best].t0;
        gm.climit = 32000u;
    } else if (gm.kind == VK_POLICE) {
        if (!rob.on || rob.phase > 1) {
            if (rob.on || !start_robbery())
                return 0;
            rob.robber.k = (uint8_t)(rob.robber.len - 1);   /* already there: the hold-up's on */
            rob.robber.d = 0.0f;
            rob.phase = 1;
            rob.t1 = now;
            news(N_ROBBERY, c_st[rob.si].type, c_st[rob.si].L);
        }
        gm.call = GC_ROB;
        gm.csi = rob.si;
        gm.cborn = rob.born;
        gm.target = rob.road;
        gm.ct0 = rob.phase == 1 ? rob.t1 : now;
        gm.climit = 40000u;
    } else {
        if (!crash.on || crash.phase) {
            if (crash.on || !start_crash())
                return 0;
        }
        gm.call = GC_CRASH;
        gm.target = crash.i;
        gm.cborn = crash.t0;
        gm.ct0 = crash.t0;
        gm.climit = 50000u;
    }
    if (gm.target < 0) {
        gm.call = GC_NONE;
        return 0;
    }
    en_event(E_RISE, 0, 0);
    en_event(E_METEOR, 0, 0);
    g_banner(gm.call == GC_FIRE ? "FIRE!" : gm.call == GC_ROB ? "ROBBERY!" : "CRASH!", "FOLLOW THE ARROW", 1600u);
    return 1;
}
static int g_call_live(void)                    /* is our call still waiting for us? */
{
    switch (gm.call) {
    case GC_FIRE: return fires[gm.fi].on && fires[gm.fi].si == gm.csi && fires[gm.fi].born == gm.cborn;
    case GC_ROB: return rob.on && rob.born == gm.cborn && rob.si == gm.csi && rob.phase <= 1;
    case GC_CRASH: return crash.on && crash.t0 == gm.cborn && crash.phase == 0;
    default: return 0;
    }
}
static uint32_t g_left_ms(void)
{
    uint32_t el = plat_ms() - gm.ct0;
    return el < gm.climit ? gm.climit - el : 0u;
}
static void g_arrive(void)                      /* on the scene */
{
    uint32_t now = plat_ms(), left = g_left_ms();
    car_t *c = g_car();
    gm.st = GS_SCENE;
    gm.scene_ms = now;
    en_event(E_SIREN, 0, 0);
    if (gm.call == GC_FIRE) {                    /* the hose (ui.c draw_fires) */
        fires[gm.fi].truck = (int8_t)game_car;
        if (c) {
            c->arrived = now | 1u;
            c->duty = 1;                         /* (so it isn't left to burn meanwhile) */
        }
    } else if (gm.call == GC_ROB) {
        rob.phase = 2;
        rob.t2 = now;
        rob.cop = -1;
    } else {
        crash.phase = 1;
        crash.t2 = now;
        crash.amb = -1;
    }
    gm.calls++;
    gm.score += 100u + left / 200u;              /* 100, and 5 a second to spare */
    en_event(E_COIN, 0, 0);
    en_event(E_JINGLE, 0, 0);
    g_banner(gm.call == GC_FIRE ? "PUT OUT!" : gm.call == GC_ROB ? "CAUGHT!" : "TREATED!", 0, 1800u);
    {
        char *t = gm.sub;
        s_cpy(t, "+");
        itoa_u(100u + left / 200u, t + 1);
    }
}
static void g_scene_done(void)
{
    car_t *c = g_car();
    if (gm.call == GC_FIRE && fires[gm.fi].on && fires[gm.fi].si == gm.csi) {
        c_st[gm.csi].flags &= (uint8_t)~SF_BURN;
        fires[gm.fi].on = 0;
        fires[gm.fi].truck = -1;
        news(N_SAVED, 0, 0);
    }
    if (c) {
        c->arrived = 0;
        c->duty = 0;
    }
    gm.call = GC_NONE;
    gm.st = GS_DRIVE;
    gm.next_ms = plat_ms() + 1200u;
    en_event(E_SIREN, 0, 0);
}
static void g_miss(void)
{
    gm.strikes++;
    gm.call = GC_NONE;
    gm.next_ms = plat_ms() + 1800u;
    en_event(E_DUSK, 0, 0);
    en_event(E_SIREN, 0, 0);
    g_banner(gm.kind == VK_FIRE ? "BURNT DOWN" : gm.kind == VK_POLICE ? "GOT AWAY" : "TOWED AWAY", gm.strikes >= 3 ? 0 : "STRIKE", 1800u);
}

/* ------------------------------------------------------------ start, stop --- */
static void g_release(void)
{
    if (game_car >= 0) {
        cars[game_car].on = 0;
        cars[game_car].keep = 0;
    }
    game_car = -1;
    game_kind = -1;
}
static void g_reset(void)
{
    g_release();
    gm.st = GS_PICK;
    gm.strikes = 0;
    gm.score = 0;
    gm.calls = 0;
    gm.call = GC_NONE;
    gm.want = 0;
    gm.last_ms = plat_ms();
    g_banner("FIRST RESPONDER", "PICK YOUR RIDE", 0u);
}
static void g_go(int kind)
{
    gm.kind = (uint8_t)kind;
    if (!g_place(kind)) {
        g_banner("NO ROADS YET", 0, 1500u);
        return;
    }
    gm.st = GS_DRIVE;
    gm.next_ms = plat_ms() + 900u;
    {
        float x, y;
        g_pos(&x, &y);
        cam.x = x;
        cam.y = y;
    }
    en_event(E_SIREN, 0, 0);
    g_banner(kind == VK_FIRE ? "FIRE TRUCK" : kind == VK_POLICE ? "POLICE CAR" : "AMBULANCE", "ON SHIFT", 1200u);
}
static void game_start(void)
{
    gm.on = 1;
    gm.back_view = ui.view;
    ui.full = 0;
    g_reset();
    lcd_fill(0, 0, 240, 240, K_BG);
}
static void game_stop(void)
{
    gm.on = 0;
    en_event(E_SIREN, 0, 0);
    if (gm.call == GC_FIRE && fires[gm.fi].on && fires[gm.fi].truck == game_car)
        fires[gm.fi].truck = -1;
    g_release();
    ui.view = gm.back_view;
    ui.sig[0] = ui.sig[1] = ui.sig[2] = 0;
    ui.act_t = plat_ms();
    lcd_fill(0, 0, 240, 240, K_BG);
}

static void game_update(void)
{
    uint32_t now = plat_ms();
    float dt = (float)(now - gm.last_ms) * 0.001f, x, y, tx, ty;
    gm.last_ms = now;
    if (dt > 0.2f)
        dt = 0.2f;
    if (gm.st == GS_PICK || gm.st == GS_OVER)
        return;
    if (game_car < 0 || !cars[game_car].on) {   /* (a new city took it) */
        g_reset();
        return;
    }
    if (gm.st == GS_SCENE) {
        if (now - gm.scene_ms >= G_WORK_MS)
            g_scene_done();
        return;
    }
    WHERE(91);
    g_drive(dt);
    if (gm.call && !g_call_live()) {
        g_miss();
        if (gm.strikes >= 3) {
            gm.st = GS_OVER;
            gm.over_ms = now;
            if (gm.score > gm.best)
                gm.best = gm.score;
            en_event(E_SIREN, 0, 0);
            g_banner("SHIFT OVER", 0, 0u);
            return;
        }
    }
    if (!gm.call && (int32_t)(now - gm.next_ms) >= 0) {
        if (g_pick_call())
            en_event(E_SIREN, gm.kind == VK_FIRE ? 1 : gm.kind == VK_POLICE ? 2 : 3, 0);
        else
            gm.next_ms = now + 1000u;
    }
    if (gm.call) {
        g_pos(&x, &y);
        g_target_xy(&tx, &ty);
        if (lhyp(tx - x, ty - y) < 1.6f)
            g_arrive();
    }
}

/* ------------------------------------------------------------- the drawing --- */
static void g_arrow(float cx, float cy, float ux, float uy, float r, uint16_t c)   /* a chevron at r px, pointing u */
{
    float tipx = cx + ux * (r + 9.0f), tipy = cy + uy * (r + 9.0f), bx = cx + ux * r, by = cy + uy * r, k;
    for (k = -6.0f; k <= 6.0f; k += 0.5f)
        cv_line((int)(bx - uy * k), (int)(by + ux * k), (int)tipx, (int)tipy, c);
}
static void game_draw(int half)
{
    uint32_t now = plat_ms();
    int k;
    cv_oy = 0;
    if ((gm.st == GS_DRIVE || gm.st == GS_SCENE) && gm.call) {
        float x, y, tx, ty, dx, dy, d, cx, cy, mx, my;
        g_pos(&x, &y);
        g_target_xy(&tx, &ty);
        dx = tx - x;
        dy = ty - y;
        d = lhyp(dx, dy);
        mx = (float)VOX + tx * (float)VS;            /* the scene, when it's on screen: a beacon over it */
        my = (float)VOY + ty * (float)VS - (float)half;
        if (mx > -10.0f && mx < 250.0f && my > -40.0f && my < 140.0f) {
            float bob = fm_sinf((float)now / 180.0f) * 3.0f;
            uint16_t bc = (now / 250u) & 1u ? H(0xffe066) : H(0xff6a3a);
            for (k = 0; k < 5; k++)
                cv_line((int)mx - 4 + k * 2, (int)(my - 24.0f + bob), (int)mx, (int)(my - 12.0f + bob), bc);
            cv_rect((int)mx - 5, (int)(my - 28.0f + bob), 11, 4, bc);
        }
        if (d > 2.5f && gm.st == GS_DRIVE) {         /* the arrow round the vehicle, the way to go */
            cx = (float)VOX + x * (float)VS;
            cy = (float)VOY + y * (float)VS - (float)half;
            g_arrow(cx + 1.0f, cy + 1.0f, dx / d, dy / d, 22.0f, H(0x1a1c20));
            g_arrow(cx, cy, dx / d, dy / d, 22.0f, (now / 300u) & 1u ? H(0xffe066) : H(0xffd02a));
        }
    }
    if (half == 0) {                                 /* the top bar: calls, the score, the strikes */
        char t[24];
        rect_mix(0, 0, 240, 20, H(0x101215), 180);
        s_cpy(t, "CALLS ");
        itoa_u(gm.calls, t + 6);
        cv_text(6, 1, &FONT_S, t, H(0x96ffe6));
        itoa_u(gm.score, t);
        text_c(120, 1, &FONT_S, t, H(0xeef0f2));
        for (k = 0; k < 3; k++)
            cv_round(212 + k * 9, 6, 7, 7, 3, k < (int)gm.strikes ? H(0xff5a4a) : H(0x3a3d44));
    } else if (gm.st == GS_DRIVE || gm.st == GS_SCENE) {   /* the bottom bar: the call, its clock, the turn held */
        char t[24], u[12];
        int by = 220 - half;
        float x, y, tx, ty;
        rect_mix(0, by - 2, 240, 22, H(0x101215), 180);
        if (gm.call) {
            uint32_t left = g_left_ms() / 1000u;
            s_cpy(t, gm.call == GC_FIRE ? "FIRE " : gm.call == GC_ROB ? "ROBBERY " : "CRASH ");
            g_pos(&x, &y);
            g_target_xy(&tx, &ty);
            itoa_u((uint32_t)(lhyp(tx - x, ty - y) * 10.0f), u);
            s_cat(u, "m");
            s_cat(t, u);
            cv_text(6, by, &FONT_S, t, H(0xffe066));
            itoa_u(left, u);
            s_cat(u, "s");
            cv_text(234 - text_w(&FONT_S, u), by, &FONT_S, u, left < 8u && (now / 250u) & 1u ? H(0xff5a4a) : H(0xeef0f2));
        } else {
            cv_text(6, by, &FONT_S, "ON PATROL", H(0x96a0aa));
        }
        if (gm.want)
            text_c(150, by, &FONT_S, gm.want < 0 ? "< LEFT" : "RIGHT >", H(0x82f0d7));
    }
    if (now < gm.banner_until || gm.st == GS_PICK || gm.st == GS_OVER) {
        int by = 64 - half;
        if (by > -80 && by < 120) {
            rect_mix(10, by - 6, 220, gm.st == GS_PICK || gm.st == GS_OVER ? 112 : 54, H(0x101215), 150);
            text_c(120, by, &FONT_L, gm.banner, H(0xffe066));
            if (gm.sub[0])
                text_c(120, by + 30, &FONT_S, gm.sub, H(0xeef0f2));
            if (gm.st == GS_PICK) {
                text_c(120, by + 52, &FONT_XS, "KEY 1  FIRE TRUCK", H(0xff6a5a));
                text_c(120, by + 66, &FONT_XS, "KEY 2  POLICE CAR", H(0x6aa8ff));
                text_c(120, by + 80, &FONT_XS, "KEY 3  AMBULANCE", H(0xf4f4f2));
            } else if (gm.st == GS_OVER) {
                char t[24];
                s_cpy(t, "SCORE ");
                itoa_u(gm.score, t + 6);
                text_c(120, by + 30, &FONT_S, t, H(0xeef0f2));
                s_cpy(t, "BEST ");
                itoa_u(gm.best, t + 5);
                text_c(120, by + 50, &FONT_S, t, H(0x96ffe6));
                if (now - gm.over_ms > 1500u)
                    text_c(120, by + 78, &FONT_XS, "A KEY: AGAIN   HOME: BACK", H(0x96a0aa));
            }
        }
    }
}

static void game_input(uint32_t btn, uint32_t keys)
{
    uint32_t pressed = btn & ~ui.btn, kp = keys & ~ui.keys;
    if (((pressed >> B_EDIT & 1u) && (btn >> B_SEL & 1u)) || (pressed >> B_HOME & 1u)) {
        ui.btn = btn;
        ui.keys = keys;
        ui.btn_used |= btn;
        game_stop();
        return;
    }
    if (gm.st == GS_PICK) {                      /* white keys 1, 2, 3: the ride */
        if (kp >> WHITE_KEY[0] & 1u)
            g_go(VK_FIRE);
        else if (kp >> WHITE_KEY[1] & 1u)
            g_go(VK_POLICE);
        else if (kp >> WHITE_KEY[2] & 1u)
            g_go(VK_AMB);
    } else if (gm.st == GS_OVER) {
        if (kp && plat_ms() - gm.over_ms > 1500u)
            g_reset();
    } else {                                     /* white key 1 left, 2 right, at the next junction */
        if (kp >> WHITE_KEY[0] & 1u)
            gm.want = gm.want < 0 ? 0 : -1;
        else if (kp >> WHITE_KEY[1] & 1u)
            gm.want = gm.want > 0 ? 0 : 1;
    }
    ui.btn = btn;
    ui.keys = keys;
    (void)plat_enc(EN_SELECT);
    (void)plat_enc(EN_K1);
    (void)plat_enc(EN_ALGO);
    (void)plat_enc(EN_PRESET);
    (void)plat_enc(EN_K2);
    (void)plat_enc(EN_K3);
    (void)plat_enc(EN_K4);
    ui.act_t = plat_ms();
}
