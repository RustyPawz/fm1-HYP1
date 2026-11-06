// SPDX-License-Identifier: GPL-3.0-only
// BIT-1's Voices page: the list, hearing a voice, and where the bank goes (the emulator, a file, the FM-1).
import { BANK_MAX, BANK_HDR, NROM, readRom, envOf, pcmOf, roomOf, buildBank, bankOk, sendBank } from "./voices.js";

const BANK_KEY = "bit1-voices";
const NOTE = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
const noteName = (n) => NOTE[n % 12] + (Math.floor(n / 12) - 1);
const $ = (id) => document.getElementById(id);

let rom = null, items = [], picked = [], fileBank = null, ac = null;
// the terms first: the ROM can't be chosen until they're agreed (remembered in this browser)
try { $("agree").checked = localStorage.getItem("bit1-voices-terms") === "1"; } catch { /* not kept */ }
$("rom").disabled = !$("agree").checked;
$("agree").onchange = () => {
  $("rom").disabled = !$("agree").checked;
  try { if ($("agree").checked) localStorage.setItem("bit1-voices-terms", "1"); else localStorage.removeItem("bit1-voices-terms"); } catch { /* not kept */ }
};
for (let k = 36; k <= 84; k++) $("note").add(new Option(noteName(k), k, false, k === 60));

function describe(e) {
  if (e.kind === "ds") return `Sample, ${(e.s.rate / 1024 / 1000).toFixed(1)} kHz${e.s.loops ? ", loops" : ""}${e.fixed ? ", a drum" : ""}`;
  if (e.kind === "sq") return `Square, ${[12.5, 25, 50, 75][e.duty]}%${e.sweep & 0x70 ? ", sweep" : ""}`;
  if (e.kind === "wave") return "Wave (4-bit)";
  return `Noise${e.short ? ", metal" : ""}`;
}
const lengthOf = (it) => (it.e.kind === "ds" ? (it.e.s.len / (it.e.s.rate / 1024)).toFixed(2) + " s" : "—");
const bankNow = () => (picked.length ? buildBank(rom, picked) : fileBank);

function show() {
  const tb = $("list").tBodies[0], only = $("onlysmp").checked;
  tb.innerHTML = "";
  items.forEach((it) => {
    if (only && it.e.kind !== "ds") return;
    const tr = tb.insertRow();
    tr.className = it.on ? "on" : "";
    const box = Object.assign(document.createElement("input"), { type: "checkbox", checked: it.on, title: "In the bank" });
    box.onchange = () => {
      if (box.checked && picked.length >= NROM) { box.checked = false; return; }
      it.on = box.checked;
      picked = items.filter((x) => x.on);
      show();
    };
    tr.insertCell().append(box);
    const play = Object.assign(document.createElement("button"), { className: "quiet", textContent: "▶", title: "Hear it" });
    play.onclick = () => audition(it.e, +$("note").value);
    tr.insertCell().append(play);
    const name = Object.assign(document.createElement("input"), { type: "text", value: it.name, maxLength: 12 });
    name.oninput = () => { it.name = name.value.replace(/[^\x20-\x7E]/g, "").slice(0, 12); };
    tr.insertCell().append(name);
    tr.insertCell().textContent = describe(it.e);
    tr.insertCell().textContent = lengthOf(it);
    tr.insertCell().textContent = it.e.kind === "ds" ? (roomOf(it) / 1024).toFixed(1) + " KB" : "0";
    const half = Object.assign(document.createElement("input"), { type: "checkbox", checked: it.half, disabled: it.e.kind !== "ds", title: "Half the rate" });
    half.onchange = () => { it.half = half.checked; show(); };
    tr.insertCell().append(half);
  });
  const used = picked.reduce((s, it) => s + roomOf(it), 0), max = BANK_MAX - BANK_HDR;
  $("meter").classList.toggle("over", used > max);
  $("meter").firstElementChild.style.width = Math.min(100, used / max * 100) + "%";
  $("room").textContent = picked.length
    ? `${picked.length} of ${NROM} picked: ${(used / 1024).toFixed(1)} KB of ${(max / 1024).toFixed(0)} KB${used > max ? " — too much: halve some, or pick fewer" : ""}.`
    : fileBank ? "A saved bank, ready to use or send." : "Nothing picked.";
  $("count").textContent = items.length ? `${items.length} instruments found` : "";
  const ok = (picked.length > 0 && used <= max) || (!picked.length && !!fileBank);
  $("useemu").disabled = $("send").disabled = !ok;
  $("save").disabled = !(picked.length > 0 && used <= max);
}

$("onlysmp").onchange = show;
$("rom").onchange = async () => {
  const f = $("rom").files[0];
  if (!f) return;
  $("status").textContent = "Reading…";
  await new Promise((r) => setTimeout(r, 20));
  rom = new Uint8Array(await f.arrayBuffer());
  let ns = 0, nq = 0, nw = 0, nn = 0;
  items = readRom(rom).map((e) => ({ e, on: false, half: e.kind === "ds" && e.s.len > 24000,
    name: e.kind === "ds" ? `Sample ${++ns}` : e.kind === "sq" ? `Square ${++nq}` : e.kind === "wave" ? `Wave ${++nw}` : `Noise ${++nn}` }));
  items.sort((a, b) => (a.e.kind === "ds" ? 0 : 1) - (b.e.kind === "ds" ? 0 : 1));
  picked = [];
  fileBank = null;
  $("status").textContent = items.length ? `${f.name}: ${ns} samples and ${nq + nw + nn} Game Boy-style voices.`
    : `${f.name}: no MP2K instruments found. This game may use another sound engine.`;
  show();
};

// hearing one, before it goes in
function audition(e, note) {
  ac ??= new AudioContext();
  const t = ac.currentTime, g = ac.createGain(), env = envOf(e), dur = 0.9;
  g.gain.setValueAtTime(0, t);
  g.gain.linearRampToValueAtTime(0.5, t + env.att / 1000);
  g.gain.setTargetAtTime(0.5 * env.sus / 255, t + env.att / 1000, env.dec / 1000 / 6.9);
  g.gain.setTargetAtTime(0, t + dur, env.rel / 1000 / 6.9 + 0.01);
  g.connect(ac.destination);
  const src = ac.createBufferSource();
  if (e.kind === "ds") {
    const rate = Math.min(96000, Math.max(3000, e.s.rate / 1024)), d = pcmOf(rom, e, false), b = ac.createBuffer(1, d.length, rate);
    b.copyToChannel(d, 0);
    src.buffer = b;
    if (e.s.loops) { src.loop = true; src.loopStart = e.s.loop / rate; src.loopEnd = e.s.len / rate; }
    if (!e.fixed) src.playbackRate.value = 2 ** ((note - 60) / 12);
  } else {
    const f = 440 * 2 ** ((note - 69) / 12), sr = ac.sampleRate, b = ac.createBuffer(1, Math.round(sr * 2), sr), d = b.getChannelData(0);
    if (e.kind === "noise") {
      let l = 0x7FFF;
      for (let i = 0; i < d.length; i++) { const bit = (l ^ (l >> 1)) & 1; l = (l >> 1) | (bit << 14); if (e.short) l = (l & ~0x40) | (bit << 6); d[i] = l & 1 ? 0.5 : -0.5; }
    } else {
      for (let i = 0; i < d.length; i++) {
        const ph = (i * f / sr) % 1, st = ph * 32 | 0;
        d[i] = e.kind === "sq" ? (ph < [0.125, 0.25, 0.5, 0.75][e.duty] ? 0.4 : -0.4) : (((e.wave[st >> 1] >> (st & 1 ? 0 : 4)) & 15) - 7.5) / 15;
      }
    }
    src.buffer = b;
    src.loop = true;
  }
  src.connect(g);
  src.start(t);
  src.stop(t + dur + env.rel / 1000 + 0.3);
}

// where it goes
const b64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(s); };
$("useemu").onclick = () => {
  const b = bankNow();
  if (!b) return;
  try { localStorage.setItem(BANK_KEY, b64(b)); $("sendstatus").innerHTML = 'In the emulator\'s keeping: <a href="../emu/">switch it on</a> and the ROM sounds are there.'; }
  catch (err) { $("sendstatus").textContent = "This browser wouldn't keep it: " + err.message; }
};
$("clearemu").onclick = () => { try { localStorage.removeItem(BANK_KEY); } catch { /* none kept */ } $("sendstatus").textContent = "The emulator's bank is cleared."; };
$("save").onclick = () => {
  const a = Object.assign(document.createElement("a"), { href: URL.createObjectURL(new Blob([buildBank(rom, picked)])), download: "voices.bitv" });
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
};
$("bankfile").onchange = async () => {
  const f = $("bankfile").files[0];
  if (!f) return;
  const b = new Uint8Array(await f.arrayBuffer()), n = bankOk(b);
  if (n < 0) { $("status").textContent = `${f.name}: not a BIT-1 bank.`; return; }
  fileBank = b.slice(0, new DataView(b.buffer).getUint32(8, true));
  items.forEach((it) => { it.on = false; });
  picked = [];
  $("status").textContent = `${f.name}: a bank of ${n} voices.`;
  show();
};

// to the FM-1 (bank.c, voices.js sendBank)
$("send").onclick = async () => {
  const say = (t) => { $("sendstatus").textContent = t; }, bank = bankNow();
  if (!bank) return;
  if (!navigator.requestMIDIAccess) { say("This browser has no Web MIDI: use Chrome or Edge."); return; }
  $("send").disabled = true;
  try {
    const n = await sendBank(() => navigator.requestMIDIAccess({ sysex: true }), bank, say);
    say(`Done: ${n} ROM voices on the FM-1 (sounds ROM 1 on, after Noise sweep).`);
  } catch (err) {
    say("Not sent: " + (err.name === "SecurityError" || err.name === "NotAllowedError" ? "MIDI (with SysEx) wasn't allowed" : err.message) + ". Try again; the FM-1 keeps no half bank.");
  } finally {
    $("send").disabled = false;
  }
};
show();
