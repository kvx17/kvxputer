"""kvx wifi analyzer — channel airtime bars + AP list."""

from __future__ import annotations

import sys
from typing import Dict, Tuple

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import run_until_quit


def _clear() -> None:
    sys.stdout.write("\033[H\033[J")
    sys.stdout.flush()


def run(session: PcConnectSession) -> str:
    loads: Dict[int, Tuple[int, int, int]] = {ch: (0, 0, -128) for ch in range(1, 12)}
    aps: Dict[str, dict] = {}

    def bar(pct: int, width: int = 20) -> str:
        pct = max(0, min(100, pct))
        n = int(round(pct * width / 100))
        return "#" * n + "." * (width - n)

    def redraw() -> None:
        _clear()
        print("kvx wifi analyzer")
        print("-" * 48)
        for ch in range(1, 12):
            load, peak, rssi = loads[ch]
            print(f"Ch{ch:2d} [{bar(load)}] {load:3d}% pk{peak:3d}% {rssi:4d}dBm")
        print("-" * 48)
        rows = sorted(aps.values(), key=lambda a: a.get("rssi", -999), reverse=True)[:16]
        if not rows:
            print("(no APs yet)")
        else:
            for a in rows:
                ssid = a.get("ssid") or ("<hidden>" if a.get("hidden") else "")
                print(
                    f"  ch{a.get('ch', '?'):>2}  {ssid[:20]:<20}  "
                    f"{a.get('rssi', '?'):>4}dBm  {a.get('auth', '')}  {a.get('bssid', '')}"
                )
        print("\nq / Esc = back to menu")
        sys.stdout.flush()

    def on_event(ev: dict) -> None:
        evt = ev.get("evt")
        if evt == "analyzer":
            ch = int(ev.get("ch", 0))
            if 1 <= ch <= 11:
                loads[ch] = (int(ev.get("load", 0)), int(ev.get("peak", 0)), int(ev.get("rssi", -128)))
            redraw()
        elif evt == "ap":
            bssid = ev.get("bssid") or ""
            if bssid:
                aps[bssid] = ev
            redraw()

    redraw()
    return run_until_quit(session, APP_COMMANDS["wifi.analyzer"], on_event)


if __name__ == "__main__":
    print("Launch via pcconnect.py")
