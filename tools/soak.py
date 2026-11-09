#!/usr/bin/env python3
"""Soak test: polls a camera's /api/v1/status for hours and checks the reliability goals.

Usage:
  python3 tools/soak.py http://prusacam.local --hours 48 [--user admin] [--interval 60]

The password is read from PRUSACAM_PASSWORD or asked interactively (never pass it as an
argument: it would end up in the shell history). Every sample is appended to a CSV
(soak-<host>-<date>.csv) so an interrupted run can still be analysed with --report <csv>.

Goal (docs/ARCHITECTURE.md §1): >= 99 % successful uploads over 48 h, no avoidable reboots.
Only standard library: runs anywhere Python 3.9+ is installed.
"""
import argparse
import csv
import getpass
import http.cookiejar
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from urllib.parse import urlparse

FIELDS = ["time", "uptime_s", "state", "uploads_ok", "uploads_failed", "capture_failures",
          "last_error", "rssi", "internal_free", "internal_min", "psram_free", "reachable"]


class Camera:
    def __init__(self, base, user, password):
        self.base = base.rstrip("/")
        self.user, self.password = user, password
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))

    def _request(self, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(f"{self.base}/api/v1/{path}", data=data,
                                     headers={"Content-Type": "application/json"} if data else {})
        with self.opener.open(req, timeout=15) as res:
            return json.loads(res.read() or b"{}")

    def login(self):
        self._request("login", {"username": self.user, "password": self.password})

    def status(self):
        try:
            return self._request("status")
        except urllib.error.HTTPError as e:
            if e.code != 401:  # session lost (e.g. the camera rebooted): log in again
                raise
            self.login()
            return self._request("status")


def sample(cam):
    row = {"time": datetime.now().isoformat(timespec="seconds"), "reachable": 0}
    try:
        s = cam.status()
    except (urllib.error.URLError, OSError, ValueError) as e:
        row["last_error"] = f"unreachable: {e}"
        return row
    c, w, h = s.get("connect", {}), s.get("wifi", {}), s.get("heap", {})
    row.update(reachable=1, uptime_s=s.get("uptime_s"), state=c.get("state"),
               uploads_ok=c.get("uploads_ok"), uploads_failed=c.get("uploads_failed"),
               capture_failures=c.get("capture_failures"), last_error=c.get("last_error") or "",
               rssi=w.get("rssi"), internal_free=h.get("internal_free"),
               internal_min=h.get("internal_min"), psram_free=h.get("psram_free"))
    return row


def report(rows):
    """Counters reset on reboot, so totals are accumulated per boot segment."""
    up = [r for r in rows if str(r.get("reachable")) == "1"]
    if not up:
        print("no successful samples")
        return 1
    ok = failed = captures = 0
    reboots = []
    prev = None
    for r in up:
        cur = {k: int(r[k] or 0) for k in ("uptime_s", "uploads_ok", "uploads_failed",
                                           "capture_failures")}
        if prev is None:  # first sample is the baseline: count only what happens during the soak
            prev = cur
            continue
        if cur["uptime_s"] < prev["uptime_s"]:
            reboots.append(r["time"])
            prev = dict.fromkeys(cur, 0)  # new boot: the device counters restarted from zero
        ok += cur["uploads_ok"] - prev["uploads_ok"]
        failed += cur["uploads_failed"] - prev["uploads_failed"]
        captures += cur["capture_failures"] - prev["capture_failures"]
        prev = cur

    hours = (datetime.fromisoformat(rows[-1]["time"]) -
             datetime.fromisoformat(rows[0]["time"])).total_seconds() / 3600
    total = ok + failed
    rate = 100.0 * ok / total if total else 0.0
    unreachable = len(rows) - len(up)
    rssi = [int(r["rssi"]) for r in up if r.get("rssi") not in (None, "")]
    heap_min = min(int(r["internal_min"]) for r in up if r.get("internal_min") not in (None, ""))
    states = {}
    for r in up:
        states[r["state"]] = states.get(r["state"], 0) + 1

    print(f"duration          {hours:.1f} h, {len(rows)} samples ({unreachable} unreachable)")
    print(f"uploads           {ok} ok / {failed} failed  -> {rate:.2f} % success")
    print(f"capture failures  {captures}")
    print(f"reboots           {len(reboots)}" + (f"  at {', '.join(reboots)}" if reboots else ""))
    if rssi:
        print(f"RSSI              {min(rssi)} .. {max(rssi)} dBm (avg {sum(rssi) / len(rssi):.0f})")
    print(f"internal heap min {heap_min // 1024} KB")
    print("connect states    " + ", ".join(f"{k}: {v}" for k, v in sorted(states.items())))
    passed = rate >= 99.0 and not reboots and hours >= 47.5
    print("RESULT            " + ("PASS" if passed else "FAIL") +
          " (goal: >= 99 % over 48 h, no reboots)")
    return 0 if passed else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("url", nargs="?", help="camera base URL, e.g. http://prusacam.local")
    ap.add_argument("--hours", type=float, default=48)
    ap.add_argument("--interval", type=int, default=60, help="seconds between samples")
    ap.add_argument("--user", default=os.environ.get("PRUSACAM_USER", "admin"))
    ap.add_argument("--report", metavar="CSV", help="only summarise an existing CSV")
    args = ap.parse_args()

    if args.report:
        with open(args.report, newline="") as f:
            return report(list(csv.DictReader(f)))
    if not args.url:
        ap.error("url is required unless --report is given")

    password = os.environ.get("PRUSACAM_PASSWORD") or getpass.getpass(f"password for {args.user}: ")
    cam = Camera(args.url, args.user, password)
    cam.login()
    host = urlparse(args.url).hostname
    path = f"soak-{host}-{datetime.now():%Y%m%d-%H%M}.csv"
    print(f"sampling every {args.interval} s for {args.hours} h -> {path} (Ctrl-C stops and reports)")

    rows = []
    end = time.monotonic() + args.hours * 3600
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        try:
            while time.monotonic() < end:
                row = sample(cam)
                writer.writerow(row)
                f.flush()
                rows.append({k: ("" if row.get(k) is None else row.get(k)) for k in FIELDS})
                print(f"{row['time']}  {row.get('state', '-'):>14}  ok={row.get('uploads_ok', '-')} "
                      f"failed={row.get('uploads_failed', '-')} rssi={row.get('rssi', '-')}",
                      flush=True)
                time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\ninterrupted")
    return report(rows)


if __name__ == "__main__":
    sys.exit(main())
