#!/usr/bin/env python3
"""Compile mac-vendors-export.csv into a compact longest-prefix OUI table.

Binary layout (little-endian):
  magic[4] = b'OUI1'
  version u16 = 1
  reserved u16 = 0
  string_pool_size u32
  section_count u32 = 4
  For each section (12, 24, 28, 36 bits, in that order):
    bit_length u16
    reserved u16
    entry_count u32
  entries for section 12, then 24, then 28, then 36:
    prefix u64   # significant bits in the low end
    vendor_off u32  # offset into string pool (0 = empty)
    flags u8     # bit0 = private
    block_type u8  # 0=-, 1=MA-L, 2=MA-M, 3=MA-S, 4=IAB, 5=CID
    pad u16 = 0
  string pool: concatenated NUL-terminated UTF-8 vendor names (index 0 is "")
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
from pathlib import Path

BLOCK = {"": 0, "-": 0, "MA-L": 1, "MA-M": 2, "MA-S": 3, "IAB": 4, "CID": 5}
BIT_ORDER = (12, 24, 28, 36)


def hex_digits(s: str) -> str:
    return "".join(c for c in s.upper() if c in "0123456789ABCDEF")


def parse_prefix(raw: str) -> tuple[int, int] | None:
    """Return (prefix_bits, bit_length) or None if unusable."""
    raw = raw.strip()
    if not raw:
        return None
    bits = 0
    if "/2:8" in raw:
        bits = 28
        raw = raw.split("/2:8", 1)[0]
    elif "/3:6" in raw:
        bits = 36
        raw = raw.split("/3:6", 1)[0]
    hx = hex_digits(raw)
    if not hx:
        return None
    if bits == 0:
        n = len(hx)
        if n == 3:
            bits = 12
        elif n == 6:
            bits = 24
        elif n == 7:
            bits = 28
        elif n == 9:
            bits = 36
        else:
            # Fall back by rounding down to a known length.
            if n >= 9:
                bits = 36
                hx = hx[:9]
            elif n >= 7:
                bits = 28
                hx = hx[:7]
            elif n >= 6:
                bits = 24
                hx = hx[:6]
            elif n >= 3:
                bits = 12
                hx = hx[:3]
            else:
                return None
    else:
        need = bits // 4
        if len(hx) < need:
            hx = hx.ljust(need, "0")
        else:
            hx = hx[:need]
    try:
        value = int(hx, 16)
    except ValueError:
        return None
    return value, bits


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path", type=Path)
    ap.add_argument("out_path", type=Path)
    args = ap.parse_args()

    sections: dict[int, list[tuple[int, str, int, int]]] = {b: [] for b in BIT_ORDER}
    with args.csv_path.open(newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        for row in reader:
            parsed = parse_prefix(row.get("Mac Prefix") or "")
            if not parsed:
                continue
            value, bits = parsed
            if bits not in sections:
                continue
            vendor = (row.get("Vendor Name") or "").strip()
            private = 1 if (row.get("Private") or "").strip().lower() == "true" or not vendor else 0
            if not vendor and private:
                private = 1
            bt = BLOCK.get((row.get("Block Type") or "").strip(), 0)
            sections[bits].append((value, vendor, private, bt))

    # Deduplicate by prefix (last wins), then sort.
    for bits in BIT_ORDER:
        by_pref: dict[int, tuple[int, str, int, int]] = {}
        for value, vendor, private, bt in sections[bits]:
            by_pref[value] = (value, vendor, private, bt)
        sections[bits] = sorted(by_pref.values(), key=lambda t: t[0])

    # String pool: offset 0 is empty string "".
    pool = bytearray(b"\0")
    offsets: dict[str, int] = {"": 0}

    def intern(s: str) -> int:
        if s in offsets:
            return offsets[s]
        off = len(pool)
        pool.extend(s.encode("utf-8", errors="replace"))
        pool.append(0)
        offsets[s] = off
        return off

    entries_blob = bytearray()
    section_meta: list[tuple[int, int]] = []
    for bits in BIT_ORDER:
        rows = sections[bits]
        section_meta.append((bits, len(rows)))
        for value, vendor, private, bt in rows:
            entries_blob += struct.pack(
                "<QIBBH",
                value,
                intern(vendor),
                private & 0xFF,
                bt & 0xFF,
                0,
            )

    header = bytearray()
    header += b"OUI1"
    header += struct.pack("<HH", 1, 0)
    header += struct.pack("<II", len(pool), len(BIT_ORDER))
    for bits, count in section_meta:
        header += struct.pack("<HHI", bits, 0, count)

    args.out_path.parent.mkdir(parents=True, exist_ok=True)
    args.out_path.write_bytes(bytes(header) + bytes(entries_blob) + bytes(pool))
    total = sum(c for _, c in section_meta)
    print(
        f"wrote {args.out_path} ({args.out_path.stat().st_size} bytes, {total} prefixes)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
