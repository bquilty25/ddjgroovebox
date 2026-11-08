// SPDX-License-Identifier: GPL-3.0-only
// The TRIGDECK engine on the browser's audio thread. The page sends engine calls ({ calls: [[name, ...args]] }) and
// save requests; this renders, and ~30 times a second posts a snapshot of the state (src/engine.js reads it): the
// light part every time, the heavy part (steps, locks, scenes) when the pattern changed.
import { instantiate, loadProject, saveProject } from "./src/wasi.js";

class TrigdeckProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.blocks = 0;
        this.hash = null;
        try {
            this.x = instantiate(options.processorOptions.wasm);
            this.x.tdw_init();
            const saved = options.processorOptions.project;
            if (saved && loadProject(this.x, saved) !== 0) {
                this.port.postMessage({ error: "the saved project could not be read: the default patterns instead" });
            }
        } catch (err) {
            this.port.postMessage({ error: "the engine did not start: " + String(err) });
            this.x = null;
            return;
        }
        this.port.onmessage = (e) => this.receive(e.data);
        this.post(true);
    }

    receive(m) {
        if (!this.x) return;
        for (const [name, ...args] of m.calls || []) {
            if (typeof this.x[name] === "function") this.x[name](...args);
        }
        if (m.save) {
            const project = saveProject(this.x);
            if (project) this.port.postMessage({ project });
        }
        if (m.calls?.length) this.post(false);                // (at once after an edit: the screen follows)
    }

    post(forceHeavy) {
        const x = this.x;
        const hash = x.tdw_pattern_hash();
        const full = forceHeavy || hash !== this.hash;
        this.hash = hash;
        const at = x.tdw_snapshot(full ? 1 : 0), len = x.tdw_snapshot_len(full ? 1 : 0);   // (calls before the view)
        const all = new Int32Array(x.memory.buffer, at, len);
        const light = all.slice(0, x.tdw_snapshot_len(0));
        const msg = { light };
        if (full) msg.heavy = all.slice();
        this.port.postMessage(msg);
    }

    process(_inputs, outputs) {
        if (!this.x) return true;
        const out = outputs[0];
        const n = out[0].length;                                       // 128
        const p = this.x.tdw_render(n);
        const lr = new Float32Array(this.x.memory.buffer, p, n * 2);
        const L = out[0], R = out[1] || out[0];
        for (let i = 0; i < n; i++) { L[i] = lr[2 * i]; R[i] = lr[2 * i + 1]; }
        if (++this.blocks % 12 === 0) this.post(false);                // (~31 a second at 48 kHz)
        return true;
    }
}
registerProcessor("trigdeck", TrigdeckProcessor);
