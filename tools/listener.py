# SPDX-License-Identifier: MIT
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow"]
# ///
"""A local stand-in for PostHog capture that also receives debug frames.

POST /shot?tick=..&width=..&height=.. saves the Wii display buffer as a PNG.
POST /done creates the file "done", which tells tools/shots.sh to stop.
The listener appends every other POST, with its body, to posts.log.

Usage: listener.py <output dir> [port]
"""

import http.server
import sys
import urllib.parse
from pathlib import Path

from PIL import Image

ARGUMENTS = sys.argv[1:]
OUT = Path(ARGUMENTS[0])
PORT = int(ARGUMENTS[1]) if ARGUMENTS[1:] else 18010


def decode_display_buffer(body: bytes, size: tuple[int, int]) -> Image.Image:
    """Decode a display buffer, which holds two pixels in every four bytes: Y0, Cb, Y1, Cr."""
    luma = bytearray(size[0] * size[1])
    luma[0::2], luma[1::2] = body[0::4], body[2::4]
    blue = bytearray(len(luma))
    blue[0::2] = blue[1::2] = body[1::4]
    red = bytearray(len(luma))
    red[0::2] = red[1::2] = body[3::4]
    planes = [Image.frombytes("L", size, bytes(plane)) for plane in (luma, blue, red)]
    return Image.merge("YCbCr", planes).convert("RGB")


class Handler(http.server.BaseHTTPRequestHandler):
    """Answer every POST as PostHog capture does."""

    def do_POST(self) -> None:
        """Save the request as the module docstring describes."""
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        url = urllib.parse.urlparse(self.path)
        query = {key: values[0] for key, values in urllib.parse.parse_qs(url.query).items()}
        if url.path == "/shot":
            size = (int(query["width"]), int(query["height"]))
            decode_display_buffer(body, size).save(OUT / f"shot-{int(query['tick']):04d}.png")
        elif url.path == "/done":
            (OUT / "done").touch()
        else:
            with (OUT / "posts.log").open("ab") as log:
                log.write(self.path.encode() + b" " + body + b"\n")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(b'{"status":"Ok"}')

    def log_message(self, message_format: str, *args: object) -> None:
        """Print nothing, so the frame capture's output stays readable."""


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    http.server.HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
