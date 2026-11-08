// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK in the browser (a first version): the engine (WebAssembly) in an AudioWorklet, a DDJ-400 or DDJ-FLX4 over
// Web MIDI (a subset of the Mac app's mapping, its LEDs paced), and the tracks and patterns on a canvas.

const MACHINES = ["KICK", "SNARE", "HAT", "CLAP", "TOM", "SYNTH", "FM", "SAMPLE"];
const COLORS = ["#FF4A3D", "#FF9F1C", "#FFD60A", "#7CDB3C", "#2EC4F2", "#4D7CFF", "#A66BFF", "#FF5FB4"];
const PATTERNS = ["DEEP", "TECH", "JACKIN", "AFRO", "TECHNO", "ACID", "DUB", "MELODIC",
                  "2-STEP", "SPEED", "4X4", "BASSLINE", "LIQUID", "NEURO", "JUNGLE", "ROLLER"];
const GENRES = ["HOUSE", "TECHNO", "GARAGE", "DRUM & BASS"];
const PERFS = ["REPEAT", "STUTTER", "TAPE STOP", "DLY THROW", "REV THROW", "FILTER", "CRUSH", "DUB HOLD", "ECHO",
               "FLANGER", "PHASER", "TRANS", "AUTOPAN", "REVERSE", "HALF", "NOISE", "DISTORT", "SWEEP"];
const DIVS = ["1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS"];

// ---- the engine
let ctx = null, node = null;
let state = null, grid = new Float32Array(8 * 64);
const ui = { track: 0, page: 0, modes: [0x00, 0x00], perf: 0, perfOn: false, div: 3, amount: 0.6, target: 2,
             tempoPicked: false, lastTempo: null, jogAcc: [0, 0], message: "" };

function call(fn, ...args) { node?.port.postMessage({ call: fn, args, state: true }); }

async function startAudio() {
    if (ctx) return;
    ctx = new AudioContext({ sampleRate: 48000, latencyHint: "interactive" });
    const wasm = await (await fetch("trigcore.wasm")).arrayBuffer();
    await ctx.audioWorklet.addModule("worklet.js");
    node = new AudioWorkletNode(ctx, "trigdeck", { outputChannelCount: [2], processorOptions: { wasm } });
    node.port.onmessage = (e) => {
        if (e.data.error) { ui.message = "ENGINE: " + e.data.error; return; }
        state = e.data.state; grid = e.data.grid;
    };
    node.onprocessorerror = (e) => { ui.message = "ENGINE STOPPED: " + (e.message || "an error"); };
    ui.message = `AUDIO ${ctx.state.toUpperCase()} AT ${ctx.sampleRate} HZ`;
    node.connect(ctx.destination);
    document.getElementById("start").textContent = "AUDIO ON";
    document.getElementById("start").disabled = true;
}

// ---- the controller (Web MIDI)
let midiIn = null, midiOut = null, isFLX4 = false, keepAlive = null;
const sent = new Map(), queue = [];
const POSITIONS = [0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x02, 0x06, 0x00, 0x03, 0x01, 0xF7];   // DDJ-400: report controls
const FLX4_KEEPALIVE = [0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xF7];   // (Mixxx's FLX4 script)

async function startMIDI() {
    if (!navigator.requestMIDIAccess) { ui.message = "THIS BROWSER HAS NO WEB MIDI (USE CHROME OR EDGE)"; return; }
    let access;
    try { access = await navigator.requestMIDIAccess({ sysex: true }); }
    catch { try { access = await navigator.requestMIDIAccess(); } catch { ui.message = "MIDI NOT ALLOWED"; return; } }
    const find = () => {
        const isDDJ = (p) => /DDJ-(400|FLX4)/i.test(p.name);
        midiIn = [...access.inputs.values()].find(isDDJ) || null;
        midiOut = [...access.outputs.values()].find(isDDJ) || null;
        if (midiIn) midiIn.onmidimessage = (e) => onMIDI(e.data);
        isFLX4 = /FLX4/i.test(midiOut?.name || "");
        sent.clear();
        clearInterval(keepAlive);
        if (midiOut) {
            try { midiOut.send(isFLX4 ? FLX4_KEEPALIVE : POSITIONS); } catch { /* (no SysEx permission) */ }
            if (isFLX4) keepAlive = setInterval(() => { try { midiOut.send(FLX4_KEEPALIVE); } catch {} }, 200);
        }
        ui.message = midiIn ? `${midiIn.name} CONNECTED` : "NO DDJ-400 / FLX4: PLUG IT IN (OR USE THE MOUSE AND KEYS)";
    };
    access.onstatechange = find;
    find();
}

const press = (d) => d[2] > 0 && (d[0] & 0xF0) === 0x90;
function onMIDI(d) {
    const st = d[0], n = d[1], v = d[2], ch = st & 0x0F, kind = st & 0xF0;
    if (kind === 0x90 || kind === 0x80) {
        const down = press(d);
        if (ch <= 1) {                                                  // deck buttons
            const deck = ch;
            if (n === 0x0B && down) call("td_play", state?.playing ? 0 : 1);
            const modes = { 0x1B: 0x00, 0x6D: 0x60, 0x20: 0x20, 0x22: 0x30 };
            if (n in modes && down) { ui.modes[deck] = modes[n]; sent.clear(); }
            if (deck === 0 && n === 0x58) call("td_fill", down ? 1 : 0);          // FILL while held
            if (deck === 1 && n === 0x51 && down) ui.page = Math.max(0, ui.page - 1);
            if (deck === 1 && n === 0x53 && down) ui.page = Math.min(pages() - 1, ui.page + 1);
            if (deck === 1 && (n === 0x3E || n === 0x3D) && down) lengthBar(n === 0x3D ? 1 : -1);   // SHIFT: a bar
            if (deck === 0 && (n === 0x51 || n === 0x53) && down) ui.track = (ui.track + (n === 0x53 ? 1 : 7)) % 8;
        } else if (ch === 4 && down) {                                  // beat FX
            if (n === 0x63 || n === 0x64) { const old = ui.perf; ui.perf = (ui.perf + (n === 0x64 ? 17 : 1)) % 18; if (ui.perfOn) { perf(false, old); perf(true); } }
            if (n === 0x4A) { ui.div = Math.max(0, ui.div - 1); if (ui.perfOn) perf(true); }
            if (n === 0x4B) { ui.div = Math.min(7, ui.div + 1); if (ui.perfOn) perf(true); }
            if (n === 0x47) { ui.perfOn = !ui.perfOn; perf(ui.perfOn); }
            if (n === 0x10 || n === 0x11 || n === 0x14) { ui.target = { 0x10: 0, 0x11: 1, 0x14: 2 }[n]; if (ui.perfOn) perf(true); }
        } else if (ch === 6 && down) {                                  // LOAD: the previous / next pattern
            if (n === 0x46 || n === 0x47) queuePattern(((state?.queued >= 0 ? state.queued : state?.pattern ?? 0) + (n === 0x47 ? 1 : -1) + 16) % 16);
        } else if (ch >= 7 && ch <= 10 && down) {                       // pads
            const deck = ch <= 8 ? 0 : 1, mode = n & 0xF0, i = n & 0x0F;
            if (i > 7) return;
            if (mode === 0x00) call("td_step_toggle", ui.track, ui.page * 16 + deck * 8 + i);
            if (mode === 0x20) { if (i < 4) selectTrack(deck * 4 + i); else call("td_mute", deck * 4 + i - 4, state?.tracks[deck * 4 + i - 4].muted ? 0 : 1); }
            if (mode === 0x30) queuePattern(deck * 8 + i);
        }
    } else if (kind === 0xB0) {
        if ((st === 0xB0 || st === 0xB1) && (n === 0x21 || n === 0x22)) {          // jog side: the track ring
            const deck = st - 0xB0;
            ui.jogAcc[deck] += v - 64;
            if (Math.abs(ui.jogAcc[deck]) >= 180) {
                const base = deck * 4, cur = ui.track >= base && ui.track < base + 4 ? ui.track - base : 0;
                selectTrack(base + (cur + Math.sign(ui.jogAcc[deck]) + 4) % 4);
                ui.jogAcc[deck] = 0;
            }
        }
        const x = v / 127;
        if (st === 0xB6 && n === 0x1F) call("td_crossfader", x);
        if ((st === 0xB0 || st === 0xB1) && n === 0x13) call("td_deck_level", st - 0xB0, x);
        if (st === 0xB0 && n === 0x00) tempo(60 + x * 140);                         // towards you: faster
        if (st === 0xB4 && n === 0x02) { ui.amount = x; if (ui.perfOn) perf(true); }
    }
}

function perf(on, type = ui.perf) { call("td_perf", type, ui.div, ui.target, on ? 1 : 0, ui.amount); }
function queuePattern(p) { call("td_pattern_queue", p); }
function selectTrack(t) { ui.track = t; ui.page = 0; }
function pages() { return Math.max(1, Math.ceil((state?.tracks[ui.track].length ?? 16) / 16)); }
function lengthBar(dir) {
    const n = state?.tracks[ui.track].length ?? 16;
    const next = dir > 0 ? Math.min(64, (Math.floor(n / 16) + 1) * 16) : (n > 16 ? Math.floor((n - 1) / 16) * 16 : n);
    call("td_set_length_repeat", ui.track, next);
    ui.page = Math.floor((next - 1) / 16);
}
function tempo(bpm) {
    const cur = state?.bpm ?? 120;
    if (ui.lastTempo !== null && Math.abs(cur - ui.lastTempo) > 1.5) ui.tempoPicked = false;   // (the pattern changed it)
    if (!ui.tempoPicked && Math.abs(bpm - cur) > 2) return;                                     // soft takeover
    ui.tempoPicked = true;
    ui.lastTempo = Math.round(bpm);
    call("td_set_tempo", ui.lastTempo);
}

// ---- LEDs: what they should show, diffed, at most 24 messages a frame, one per send (a burst can jam the controller)
function leds() {
    if (!midiOut || !state) return;
    const want = new Map();
    const put = (s, n, on) => want.set((s << 8) | n, on ? 0x7F : 0);
    for (const d of [0, 1]) {
        put(0x90 + d, 0x0B, state.playing); put(0x90 + d, 0x47, state.playing);
        const mode = ui.modes[d], tr = state.tracks[ui.track];
        for (let i = 0; i < 8; i++) {
            let on = false;
            if (mode === 0x00) {
                const s = ui.page * 16 + d * 8 + i;
                on = s < tr.length && grid[ui.track * 64 + s] > 0;
                if (state.playing && tr.step === s) on = !on;
            } else if (mode === 0x20) on = i < 4 ? ui.track === d * 4 + i : !state.tracks[d * 4 + i - 4].muted;
            else if (mode === 0x30) on = state.pattern === d * 8 + i || (state.queued === d * 8 + i && (performance.now() % 400) < 200);
            put(0x97 + 2 * d, mode + i, on); put(0x98 + 2 * d, mode + i, on);
        }
    }
    put(0x94, 0x47, ui.perfOn);
    for (const [k, v] of want) if (sent.get(k) !== v) queue.push(k), sent.set(k, v);
    for (let i = 0; i < 24 && queue.length; i++) {
        const k = queue.shift();
        try { midiOut.send([k >> 8, k & 0xFF, sent.get(k)]); } catch {}
    }
}

// ---- the screen
const cv = document.getElementById("screen"), g = cv.getContext("2d");
function draw() {
    const W = cv.width = cv.clientWidth * devicePixelRatio, H = cv.height = cv.clientHeight * devicePixelRatio;
    g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
    const w = W / devicePixelRatio, h = H / devicePixelRatio;
    g.fillStyle = "#0B0B0E"; g.fillRect(0, 0, w, h);
    g.font = "16px 'Terminus', monospace"; g.textBaseline = "top";
    let x = 16;
    for (const [i, ch] of [..."TRIGDECK"].entries()) { g.fillStyle = COLORS[i]; g.fillText(ch, x, 14); x += 10; }
    g.fillStyle = "#888"; g.fillText(ui.message, 120, 14);
    if (!state) { g.fillStyle = "#ddd"; g.fillText("PRESS START (AUDIO NEEDS A CLICK)", 16, 50); return; }
    g.font = "32px 'Terminus', monospace"; g.fillStyle = "#eee"; g.fillText(state.bpm.toFixed(0), 16, 40);
    g.font = "16px 'Terminus', monospace";
    g.fillText(`BPM   P${String(state.pattern + 1).padStart(2, "0")} ${PATTERNS[state.pattern]}` +
               (state.queued >= 0 ? `  →  P${String(state.queued + 1).padStart(2, "0")} ${PATTERNS[state.queued]}` : "") +
               `   ${state.playing ? "▶ PLAY" : "■ STOP"}   FX ${PERFS[ui.perf]} ${DIVS[ui.div]} ${ui.perfOn ? "ON" : "off"}`, 80, 54);
    // tracks: the whole length, the page on the pads framed
    const top = 90, rowH = Math.min(44, (h - top - 150) / 8), gx = 150, gw = w - gx - 40;
    for (let t = 0; t < 8; t++) {
        const tr = state.tracks[t], y = top + t * rowH, active = t === ui.track;
        g.fillStyle = active ? COLORS[t] : COLORS[t] + "55"; g.fillRect(16, y + 2, rowH - 4, rowH - 4);
        g.fillStyle = active ? "#000" : "#eee"; g.fillText(String(t + 1), 16 + rowH / 2 - 4, y + rowH / 2 - 8);
        g.fillStyle = tr.muted ? "#555" : active ? "#eee" : "#999"; g.fillText(tr.muted ? "MUTED" : MACHINES[tr.machine], 16 + rowH + 6, y + rowH / 2 - 8);
        const n = tr.length, bars = Math.ceil(n / 16), gap = bars > 1 ? 4 : 0, cell = (gw - (bars - 1) * gap) / Math.max(16, n);
        const cx = (s) => gx + s * cell + Math.floor(s / 16) * gap;
        for (let s = 0; s < n; s++) {
            g.fillStyle = grid[t * 64 + s] > 0 ? (tr.muted ? COLORS[t] + "55" : COLORS[t]) : (s % 4 === 0 ? "#2a2a30" : "#1a1a1f");
            g.fillRect(cx(s) + 1, y + rowH * 0.25, cell - 2, rowH * 0.5);
            if (state.playing && tr.step === s) { g.strokeStyle = "#fff"; g.lineWidth = 2; g.strokeRect(cx(s), y + rowH * 0.25 - 1, cell, rowH * 0.5 + 2); }
        }
        if (active && bars > 1) {
            const p = Math.min(ui.page, bars - 1), x0 = cx(p * 16) - 2, x1 = cx(Math.min(n, p * 16 + 16) - 1) + cell + 2;
            g.strokeStyle = "#fff"; g.lineWidth = 1.5; g.strokeRect(x0, y + rowH * 0.25 - 4, x1 - x0, rowH * 0.5 + 8);
        }
        g.fillStyle = "#222"; g.fillRect(w - 30, y + 4, 6, rowH - 8);
        g.fillStyle = COLORS[t]; const lv = Math.min(1, tr.level * 2); g.fillRect(w - 30, y + 4 + (rowH - 8) * (1 - lv), 6, (rowH - 8) * lv);
    }
    // patterns: four genres of four (click to queue)
    const py = top + 8 * rowH + 20, pw = (w - 32) / 16;
    for (let p = 0; p < 16; p++) {
        const px = 16 + p * pw, on = state.pattern === p, q = state.queued === p;
        g.fillStyle = on ? "#eee" : q ? "#666" : "#1d1d22"; g.fillRect(px + 2, py + 18, pw - 4, 34);
        g.fillStyle = on ? "#000" : "#ccc"; g.font = "11px 'Terminus', monospace";
        g.fillText(PATTERNS[p], px + 6, py + 30);
        if (p % 4 === 0) { g.fillStyle = "#888"; g.fillText(GENRES[p / 4], px + 4, py + 2); }
    }
    g.font = "16px 'Terminus', monospace";
    layout.patterns = { y: py + 18, h: 34, pw }; layout.rows = { top, rowH, gx, gw };
}
const layout = {};

cv.addEventListener("click", (e) => {
    if (!state) return;
    const r = cv.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
    const { patterns: P, rows: R } = layout;
    if (P && y >= P.y && y <= P.y + P.h) return queuePattern(Math.min(15, Math.floor((x - 16) / P.pw)));
    if (R && y >= R.top && y < R.top + 8 * R.rowH) {
        const t = Math.floor((y - R.top) / R.rowH);
        if (x < R.gx) return selectTrack(t);
        const n = state.tracks[t].length, bars = Math.ceil(n / 16), gap = bars > 1 ? 4 : 0, cell = (R.gw - (bars - 1) * gap) / Math.max(16, n);
        for (let s = 0; s < n; s++) {
            const cx = R.gx + s * cell + Math.floor(s / 16) * gap;
            if (x >= cx && x < cx + cell) return call("td_step_toggle", t, s);
        }
    }
});
addEventListener("keydown", (e) => {
    if (!state) return;
    if (e.code === "Space") { e.preventDefault(); call("td_play", state.playing ? 0 : 1); }
    if (e.key === "ArrowRight") queuePattern((state.pattern + 1) % 16);
    if (e.key === "ArrowLeft") queuePattern((state.pattern + 15) % 16);
    if (e.key === "ArrowDown") selectTrack((ui.track + 1) % 8);
    if (e.key === "ArrowUp") selectTrack((ui.track + 7) % 8);
});
document.getElementById("start").addEventListener("click", async () => { await startAudio(); await startMIDI(); });

(function frame() { draw(); leds(); requestAnimationFrame(frame); })();
if (location.hash === "#autostart") startAudio().then(() => call("td_play", 1));   // (headless checks)
