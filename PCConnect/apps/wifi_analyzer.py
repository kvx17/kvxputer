"""kvx wifi analyzer — channel airtime bars + AP list."""

from __future__ import annotations

import csv
from datetime import datetime, timezone
from typing import Dict, List, Tuple

from protocol import PcConnectSession

from apps._common import DirtyRedraw, KeyReader, LOG_DIR, UiSession, ensure_log_dir, term_size


def _bar(pct: int, width: int = 20) -> str:
    pct = max(0, min(100, pct))
    n = int(round(pct * width / 100))
    return "#" * n + "." * (width - n)


def _cmd(dwell: int, lock_ch: int) -> str:
    if lock_ch > 0:
        return f"wifi.analyzer start {dwell} {lock_ch}"
    return f"wifi.analyzer start {dwell}"


def run(session: PcConnectSession) -> str:
    loads: Dict[int, Tuple[int, int, int]] = {ch: (0, 0, -128) for ch in range(1, 12)}
    aps: Dict[str, dict] = {}
    dwell = 350
    lock_ch = 0  # 0 = hop
    ap_scroll = 0
    ui = UiSession()
    coalesce = DirtyRedraw(0.08)

    def build_lines() -> List[str]:
        nonlocal ap_scroll
        cols, rows = term_size()
        bar_w = max(10, min(28, cols - 28))
        lock_s = f"lock=ch{lock_ch}" if lock_ch else "lock=hop"
        lines: List[str] = [
            f"kvx wifi analyzer  dwell={dwell}ms  {lock_s}",
            "-" * min(48, cols),
        ]
        for ch in range(1, 12):
            load, peak, rssi = loads[ch]
            mark = "*" if lock_ch == ch else " "
            lines.append(
                f"{mark}Ch{ch:2d} [{_bar(load, bar_w)}] {load:3d}% pk{peak:3d}% {rssi:4d}dBm"
            )
        lines.append("-" * min(48, cols))

        # Fixed chrome ≈ 15 lines (title+sep+11ch+sep+help). Rest = APs.
        chrome = 15
        ap_budget = max(1, rows - chrome)
        ranked = sorted(aps.values(), key=lambda a: a.get("rssi", -999), reverse=True)
        if ap_scroll > max(0, len(ranked) - 1):
            ap_scroll = max(0, len(ranked) - 1)
        window = ranked[ap_scroll : ap_scroll + ap_budget]
        if not ranked:
            lines.append("(no APs yet)")
        else:
            for a in window:
                ssid = a.get("ssid") or ("<hidden>" if a.get("hidden") else "")
                lines.append(
                    f"  ch{a.get('ch', '?'):>2}  {ssid[:20]:<20}  "
                    f"{a.get('rssi', '?'):>4}dBm  {a.get('auth', '')}  {a.get('bssid', '')}"
                )
            if len(ranked) > ap_budget:
                lines.append(f"  … {len(ranked)} APs  scroll={ap_scroll} (↑↓)")
        lines.append("q/Esc=menu  d=dwell  1-9/0/-=lock ch  a=hop  e=export  ↑↓=scroll APs")
        return lines

    def redraw(force: bool = False) -> None:
        coalesce.mark()
        coalesce.maybe(lambda: ui.render(build_lines()), force=force)

    def export_csv() -> None:
        ensure_log_dir()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        path = LOG_DIR / f"wifi.analyzer-{stamp}.csv"
        with path.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["section", "ch", "load", "peak", "rssi"])
            for ch in range(1, 12):
                load, peak, rssi = loads[ch]
                w.writerow(["channel", ch, load, peak, rssi])
            w.writerow([])
            w.writerow(["section", "ssid", "bssid", "ch", "rssi", "auth", "hidden"])
            for a in sorted(aps.values(), key=lambda x: x.get("rssi", -999), reverse=True):
                w.writerow(
                    [
                        "ap",
                        a.get("ssid", ""),
                        a.get("bssid", ""),
                        a.get("ch", ""),
                        a.get("rssi", ""),
                        a.get("auth", ""),
                        a.get("hidden", ""),
                    ]
                )
        ui.flash(f"Exported {path}")

    def apply_lock(ch: int) -> None:
        nonlocal lock_ch
        lock_ch = ch
        session.write_line(_cmd(dwell, lock_ch))
        redraw(force=True)

    keys = KeyReader()
    reason = "quit"
    try:
        ui.enter()
        redraw(force=True)
        session.write_line(_cmd(dwell, lock_ch))
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                if ch in ("q", "Q", "\x1b"):
                    reason = "quit"
                    break
                if ch == "d":
                    dwell = {150: 350, 350: 700, 700: 150}.get(dwell, 350)
                    session.write_line(_cmd(dwell, lock_ch))
                    redraw(force=True)
                    continue
                if ch == "a":
                    apply_lock(0)
                    continue
                if ch == "e":
                    export_csv()
                    redraw(force=True)
                    continue
                if ch == "up":
                    ap_scroll = max(0, ap_scroll - 1)
                    redraw(force=True)
                    continue
                if ch == "down":
                    ap_scroll += 1
                    redraw(force=True)
                    continue
                if ch in "123456789":
                    apply_lock(int(ch))
                    continue
                if ch == "0":
                    apply_lock(10)
                    continue
                if ch == "-":
                    apply_lock(11)
                    continue
                # Ignore other keys (arrows already handled; mouse CSI drained).
            else:
                redraw()

            ev = session.read_event(timeout=0.05)
            if ev is None:
                continue
            evt = ev.get("evt")
            if evt == "bye":
                reason = "bye"
                break
            if evt == "err":
                ui.leave()
                print(f"error: {ev.get('msg', ev)}")
                reason = "err"
                break
            if evt == "ack":
                # Prefer firmware-confirmed lock/dwell when present.
                if "dwell" in ev:
                    try:
                        dwell = int(ev["dwell"])
                    except (TypeError, ValueError):
                        pass
                if "ch" in ev:
                    try:
                        lock_ch = int(ev["ch"])
                    except (TypeError, ValueError):
                        pass
                redraw(force=True)
                continue
            if evt == "analyzer":
                c = int(ev.get("ch", 0))
                if 1 <= c <= 11:
                    loads[c] = (
                        int(ev.get("load", 0)),
                        int(ev.get("peak", 0)),
                        int(ev.get("rssi", -128)),
                    )
                redraw()
            elif evt == "ap":
                bssid = ev.get("bssid") or ""
                if bssid:
                    aps[bssid] = ev
                redraw()
        else:
            reason = "disconnect" if not session.alive else "quit"
    except KeyboardInterrupt:
        reason = "quit"
    finally:
        keys.close()
        ui.leave()
        if session.alive and reason != "bye":
            session.stop()
    return reason


if __name__ == "__main__":
    print("Launch via pcconnect.py")
