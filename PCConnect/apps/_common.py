"""Shared helpers for PC Connect apps."""

from __future__ import annotations

import csv
import json
import select
import shutil
import sys
import termios
import time
import tty
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

from protocol import PcConnectSession

LOG_DIR = Path(__file__).resolve().parent.parent / "logs"


def term_size() -> Tuple[int, int]:
    """(columns, rows), never below a usable minimum."""
    sz = shutil.get_terminal_size(fallback=(80, 24))
    return max(40, sz.columns), max(8, sz.lines)


def clear_screen() -> None:
    """Legacy clear — prefer UiSession.render() for live UIs."""
    sys.stdout.write("\033[H\033[J")
    sys.stdout.flush()


def bell() -> None:
    sys.stdout.write("\a")
    sys.stdout.flush()


def ensure_log_dir() -> Path:
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    return LOG_DIR


def clip_line(text: str, cols: int) -> str:
    text = text.replace("\r", " ").replace("\n", " ")
    if cols <= 1:
        return text[:1]
    if len(text) <= cols:
        return text
    if cols <= 2:
        return text[:cols]
    return text[: cols - 1] + "…"


class UiSession:
    """Alternate-screen fixed-size redraw (no scroll flood)."""

    def __init__(self) -> None:
        self._active = False
        self._status = ""
        self._status_until = 0.0

    def __enter__(self) -> "UiSession":
        self.enter()
        return self

    def __exit__(self, *args: Any) -> None:
        self.leave()

    def enter(self) -> None:
        if not sys.stdout.isatty():
            return
        sys.stdout.write("\033[?1049h\033[?25l\033[H\033[J")
        sys.stdout.flush()
        self._active = True

    def leave(self) -> None:
        if not self._active:
            return
        sys.stdout.write("\033[?25h\033[?1049l")
        sys.stdout.flush()
        self._active = False

    def flash(self, msg: str, seconds: float = 1.2) -> None:
        self._status = msg
        self._status_until = time.time() + seconds

    def render(self, lines: Sequence[str]) -> None:
        cols, rows = term_size()
        body = [clip_line(str(ln), cols) for ln in lines]

        status = ""
        if self._status and time.time() < self._status_until:
            status = self._status
        else:
            self._status = ""

        # Reserve last row for flash status when present.
        usable = rows - (1 if status else 0)
        if usable < 3:
            usable = rows
            status = ""

        if len(body) > usable:
            # Keep head + tail so help/footer stays visible.
            keep_tail = min(3, usable // 3)
            keep_head = max(1, usable - keep_tail - 1)
            body = body[:keep_head] + ["…"] + body[-keep_tail:]
            body = body[:usable]

        while len(body) < usable:
            body.append("")

        out: List[str] = ["\033[H"]
        for i, line in enumerate(body):
            out.append(line.ljust(cols)[:cols])
            if i < usable - 1 or status:
                out.append("\r\n")
        if status:
            out.append(clip_line(status, cols).ljust(cols)[:cols])
        # Clear anything below the painted area (should be none in alt screen).
        out.append("\033[J")
        sys.stdout.write("".join(out))
        sys.stdout.flush()


class DirtyRedraw:
    """Coalesce rapid redraw requests (serial events) to ~fps."""

    def __init__(self, min_interval: float = 0.08) -> None:
        self.min_interval = min_interval
        self._dirty = True
        self._last = 0.0

    def mark(self) -> None:
        self._dirty = True

    def maybe(self, draw: Callable[[], None], force: bool = False) -> None:
        if not self._dirty and not force:
            return
        now = time.time()
        if not force and (now - self._last) < self.min_interval:
            return
        self._dirty = False
        self._last = now
        draw()


class KeyReader:
    """Non-blocking key reader. Drains CSI so arrows/mouse do not look like Esc."""

    def __init__(self) -> None:
        self._fd = None
        self._old = None
        if sys.stdin.isatty():
            self._fd = sys.stdin.fileno()
            self._old = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)

    def close(self) -> None:
        if self._fd is not None and self._old is not None:
            termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)
            self._fd = None

    def _read_ready(self, timeout: float = 0.0) -> bool:
        if self._fd is None:
            return False
        r, _, _ = select.select([sys.stdin], [], [], timeout)
        return bool(r)

    def _read1(self) -> str:
        return sys.stdin.read(1)

    def _drain_csi(self) -> Optional[str]:
        """Consume CSI/SS3 after ESC. Return semantic key or None to ignore."""
        if not self._read_ready(0.03):
            return "\x1b"  # lone Esc
        n1 = self._read1()
        if n1 == "[":
            params = ""
            while self._read_ready(0.05):
                c = self._read1()
                if c.isalpha() or c == "~":
                    # Arrow / page / mouse — map a few, ignore rest.
                    if c == "A":
                        return "up"
                    if c == "B":
                        return "down"
                    if c == "C":
                        return "right"
                    if c == "D":
                        return "left"
                    return None
                params += c
                if len(params) > 32:
                    return None
            return None
        if n1 == "O":
            if self._read_ready(0.05):
                c = self._read1()
                if c == "A":
                    return "up"
                if c == "B":
                    return "down"
                if c == "C":
                    return "right"
                if c == "D":
                    return "left"
            return None
        # ESC + other char: treat as Esc (discard the other? keep as meta)
        # Prefer quitting only on lone Esc; ignore ESC+char.
        return None

    def poll(self) -> Optional[str]:
        if self._fd is None:
            return None
        if not self._read_ready(0.0):
            return None
        ch = self._read1()
        if ch == "\x1b":
            return self._drain_csi()
        return ch

    def read_line(self, prompt: str = "") -> str:
        """Temporarily restore cooked mode and read a line."""
        if self._fd is None or self._old is None:
            return input(prompt)
        # Show cursor for the prompt.
        sys.stdout.write("\033[?25h")
        sys.stdout.flush()
        termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)
        try:
            return input(prompt)
        finally:
            tty.setcbreak(self._fd)
            sys.stdout.write("\033[?25l")
            sys.stdout.flush()


def run_until_quit(
    session: PcConnectSession,
    command: str,
    on_event: Callable[[dict], None],
    on_key: Optional[Callable[[str], Optional[str]]] = None,
    help_hint: str = "q / Esc = back to menu",
) -> str:
    """Start command, call on_event(ev) for each event.

    on_key(ch) may return:
      None / "" — keep running
      "quit" — leave tool (stop radio)
      "restart:<cmd>" — write a new start command without leaving the session
      "bye" — treat as device bye (rare)

    Returns reason: quit|bye|err|disconnect.
    """
    keys = KeyReader()
    reason = "quit"
    current_cmd = command
    try:
        print(f"{help_hint}\n")
        session.write_line(current_cmd)
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                if on_key is not None:
                    action = on_key(ch)
                    if action == "quit":
                        reason = "quit"
                        break
                    if action == "bye":
                        reason = "bye"
                        break
                    if isinstance(action, str) and action.startswith("restart:"):
                        current_cmd = action[len("restart:") :]
                        session.write_line(current_cmd)
                        continue
                elif ch in ("q", "Q", "\x1b"):
                    reason = "quit"
                    break
            ev = session.read_event(timeout=0.2)
            if ev is None:
                continue
            evt = ev.get("evt")
            if evt == "bye":
                reason = "bye"
                break
            if evt == "err":
                print(f"error: {ev.get('msg', ev)}")
                reason = "err"
                break
            if evt == "ack":
                continue
            on_event(ev)
        else:
            reason = "disconnect" if not session.alive else "quit"
    except KeyboardInterrupt:
        reason = "quit"
    finally:
        keys.close()
        if session.alive and reason != "bye":
            session.stop()
    return reason


def one_shot(
    session: PcConnectSession, command: str, timeout: float = 5.0
) -> tuple[str, Optional[dict]]:
    """Send a one-shot command; return (reason, event).

    reason is ack|err|bye|disconnect|timeout.
    """
    if not session.alive:
        return "disconnect", None
    session.write_line(command)
    deadline = time.time() + timeout
    while time.time() < deadline and session.alive:
        ev = session.read_event(timeout=0.2)
        if ev is None:
            continue
        evt = ev.get("evt")
        if evt == "bye":
            return "bye", ev
        if evt in ("ack", "err"):
            return evt, ev
    if not session.alive:
        return "disconnect", None
    return "timeout", None


class LiveTable:
    """Keyed live table with sort / filter / detail / export / JSONL log."""

    def __init__(
        self,
        title: str,
        app_id: str,
        columns: Sequence[str],
        key_field: str = "mac",
        sort_keys: Optional[Sequence[str]] = None,
        bell_on_new: bool = False,
        bell_predicate: Optional[Callable[[dict], bool]] = None,
        row_filter: Optional[Callable[[dict], bool]] = None,
        render_row: Optional[Callable[[dict], str]] = None,
        render_detail: Optional[Callable[[dict], List[str]]] = None,
        text_fields: Optional[Sequence[str]] = None,
        help_extra: str = "",
    ) -> None:
        self.title = title
        self.app_id = app_id
        self.columns = list(columns)
        self.key_field = key_field
        self.sort_keys = list(sort_keys or ["rssi", "name", key_field])
        self.sort_idx = 0
        self.filter_text = ""
        self.bell_on_new = bell_on_new
        self.bell_predicate = bell_predicate or (lambda _r: True)
        self.row_filter = row_filter
        self.render_row = render_row
        self.render_detail = render_detail
        self.text_fields = list(text_fields or ["name", "mac", "ssid", "bssid", "rule"])
        self.help_extra = help_extra
        self.rows: Dict[str, dict] = {}
        self.detail_key: Optional[str] = None
        self.log_enabled = False
        self._log_fp = None
        self._matched_only = False
        self._dirty = True
        self._keys: Optional[KeyReader] = None
        self.ui = UiSession()
        self._coalesce = DirtyRedraw(0.08)
        self.scroll = 0

    def upsert(self, ev: dict) -> None:
        key = str(ev.get(self.key_field) or "")
        if not key:
            return
        is_new = key not in self.rows
        self.rows[key] = ev
        self._dirty = True
        if is_new and self.bell_on_new and self.bell_predicate(ev):
            bell()
        if self.log_enabled:
            self._append_log(ev)

    def set_matched_only(self, value: bool) -> None:
        self._matched_only = value
        self._dirty = True

    def toggle_matched_only(self) -> None:
        self._matched_only = not self._matched_only
        self._dirty = True

    def toggle_log(self) -> None:
        if self.log_enabled:
            self._close_log()
            self.log_enabled = False
        else:
            self._open_log()
            self.log_enabled = True
        self._dirty = True

    def export_csv(self) -> Optional[Path]:
        ensure_log_dir()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        path = LOG_DIR / f"{self.app_id}-{stamp}.csv"
        visible = self._visible_rows()
        fields: List[str] = []
        for r in visible:
            for k in r:
                if k not in fields:
                    fields.append(k)
        if not fields:
            fields = list(self.columns) or [self.key_field]
        with path.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
            w.writeheader()
            for r in visible:
                w.writerow({k: r.get(k, "") for k in fields})
        return path

    def redraw(self, footer: str = "", force: bool = False) -> None:
        self._coalesce.mark()

        def _draw() -> None:
            cols, rows = term_size()
            lines: List[str] = [self.title, "-" * min(72, cols)]
            if self.detail_key and self.detail_key in self.rows:
                lines.extend(self._detail_lines(self.rows[self.detail_key]))
            else:
                lines.extend(self._list_lines(max_body=max(1, rows - 6)))
            bits = [
                f"sort={self.sort_keys[self.sort_idx]}",
                f"filter={self.filter_text!r}" if self.filter_text else "filter=off",
                f"log={'on' if self.log_enabled else 'off'}",
            ]
            if self._matched_only:
                bits.append("matched-only")
            lines.append("-" * min(72, cols))
            lines.append("  ".join(bits))
            help_line = "q=quit  s=sort  /=filter  Enter=detail  e=export  l=log  ↑↓=scroll"
            if self.help_extra:
                help_line += f"  {self.help_extra}"
            lines.append(help_line)
            if footer:
                lines.append(footer)
            self.ui.render(lines)
            self._dirty = False

        self._coalesce.maybe(_draw, force=force)

    def handle_key(self, ch: str, keys: Optional[KeyReader] = None) -> Optional[str]:
        """Return 'quit' to leave, None to continue."""
        if ch in ("q", "Q"):
            return "quit"
        if ch == "\x1b":
            if self.detail_key is not None:
                self.detail_key = None
                self._dirty = True
                return None
            return "quit"
        if ch == "up":
            self.scroll = max(0, self.scroll - 1)
            self._dirty = True
            return None
        if ch == "down":
            self.scroll += 1
            self._dirty = True
            return None
        if ch in ("\r", "\n"):
            visible = self._visible_rows()
            if visible and self.detail_key is None:
                idx = min(self.scroll, len(visible) - 1)
                self.detail_key = str(visible[idx].get(self.key_field) or "")
                self._dirty = True
            return None
        if ch == "s":
            self.sort_idx = (self.sort_idx + 1) % len(self.sort_keys)
            self.scroll = 0
            self._dirty = True
            return None
        if ch == "/":
            reader = keys or self._keys
            if reader is not None:
                self.filter_text = reader.read_line("filter (empty=clear): ").strip()
            else:
                self.filter_text = input("filter (empty=clear): ").strip()
            self.scroll = 0
            self._dirty = True
            return None
        if ch == "e":
            path = self.export_csv()
            if path:
                self.ui.flash(f"Exported {path}")
            self._dirty = True
            return None
        if ch == "l":
            self.toggle_log()
            return None
        if ch == "m":
            self.toggle_matched_only()
            return None
        return None

    def close(self) -> None:
        self._close_log()
        self.ui.leave()

    def _visible_rows(self) -> List[dict]:
        rows = list(self.rows.values())
        if self.row_filter:
            rows = [r for r in rows if self.row_filter(r)]
        if self._matched_only:
            rows = [r for r in rows if r.get("matched")]
        if self.filter_text:
            ft = self.filter_text.lower()
            filtered = []
            for r in rows:
                blob = " ".join(str(r.get(f, "") or "") for f in self.text_fields).lower()
                if ft in blob or ft in json.dumps(r, default=str).lower():
                    filtered.append(r)
            rows = filtered
        sk = self.sort_keys[self.sort_idx]
        reverse = sk == "rssi"

        def sort_val(r: dict) -> Any:
            v = r.get(sk)
            if v is None:
                return -999 if reverse else ""
            return v

        try:
            rows.sort(key=sort_val, reverse=reverse)
        except TypeError:
            rows.sort(key=lambda r: str(r.get(sk, "")), reverse=reverse)
        return rows

    def _list_lines(self, max_body: int = 40) -> List[str]:
        all_rows = self._visible_rows()
        if self.scroll > max(0, len(all_rows) - 1):
            self.scroll = max(0, len(all_rows) - 1)
        body_budget = max(1, max_body - 2)
        rows = all_rows[self.scroll : self.scroll + body_budget]
        lines: List[str] = []
        if not all_rows:
            lines.append("(no rows yet)")
        elif self.render_row:
            for r in rows:
                lines.append(self.render_row(r))
        else:
            hdr = "  ".join(f"{c:<16}" for c in self.columns)
            lines.append(hdr)
            for r in rows:
                lines.append("  ".join(f"{str(r.get(c, ''))[:16]:<16}" for c in self.columns))
        lines.append(f"{len(self.rows)} total / {len(all_rows)} shown  scroll={self.scroll}")
        return lines

    def _detail_lines(self, row: dict) -> List[str]:
        lines = [f"Detail  ({self.key_field}={row.get(self.key_field)})", ""]
        if self.render_detail:
            lines.extend(self.render_detail(row))
        else:
            for k, v in sorted(row.items()):
                lines.append(f"  {k}: {v}")
        lines.append("")
        lines.append("Esc = back to list")
        return lines

    def _open_log(self) -> None:
        ensure_log_dir()
        path = LOG_DIR / f"{self.app_id}.jsonl"
        self._log_fp = path.open("a", encoding="utf-8")

    def _close_log(self) -> None:
        if self._log_fp is not None:
            try:
                self._log_fp.close()
            except Exception:
                pass
            self._log_fp = None

    def _append_log(self, ev: dict) -> None:
        if self._log_fp is None:
            return
        try:
            self._log_fp.write(json.dumps(ev, default=str) + "\n")
            self._log_fp.flush()
        except Exception:
            pass


def run_live_table(
    session: PcConnectSession,
    command: str,
    table: LiveTable,
    evt_name: str,
    on_key_extra: Optional[Callable[[str, LiveTable, KeyReader], Optional[str]]] = None,
    help_hint: Optional[str] = None,
) -> str:
    """Wire LiveTable for a single event type with fixed-frame UI."""
    keys_holder: Dict[str, Any] = {"keys": None}

    def on_event(ev: dict) -> None:
        if ev.get("evt") != evt_name:
            return
        table.upsert(ev)
        table.redraw()

    def on_key(ch: str) -> Optional[str]:
        if on_key_extra is not None:
            action = on_key_extra(ch, table, keys_holder.get("keys"))
            if action is not None:
                if action == "redraw":
                    table.redraw(force=True)
                    return None
                return action
        action = table.handle_key(ch, keys_holder.get("keys"))
        if action == "quit":
            return "quit"
        if table._dirty:
            table.redraw(force=True)
        return None

    keys = KeyReader()
    keys_holder["keys"] = keys
    table._keys = keys
    reason = "quit"
    try:
        table.ui.enter()
        table.redraw(force=True)
        session.write_line(command)
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                action = on_key(ch)
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
            else:
                # Flush coalesced redraws even when idle.
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
            on_event(ev)
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
