#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Serve TRIGDECK's web version on http://localhost:8000/web/ (Web MIDI and AudioWorklets need http, and localhost
# counts as a secure origin). Builds the engine first if it is missing.
set -e
cd "$(dirname "$0")/.."
[ -f web/trigcore.wasm ] || sh web/build.sh
echo "open http://localhost:8000/web/ in Chrome or Edge"
exec python3 -m http.server 8000 --bind 127.0.0.1
