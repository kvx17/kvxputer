"""Shared helpers for PC Connect apps."""

from __future__ import annotations

import select
import sys
import termios
import tty
from typing import Callable, Optional

from protocol import PcConnectSession


class KeyReader:
    """Non-blocking single-key reader (Unix)."""

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

    def poll(self) -> Optional[str]:
        if self._fd is None:
            return None
        r, _, _ = select.select([sys.stdin], [], [], 0)
        if not r:
            return None
        ch = sys.stdin.read(1)
        return ch


def run_until_quit(
    session: PcConnectSession, command: str, on_event: Callable[[dict], None]
) -> str:
    """Start command, call on_event(ev) for each event.

    Returns reason: quit|bye|err|disconnect.
    Press q or Esc to leave the tool (session stays open after stop).
    """
    keys = KeyReader()
    reason = "quit"
    try:
        print("q / Esc = back to menu\n")
        session.write_line(command)
        while session.alive:
            ch = keys.poll()
            if ch in ("q", "Q", "\x1b"):
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
