/* SPDX-License-Identifier: GPL-3.0-only */
/* BIT-1's voice bank over USB: the Voices page (web/voices/) sends a bank built from a ROM the user owns, and
 * this writes it where M-VAVE keeps its user samples (0xA0000..0xDBFFF) and plays from it, read in place through
 * the flash's plain window (fm1_flash.h fl_plain_window_init). Part of the unity build, after ota.c, whose frames
 * it shares: F0 pack7(00 59 cmd len24 body ~sum) F7, each answered with its command.
 *   0x60 BEGIN  u32 size, u32 CRC (the bank's own)   the music stops, the bank's sectors are erased   [status]
 *   0x61 DATA   u32 offset, up to 512 bytes            written                                          [status, u32 offset]
 *   0x62 END                                           read back and checked whole; played from now on  [status, voices]
 * Status 0 done; 1 a size it can't take, 2 erase failed, 3 out of order or too long, 4 write failed, 5 the bank
 * doesn't check out, 9 not expected now. A bank half sent is no bank: its header goes in last (the page sends
 * offset 0 first: it is held here and written after the rest). */
#define BANK_OFF 0x000A0000u
static uint32_t bank_size, bank_crc, bank_next_off;
static uint8_t bank_on, bank_head[512] __attribute__((aligned(4)));
static uint32_t bank_head_n;

static uint32_t bank_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static const uint8_t *bank_flash(void) { return fm1_xip_ptr(BANK_OFF); }

static void bank_answer(uint32_t cmd, const uint8_t *b, uint32_t n) { ota_send_msg(cmd, b, n); }

/* at boot (and after a send): play from the bank in flash, if there is one */
static void bank_boot(void)
{
    if (flash_ok)
        en_bank(bank_flash(), EN_BANK_MAX);
}

static void bank_frame(const uint8_t *m, uint32_t d)
{
    const uint32_t cmd = m[2], len = ota_rd24(m + 3);
    const uint8_t *body = m + 6;
    uint8_t r[8] = {0};
    uint32_t off, t0;
    (void)d;
    if (cmd == 0x60 && len == 8u) {              /* BEGIN */
        bank_on = 0;
        bank_size = bank_rd32(body), bank_crc = bank_rd32(body + 4);
        if (!flash_ok || bank_size < EN_BANK_HDR || bank_size > EN_BANK_MAX) {
            r[0] = 1;
            bank_answer(cmd, r, 1);
            return;
        }
        en_stop();
        en_bank(0, 0);                           /* the render lets go of the old bank before it goes */
        for (t0 = fm1_ms; fm1_ms - t0 < 30u;)
            fm1_wdt_feed();
        ui_say("VOICES", "ERASING");
        for (off = 0; off < bank_size; off += 0x1000u) {
            if (st_erase(BANK_OFF + off)) {
                r[0] = 2;
                bank_answer(cmd, r, 1);
                return;
            }
            fm1_wdt_feed();
        }
        bank_on = 1, bank_next_off = 0, bank_head_n = 0;
        bank_answer(cmd, r, 1);
    } else if (cmd == 0x61 && len >= 5u && bank_on) {   /* DATA, in order */
        uint32_t n = len - 4u;
        off = bank_rd32(body);
        if (off != bank_next_off || n > 512u || off + n > bank_size) {
            r[0] = off + n <= bank_next_off ? 0 : 3;   /* a resend of what's already in: fine */
        } else if (off == 0) {                   /* the header: held, written last */
            uint32_t i;
            for (i = 0; i < n; i++)
                bank_head[i] = body[4 + i];
            bank_head_n = n;
            bank_next_off = n;
        } else if (st_prog(BANK_OFF + off, body + 4, n)) {
            r[0] = 4;
        } else {
            bank_next_off = off + n;
            if (!(off & 0x3FFFu)) {
                char pc[8] = {0};
                uint32_t v = off * 100u / bank_size;
                pc[0] = (char)('0' + v / 10u), pc[1] = (char)('0' + v % 10u), pc[2] = '%';
                ui_say("VOICES", pc);
            }
        }
        r[1] = (uint8_t)off, r[2] = (uint8_t)(off >> 8), r[3] = (uint8_t)(off >> 16), r[4] = (uint8_t)(off >> 24);
        bank_answer(cmd, r, 5);
    } else if (cmd == 0x62 && bank_on) {         /* END: the header in, then all of it read back */
        int v = -1;
        bank_on = 0;
        if (bank_next_off == bank_size && bank_head_n && !st_prog(BANK_OFF, bank_head, bank_head_n) &&
            bank_rd32(bank_flash() + 12) == bank_crc)
            v = en_bank_check(bank_flash(), EN_BANK_MAX);
        if (v < 0) {
            r[0] = 5;
            ui_say("VOICES", "DIDN'T CHECK OUT");
        } else {
            char t[12] = "0 READY";
            r[1] = (uint8_t)v;
            bank_boot();
            if (v >= 10)
                t[0] = '1', t[1] = (char)('0' + v - 10), t[2] = ' ', t[3] = 'R', t[4] = 'E', t[5] = 'A', t[6] = 'D', t[7] = 'Y', t[8] = 0;
            else
                t[0] = (char)('0' + v);
            ui_say("VOICES", t);
        }
        bank_answer(cmd, r, 2);
    } else {
        r[0] = 9;
        bank_answer(cmd, r, 1);
    }
}
