"""BLE Scan — live advertisement table."""

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
        print("BLE Scan")
        print("-" * 72)
        rows = sorted(devices.values(), key=lambda d: d.get("rssi", -999), reverse=True)[:40]
        print(f"{'RSSI':>5}  {'MAC':<17}  {'Name':<22}  Addr    Services")
        for d in rows:
            name = (d.get("name") or "")[:22]
            print(
                f"{d.get('rssi', 0):5d}  {d.get('mac', ''):<17}  {name:<22}  "
                f"{(d.get('addr_type') or '')[:6]:<6}  {(d.get('services') or '')[:20]}"
            )
        print(f"\n{len(devices)} devices — q / Esc = back to menu")
        sys.stdout.flush()

    def on_event(ev: dict) -> None:
        if ev.get("evt") != "ble":
            return
        mac = ev.get("mac") or ""
        if not mac:
            return
        devices[mac] = ev
        redraw()

    redraw()
    return run_until_quit(session, APP_COMMANDS["ble.scan"], on_event)
