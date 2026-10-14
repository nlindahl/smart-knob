"""Serve the prebuilt editor on localhost (a secure context for Web Serial)."""
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from functools import partial
from pathlib import Path

root = Path(__file__).resolve().parents[1]
print("SmartKnob Studio: http://localhost:8080", flush=True)
ThreadingHTTPServer(("127.0.0.1", 8080), partial(SimpleHTTPRequestHandler, directory=str(root / "web"))).serve_forever()
