"""IR receive stream + optional TX from a local .ir file."""

from __future__ import annotations

import re
from pathlib import Path
from typing import List, Optional

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import KeyReader, LiveTable, one_shot, run_live_table


def parse_ir_file(path: Path) -> List[dict]:
    """Parse Flipper-style .ir signals from a PC-side file."""
    text = path.read_text(encoding="utf-8", errors="replace")
    blocks: List[dict] = []
    cur: dict = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.lower().startswith("name:"):
            if cur.get("name"):
                blocks.append(cur)
            cur = {"name": line.split(":", 1)[1].strip()}
            continue
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        cur[k.strip().lower()] = v.strip()
    if cur.get("name") or cur.get("type") or cur.get("protocol"):
        blocks.append(cur)
    return blocks


def _tx_command(block: dict) -> Optional[str]:
    typ = (block.get("type") or "").lower()
    if typ == "raw" or block.get("data"):
        freq = block.get("frequency") or "38000"
        data = block.get("data") or ""
        if not data:
            return None
        return f"ir.tx_raw {freq} {data}"
    protocol = block.get("protocol") or ""
    address = (block.get("address") or "").replace(" ", "")
    command = (block.get("command") or "").replace(" ", "")
    if not protocol or not address or not command:
        return None
    # Pad to 8 hex chars if short
    if re.fullmatch(r"[0-9A-Fa-f]+", address) and len(address) < 8:
        address = address.ljust(8, "0")
    if re.fullmatch(r"[0-9A-Fa-f]+", command) and len(command) < 8:
        command = command.ljust(8, "0")
    return f"ir.tx {protocol} {address} {command}"


def run_rx(session: PcConnectSession) -> str:
    raw_mode = False
    hit_n = 0

    def render_row(d: dict) -> str:
        if d.get("raw"):
            data = (d.get("data") or "")[:40]
            return f"raw  freq={d.get('freq')}  {data}"
        return (
            f"{(d.get('protocol') or '?'):<12}  "
            f"addr={d.get('address') or '-'}  cmd={d.get('command') or '-'}  "
            f"bits={d.get('bits', '?')}"
        )

    table = LiveTable(
        title="IR Receive",
        app_id="ir.rx",
        columns=["protocol", "address", "command", "bits"],
        key_field="_id",
        sort_keys=["_id", "protocol"],
        bell_on_new=True,
        render_row=render_row,
        text_fields=["protocol", "address", "command", "data"],
        help_extra="t=tx .ir file  r=toggle raw",
    )

    def on_event_wrap(ev: dict) -> None:
        nonlocal hit_n
        if ev.get("evt") != "ir":
            return
        hit_n += 1
        row = dict(ev)
        row["_id"] = str(hit_n)
        table.upsert(row)
        table.redraw()

    def on_key_extra(ch: str, tbl: LiveTable, keys) -> Optional[str]:
        nonlocal raw_mode
        if ch == "r":
            raw_mode = not raw_mode
            cmd = "ir.rx start raw" if raw_mode else "ir.rx start"
            tbl.title = f"IR Receive  ({'raw' if raw_mode else 'decoded'})"
            tbl._dirty = True
            return f"restart:{cmd}"
        if ch == "t" and keys is not None:
            path_s = keys.read_line("path to .ir file: ").strip()
            if not path_s:
                return "redraw"
            path = Path(path_s).expanduser()
            if not path.is_file():
                tbl.ui.flash(f"not found: {path}")
                return "redraw"
            blocks = parse_ir_file(path)
            if not blocks:
                tbl.ui.flash("no signals in file")
                return "redraw"
            session.stop()
            ok_n = 0
            for i, b in enumerate(blocks):
                name = b.get("name") or f"signal{i}"
                cmd = _tx_command(b)
                if not cmd:
                    continue
                status, ev = one_shot(session, cmd, timeout=5.0)
                if status == "bye":
                    return "bye"
                if status == "ack" and (not ev or ev.get("ok") is not False):
                    ok_n += 1
            tbl.ui.flash(f"TX {ok_n}/{len(blocks)} from {path.name}")
            cmd = "ir.rx start raw" if raw_mode else "ir.rx start"
            return f"restart:{cmd}"
        return None

    keys = KeyReader()
    table._keys = keys
    reason = "quit"
    cmd = APP_COMMANDS["ir.rx"]
    try:
        table.ui.enter()
        table.redraw(force=True)
        session.write_line(cmd)
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                action = on_key_extra(ch, table, keys)
                if action is None:
                    action = table.handle_key(ch, keys)
                if action == "quit":
                    reason = "quit"
                    break
                if action == "bye":
                    reason = "bye"
                    break
                if isinstance(action, str) and action.startswith("restart:"):
                    session.write_line(action[len("restart:") :])
                    table.redraw(force=True)
                    continue
                if action == "redraw" or table._dirty:
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
            on_event_wrap(ev)
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
