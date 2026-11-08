// SPDX-License-Identifier: GPL-3.0-only
// The TRIGDECK engine on the browser's audio thread. The page sends calls ({call, args}); 20 times a second this sends
// back the state (pattern, tempo, playheads, levels, the current pattern's trigs).
class TrigdeckProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.blocks = 0;
        try {
            const module = new WebAssembly.Module(options.processorOptions.wasm);   // (synchronous: allowed off the main thread)
            this.x = new WebAssembly.Instance(module, {}).exports;
            this.x._initialize();
            this.x.tdw_init();
        } catch (err) {
            this.port.postMessage({ error: String(err) });
            this.x = null;
            return;
        }
        this.port.onmessage = (e) => {
            const { call, args } = e.data;
            if (typeof this.x[call] === "function") this.x[call](...(args || []));
            if (e.data.state) this.sendState();
        };
        this.sendState();
    }

    sendState() {
        const x = this.x;
        x.tdw_state();
        const tracks = [];
        for (let t = 0; t < 8; t++) {
            tracks.push({ step: x.tdw_step(t), level: x.tdw_level(t), length: x.tdw_length(t), machine: x.tdw_machine(t), muted: x.tdw_muted(t) });
        }
        const grid = new Float32Array(x.memory.buffer, x.tdw_grid(), 8 * 64).slice();
        this.port.postMessage({
            state: { pattern: x.tdw_pattern(), queued: x.tdw_queued(), bpm: x.tdw_bpm(), playing: x.tdw_playing(), tracks },
            grid,
        });
    }

    process(_inputs, outputs) {
        if (!this.x) return true;
        const out = outputs[0];
        const n = out[0].length;                                       // 128
        const p = this.x.tdw_render(n);
        const lr = new Float32Array(this.x.memory.buffer, p, n * 2);
        const L = out[0], R = out[1] || out[0];
        for (let i = 0; i < n; i++) { L[i] = lr[2 * i]; R[i] = lr[2 * i + 1]; }
        if (++this.blocks % 19 === 0) this.sendState();                // (~20 a second at 48 kHz)
        return true;
    }
}
registerProcessor("trigdeck", TrigdeckProcessor);
