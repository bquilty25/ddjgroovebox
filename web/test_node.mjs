// SPDX-License-Identifier: GPL-3.0-only
// The WebAssembly engine, checked in Node: it starts, plays P01 and P13 (their own tempos), at a sane level, and a
// step toggled from outside is heard. Usage: node web/test_node.mjs
import { readFileSync } from "node:fs";
import { instantiate, loadProject, saveProject } from "./src/wasi.js";

const bytes = readFileSync(new URL("./trigcore.wasm", import.meta.url));
const x = instantiate(bytes);
let fails = 0;
const check = (msg, ok) => { console.log(`  ${msg.padEnd(70)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

function render(frames) {                                  // in 128-frame blocks, as an AudioWorklet does
    let e = 0, pk = 0, finite = true;
    for (let i = 0; i < frames; i += 128) {
        const p = x.tdw_render(128);
        const out = new Float32Array(x.memory.buffer, p, 256);
        for (const v of out) { e += v * v; pk = Math.max(pk, Math.abs(v)); finite &&= Number.isFinite(v); }
    }
    return { rms: 10 * Math.log10(e / (2 * frames) + 1e-12), pk, finite };
}

check("initialises", x.tdw_init() === 0);
x.tdw_state();
check(`opens on P01 at 122 BPM (${x.tdw_bpm().toFixed(1)})`, x.tdw_pattern() === 0 && Math.abs(x.tdw_bpm() - 122) < 0.01);
x.td_play(1);
let r = render(48000 * 8);
check(`P01, 8 s: rms ${r.rms.toFixed(1)} dBFS, peak ${r.pk.toFixed(2)}`, r.finite && r.pk <= 1 && r.rms > -24 && r.rms < -9);
x.tdw_state();
check("the playhead moves", x.tdw_playing() === 1 && x.tdw_step(0) >= 0);

x.td_pattern_queue(12);                                    // P13 liquid: switches at the end of P01 (4 bars)
render(48000 * 9);
x.tdw_state();
check(`switching to P13 brings 174 BPM (${x.tdw_bpm().toFixed(1)})`, x.tdw_pattern() === 12 && Math.abs(x.tdw_bpm() - 174) < 0.01);
r = render(48000 * 4);
check(`P13, 4 s: rms ${r.rms.toFixed(1)} dBFS`, r.finite && r.pk <= 1 && r.rms > -26 && r.rms < -9);

const g = new Float32Array(x.memory.buffer, x.tdw_grid(), 8 * 64);
const before = g[0 * 64 + 1];
x.td_step_toggle(0, 1);
const g2 = new Float32Array(x.memory.buffer, x.tdw_grid(), 8 * 64);
check("a step toggled from the page", g2[0 * 64 + 1] === 1 - before);

// a saved project into a fresh engine, as the AudioWorklet does at start (the first allocation can grow the memory:
// a view taken before it was detached in Firefox: "attempting to access detached ArrayBuffer")
x.td_set_tempo(133);
const saved = saveProject(x);
check(`the project saves (${saved?.length} bytes)`, saved && saved.length > 1000);
for (let i = 0; i < 3; i++) {
    const y = instantiate(bytes);
    y.tdw_init();
    let ok = false;
    try { ok = loadProject(y, saved) === 0; } catch (e) { console.log("    " + e); }
    y.tdw_state();
    check(`a fresh engine loads it (${i + 1}): ${y.tdw_bpm().toFixed(0)} BPM`, ok && Math.abs(y.tdw_bpm() - 133) < 0.01);
}

// (DJ decks) both decks playing, blended half way: a sane level, and well within real time (16 tracks)
{
    const z = instantiate(bytes);
    z.tdw_init();
    z.td_deck_play(0, 1);
    for (let i = 0; i < 48000 * 2; i += 128) z.tdw_render(128);
    z.td_deck_play(1, 1);
    z.td_blend_fader(0.5);
    let e = 0, pk = 0;
    const N = 48000 * 8, t0 = performance.now();
    for (let i = 0; i < N; i += 128) {
        const o = new Float32Array(z.memory.buffer, z.tdw_render(128), 256);
        for (const v of o) { e += v * v; pk = Math.max(pk, Math.abs(v)); }
    }
    const ms = performance.now() - t0, rms = 10 * Math.log10(e / (2 * N) + 1e-12);
    z.tdw_state();
    check(`two decks (P01 + P05) mid-blend, 8 s: rms ${rms.toFixed(1)} dBFS, peak ${pk.toFixed(2)}`, pk <= 1 && rms > -24 && rms < -8);
    check(`  rendered in ${ms.toFixed(0)} ms (${(ms / 80).toFixed(1)} % of real time)`, ms < 8000 * 0.25);
}

console.log(fails ? `${fails} failed` : "all ok");
process.exit(fails ? 1 : 0);
