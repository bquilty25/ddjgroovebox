// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK in the browser: the app around the engine, as the Mac app's AppController and TrigInstrument are. Events
// from the DDJ (Web MIDI), the mouse and the keyboard go through the mapper into the engine; ~20 times a second the
// drawing, the strip and the DDJ's LEDs follow the engine and the mapper.
import { C, PAD_MODES, COLORS, ev, press, DECKS } from "./vocab.js";
import { TrigWebEngine, exportProject, forgetProject } from "./engine.js";
import { TrigMapper, DDJ400Decoder, DDJ400LEDWriter } from "./mapping/index.js";
import { GrooveboxLayout } from "./view/layout.js";
import { readScope } from "./view/scope.js";

// every control the drawing can show (Swift's allDDJControls)
const CONTROLS = [C.crossfader, C.browse, C.browsePress, C.beatLeft, C.beatRight, C.fxSelect, C.fxOnOff, C.fxLevel,
                  C.headphoneLevel, C.fxChannel(0), C.fxChannel(1), C.fxChannel(2)];
for (const d of DECKS) {
    CONTROLS.push(C.play(d), C.cue(d), C.sync(d), C.shift(d), C.loopIn(d), C.loopOut(d), C.reloop(d), C.callLeft(d),
                  C.callRight(d), C.jog(d), C.jogTouch(d), C.tempo(d), C.channelFader(d), C.trim(d), C.eqHi(d), C.eqMid(d),
                  C.eqLow(d), C.cfx(d), C.headphoneCue(d), C.load(d));
    for (let i = 0; i < 8; i++) CONTROLS.push(C.pad(d, i));
    for (const m of PAD_MODES) CONTROLS.push(C.padModeButton(d, m));
}

const $ = (id) => document.getElementById(id);
const status = (s, bad = false) => { $("status").textContent = s; $("status").style.color = bad ? "#FF4A3D" : "#888"; };
addEventListener("error", (e) => status("error: " + (e.message || e.error), true));
addEventListener("unhandledrejection", (e) => status("error: " + (e.reason?.message || e.reason), true));

await document.fonts.load('16px "Terminus"').catch(() => {});
const layout = new GrooveboxLayout($("app"));
const { ddj, strip } = layout;
const engine = new TrigWebEngine();
let mapper = null, force = true, frame = 0, crossfader = 0.5, problem = null;
globalThis.trigdeck = { engine, layout, get mapper() { return mapper; } };   // (for the browser's console)

// ---- events: the mapper, then the engine (the mouse's own presses are already drawn; the rest are shown)
const fromMouse = new WeakSet();
ddj.onEvent((e) => fromMouse.add(e));
layout.onEvent((e) => {
    handle(e);
    queueMicrotask(() => { if (!fromMouse.has(e)) ddj.show(e); });
});
function handle(e) {
    if (!mapper) return;
    force = true;
    const actions = mapper.handle(e);
    for (const a of actions) if (a.type === "crossfader") crossfader = a.value;
    engine.apply(actions);
    if (actions.some((a) => a.type === "capture")) setTimeout(() => engine.captureTo(mapper.track), 50);
}
strip.onSelectTrack = (i) => {                                // (DJ decks: a row on the other side edits that deck)
    if (!mapper) return;
    force = true;
    const d = i >> 3;
    if (d !== engine.focus) engine.apply([{ type: "focus", deck: d }]);
    engine.apply(mapper.select(i & 7));
};
ddj.onReset((c) => { if (mapper) { force = true; engine.apply(mapper.reset(c)); } });   // (a double-click on a knob)
// the help line in the bar: what the control under the mouse does now (kept up to date while it stays there)
let hovered = null;
const helpline = $("helpline");
function showHelp() {
    if (!hovered || !mapper) { helpline.textContent = "point at any control to see what it does"; helpline.title = ""; return; }
    const f = mapper.face(hovered), name = f ? [f.title, f.value].filter(Boolean).join(" ") : "";
    const text = mapper.explain(hovered);
    helpline.innerHTML = "";
    if (name) { const b = document.createElement("b"); b.textContent = name + ": "; helpline.append(b); }
    helpline.append(text);
    helpline.title = (name ? name + ": " : "") + text;          // (the whole text, when the line is too narrow)
}
ddj.onHover((c) => { hovered = c; showHelp(); });
ddj.helpText = (c) => {
    const f = mapper?.face(c);
    return f ? [f.title, f.value].filter(Boolean).join(": ") + (mapper ? "\n" + mapper.explain(c) : "") : null;
};

/** every parameter the pattern's scenes hold: its name, range, scene A's and B's values (or null), its written value */
function sceneRows() {
    const rows = [];
    for (let t = 0; t < 8; t++) {
        for (let p = 0; p < 48; p++) {
            const a = engine.scene(0, t, p), b = engine.scene(1, t, p);
            if (a === null && b === null) continue;
            const info = engine.info(t, p);
            if (!info) continue;
            rows.push({ label: `${t + 1} ${info.name}`, color: COLORS.track[t], min: info.min, max: info.max, a, b,
                        base: engine.value(t, p) });
        }
    }
    return rows;
}

// ---- the frame: the mapper's timed releases; the drawing 20 times a second (at once after an event)
function update() {
    if (!mapper) { requestAnimationFrame(update); return; }
    engine.apply(mapper.tick());
    frame++;
    if (hovered && (frame % 6 === 0 || force)) showHelp();     // (it follows a held step, a page change...)
    if (frame % 3 === 0 || force) {
        force = false;
        const faces = new Map();
        for (const c of CONTROLS) { const f = mapper.face(c); if (f) faces.set(c, f); }
        ddj.faces = faces;
        ddj.padModes = [mapper.padMode(0), mapper.padMode(1)];
        ddj.header = mapper.header;
        ddj.deckTracks = DECKS.map((d) => [0, 1, 2, 3].map((i) => {
            const t = d * 4 + i;
            return { name: engine.machineName(engine.machine(t)), color: COLORS.track[t],
                     level: Math.round(engine.trackLevel(t) * 8) / 8, active: t === mapper.track, muted: engine.muted(t) };
        }));
        ddj.jogDials = DECKS.map((d) => mapper.jogDialPosition(d));
        ddj.meters = engine.meters;
        ddj.connected = !!midiIn;
    }
    const h = mapper.header;
    strip.update({
        header: h,
        // (DJ decks) 16 rows: deck A's pattern's 8 tracks, then deck B's (steps: 1 trig, 2 locks, 4 a condition)
        rows: Array.from({ length: 16 }, (_, i) => {
            const d = i >> 3, t = i & 7, row = engine.deckRow(d, t), steps = new Array(row.length);
            for (let s = 0; s < row.length; s++) steps[s] = engine.deckStep(d, t, s);
            return { name: engine.machineName(row.machine), color: COLORS.track[t], active: d === engine.focus && t === mapper.track,
                     muted: row.muted, level: row.level, steps, playhead: row.step >= 0 ? row.step : null };
        }),
        decks: [0, 1].map((d) => {
            const p = engine.deckPattern(d), q = engine.deckQueued(d), nm = (x) => `P${String(x + 1).padStart(2, "0")} ${engine.patternName(x)}`.trim();
            return { label: d === 0 ? "A" : "B", pattern: nm(p) + (q !== null ? " → " + nm(q) : ""), playing: engine.deckPlaying(d),
                     onAir: engine.onAir === d, focus: engine.focus === d };
        }),
        focus: engine.focus, onAir: engine.onAir,
        blend: engine.blending ? { to: engine.blendTo, x: engine.blendX, bpm: engine.bpm, bpmFrom: engine.blendTempos.from,
                                   bpmTo: engine.blendTempos.to, gains: [engine.deckGains(0), engine.deckGains(1)] } : null,
        crossfader, autopilot: engine.autopilot, connected: !!midiIn, problem, sceneRows: sceneRows(),
        patternNames: Array.from({ length: 16 }, (_, p) => engine.patternName(p)),
        scope: readScope(engine, h.page, mapper.track, COLORS.track[mapper.track]),
        // (the picture shows when what the knobs show changes: not a lock playing or an LFO moving)
        scopeKey: `${h.page}:${mapper.track}:${engine.machine(mapper.track)}:` +
                  Array.from({ length: 8 }, (_, s) => mapper.knobFace(s).value).join("|"),
        // the pack list (MASTER page) or, BROWSE turned on any other page, the patterns' (DJ decks)
        packs: mapper.patternBrowsed > mapper.packBrowsed
            ? { kind: "pattern", names: Array.from({ length: 16 }, (_, p) => engine.patternName(p) || `P${String(p + 1).padStart(2, "0")}`),
                cursor: mapper.patternCursor, browsedAt: mapper.patternBrowsed,
                loaded: mapper.patternLoaded ? { index: mapper.patternLoaded.index, at: mapper.patternLoaded.at } : null, revertedAt: null }
            : { names: engine.packNames, cursor: mapper.packCursor, browsedAt: mapper.packBrowsed,
                loaded: mapper.packLoaded, revertedAt: mapper.packReverted },
        lengthChangedAt: mapper.lengthChanged,
        clock: mapper.now(),
    });
    leds();
    requestAnimationFrame(update);
}

// ---- the DDJ-400 / DDJ-FLX4 over Web MIDI
let midiIn = null, midiOut = null, isFLX4 = false, keepAlive = null, asked = null, answers = new Set(), unanswered = 0;
const decoder = new DDJ400Decoder();
const writer = new DDJ400LEDWriter();
const ASK_400 = [0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x02, 0x06, 0x00, 0x03, 0x01, 0xF7];   // report your controls
const FLX4_KEEPALIVE = [0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xF7];   // (Mixxx's FLX4 script)

// Ask for MIDI straight from a click, before anything is awaited: Firefox shows its permission prompt (a site
// permission add-on) only while the click still counts as the user's, and on a hosted page fetching the engine first
// takes long enough for that to lapse.
function requestMIDI() {
    if (!navigator.requestMIDIAccess) return null;
    return navigator.requestMIDIAccess({ sysex: true });
}
async function startMIDI(request) {
    if (!request) { problem = "THIS BROWSER HAS NO WEB MIDI: USE FIREFOX, CHROME OR EDGE (THE MOUSE AND KEYS WORK)"; return; }
    let access;
    try { access = await request; }
    catch (sysexErr) {
        try { access = await navigator.requestMIDIAccess(); }
        catch (err) {
            const why = (err?.message || sysexErr?.message || "").replace(/\s+/g, " ").trim();
            problem = /firefox/i.test(navigator.userAgent)
                ? "FIREFOX REFUSED MIDI: PRESS MIDI (TOP LEFT) AND ALLOW THE SITE PERMISSION ADD-ON (THE MOUSE AND KEYS WORK)"
                : "MIDI NOT ALLOWED: ALLOW IT IN THE SITE SETTINGS AND PRESS MIDI (THE MOUSE AND KEYS WORK)";
            status(problem.toLowerCase() + (why ? ` [${why}]` : ""), true);
            $("midi").hidden = false;
            return;
        }
    }
    $("midi").hidden = true;
    problem = null;
    const find = () => {
        const isDDJ = (p) => /DDJ-(400|FLX4)/i.test(p.name);
        const was = midiIn;
        midiIn = [...access.inputs.values()].find(isDDJ) || null;
        midiOut = [...access.outputs.values()].find(isDDJ) || null;
        isFLX4 = /FLX4/i.test(midiOut?.name || "");
        if (midiIn) midiIn.onmidimessage = (m) => {
            if (asked !== null) { answers.add((m.data[0] << 8) | m.data[1]); if (answers.size >= 8) { asked = null; unanswered = 0; problem = null; } }
            const e = decoder.decode(m.data);
            if (e) { handle(e); ddj.show(e); }
        };
        clearInterval(keepAlive);
        writer.invalidate();
        decoder.reset();
        if (midiOut) {
            ask();
            if (isFLX4) keepAlive = setInterval(() => sysex(FLX4_KEEPALIVE), 200);
        } else if (was && mapper) {
            engine.apply(mapper.releaseAll());
            ddj.clearPressed();
        }
        status(midiIn ? `${midiIn.name} connected` : "no DDJ-400 / FLX4: plug it in (the mouse and keys play too)");
    };
    access.onstatechange = find;
    find();
}
function sysex(bytes) { try { midiOut?.send(bytes); } catch { /* (no SysEx permission) */ } }
function ask() { answers = new Set(); asked = performance.now(); sysex(isFLX4 ? FLX4_KEEPALIVE : ASK_400); }
setInterval(() => {                                           // three unanswered requests: the DDJ is not receiving
    if (asked === null || !midiOut || performance.now() - asked < 2000) return;
    if (++unanswered >= 3) problem = "THE DDJ IS NOT RECEIVING (NO LEDS): UNPLUG IT AND PLUG IT BACK IN";
    ask();
}, 500);

// LEDs: diffed, at most 24 a frame, one message per send (a packed burst can jam the controller)
function leds() {
    if (!midiOut) return;
    for (const m of writer.messages(mapper, engine.meters)) { try { midiOut.send(m); } catch { return; } }
}
addEventListener("pagehide", () => {
    if (!midiOut) return;
    const off = writer.allOff();
    for (let i = 0; i < off.length; i++) { try { midiOut.send(off[i], performance.now() + Math.floor(i / 16) * 4); } catch {} }
});

// ---- the bar: start (audio needs a click), a new project, the project as a file
$("midi").addEventListener("click", () => startMIDI(requestMIDI()));    // (a retry, in its own click)
$("start").addEventListener("click", async () => {
    $("start").disabled = true;
    status("starting…");
    let midi = null;
    try { midi = requestMIDI(); midi?.catch(() => {}); } catch { /* (handled in startMIDI) */ }
    try {
        engine.onError = (msg) => { problem = msg.toUpperCase(); status(msg, true); };
        await engine.start();
        mapper = new TrigMapper(engine);
        $("overlay").remove();
        status("playing without a controller until MIDI is allowed");
        startMIDI(midi);                                      // (not awaited: the browser's permission prompt can wait)
    } catch (err) {
        status("could not start: " + err, true);
        $("start").disabled = false;
    }
});
$("new").addEventListener("click", () => {
    if (!mapper || !confirm("Start a new project? TRIGDECK loads its 16 default patterns; your project is replaced (export it first to keep it).")) return;
    engine.apply(mapper.releaseAll());
    engine.newProject();
});
$("export").addEventListener("click", () => {
    engine.save(true);
    setTimeout(() => {
        const blob = exportProject();
        if (!blob) return;
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = "trigdeck.project";
        a.click();
        URL.revokeObjectURL(a.href);
    }, 300);
});
$("import").addEventListener("change", async (e) => {
    const f = e.target.files?.[0];
    if (!f || !confirm(`Load ${f.name}? It replaces the current project.`)) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    const s = btoa(String.fromCharCode(...bytes));
    try { localStorage.setItem("trigdeck.project.v2", s); location.reload(); } catch { status("could not load that file", true); }
});
$("forget").addEventListener("click", () => {
    if (confirm("Forget the project saved in this browser? The default patterns load next time.")) { forgetProject(); location.reload(); }
});
requestAnimationFrame(update);
if (location.hash === "#autostart") {                         // (headless checks: start, play)
    $("start").click();
    const wait = setInterval(() => { if (mapper) { clearInterval(wait); handle(ev(C.play(0), press(true))); handle(ev(C.play(0), press(false))); } }, 50);
}
