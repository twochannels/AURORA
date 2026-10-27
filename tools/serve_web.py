#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Serve the site with the emulator: python3 tools/serve_web.py [port]  ->  http://localhost:8000/ (the emulator at /emu/)
Web MIDI and AudioWorklet need a secure context: localhost counts. Run web/site/build.sh first."""
import http.server
import socketserver
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "build" / "site"
if not (root / "emu" / "aurora.wasm").exists():
    raise SystemExit("serve_web: no build/site/emu/aurora.wasm (run web/site/build.sh)")
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000


class H(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript", ".mjs": "text/javascript"}

    def __init__(self, *a, **k):
        super().__init__(*a, directory=str(root), **k)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


class S(http.server.ThreadingHTTPServer):
    def server_bind(self):                   # HTTPServer's does a reverse DNS lookup of the address, which can hang
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.socket.getsockname()[1]


srv = S(("127.0.0.1", port), H)
print(f"AURORA site: http://localhost:{port}/  emulator: http://localhost:{port}/emu/  (Ctrl-C stops)", flush=True)
srv.serve_forever()
