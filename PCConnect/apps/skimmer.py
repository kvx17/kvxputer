"""Skimmer Detector — BLE GAP names / MAC prefixes."""

from __future__ import annotations

import sys
from typing import Dict

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import run_until_quit


def _clear() -> None:
    sys.stdout.write("\033[H\033[J")
    sys.stdout.flush()


def run(session: PcConnectSession) -> str:
    devices: Dict[str, dict] = {}

    def redraw() -> None:
        _clear()
        print("Skimmer Detector")
        print("-" * 64)
        rows = list(devices.values())
        rows.sort(key=lambda d: (not d.get("matched"), -int(d.get("rssi", -999))))
        matched = sum(1 for d in rows if d.get("matched"))
        if not rows:
            print("(scanning…)")
        else:
            for d in rows[:50]:
                flag = "! " if d.get("matched") else "  "
                name = (d.get("name") or d.get("mac") or "")[:24]
                rule = d.get("rule") or ""
                print(
                    f"{flag}{d.get('rssi', 0):5d}dBm  {d.get('mac', ''):<17}  "
                    f"{name:<24}  {rule}"
                )
        print(f"\n{matched} match(es) / {len(devices)} devices — q / Esc = back to menu")
        sys.stdout.flush()

    def on_event(ev: dict) -> None:
        if ev.get("evt") != "skimmer":
            return
        mac = ev.get("mac") or ""
        if mac:
            devices[mac] = ev
            redraw()

    redraw()
    return run_until_quit(session, APP_COMMANDS["ble.skimmer"], on_event)
