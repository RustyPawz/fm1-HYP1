// SPDX-License-Identifier: GPL-3.0-only
// The ROM voices end to end, without a game: a made-up ROM with an MP2K-style voicegroup (samples, squares, a
// wave, a noise, a keysplit) among random bytes is read by the Voices page's code (web/voices/voices.js), the
// bank it builds checks out, and the engine (the emulator's WebAssembly) takes it and plays each voice. Then the
// bank protocol's frames go out and come back.
//   node tests/host/voices_test.mjs build/emu/amb.wasm
import fs from "fs";
import { readRom, buildBank, bankOk, frame, unframe, le32, BANK_HDR, crc32 } from "../../web/voices/voices.js";

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log("FAIL " + what); fails++; } };

// the ROM: 1 MB of noise (a fixed seed), and in it a voicegroup of 20 entries and what they point at
const n = 1 << 20, rom = new Uint8Array(n);
let seed = 12345;
for (let i = 0; i < n; i++) { seed = (seed * 1103515245 + 12345) >>> 0; rom[i] = seed >>> 24; }
const w32 = (o, v) => { rom[o] = v & 255; rom[o + 1] = (v >>> 8) & 255; rom[o + 2] = (v >>> 16) & 255; rom[o + 3] = (v >>> 24) & 255; };
const P = (o) => 0x08000000 + o;
const smp = [];
let at = 0x40000;
for (let k = 0; k < 3; k++) {                          // three samples: a looped sine, a long one, a drum
  const len = k === 1 ? 30000 : 2000, loops = k === 0;
  w32(at, loops ? 0x40000000 : 0); w32(at + 4, 13379 * 1024); w32(at + 8, loops ? 1000 : 0); w32(at + 12, len);
  for (let i = 0; i < len; i++) rom[at + 16 + i] = (Math.round(90 * Math.sin(2 * Math.PI * i / (k === 2 ? 7 : 50))) * (k === 2 ? (len - i) / len : 1)) & 255;
  smp.push(at);
  at += 16 + len + ((4 - (len % 4)) % 4);
}
const wave = at; for (let i = 0; i < 16; i++) rom[wave + i] = i < 8 ? 0xFF : 0x00; at += 16;
const vg = 0x10000;
for (let e = 0; e < 20; e++) {                         // the voicegroup
  const o = vg + 12 * e;
  rom.fill(0, o, o + 12);
  const kind = e % 6;
  if (kind <= 2) { rom[o] = kind === 2 ? 0x08 : 0x00; rom[o + 1] = 60; w32(o + 4, P(smp[kind])); rom.set([255, 0, 255, 165], o + 8); }
  else if (kind === 3) { rom[o] = 0x01; w32(o + 4, 2); rom.set([0, 2, 10, 3], o + 8); }
  else if (kind === 4) { rom[o] = 0x03; w32(o + 4, P(wave)); rom.set([0, 3, 12, 2], o + 8); }
  else { rom[o] = 0x04; w32(o + 4, e > 10 ? 1 : 0); rom.set([0, 1, 0, 1], o + 8); }
}
rom.fill(0, vg + 12 * 20, vg + 12 * 21);
rom[vg + 12 * 20] = 0x40; w32(vg + 12 * 20 + 4, P(vg)); w32(vg + 12 * 20 + 8, P(vg + 600));   // a keysplit: kept out

const found = readRom(rom);
const ds = found.filter((e) => e.kind === "ds"), sq = found.filter((e) => e.kind === "sq"), wv = found.filter((e) => e.kind === "wave"), nz = found.filter((e) => e.kind === "noise");
console.log(`  found: ${ds.length} samples, ${sq.length} squares, ${wv.length} waves, ${nz.length} noises`);
check(ds.length === 3, `three samples, not ${ds.length}`);
check(sq.length === 1 && wv.length === 1 && nz.length === 2, "one square, one wave, two noises");
check(ds.some((e) => e.fixed) && ds.some((e) => e.s.loops), "a drum and a loop among the samples");
check(found.length < 12, `nothing found in the noise: ${found.length} in all`);

// the bank: every one of them, the long sample halved
const picks = found.map((e, i) => ({ e, name: "V" + i, half: e.kind === "ds" && e.s.len > 24000 }));
const bank = buildBank(rom, picks);
check(bankOk(bank) === found.length, `the bank checks out: ${bankOk(bank)}`);
check(bank.length === BANK_HDR + 2000 + 15000 + 2000, `the bank's size: ${bank.length}`);
const bad = bank.slice(); bad[BANK_HDR + 3] ^= 1;
check(bankOk(bad) < 0, "a changed bank still checks out");

// the engine takes it and plays each voice (the emulator's WebAssembly)
const buf = fs.readFileSync(process.argv[2] || "build/emu/amb.wasm");
const stub = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(buf, { wasi_snapshot_preview1: stub, env: stub });
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.web_master(2800);
ex.web_boot();
for (let b = 0; b < 300; b++) ex.web_render(128);
new Uint8Array(ex.memory.buffer, ex.web_bank(), bank.length).set(bank);
check(ex.web_bank_load(bank.length) === found.length, "the engine refuses the bank");
for (let b = 0; b < 4; b++) ex.web_render(128);
check(ex.web_rom_count() === found.length, `the engine's ROM voices: ${ex.web_rom_count()}`);
const level = (sound, note) => {
  let s = 0, k = 0, nan = 0;
  ex.web_test_key(3, sound, note, 1);
  for (let b = 0; b < 120; b++) {
    ex.web_render(128);
    const a = new Float32Array(ex.memory.buffer, ex.web_out(0), 128);
    for (const x of a) { if (Number.isNaN(x)) nan++; s += x * x; k++; }
  }
  ex.web_test_key(3, sound, note, 0);
  for (let b = 0; b < 400; b++) ex.web_render(128);
  return nan ? NaN : 10 * Math.log10(s / k + 1e-20);
};
found.forEach((e, i) => {
  const db = level(24 + i, 60);
  check(db > -45, `ROM voice ${i + 1} (${e.kind}): ${db.toFixed(1)} dB`);
});
new Uint8Array(ex.memory.buffer, ex.web_bank(), 16).fill(0);
check(ex.web_bank_load(bank.length) < 0, "a broken bank is taken");

// the protocol's frames: out and back, the checksum checked
const f = frame(0x61, [...le32(4096), ...bank.subarray(4096, 4096 + 512)]);
check(f.length <= 640 && f.every((b, i) => i === 0 || i === f.length - 1 || b < 0x80), `a DATA frame: ${f.length} bytes, 7-bit`);
const r = unframe(f);
check(r && r.cmd === 0x61 && r.body.length === 516 && r.body[4] === bank[4096], "a frame comes back as it went");
const g = f.slice(); g[20] ^= 1;
check(!unframe(g) || unframe(g).body[10] !== r.body[10], "a changed frame comes back unchanged");
check(crc32(new TextEncoder().encode("123456789")) === 0xCBF43926, "CRC-32 of 123456789");

// what the page would send the FM-1, for the device's side (tests/bank_test.c): the bank, then its frames
{
  const frames = [frame(0x60, [...le32(bank.length), ...le32(new DataView(bank.buffer).getUint32(12, true))])];
  for (let o = 0; o < bank.length; o += 512) frames.push(frame(0x61, [...le32(o), ...bank.subarray(o, Math.min(bank.length, o + 512))]));
  frames.push(frame(0x62, []));
  fs.mkdirSync("build/host", { recursive: true });
  fs.writeFileSync("build/host/voices.bitv", bank);
  fs.writeFileSync("build/host/voices.frames", Uint8Array.from(frames.flat()));
  check(frames.every((f) => f.length <= 640), "every frame fits the FM-1's 640 bytes");
}

console.log(fails ? `voices: ${fails} FAILED` : "voices: all ok");
process.exit(fails ? 1 : 0);
