#!/usr/bin/env python3
"""Serves web/src with a fake /api/v1 so the UI can be developed without the camera.

Usage: python3 tools/mock_server.py [port]   (login: admin / admin)
"""
import json
import sys
import time
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web" / "src"
SAMPLE = Path(__file__).resolve().parent / "sample.jpg"
STARTED = time.time()

state = {
    "session": None,
    "light": False,
    "camera": {
        "resolution": "svga", "jpeg_quality": 12, "rotation": 0, "hmirror": False, "vflip": False,
        "brightness": 0, "contrast": 0, "saturation": 0, "auto_exposure": True, "aec_dsp": True,
        "ae_level": 0, "manual_exposure": 300, "auto_gain": True, "gain_ceiling": 4,
        "manual_gain": 0, "auto_white_balance": True, "lens_correction": True,
        "flash_on_capture": False, "flash_lead_ms": 200, "flash_duty_pct": 80,
    },
    "connect": {"token_set": True, "hostname": "connect.prusa3d.com", "default_interval_s": 30,
                "fingerprint": "MjQ4MTc5MTgzMTY4NjkxMzIgMDA6MDA6MDA6MDA6MDA6MDA="},
    "network": {"ssid": "nietogiardinieri", "password_set": True, "hostname": "prusa-esp32cam"},
    "seq": 1,
    "uploads_ok": 42,
}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, fmt, *args):
        sys.stderr.write("mock: " + fmt % args + "\n")

    # ---- helpers ----
    def send_json(self, payload, status=HTTPStatus.OK, headers=None):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        length = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(length) or b"{}")

    def authed(self):
        cookie = self.headers.get("Cookie") or ""
        return state["session"] is not None and f"sid={state['session']}" in cookie

    def api(self, method):
        path = self.path.split("?")[0][len("/api/v1/"):]
        if path == "login" and method == "POST":
            body = self.read_json()
            if body.get("username") == "admin" and body.get("password") == "admin":
                state["session"] = "mock"
                return self.send_json({"ok": True}, headers={
                    "Set-Cookie": "sid=mock; HttpOnly; SameSite=Strict; Path=/"})
            return self.send_json({"error": "bad credentials"}, HTTPStatus.UNAUTHORIZED)
        if not self.authed():
            return self.send_json({"error": "login required"}, HTTPStatus.UNAUTHORIZED)

        if path == "status":
            return self.send_json({
                "version": "0.1.0-mock", "uptime_s": int(time.time() - STARTED) + 3600,
                "light": state["light"],
                "connect": {"state": "online", "interval_s": 30, "last_upload_ago_s": 12,
                            "uploads_ok": state["uploads_ok"], "uploads_failed": 1,
                            "last_error": None, "frame_seq": state["seq"], "frame_age_s": 12},
                "wifi": {"ssid": "nietogiardinieri", "ip": "192.168.0.61", "rssi": -52},
                "heap": {"internal_free": 101_000, "internal_min": 72_000, "psram_free": 3_100_000},
                "resets": [{"reason": "power-on", "uptime_s": 0},
                           {"reason": "connectivity lost", "uptime_s": 7200}],
            })
        if path == "snapshot.jpg":
            data = SAMPLE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if path == "logs":
            body = b"00:00:01 I boot: PrusaCam byClaude 0.1.0-mock\n00:00:03 I wifi: connected\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            return self.wfile.write(body)
        if path.startswith("config/"):
            section = path.split("/", 1)[1]
            if method == "PUT":
                changes = self.read_json()
                for secret, flag in (("token", "token_set"), ("password", "password_set")):
                    if secret in changes:
                        state[section][flag] = bool(changes.pop(secret))
                state[section].update(changes)
            return self.send_json(state[section])
        if path == "wifi/scan":
            return self.send_json([
                {"ssid": "nietogiardinieri", "rssi": -48, "channel": 6, "auth": "WPA2"},
                {"ssid": "<script>alert(1)</script>", "rssi": -80, "channel": 11, "auth": "WPA2"},
            ])
        if path == "snapshot" and method == "POST":
            state["seq"] += 1
            state["uploads_ok"] += 1
            return self.send_json({"ok": True})
        if path == "light" and method == "POST":
            state["light"] = bool(self.read_json().get("on"))
            return self.send_json({"ok": True})
        if path in ("reboot", "logout") and method == "POST":
            if path == "logout":
                state["session"] = None
            return self.send_json({"ok": True})
        return self.send_json({"error": "not found"}, HTTPStatus.NOT_FOUND)

    def do_GET(self):
        if self.path.startswith("/api/v1/"):
            return self.api("GET")
        return super().do_GET()

    def do_POST(self):
        return self.api("POST")

    def do_PUT(self):
        return self.api("PUT")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"PrusaCam mock UI on http://localhost:{port}  (admin / admin)")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
