#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Generate the headers SLOOP's firmware is built with (fonts, icons, DSP tables, samples, drum kits, FM6 patches,
# logo) into vendor/sloop/build/gen, with SLOOP's own generators (as its tools/build.py does). Needs Python 3 with
# Pillow. Run once after cloning; scripts/build_app.sh runs it when a header is missing.
set -e
cd "$(dirname "$0")/.."
TOOLS=vendor/sloop/tools
GEN=vendor/sloop/build/gen
mkdir -p "$GEN"
python3 -I "$TOOLS/gen_font.py" "$GEN/felucca_font.h"
python3 -I "$TOOLS/gen_icons.py" "$GEN/felucca_icons.h"
python3 -I "$TOOLS/gen_tables.py" "$GEN/felucca_tables.h"
python3 -I "$TOOLS/gen_samples.py" "$GEN/felucca_samples.h"
python3 -I "$TOOLS/gen_drumkits.py" "$GEN/felucca_drumkits.h"
python3 -I "$TOOLS/gen_fm6_patches.py" "$GEN/felucca_fm6.h"
python3 -I "$TOOLS/gen_logo.py" "$GEN/sloop_logo.h"
