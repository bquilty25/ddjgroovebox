// SPDX-License-Identifier: GPL-3.0-only
// The few system calls the engine's C library imports (wasi_snapshot_preview1): a clock (tap tempo) and file stubs
// (its text formatting links them; nothing is ever written). memory: () => the instance's WebAssembly.Memory.
export function wasiImports(memory) {
    const now = () => (globalThis.performance ? performance.now() : Date.now()) * 1e6;   // ns
    return {
        wasi_snapshot_preview1: {
            clock_time_get(_id, _precision, out) {
                new DataView(memory().buffer).setBigUint64(out, BigInt(Math.round(now())), true);
                return 0;
            },
            fd_write(_fd, _iovs, _n, written) { new DataView(memory().buffer).setUint32(written, 0, true); return 0; },
            fd_close() { return 0; },
            fd_seek() { return 70; },                          // (ESPIPE: not seekable)
        },
    };
}

/** Instantiate the engine synchronously from its bytes (allowed in workers and worklets, and for Node). */
export function instantiate(bytes) {
    let instance;
    const imports = wasiImports(() => instance.exports.memory);
    instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), imports);
    instance.exports._initialize();
    return instance.exports;
}

/** Instantiate asynchronously (the page's main thread: synchronous compiling is limited there). */
export async function instantiateAsync(bytes) {
    let instance;
    const imports = wasiImports(() => instance.exports.memory);
    ({ instance } = await WebAssembly.instantiate(bytes, imports));
    instance.exports._initialize();
    return instance.exports;
}

/** A project's bytes into the engine (0: loaded). Careful with the order: tdw_project_buf can allocate and so grow
 * the memory, which detaches any view of the old buffer; the view is made after it. */
export function loadProject(x, bytes) {
    if (!bytes || bytes.length > x.tdw_project_size()) return -1;
    const at = x.tdw_project_buf();
    new Uint8Array(x.memory.buffer, at, bytes.length).set(bytes);
    return x.tdw_project_load(bytes.length);
}

/** The engine's project as bytes (a copy), or null */
export function saveProject(x) {
    const n = x.tdw_project_save();
    if (n <= 0) return null;
    const at = x.tdw_project_buf();
    return new Uint8Array(x.memory.buffer, at, n).slice();
}

/** A C string at ptr in the instance's memory */
export function cString(x, ptr) {
    if (!ptr) return "";
    const m = new Uint8Array(x.memory.buffer, ptr);
    let n = 0;
    while (m[n]) n++;
    return new TextDecoder().decode(m.subarray(0, n));
}
