#!/usr/bin/env python3
"""Serves web/src with a fake /api/v1 so the UI can be developed without the camera.

Usage: python3 tools/mock_server.py [port]
First load asks to create a user, like a fresh device.
"""
import json
import os
import sys
import time
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web" / "src"
SAMPLE = Path(__file__).resolve().parent / "sample.jpg"
STARTED = time.time()

state = {
    "session": None,
    "user": None,  # first run: the UI must create it (set MOCK_USER=1 env to skip)
    "light": False,
    "camera": {
        "resolution": "svga", "jpeg_quality": 12, "rotation": 0, "hmirror": False, "vflip": False,
        "brightness": 0, "contrast": 0, "saturation": 0, "auto_exposure": True, "aec_dsp": True,
        "ae_level": 0, "manual_exposure": 300, "auto_gain": True, "gain_ceiling": 4,
        "manual_gain": 0, "auto_white_balance": True, "lens_correction": True,
        "flash_on_capture": False, "flash_lead_ms": 200, "flash_duty_pct": 80,
        "zoom_pct": 100, "pan_x": 0, "pan_y": 0, "live_jpeg_quality": 30,
        "sharpness": 0, "denoise": 0, "wb_mode": 0, "supports_sharpness": True,
        "sensor": "OV3660", "max_resolution": "qxga", "max_zoom_pct": 256,  # read-only
    },
    "connect": {"token_set": True, "hostname": "connect.prusa3d.com",
                "fingerprint": "MjQ4MTc5MTgzMTY4NjkxMzIgMDA6MDA6MDA6MDA6MDA6MDA="},
    "network": {"ssid": "nietogiardinieri", "password_set": True, "hostname": "prusa-esp32cam",
                "tx_power_dbm": 20},
    "coredump": {"size": 65536 - 4096, "task": "httpd", "pc": "0x400d1234",
                 "reason": "LoadProhibited"},  # set to None to simulate a clean device
    "printer": {"host": "192.168.0.50", "user": "maker", "password_set": True,
                "upload_only_while_printing": True},
    "mqtt": {"uri": "mqtt://192.168.0.10:1883", "user": "ha", "password_set": True,
             "publish_images": True, "connected": True},
    "device": {"status_led": True},
    "api_token": None,
    "storage": {"log_to_sd": True, "timelapse": "printing", "timelapse_interval_s": 60},
    "sd": {  # fake card: folders are dicts, files are sizes
        "logs": {"2026-11-08.log": 18_200, "2026-11-09.log": 4_096},
        "timelapse": {"2026-11-09_1200_Benchy": {"120005.jpg": 61_000, "120105.jpg": 60_500}},
    },
    "seq": 1,
    "uploads_ok": 42,
}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def end_headers(self):
        if not self.path.startswith("/api/"):
            self.send_header("Cache-Control", "no-store")  # always serve the latest UI while developing
        super().end_headers()

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
        cookie = {"Set-Cookie": "sid=mock; HttpOnly; SameSite=Strict; Path=/"}
        if path == "session":
            return self.send_json({"authenticated": self.authed(),
                                   "setup_required": state["user"] is None,
                                   "setup_window_open": True})
        if path == "setup" and method == "POST":
            body = self.read_json()
            if state["user"] is not None:
                return self.send_json({"error": "initial setup is closed"}, HTTPStatus.UNAUTHORIZED)
            if len(body.get("password", "")) < 8:
                return self.send_json({"error": "at least 8 characters"}, HTTPStatus.BAD_REQUEST)
            state["user"] = (body["username"], body["password"])
            state["session"] = "mock"
            return self.send_json({"ok": True}, headers=cookie)
        if path == "login" and method == "POST":
            body = self.read_json()
            if state["user"] == (body.get("username"), body.get("password")):
                state["session"] = "mock"
                return self.send_json({"ok": True}, headers=cookie)
            return self.send_json({"error": "wrong username or password"}, HTTPStatus.UNAUTHORIZED)
        if not self.authed():
            # Read-only API token (ADR-0016): Bearer header or ?token=, only for watching.
            token = (self.headers.get("Authorization") or "").removeprefix("Bearer ") or \
                parse_qs(urlparse(self.path).query).get("token", [""])[0]
            read_only = method == "GET" and path in ("status", "snapshot.jpg", "stream")
            if not (read_only and state["api_token"] and token == state["api_token"]):
                return self.send_json({"error": "login required"}, HTTPStatus.UNAUTHORIZED)

        if path == "status":
            return self.send_json({
                "version": "0.1.0-mock", "uptime_s": int(time.time() - STARTED) + 3600,
                "light": state["light"],
                "connect": {"state": "online", "interval_s": 30, "last_upload_ago_s": 12,
                            "uploads_ok": state["uploads_ok"], "uploads_failed": 1,
                            "last_error": None, "frame_seq": state["seq"], "frame_age_s": 12},
                "wifi": {"ssid": "nietogiardinieri", "ip": "192.168.0.61", "rssi": -52},
                "heap": {"internal_free": 101_000, "internal_min": 72_000, "internal_total": 296_000,
                         "psram_free": 1_556_000, "psram_total": 4_192_000},
                "printer": {"reachable": True, "error": "", "phase": "printing",
                            "file": "Benchy 0.2mm PLA.bgcode", "progress": 45.5,
                            "time_left_s": 3120} if state["printer"]["host"] else None,
                "firmware": {"partition": "ota_0", "pending_verify": False, "rolled_back": None},
                "coredump": state["coredump"],
                "resets": [{"reason": "power-on", "uptime_s": 0},
                           {"reason": "connectivity lost", "uptime_s": 7200}],
            })
        if path == "stream":
            data = SAMPLE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace;boundary=prusacamframe")
            self.end_headers()
            try:
                for _ in range(600):  # ~2 min at 5 fps
                    self.wfile.write(b"--prusacamframe\r\nContent-Type: image/jpeg\r\n"
                                     + f"Content-Length: {len(data)}\r\n\r\n".encode() + data + b"\r\n")
                    self.wfile.flush()
                    time.sleep(0.2)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return None
        if path == "snapshot.jpg":
            data = SAMPLE.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if path == "logs/previous":
            body = b"00:41:10 I core: upload ok\n00:41:12 E camera: capture timeout\n00:41:20 W main: watchdog\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            return self.wfile.write(body)
        if path == "logs":
            body = b"00:00:01 I boot: PrusaCam byClaude 0.1.0-mock\n00:00:03 I wifi: connected\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            return self.wfile.write(body)
        if path.startswith("config/"):
            section = path.split("/", 1)[1]
            extra = {}
            if method == "PUT":
                changes = self.read_json()
                for secret, flag in (("token", "token_set"), ("password", "password_set")):
                    if secret in changes:
                        state[section][flag] = bool(changes.pop(secret))
                state[section].update(changes)
                if section == "network":
                    # MOCK_SETUP_AP=1 simulates a camera without a working station.
                    extra = {"reboot_required": True,
                             "rebooting": os.environ.get("MOCK_SETUP_AP") == "1"}
            return self.send_json({**state[section], **extra})
        if path == "wifi/scan":
            return self.send_json([
                {"ssid": "nietogiardinieri", "rssi": -48, "channel": 6, "auth": "WPA2"},
                {"ssid": "<script>alert(1)</script>", "rssi": -80, "channel": 11, "auth": "WPA2"},
            ])
        if path == "preview" and method == "POST":
            state["seq"] += 1  # a fresh local frame, nothing uploaded
            return self.send_json({"ok": True}, HTTPStatus.ACCEPTED)
        if path == "snapshot" and method == "POST":
            state["seq"] += 1
            state["uploads_ok"] += 1
            return self.send_json({"ok": True})
        if path == "light" and method == "POST":
            state["light"] = bool(self.read_json().get("on"))
            return self.send_json({"ok": True})
        if path == "sd":
            return self.send_json({"mounted": True, "total_bytes": 31_910_000_000,
                                   "free_bytes": 30_100_000_000})
        if path in ("sd/list", "sd/file"):
            query = parse_qs(urlparse(self.path).query)
            parts = [p for p in query.get("path", [""])[0].split("/") if p]
            node = state["sd"]
            for p in parts:
                if not isinstance(node, dict) or p not in node:
                    return self.send_json({"error": "no such file"}, HTTPStatus.NOT_FOUND)
                node = node[p]
            if path == "sd/list":
                if not isinstance(node, dict):
                    return self.send_json({"error": "no such folder"}, HTTPStatus.NOT_FOUND)
                return self.send_json({"path": "/".join(parts), "entries": [
                    {"name": k, "dir": isinstance(v, dict), "size": 0 if isinstance(v, dict) else v}
                    for k, v in node.items()]})
            data = SAMPLE.read_bytes() if parts[-1].endswith(".jpg") else b"00:00:01 I mock log\n"
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg" if parts[-1].endswith(".jpg") else "text/plain")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if path == "sd/delete" and method == "POST":
            parts = [p for p in self.read_json().get("path", "").split("/") if p]
            node = state["sd"]
            for p in parts[:-1]:
                node = node.get(p, {})
            if not parts or parts[-1] not in node:
                return self.send_json({"error": "no such file"}, HTTPStatus.NOT_FOUND)
            del node[parts[-1]]
            return self.send_json({"ok": True})
        if path == "ota" and method == "POST":
            if self.headers.get("Content-Type") != "application/octet-stream":
                return self.send_json({"error": "send the .bin as application/octet-stream"},
                                      HTTPStatus.UNSUPPORTED_MEDIA_TYPE)
            length = int(self.headers.get("Content-Length") or 0)
            data = self.rfile.read(length)
            if not data or data[0] != 0xE9:  # ESP image magic
                return self.send_json({"error": "not an ESP32 firmware image"}, HTTPStatus.BAD_REQUEST)
            return self.send_json({"ok": True, "version": "0.2.0-mock"})
        if path == "coredump" and method == "GET":
            if not state["coredump"]:
                return self.send_json({"error": "no core dump stored"}, HTTPStatus.NOT_FOUND)
            data = b"\x7fELF" + bytes(state["coredump"]["size"] - 4)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", 'attachment; filename="coredump-0.1.0-mock.elf"')
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if path == "coredump/erase" and method == "POST":
            state["coredump"] = None
            return self.send_json({"ok": True})
        if path == "api-token":
            if method == "POST":
                state["api_token"] = os.urandom(16).hex()
                return self.send_json({"token": state["api_token"]})
            return self.send_json({"set": state["api_token"] is not None})
        if path == "api-token/revoke" and method == "POST":
            state["api_token"] = None
            return self.send_json({"ok": True})
        if path == "factory-reset" and method == "POST":
            state["user"] = None
            state["session"] = None
            return self.send_json({"ok": True}, HTTPStatus.ACCEPTED)
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
    print(f"PrusaCam mock UI on http://localhost:{port}")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
