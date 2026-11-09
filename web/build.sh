#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the TRIGDECK engine (Sources/TrigCore, unchanged) to WebAssembly: web/trigcore.wasm.
# Needs Homebrew's llvm, lld, wasi-libc and wasi-runtimes:  brew install llvm lld wasi-libc wasi-runtimes
# Elsewhere (e.g. wasi-sdk on Linux, as the Pages workflow does) set CLANG, SYSROOT and BUILTINS.
set -e
cd "$(dirname "$0")/.."
CLANG="${CLANG:-/opt/homebrew/opt/llvm/bin/clang}"
SYSROOT="${SYSROOT:-/opt/homebrew/share/wasi-sysroot}"
BUILTINS="${BUILTINS:-$(ls /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1/libclang_rt.builtins.a)}"
EXPORTS=""
# every engine call the app's actions use (as Sources/DDJGroovebox/TrigEngine.swift applies them)
for f in td_play td_restart td_record td_tap_tempo td_fill td_set_tempo td_set_swing td_nudge \
         td_step_set td_step_toggle td_step_lock td_step_micro td_step_cond td_step_slide td_step_accent \
         td_param_set td_global_set td_set_machine td_set_sample td_trigger td_release td_mute \
         td_pattern_queue td_pattern_copy td_scene_set td_scene_clear td_crossfader td_deck_level td_deck_drive \
         td_scratch td_perf td_capture td_euclid td_rotate td_dice td_autopilot \
         td_pack_load td_pack_revert td_pack_count td_set_length_repeat td_project_new td_midi_config \
         td_deck_play td_deck_pattern td_focus td_on_air td_blend_fader td_deck_eq td_deck_filter; do
    EXPORTS="$EXPORTS -Wl,--export=$f"
done
PATH="/opt/homebrew/opt/lld/bin:$PATH" "$CLANG" --target=wasm32-wasip1 --sysroot="$SYSROOT" \
    -O2 -ffast-math -fno-finite-math-only -mexec-model=reactor \
    -ISources/TrigCore/include -ISources/TrigCore \
    Sources/TrigCore/td_*.c web/td_web.c "$BUILTINS" \
    -Wl,--growable-table -Wl,--max-memory=268435456 $EXPORTS \
    -o web/trigcore.wasm
ls -l web/trigcore.wasm | awk '{print "built web/trigcore.wasm, " $5 " bytes"}'
