// SPDX-License-Identifier: GPL-3.0-only
// BIT-1's ROM voices: the pure parts of the Voices page (index.html), for the page and the tests
// (tests/host/voices_test.mjs). Reads a GBA ROM's MP2K instruments; builds BIT-1's voice bank (amb.h); frames
// the bank protocol's messages (firmware/src/bank.c).

export const BANK_MAX = 0x3C000, BANK_HDR = 4096, BANK_MAGIC = 0x56544942, NROM = 16;
export const RV = { SAMPLE: 1, SQUARE: 2, WAVE: 3, NOISE: 4 };

let CRC;
export function crc32(b, from = 0, to = b.length) {
  if (!CRC) { CRC = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; CRC[n] = c >>> 0; } }
  let c = 0xFFFFFFFF;
  for (let i = from; i < to; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// MP2K keeps each song's instruments in a voicegroup: 12-byte entries. A DirectSound entry (type 0x00, 0x08 a
// fixed-pitch one, 0x10/0x18 played backwards) points at a sample: u32 flags (0x40000000 loops), u32 rate (Hz x
// 1024), u32 loop start, u32 length, then 8-bit signed data. Game Boy-style ones: 1/9 square with sweep, 2/10
// square, 3/11 wave (a pointer to 16 bytes), 4/12 noise; their envelopes in small steps. 0x40 a keysplit, 0x80 a
// drum kit (pointers to other voicegroups). We look at every 4-byte step for entries, and keep runs of sixteen or
// more in a row: a sample header that checks out, in a long run, is not chance.
export function readRom(rom) {
  const n = rom.length, u32 = (o) => (rom[o] | rom[o + 1] << 8 | rom[o + 2] << 16 | rom[o + 3] << 24) >>> 0;
  const ptr = (v) => { const o = v - 0x08000000; return v >= 0x08000000 && o < n ? o : -1; };
  const sampleAt = (o) => {
    if (o < 0 || (o & 3) || o + 16 > n) return null;
    const flags = u32(o); if (flags !== 0 && flags !== 0x40000000) return null;
    const rate = u32(o + 4), loop = u32(o + 8), len = u32(o + 12);
    if (rate < 1000 * 1024 || rate > 96000 * 1024 || len < 32 || len > 0x400000 || o + 16 + len > n || loop >= len) return null;
    return { at: o + 16, loops: flags !== 0, rate, loop, len };
  };
  const env = (o) => [rom[o + 8], rom[o + 9], rom[o + 10], rom[o + 11]];
  const psgEnv = (o) => rom[o + 8] <= 7 && rom[o + 9] <= 7 && rom[o + 10] <= 15 && rom[o + 11] <= 7;
  const entry = (o) => {
    const t = rom[o];
    if ((t & ~0x18) === 0) {
      const s = sampleAt(ptr(u32(o + 4)));
      return s ? { kind: "ds", fixed: !!(t & 8), s, env: env(o) } : null;
    }
    const b = t & 0xF7;
    if ((b === 1 || b === 2) && u32(o + 4) <= 3 && psgEnv(o) && (b === 2 || rom[o + 3] <= 0x7F))
      return { kind: "sq", duty: u32(o + 4), sweep: b === 1 ? rom[o + 3] : 0, env: env(o) };
    if (b === 3) { const w = ptr(u32(o + 4)); return w >= 0 && w + 16 <= n && psgEnv(o) ? { kind: "wave", wave: rom.slice(w, w + 16), env: env(o) } : null; }
    if (b === 4 && u32(o + 4) <= 1 && psgEnv(o)) return { kind: "noise", short: u32(o + 4), env: env(o) };
    if (t === 0x40 && ptr(u32(o + 4)) >= 0 && ptr(u32(o + 8)) >= 0) return { kind: "split" };
    if (t === 0x80 && ptr(u32(o + 4)) >= 0 && u32(o + 8) === 0) return { kind: "kit" };
    return null;
  };
  const found = new Map();
  const keep = (e) => {
    const key = e.kind === "ds" ? "ds" + e.s.at : e.kind === "sq" ? "sq" + e.duty + "," + e.env : e.kind === "wave" ? "wv" + [...e.wave] + "," + e.env : e.kind + e.short + "," + e.env;
    if (!found.has(key)) found.set(key, e);
  };
  for (let start = 0; start < 12; start += 4) {     // the three lanes of 12-byte entries on 4-byte steps
    let run = [];
    const flush = () => {
      if (run.length >= 16 && run.some((e) => e.kind === "ds" || e.kind === "sq" || e.kind === "wave"))
        run.forEach((e) => { if (e.kind !== "split" && e.kind !== "kit") keep(e); });
      run = [];
    };
    for (let o = start; o + 12 <= n; o += 12) {
      const e = entry(o);
      if (e) run.push(e); else flush();
    }
    flush();
  }
  return [...found.values()];
}

// envelopes in BIT-1's terms (ms, and sustain 0..255): a sample's (0..255 at 60 frames a second: attack adds,
// decay and release multiply by x/256); a Game Boy voice's (small steps)
export function envOf(e) {
  const [a, d, s, r] = e.env, frame = 1000 / 60;
  if (e.kind === "ds") {
    const fall = (x) => (x <= 0 ? frame : x >= 255 ? 8000 : Math.min(8000, Math.log(0.001) / Math.log(x / 256) * frame));
    return { att: a >= 255 || a === 0 ? 1 : Math.round(Math.min(4000, 255 / a * frame)), dec: Math.round(fall(d)), sus: s, rel: Math.round(fall(r)) };
  }
  return { att: Math.max(1, a * 40), dec: Math.max(5, d * 120), sus: Math.round(s * 17), rel: Math.max(5, r * 80) };
}

// a sample's data as floats, -1..1 (halved: every two averaged)
export function pcmOf(rom, e, half) {
  const s = e.s, d = new Float32Array(half ? s.len >> 1 : s.len);
  for (let i = 0; i < d.length; i++) {
    const at = s.at + (half ? 2 * i : i), v = rom[at] << 24 >> 24;
    d[i] = half ? ((v + (rom[at + 1] << 24 >> 24)) / 2) / 128 : v / 128;
  }
  return d;
}
export const roomOf = (it) => (it.e.kind === "ds" ? (it.half ? it.e.s.len >> 1 : it.e.s.len) : 0);

// the bank (amb.h): picks are { e, name, half }
export function buildBank(rom, picks) {
  const data = [], v = [];
  let off = BANK_HDR;
  for (const it of picks.slice(0, NROM)) {
    const e = it.e, x = { name: it.name || "", env: envOf(e), root: 60, flags: 0, duty: 0, rate: 0, off: 0, len: 0, loop: 0, wave: null };
    if (e.kind === "ds") {
      const d = pcmOf(rom, e, it.half), b = new Int8Array(d.length);
      for (let i = 0; i < d.length; i++) b[i] = Math.max(-128, Math.min(127, Math.round(d[i] * 128)));
      Object.assign(x, { type: RV.SAMPLE, flags: (e.s.loops ? 1 : 0) | (e.fixed ? 2 : 0), rate: it.half ? e.s.rate >>> 1 : e.s.rate,
                         off, len: b.length, loop: e.s.loops ? Math.min(b.length - 1, it.half ? e.s.loop >>> 1 : e.s.loop) : 0 });
      data.push(b);
      off += b.length;
    } else if (e.kind === "sq") Object.assign(x, { type: RV.SQUARE, duty: e.duty });
    else if (e.kind === "wave") Object.assign(x, { type: RV.WAVE, wave: e.wave });
    else Object.assign(x, { type: RV.NOISE, duty: e.short });
    v.push(x);
  }
  const size = off, bank = new Uint8Array(size), dv = new DataView(bank.buffer);
  dv.setUint32(0, BANK_MAGIC, true); dv.setUint16(4, 1, true); dv.setUint16(6, v.length, true); dv.setUint32(8, size, true);
  v.forEach((x, k) => {
    const o = 16 + 64 * k;
    for (let i = 0; i < Math.min(12, x.name.length); i++) bank[o + i] = x.name.charCodeAt(i) & 0x7F;
    bank[o + 12] = x.type; bank[o + 13] = x.flags; bank[o + 14] = x.root; bank[o + 15] = x.duty;
    dv.setUint16(o + 16, Math.min(65535, x.env.att), true); dv.setUint16(o + 18, Math.min(65535, x.env.dec), true);
    bank[o + 20] = Math.min(255, x.env.sus); dv.setUint16(o + 22, Math.min(65535, x.env.rel), true);
    dv.setUint32(o + 24, x.rate, true); dv.setUint32(o + 28, x.off, true); dv.setUint32(o + 32, x.len, true); dv.setUint32(o + 36, x.loop, true);
    if (x.wave) bank.set(x.wave, o + 40);
    bank[o + 56] = 128;
  });
  let at = BANK_HDR;
  for (const b of data) { bank.set(new Uint8Array(b.buffer), at); at += b.length; }
  dv.setUint32(12, crc32(bank, 16, size), true);
  return bank;
}
export function bankOk(b) {
  if (b.length < BANK_HDR) return -1;
  const dv = new DataView(b.buffer, b.byteOffset), size = dv.getUint32(8, true);
  if (dv.getUint32(0, true) !== BANK_MAGIC || size > b.length || size > BANK_MAX || crc32(b, 16, size) !== dv.getUint32(12, true)) return -1;
  return dv.getUint16(6, true);
}

// the bank protocol: as M-UPGRADE's frames, F0 pack7(00 59 cmd len24 body ~sum(body)) F7
// the 7-bit packing is a bit stream, least significant first (as fm1ota.js and ota.c's ota_unpack7)
export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}
export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}
export function frame(cmd, body) {
  let sum = 0; body.forEach((b) => { sum += b; });
  return [0xF0, ...pack7([0x00, 0x59, cmd, body.length & 255, (body.length >> 8) & 255, (body.length >> 16) & 255, ...body, ~sum & 255]), 0xF7];
}
export function unframe(msg) {            // a reply: { cmd, body } or null
  if (msg[0] !== 0xF0 || msg[msg.length - 1] !== 0xF7) return null;
  const u = unpack7(msg.slice(1, -1));
  if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59) return null;
  const len = u[3] | u[4] << 8 | u[5] << 16;
  if (len + 7 !== u.length) return null;
  let sum = 0; for (let i = 6; i < 6 + len; i++) sum += u[i];
  if ((~sum & 255) !== u[6 + len]) return null;
  return { cmd: u[2], body: u.slice(6, 6 + len) };
}
export const le32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
export const BANK_CMD = { BEGIN: 0x60, DATA: 0x61, END: 0x62 };

// sending a bank to the FM-1 (bank.c), shared by the Voices page and the installer. getAccess() gives a
// MIDIAccess (asked again on each try: after an install the FM-1 restarts and its ports come back new);
// say(text) tells the user. waitMs: how long to keep trying to reach BIT-1 (after an install it is still
// starting). Resolves to the voices on the FM-1; throws with a message for the user.
export async function sendBank(getAccess, bank, say, { waitMs = 0, tries = 3 } = {}) {
  const t0 = Date.now(), pause = (ms) => new Promise((r) => setTimeout(r, ms));
  const pick = (m) => [...m.values()].find((p) => /fm-?1|m-?vave/i.test(p.name)) || [...m.values()][0];
  let out = null, inp = null, waiting = null;
  const ask = (cmd, body, ms) => new Promise((res, rej) => {
    const me = { cmd, res };                     // this question's own: an earlier one's timer can't end it
    waiting = me;
    out.send(frame(cmd, body));
    setTimeout(() => { if (waiting === me) { waiting = null; rej(new Error("no answer")); } }, ms);
  });
  const crc = new DataView(bank.buffer, bank.byteOffset).getUint32(12, true);
  let r = null;
  for (;;) {                                     // reach BIT-1: BEGIN answered
    const access = await getAccess();
    out = pick(access.outputs);
    inp = pick(access.inputs);
    if (out && inp) {
      inp.onmidimessage = (ev) => {
        const m = unframe([...ev.data]);
        if (m && waiting && m.cmd === waiting.cmd) { const w = waiting; waiting = null; w.res(m.body); }
      };
      say("Erasing the FM-1's bank…");
      try { r = await ask(BANK_CMD.BEGIN, [...le32(bank.length), ...le32(crc)], 20000); break; }
      catch { /* not BIT-1 yet (or not at all) */ }
    }
    if (Date.now() - t0 >= waitMs) {
      if (inp) inp.onmidimessage = null;
      throw new Error(out ? "the FM-1 didn't answer: is BIT-1 on it?" : "no FM-1 found: connect it by USB, with BIT-1 on it");
    }
    say("Waiting for BIT-1 to start…");
    await pause(1500);
  }
  try {
    if (r[0] !== 0) throw new Error(`the FM-1 refused the bank (${r[0]})`);
    for (let at = 0; at < bank.length; at += 512) {
      const body = [...le32(at), ...bank.subarray(at, Math.min(bank.length, at + 512))];
      for (let k = 0; ; k++) {
        try { r = await ask(BANK_CMD.DATA, body, 2000); if (r[0] === 0) break; throw new Error(`a write failed (${r[0]})`); }
        catch (err) { if (k >= tries - 1) throw err; }
      }
      say(`Writing the ROM voices… ${Math.min(100, Math.round((at + 512) / bank.length * 100))}%`);
    }
    r = await ask(BANK_CMD.END, [], 10000);
    if (r[0] !== 0) throw new Error(`the bank didn't check out on the FM-1 (${r[0]})`);
    return r[1];
  } finally {
    inp.onmidimessage = null;
  }
}
