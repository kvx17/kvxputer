#!/usr/bin/env python3
"""PC Connect host — discover Cardputer, keep one USB session, launch tools."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Optional

_ROOT = Path(__file__).resolve().parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from protocol import (  # noqa: E402
    APP_LABELS,
    NeedPcConnectApp,
    PcConnectError,
    PcConnectSession,
    discover,
)

from apps import (  # noqa: E402
    ble_scan,
    gpio_console,
    ir,
    jam_detect,
    rfid_read,
    rf_scan,
    skimmer,
    wall_of_airtag,
    wall_of_flipper,
    wifi_analyzer,
)

TOOL_RUNNERS = {
    "wifi.analyzer": wifi_analyzer.run,
    "ble.scan": ble_scan.run,
    "ble.flipper": wall_of_flipper.run,
    "ble.airtag": wall_of_airtag.run,
    "ble.skimmer": skimmer.run,
    "gpio": gpio_console.run,
    "ir.rx": ir.run_rx,
    "rf.rx": rf_scan.run_rx,
    "rf.rssi": rf_scan.run_rssi,
    "rfid.read": rfid_read.run,
    "jam.detect": jam_detect.run,
}


def _no_device_hint() -> None:
    print("\nNo kvxputer device answered hello.")
    print("Checklist:")
    print("  1. Flash current firmware; lsusb shows Espressif 303a:1001")
    print("  2. Cable is data-capable; you are in uucp/dialout")
    print("  3. No other program holds /dev/ttyACM*")
    print("  4. On the Cardputer: USB → PC Connect")


def pick_device(port: Optional[str] = None) -> dict:
    if port:
        print(f"Using port {port}")
        return {
            "port": port,
            "name": "kvxputer",
            "mac": "?",
            "apps": [],
            "ready": False,
            "hello": {},
            "desc": "",
        }

    print("Scanning serial ports for PC Connect…")
    print("(Espressif 303a:**** / ttyACM* preferred; DTR/RTS held low)\n")
    found = discover(verbose=True)
    if not found:
        _no_device_hint()
        sys.exit(1)

    if len(found) == 1:
        d = found[0]
        ready = d.get("ready", True) is not False
        flag = "ready" if ready else "need PC Connect app"
        print(f"\nAuto-selected: {d['name']}  {d['mac']}  {d['port']}  [{flag}]")
        apps = ", ".join(d.get("apps") or []) or "(open PC Connect for app list)"
        print(f"  apps: {apps}")
        return d

    for i, d in enumerate(found, 1):
        apps = ", ".join(d.get("apps") or []) or "(open PC Connect for app list)"
        ready = d.get("ready", True) is not False
        flag = "ready" if ready else "need PC Connect app"
        print(f"  {i}) {d['name']}  {d['mac']}  {d['port']}  [{flag}]")
        print(f"      apps: {apps}")
    while True:
        raw = input("\nSelect device number (or q): ").strip()
        if raw.lower() in ("q", "quit", ""):
            sys.exit(0)
        try:
            n = int(raw)
        except ValueError:
            continue
        if 1 <= n <= len(found):
            return found[n - 1]


def app_menu(session: PcConnectSession, apps: list) -> None:
    available = [a for a in apps if a in TOOL_RUNNERS]
    if not available:
        print("Device reported no supported apps (is PC Connect open?).")
        return

    while session.alive:
        print("\n=== PC Connect ===")
        print(f"Device: {session.hello.get('name', 'kvxputer')}  {session.hello.get('mac', '')}")
        print(f"Port:   {session.port}\n")
        for i, app_id in enumerate(available, 1):
            print(f"  {i}) {APP_LABELS.get(app_id, app_id)}")
        print("  q) quit")
        raw = input("\nApp: ").strip()
        if raw.lower() in ("q", "quit"):
            break
        try:
            n = int(raw)
        except ValueError:
            continue
        if not (1 <= n <= len(available)):
            continue
        app_id = available[n - 1]
        runner = TOOL_RUNNERS[app_id]
        print(f"\nStarting {APP_LABELS.get(app_id, app_id)}…")
        reason = runner(session)
        if reason == "bye":
            print("\nDevice left PC Connect (Esc on Cardputer).")
            print("Open USB → PC Connect again to reconnect.")
            break
        if reason == "disconnect" or not session.alive:
            print("\nUSB connection lost.")
            print("Open USB → PC Connect on the device, then rerun this script.")
            break
        print("\nBack to menu.")


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="PC Connect host for kvxputer Cardputer")
    p.add_argument(
        "--port",
        metavar="PATH",
        help="Serial device path (skip discovery), e.g. /dev/ttyACM0",
    )
    return p.parse_args(argv)


def main(argv: Optional[list] = None) -> None:
    args = parse_args(argv)
    chosen = pick_device(args.port)
    try:
        session = PcConnectSession(chosen["port"], settle=0.2)
        try:
            hello = session.do_hello(timeout=3.0, require_ready=True)
        except NeedPcConnectApp:
            hello = session.wait_until_ready()
    except PcConnectError as e:
        print(f"\nConnect failed: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nCancelled.")
        sys.exit(130)
    except Exception as e:
        print(f"\nSerial error: {e}")
        sys.exit(1)

    apps = list(hello.get("apps") or chosen.get("apps") or [])
    try:
        app_menu(session, apps)
    finally:
        if session.alive:
            try:
                session.stop()
            except Exception:
                pass
        session.close()


if __name__ == "__main__":
    main()
