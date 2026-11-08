#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Serve TRIGDECK's web version on http://localhost:8642/web/ (Web MIDI and AudioWorklets need http, and localhost
# counts as a secure origin). Another port: PORT=9000 sh web/serve.sh. Builds the engine first if it is missing.
set -e
cd "$(dirname "$0")/.."
PORT="${PORT:-8642}"
[ -f web/trigcore.wasm ] || sh web/build.sh
if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "port $PORT is in use by another program: try  PORT=9000 sh web/serve.sh"
    exit 1
fi
echo "open http://localhost:$PORT/web/ in Chrome or Edge (Ctrl-C here to stop)"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
