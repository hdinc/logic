#!/usr/bin/env python3
# serves the web build with the headers threads need (cross-origin isolation)
# usage: web/serve.py [folder, default build-web] [port, default 8000]

import http.server
import sys
from functools import partial


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


Handler.extensions_map[".wasm"] = "application/wasm"

folder = sys.argv[1] if len(sys.argv) > 1 else "build-web"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
print(f"http://localhost:{port}/")
http.server.ThreadingHTTPServer(("", port), partial(Handler, directory=folder)).serve_forever()
