"""Jam Detect — deauth/disassoc rate per channel."""

from __future__ import annotations

from typing import Dict, List

from protocol import PcConnectSession

from apps._common import DirtyRedraw, KeyReader, UiSession, bell, term_size


def run(session: PcConnectSession) -> str:
    channels: Dict[int, dict] = {
        ch: {"ch": ch, "deauth": 0, "frames": 0, "rssi": -127, "alert": False}
        for ch in range(1, 12)
    }
    threshold = 10
    any_alert = False
    ui = UiSession()
    coalesce = DirtyRedraw(0.08)

    def build_lines() -> List[str]:
        cols, _rows = term_size()
        lines = [f"Jam Detect  thr={threshold}/s", "-" * min(48, cols)]
        for ch in range(1, 12):
            d = channels[ch]
            deauth = int(d.get("deauth", 0))
            alert = bool(d.get("alert"))
            mark = "!" if alert else " "
            bar_n = min(30, deauth)
            lines.append(
                f"{mark}Ch{ch:2d}  deauth={deauth:4d}/s  frames={int(d.get('frames', 0)):5d}  "
                f"rssi={int(d.get('rssi', -127)):4d}  {'#' * bar_n}"
            )
        lines.append(f"{'ALERT' if any_alert else 'scanning'} — q/Esc=menu  +/- = threshold")
        return lines

    def redraw(force: bool = False) -> None:
        coalesce.mark()
        coalesce.maybe(lambda: ui.render(build_lines()), force=force)

    keys = KeyReader()
    reason = "quit"
    try:
        ui.enter()
        redraw(force=True)
        session.write_line(f"jam.detect start {threshold}")
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                if ch in ("q", "Q", "\x1b"):
                    reason = "quit"
                    break
                if ch in ("+", "="):
                    threshold = min(250, threshold + 5)
                    session.write_line(f"jam.detect start {threshold}")
                    redraw(force=True)
                    continue
                if ch == "-":
                    threshold = max(5, threshold - 5)
                    session.write_line(f"jam.detect start {threshold}")
                    redraw(force=True)
                    continue
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
                continue
            if evt == "jam":
                c = int(ev.get("ch", 0))
                if 1 <= c <= 11:
                    was = bool(channels[c].get("alert"))
                    channels[c] = ev
                    if ev.get("alert") and not was:
                        bell()
                any_alert = any(bool(channels[i].get("alert")) for i in range(1, 12))
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
