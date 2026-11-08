# SPDX-License-Identifier: GPL-3.0-only
# A static file server for TRIGDECK's web version that tells browsers not to cache: after a rebuild, the next load
# always gets the new engine, worklet and modules (Firefox kept old AudioWorklet modules with the plain http.server).
# Usage: python3 web/serve.py [port]   (from the repo root; serve.sh runs it)
import http.server
import sys


class NoCache(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".js": "text/javascript", ".mjs": "text/javascript", ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8642
http.server.ThreadingHTTPServer(("127.0.0.1", port), NoCache).serve_forever()
