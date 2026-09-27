#!/usr/bin/env python3
"""Packs web/src into one gzip bundle embedded in the firmware (parsed by core::WebBundle).

Usage: pack_web.py <src_dir> <output.bin>
"""
import gzip
import hashlib
import struct
import sys
from pathlib import Path

TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
}


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(p for p in src.rglob("*") if p.is_file() and p.suffix in TYPES)
    if len(files) > 32:
        sys.exit("too many web assets (max 32)")

    entries = []
    digest = hashlib.sha256()
    for path in files:
        rel = path.relative_to(src).as_posix().encode()
        ctype = TYPES[path.suffix].encode()
        data = gzip.compress(path.read_bytes(), compresslevel=9, mtime=0)  # reproducible
        digest.update(rel + b"\0" + data)
        entries.append(struct.pack("<B", len(rel)) + rel + struct.pack("<B", len(ctype)) + ctype
                       + struct.pack("<I", len(data)) + data)

    etag = digest.hexdigest()[:16].encode()
    blob = b"PCWB" + bytes([1]) + etag + bytes([len(entries)]) + b"".join(entries)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"web bundle: {len(entries)} assets, {len(blob)} bytes, etag {etag.decode()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
