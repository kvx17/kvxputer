"""GPIO read / mode / set on Grove-safe pins (radio must be idle)."""

from __future__ import annotations

from protocol import PcConnectSession

from apps._common import KeyReader, one_shot


def run(session: PcConnectSession) -> str:
    print("GPIO console — Grove-safe pins only (Cardputer: 1=SCL, 2=SDA)")
    print("Commands: read <pin> | mode <pin> <0-9> | set <pin> <0|1>")
    print("Enter = type a command; q / Esc = back to menu\n")
    keys = KeyReader()
    reason = "quit"
    try:
        while session.alive:
            ch = keys.poll()
            if ch in ("q", "Q", "\x1b"):
                reason = "quit"
                break
            if ch in ("\r", "\n"):
                line = keys.read_line("gpio> ").strip()
                if not line:
                    continue
                if line.lower() in ("q", "quit", "exit"):
                    reason = "quit"
                    break
                parts = line.split()
                if not parts:
                    continue
                op = parts[0].lower()
                if op == "read" and len(parts) == 2:
                    cmd = f"gpio read {parts[1]}"
                elif op == "mode" and len(parts) == 3:
                    cmd = f"gpio mode {parts[1]} {parts[2]}"
                elif op == "set" and len(parts) == 3:
                    cmd = f"gpio set {parts[1]} {parts[2]}"
                else:
                    print("usage: read <pin> | mode <pin> <mode> | set <pin> <0|1>")
                    continue
                status, ev = one_shot(session, cmd, timeout=3.0)
                if status == "bye":
                    reason = "bye"
                    break
                if status == "disconnect":
                    reason = "disconnect"
                    break
                if ev is None:
                    print("timeout")
                    continue
                if ev.get("evt") == "err" or ev.get("ok") is False:
                    print(f"err: {ev.get('msg', ev)}")
                else:
                    if "value" in ev:
                        print(f"ok pin={ev.get('pin')} value={ev.get('value')}")
                    else:
                        print(f"ok {ev.get('cmd', op)}")
                continue

            ev = session.read_event(timeout=0.15)
            if ev and ev.get("evt") == "bye":
                reason = "bye"
                break
    except KeyboardInterrupt:
        reason = "quit"
    finally:
        keys.close()
    return reason
