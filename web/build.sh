#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the TRIGDECK engine (Sources/TrigCore, unchanged) to WebAssembly: web/trigcore.wasm.
# Needs Homebrew's llvm, lld, wasi-libc and wasi-runtimes:  brew install llvm lld wasi-libc wasi-runtimes
set -e
cd "$(dirname "$0")/.."
CLANG=/opt/homebrew/opt/llvm/bin/clang
SYSROOT=/opt/homebrew/share/wasi-sysroot
BUILTINS=$(ls /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1/libclang_rt.builtins.a)
EXPORTS=""
for f in td_play td_pattern_queue td_step_toggle td_set_tempo td_crossfader td_deck_level td_mute td_fill \
         td_perf td_pack_load td_pack_count td_set_length_repeat td_trigger td_release td_project_new; do
    EXPORTS="$EXPORTS -Wl,--export=$f"
done
PATH="/opt/homebrew/opt/lld/bin:$PATH" "$CLANG" --target=wasm32-wasip1 --sysroot="$SYSROOT" \
    -O2 -ffast-math -fno-finite-math-only -mexec-model=reactor \
    -ISources/TrigCore/include -ISources/TrigCore \
    Sources/TrigCore/td_*.c web/td_web.c "$BUILTINS" \
    -Wl,--growable-table -Wl,--max-memory=268435456 $EXPORTS \
    -o web/trigcore.wasm
ls -l web/trigcore.wasm | awk '{print "built web/trigcore.wasm, " $5 " bytes"}'
