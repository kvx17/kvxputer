"""Wall of Flipper — Flipper Zero advertisements."""

from __future__ import annotations

import sys
from typing import Dict

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import run_until_quit


def _clear() -> None:
    sys.stdout.write("\033[H\033[J")
    sys.stdout.flush()


def run(session: PcConnectSession) -> str:
    hits: Dict[str, dict] = {}

    def redraw() -> None:
        _clear()
        print("Wall of Flipper")
        print("-" * 64)
        rows = sorted(hits.values(), key=lambda d: d.get("rssi", -999), reverse=True)
        if not rows:
            print("(scanning…)")
        else:
            for d in rows:
                name = (d.get("name") or "Flipper")[:24]
                print(
                    f"{d.get('rssi', 0):5d}dBm  {d.get('mac', ''):<17}  {name:<24}  "
                    f"{d.get('services') or ''}"
                )
        print(f"\n{len(hits)} hit(s) — q / Esc = back to menu")
        sys.stdout.flush()

    def on_event(ev: dict) -> None:
        if ev.get("evt") != "flipper":
            return
        mac = ev.get("mac") or ""
        if mac:
            hits[mac] = ev
            redraw()

    redraw()
    return run_until_quit(session, APP_COMMANDS["ble.flipper"], on_event)
