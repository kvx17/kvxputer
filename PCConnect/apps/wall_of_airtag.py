"""Wall of Airtag — Apple Find My / AirTag advertisements."""

from __future__ import annotations

import sys
from typing import Dict

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import run_until_quit


def _clear() -> None:
    sys.stdout.write("\033[H\033[J")
    sys.stdout.flush()


def approx_m(rssi: int) -> float:
    d = 10.0 ** ((-59.0 - float(rssi)) / 20.0)
    return max(0.1, min(99.0, d))


def run(session: PcConnectSession) -> str:
    hits: Dict[str, dict] = {}

    def redraw() -> None:
        _clear()
        print("Wall of Airtag")
        print("-" * 64)
        rows = sorted(hits.values(), key=lambda d: d.get("rssi", -999), reverse=True)
        if not rows:
            print("(scanning…)")
        else:
            for d in rows:
                rssi = int(d.get("rssi", -99))
                print(
                    f"{rssi:5d}dBm  ~{approx_m(rssi):4.1f}m  {d.get('mac', ''):<17}  "
                    f"batt={d.get('battery', '?')}  "
                    f"{'sep' if d.get('separated') else 'near'}  "
                    f"key={d.get('key_prefix') or '-'}"
                )
        print(f"\n{len(hits)} tag(s) — q / Esc = back to menu")
        sys.stdout.flush()

    def on_event(ev: dict) -> None:
        if ev.get("evt") != "airtag":
            return
        mac = ev.get("mac") or ""
        if mac:
            hits[mac] = ev
            redraw()

    redraw()
    return run_until_quit(session, APP_COMMANDS["ble.airtag"], on_event)
