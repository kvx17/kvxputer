"""Sub-GHz RX and RSSI spectrum (CC1101)."""

from __future__ import annotations

from typing import Dict

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import DirtyRedraw, KeyReader, LiveTable, UiSession, one_shot, term_size


def run_rx(session: PcConnectSession) -> str:
    mhz = 433.92
    raw_mode = False
    hit_n = 0

    def render_row(d: dict) -> str:
        proto = (d.get("protocol") or "?")[:12]
        key = (d.get("key") or d.get("data") or "")[:24]
        return (
            f"{d.get('mhz', '?'):>7}  {proto:<12}  bits={d.get('bits', '?'):>3}  "
            f"te={d.get('te', '?')}  {key}"
        )

    table = LiveTable(
        title="Sub-GHz RX",
        app_id="rf.rx",
        columns=["mhz", "protocol", "bits", "key"],
        key_field="_id",
        sort_keys=["_id", "mhz", "protocol"],
        bell_on_new=True,
        render_row=render_row,
        text_fields=["protocol", "key", "data", "mf_name"],
        help_extra="f=freq  r=raw  t=replay last",
    )

    keys = KeyReader()
    table._keys = keys
    reason = "quit"

    def start_cmd() -> str:
        base = f"rf.rx start {mhz}"
        return base + (" raw" if raw_mode else "")

    try:
        table.ui.enter()
        table.title = f"Sub-GHz RX  {mhz} MHz  ({'raw' if raw_mode else 'decoded'})"
        table.redraw(force=True)
        session.write_line(start_cmd())
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                if ch == "f":
                    raw = keys.read_line(f"frequency MHz [{mhz}]: ").strip()
                    if raw:
                        try:
                            mhz = float(raw)
                        except ValueError:
                            pass
                    table.title = f"Sub-GHz RX  {mhz} MHz  ({'raw' if raw_mode else 'decoded'})"
                    session.write_line(start_cmd())
                    table.redraw(force=True)
                    continue
                if ch == "r":
                    raw_mode = not raw_mode
                    table.title = f"Sub-GHz RX  {mhz} MHz  ({'raw' if raw_mode else 'decoded'})"
                    session.write_line(start_cmd())
                    table.redraw(force=True)
                    continue
                if ch == "t":
                    session.stop()
                    status, ev = one_shot(session, "rf.tx", timeout=5.0)
                    if status == "bye":
                        reason = "bye"
                        break
                    table.ui.flash(f"replay: {ev}")
                    session.write_line(start_cmd())
                    table.redraw(force=True)
                    continue
                action = table.handle_key(ch, keys)
                if action == "quit":
                    reason = "quit"
                    break
                if table._dirty:
                    table.redraw(force=True)
            else:
                table.redraw()
            ev = session.read_event(timeout=0.05)
            if ev is None:
                continue
            evt = ev.get("evt")
            if evt == "bye":
                reason = "bye"
                break
            if evt == "err":
                table.ui.leave()
                print(f"error: {ev.get('msg', ev)}")
                reason = "err"
                break
            if evt == "ack":
                continue
            if evt == "rf":
                hit_n += 1
                row = dict(ev)
                row["_id"] = str(hit_n)
                table.upsert(row)
                table.redraw()
        else:
            reason = "disconnect" if not session.alive else "quit"
    except KeyboardInterrupt:
        reason = "quit"
    finally:
        keys.close()
        table.close()
        if session.alive and reason != "bye":
            session.stop()
    return reason


def run_rssi(session: PcConnectSession) -> str:
    samples: Dict[str, dict] = {}  # mhz str -> last
    scroll = 0
    ui = UiSession()
    coalesce = DirtyRedraw(0.08)

    def build_lines():
        nonlocal scroll
        cols, rows = term_size()
        lines = ["Sub-GHz RSSI spectrum (CC1101)", "-" * min(48, cols)]
        ranked = sorted(samples.values(), key=lambda d: float(d.get("mhz", 0)))
        budget = max(1, rows - 4)
        if scroll > max(0, len(ranked) - 1):
            scroll = max(0, len(ranked) - 1)
        window = ranked[scroll : scroll + budget]
        if not ranked:
            lines.append("(scanning…)")
        else:
            bar_max = max(10, min(40, cols - 28))
            for d in window:
                rssi = int(d.get("rssi", -95))
                bar_n = max(0, min(bar_max, rssi + 95))
                lines.append(f"{float(d.get('mhz', 0)):7.2f} MHz  {rssi:4d} dBm  {'#' * bar_n}")
        lines.append(f"{len(samples)} freqs — q/Esc=menu  ↑↓=scroll")
        return lines

    def redraw(force: bool = False) -> None:
        coalesce.mark()
        coalesce.maybe(lambda: ui.render(build_lines()), force=force)

    keys = KeyReader()
    reason = "quit"
    try:
        ui.enter()
        redraw(force=True)
        session.write_line(APP_COMMANDS["rf.rssi"])
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                if ch in ("q", "Q", "\x1b"):
                    reason = "quit"
                    break
                if ch == "up":
                    scroll = max(0, scroll - 1)
                    redraw(force=True)
                    continue
                if ch == "down":
                    scroll += 1
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
            if evt == "rf.rssi":
                k = f"{float(ev.get('mhz', 0)):.3f}"
                samples[k] = ev
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
