#!/usr/bin/env python3
"""Bridge to the Blender MCP extension (lab.blender.org) over its TCP port.

The path through the MCP server is the normal case. If that fails (CONNECT_TIMEOUT
at session start), this script talks to the same endpoint directly.

Protocol: one JSON message per direction, terminated with a NUL byte.

    ->  {"type": "execute", "code": "...", "strict_json": false}\0
    <-  {"status": "ok", "result": {...}, "stdout": "..."}\0

The code runs in the Blender main thread. It MUST set a variable `result`,
and it must be a dict, otherwise the extension answers with an error.

Usage:
    python tools/blender_bridge.py script.py
    python tools/blender_bridge.py -c "import bpy; result = {'v': bpy.app.version_string}"
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time

HOST = "127.0.0.1"
PORT = 9876
RECV_SLICE = 5.0        # seconds per recv attempt
PATIENCE = 240.0        # total patience; long render jobs need it


class BlenderError(RuntimeError):
    pass


def execute(code: str, strict_json: bool = False, patience: float = PATIENCE) -> dict:
    """Runs `code` in Blender and returns the extension's answer."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(RECV_SLICE)
    try:
        sock.connect((HOST, PORT))
    except OSError as ex:
        raise BlenderError(
            "No Blender reachable on %s:%d (%s). Is Blender running, and is the "
            "MCP extension active?" % (HOST, PORT, ex)
        ) from ex

    request = {"type": "execute", "code": code, "strict_json": strict_json}
    sock.sendall(json.dumps(request).encode("utf-8") + b"\x00")

    buf = b""
    deadline = time.time() + patience
    try:
        while time.time() < deadline:
            try:
                chunk = sock.recv(1 << 20)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            if b"\x00" in buf:
                head = buf.split(b"\x00", 1)[0]
                return json.loads(head.decode("utf-8"))
    finally:
        sock.close()

    raise BlenderError(
        "No complete answer within %.0fs. Received: %r" % (patience, buf[:400])
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Run Python code in a running Blender instance")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("file", nargs="?", help="script file that runs in Blender")
    src.add_argument("-c", "--code", help="code directly on the command line")
    ap.add_argument("--strict-json", action="store_true",
                    help="report non-serialisable values in `result` as errors "
                         "instead of replacing them via repr()")
    ap.add_argument("--patience", type=float, default=PATIENCE,
                    help="seconds to wait for the answer (default: %(default)s)")
    args = ap.parse_args(argv)

    code = args.code if args.code else open(args.file, encoding="utf-8").read()

    try:
        response = execute(code, strict_json=args.strict_json, patience=args.patience)
    except BlenderError as ex:
        print(str(ex), file=sys.stderr)
        return 2

    print(json.dumps(response, indent=1, ensure_ascii=False))
    return 0 if response.get("status") == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
