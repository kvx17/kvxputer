"""PC Connect NFC toolkit — Cardputer does RF; CLI shows dumps/files/status."""

from __future__ import annotations

import time
from typing import Any, Dict, List, Optional, Tuple

from protocol import PcConnectSession

from apps._common import (
    KeyReader,
    LiveTable,
    UiSession,
    drain_events,
    is_quit_key,
    request_stop,
    term_size,
)


RFID_ROOTS = [
    "/support_files/rfid",
    "/support_files/rfid/hf",
    "/support_files/rfid/scans",
    "/support_files/rfid/amiibo",
    "/support_files/rfid/nonces",
    "/support_files/rfid/apdu",
]


def _drain_until(
    session: PcConnectSession,
    timeout: float = 8.0,
    want: Optional[set] = None,
    allow_quit: bool = True,
) -> Tuple[str, List[dict]]:
    return drain_events(
        session,
        timeout=timeout,
        want=want,
        allow_quit=allow_quit,
        help_once="q / Esc = cancel" if allow_quit else "",
    )


def _collect_text(events: List[dict], evt_name: str) -> str:
    parts: List[Tuple[int, str]] = []
    for ev in events:
        if ev.get("evt") != evt_name:
            continue
        parts.append((int(ev.get("offset") or 0), str(ev.get("text") or "")))
    parts.sort(key=lambda x: x[0])
    return "".join(p[1] for p in parts)


def _cmd(session: PcConnectSession, line: str, timeout: float = 12.0) -> Tuple[str, List[dict]]:
    if not session.alive:
        return "disconnect", []
    session.write_line(line)
    return _drain_until(session, timeout=timeout, want={"nfc.dump", "nfc.file", "nfc.files", "nfc.status"})


_VAL = "\033[92m"
_RST = "\033[0m"


def _print_field(label: str, value: object) -> None:
    print(f"{label}\n{_VAL}{value}{_RST}")


def _print_tag_events(events: List[dict]) -> None:
    for ev in events:
        if ev.get("evt") == "nfc":
            _print_field("UID:", ev.get("uid"))
            _print_field("Type:", ev.get("type"))
            _print_field("SAK:", ev.get("sak"))
            _print_field("ATQA:", ev.get("atqa"))
            _print_field("Pages:", ev.get("pages"))
        if ev.get("evt") == "err":
            print(f"error: {ev.get('msg', ev)}")
        if ev.get("evt") == "ack" and not ev.get("ok", True):
            print(f"failed: {ev.get('msg', ev.get('cmd'))}")
    dump = _collect_text(events, "nfc.dump")
    if dump:
        print("--- dump ---")
        print(f"{_VAL}{dump.rstrip()}{_RST}")
        print("------------")


def _ensure_manage(session: PcConnectSession) -> bool:
    reason, events = _cmd(session, "nfc.manage start", timeout=5.0)
    if reason == "bye":
        return False
    for ev in events:
        if ev.get("evt") == "ack" and ev.get("cmd") == "nfc.manage" and ev.get("ok"):
            return True
        if ev.get("evt") == "err":
            print(f"NFC start failed: {ev.get('msg')}")
            return False
    return reason == "ok"


def live_read(session: PcConnectSession) -> str:
    dumps: Dict[str, str] = {}

    def render_row(d: dict) -> str:
        return (
            f"{(d.get('uid') or ''):<24}  {(d.get('type') or '')[:18]:<18}  "
            f"SAK={d.get('sak', '-')}  pages={d.get('pages', '?')}"
        )

    def render_detail(d: dict) -> List[str]:
        uid = str(d.get("uid") or "")

        def field(label: str, value: object) -> List[str]:
            return [label, f"{_VAL}{value}{_RST}"]

        lines: List[str] = []
        lines.extend(field("UID:", uid))
        lines.extend(field("Type:", d.get("type")))
        lines.extend(field("SAK:", d.get("sak")))
        lines.extend(field("ATQA:", d.get("atqa")))
        lines.extend(field("Pages:", f"{d.get('pages')} ok={d.get('pages_ok')}"))
        lines.append("")
        dump = dumps.get(uid) or d.get("dump") or ""
        if dump:
            for row in str(dump).splitlines()[:60]:
                lines.append(f"{_VAL}{row}{_RST}")
        else:
            lines.append("(dump arrives with tag — wait for nfc.dump chunks)")
        return lines

    table = LiveTable(
        title="NFC Live Read",
        app_id="nfc.read",
        columns=["uid", "type", "sak", "atqa", "pages"],
        key_field="uid",
        sort_keys=["uid", "type"],
        bell_on_new=True,
        render_row=render_row,
        render_detail=render_detail,
        text_fields=["uid", "type", "sak", "atqa"],
        help_extra="s=save-on-device",
    )

    def on_key_extra(ch: str, tbl: LiveTable, keys: KeyReader) -> Optional[str]:
        if ch == "s":
            name = keys.read_line("save name (stem): ").strip() or "rfid_dump"
            fmt = keys.read_line("format bruce|flipper [bruce]: ").strip() or "bruce"
            session.write_line(f"nfc.save name={name} format={fmt}")
            tbl.ui.flash(f"save {name} ({fmt})")
            return "redraw"
        return None

    # Custom loop to merge dump chunks into detail cache
    keys = KeyReader()
    table._keys = keys
    reason = "quit"
    try:
        table.ui.enter()
        table.redraw(force=True)
        if not _ensure_manage(session):
            return "err"
        session.write_line("nfc.read start")
        pending_dump: Dict[str, List[Tuple[int, str]]] = {}
        while session.alive:
            ch = keys.poll()
            if ch is not None:
                action = on_key_extra(ch, table, keys)
                if action == "redraw":
                    table.redraw(force=True)
                elif action == "quit":
                    request_stop(session)
                    reason = "quit"
                    break
                else:
                    a = table.handle_key(ch, keys)
                    if a == "quit":
                        request_stop(session)
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
            if evt == "nfc":
                table.upsert(ev)
                table.redraw()
            elif evt == "nfc.dump":
                uid = str(ev.get("uid") or "")
                pending_dump.setdefault(uid, []).append(
                    (int(ev.get("offset") or 0), str(ev.get("text") or ""))
                )
                if ev.get("done"):
                    parts = sorted(pending_dump[uid], key=lambda x: x[0])
                    dumps[uid] = "".join(p[1] for p in parts)
                    pending_dump[uid] = []
                    if uid in table.rows:
                        table.rows[uid]["dump"] = dumps[uid]
                    table.redraw(force=True)
        else:
            reason = "disconnect" if not session.alive else "quit"
    except KeyboardInterrupt:
        request_stop(session)
        reason = "quit"
    finally:
        keys.close()
        table.close()
        if session.alive and reason != "bye":
            session.stop()
    return reason


def read_once(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    raw = input("timeout ms [5000]: ").strip() or "5000"
    print("Hold tag…")
    session.write_line(f"nfc.read.once timeout={raw}")
    reason, events = _drain_until(session, timeout=max(6.0, float(raw) / 1000.0 + 3), want={"nfc.dump"})
    _print_tag_events(events)
    input("Enter to continue…")
    session.stop()
    return reason if reason != "ok" else "quit"


def emulate_ndef(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    print("1) kvxputer GitHub  2) URL  3) Text")
    choice = input("Choice [1]: ").strip() or "1"
    if choice == "1":
        typ, value = "url", "https://github.com/kvx17/kvxputer"
    elif choice == "3":
        typ, value = "text", input("Text: ").strip() or "kvxputer"
    else:
        typ, value = "url", input("URL: ").strip() or "https://github.com/kvx17/kvxputer"
    print("Emulating… tap a phone/reader.")
    print("q / Esc = stop emulate and return to menu")
    session.write_line(f"nfc.ndef.emulate type={typ} value={value}")
    keys = KeyReader()
    reason = "quit"
    try:
        while session.alive:
            ch = keys.poll()
            if is_quit_key(ch):
                print("\nStopping emulate…")
                request_stop(session)
                # Wait briefly for stopped status
                drain_events(session, timeout=3.0, want={"nfc.status"}, allow_quit=False)
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
                print(f"error: {ev.get('msg')}")
                reason = "err"
                break
            if evt == "nfc.status":
                print(f"  [{ev.get('op')}] {ev.get('state')} {ev.get('detail') or ''}")
                if ev.get("state") in ("reader_hit", "stopped", "error", "stopping"):
                    if ev.get("state") == "stopping":
                        continue
                    # Ask without blocking forever on cooked input alone —
                    # restore cooked mode via KeyReader.read_line
                    again = keys.read_line("Re-arm emulate? [Y/n/q]: ").strip().lower()
                    if again in ("q", "quit"):
                        request_stop(session)
                        break
                    if again in ("", "y", "yes"):
                        session.write_line(f"nfc.ndef.emulate type={typ} value={value}")
                    else:
                        break
    except KeyboardInterrupt:
        request_stop(session)
        reason = "quit"
    finally:
        keys.close()
        if session.alive and reason != "bye":
            session.stop()
    return reason


def files_browser(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"

    dir_idx = 0
    filter_text = ""
    files: List[dict] = []
    scroll = 0
    ui = UiSession()
    keys = KeyReader()
    reason = "quit"

    def refresh_list() -> None:
        nonlocal files
        d = RFID_ROOTS[dir_idx % len(RFID_ROOTS)]
        session.write_line(f"nfc.files list dir={d}")
        _, events = _drain_until(session, timeout=4.0, want={"nfc.files"})
        files = []
        for ev in events:
            if ev.get("evt") == "nfc.files":
                files = list(ev.get("files") or [])
                break
        if filter_text:
            ft = filter_text.lower()
            files = [f for f in files if ft in str(f.get("name", "")).lower()]

    def draw() -> None:
        cols, rows = term_size()
        d = RFID_ROOTS[dir_idx % len(RFID_ROOTS)]
        filt = repr(filter_text) if filter_text else "off"
        lines = [
            "NFC Recorded Files",
            f"dir: {d}   filter={filt}",
            "-" * min(72, cols),
        ]
        if not files:
            lines.append("(empty)")
        else:
            scroll_local = min(scroll, max(0, len(files) - 1))
            budget = max(1, rows - 8)
            view = files[scroll_local : scroll_local + budget]
            for i, f in enumerate(view):
                mark = ">" if i == 0 else " "
                lines.append(
                    f"{mark} {f.get('name', ''):<28} {int(f.get('size') or 0):>8}  {f.get('path', '')}"
                )
        lines.append("-" * min(72, cols))
        lines.append("↑↓ scroll  ←→ root  /=filter  Enter=view  l=load  d=delete  q=back")
        ui.render(lines)

    try:
        ui.enter()
        refresh_list()
        draw()
        while session.alive:
            ch = keys.poll()
            if ch is None:
                time.sleep(0.05)
                continue
            if is_quit_key(ch):
                request_stop(session)
                reason = "quit"
                break
            if ch == "up":
                scroll = max(0, scroll - 1)
                draw()
                continue
            if ch == "down":
                scroll += 1
                draw()
                continue
            if ch == "left":
                dir_idx = (dir_idx - 1) % len(RFID_ROOTS)
                scroll = 0
                refresh_list()
                draw()
                continue
            if ch == "right":
                dir_idx = (dir_idx + 1) % len(RFID_ROOTS)
                scroll = 0
                refresh_list()
                draw()
                continue
            if ch == "/":
                filter_text = keys.read_line("filter: ").strip()
                scroll = 0
                refresh_list()
                draw()
                continue
            if not files:
                continue
            idx = min(scroll, len(files) - 1)
            path = str(files[idx].get("path") or "")
            if ch in ("\r", "\n"):
                ui.leave()
                session.write_line(f"nfc.files read path={path}")
                _, events = _drain_until(session, timeout=8.0, want={"nfc.file"})
                text = _collect_text(events, "nfc.file")
                print(f"\n=== {path} ===\n")
                print(text[:8000] if text else "(empty)")
                if text and len(text) > 8000:
                    print(f"\n… truncated ({len(text)} bytes)")
                input("\nEnter to continue…")
                ui.enter()
                draw()
            elif ch == "l":
                ui.leave()
                session.write_line(f"nfc.load path={path}")
                _, events = _drain_until(session, timeout=8.0, want={"nfc.dump"})
                print(f"Loaded {path}")
                _print_tag_events(events)
                op = input("Next: (e)mulate (w)rite (c)ompare (Enter=none): ").strip().lower()
                if op == "e":
                    session.write_line("nfc.emulate start")
                    _, ev2 = _drain_until(session, timeout=35.0, want={"nfc.status"})
                    for ev in ev2:
                        if ev.get("evt") == "nfc.status":
                            print(f"  {ev.get('state')} {ev.get('detail') or ''}")
                elif op == "w":
                    session.write_line("nfc.write")
                    _, ev2 = _drain_until(session, timeout=15.0)
                    _print_tag_events(ev2)
                elif op == "c":
                    session.write_line(f"nfc.compare path={path}")
                    _, ev2 = _drain_until(session, timeout=15.0, want={"nfc.status"})
                    for ev in ev2:
                        if ev.get("evt") == "nfc.status" and ev.get("op") == "compare":
                            print(
                                f"UID {'OK' if ev.get('uid_ok') else 'DIFF'}  "
                                f"file={ev.get('file_uid')} live={ev.get('live_uid')}"
                            )
                input("Enter…")
                ui.enter()
                draw()
            elif ch == "d":
                ui.leave()
                conf = input(f"Delete {path}? type YES: ").strip()
                if conf == "YES":
                    session.write_line(f"nfc.files delete path={path}")
                    _drain_until(session, timeout=3.0)
                    print("Deleted.")
                    refresh_list()
                input("Enter…")
                ui.enter()
                draw()
    except KeyboardInterrupt:
        request_stop(session)
        reason = "quit"
    finally:
        keys.close()
        ui.leave()
        if session.alive and reason != "bye":
            session.stop()
    return reason


def lab_menu(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    print("1) APDU  2) Compare  3) Field poll")
    c = input("Lab: ").strip()
    if c == "1":
        hexline = input("APDU hex: ").strip().replace(" ", "")
        session.write_line(f"nfc.apdu hex={hexline}")
        _, events = _drain_until(session, timeout=8.0, want={"nfc.status"})
        for ev in events:
            if ev.get("evt") == "nfc.status" and ev.get("op") == "apdu":
                print(f"SW={ev.get('sw')}  RX={ev.get('rx')}")
            if ev.get("evt") == "err":
                print(ev.get("msg"))
    elif c == "2":
        path = input("File path: ").strip()
        session.write_line(f"nfc.compare path={path}")
        print("Hold live tag…")
        _, events = _drain_until(session, timeout=15.0, want={"nfc.status"})
        for ev in events:
            if ev.get("evt") == "nfc.status" and ev.get("op") == "compare":
                print(
                    f"UID {'OK' if ev.get('uid_ok') else 'DIFF'}  "
                    f"SAK {'OK' if ev.get('sak_ok') else 'DIFF'}  "
                    f"pages {'OK' if ev.get('pages_ok') else 'DIFF'}"
                )
                print(f"  file: {ev.get('file_uid')}")
                print(f"  live: {ev.get('live_uid')}")
    elif c == "3":
        session.write_line("nfc.field start")
        print("Field poll — q / Esc = stop")
        keys = KeyReader()
        try:
            while session.alive:
                ch = keys.poll()
                if is_quit_key(ch):
                    request_stop(session)
                    break
                ev = session.read_event(timeout=0.15)
                if not ev:
                    continue
                if ev.get("evt") == "bye":
                    keys.close()
                    return "bye"
                if ev.get("evt") == "nfc.status" and ev.get("op") == "field":
                    print(
                        f"\r{ev.get('state'):8}  ms_since={ev.get('ms_since_hit')}  "
                        f"uid={ev.get('uid', '')}   ",
                        end="",
                        flush=True,
                    )
        finally:
            keys.close()
            print()
    input("Enter…")
    session.stop()
    return "quit"


def classic_menu(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    print("1) Dict full  2) Dict known  3) Magic probe")
    c = input("Classic: ").strip()
    if c == "1":
        session.write_line("nfc.dict mode=full")
        print("Hold Classic tag…")
        _, events = _drain_until(session, timeout=60.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "2":
        session.write_line("nfc.dict mode=known")
        print("Hold Classic tag…")
        _, events = _drain_until(session, timeout=60.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "3":
        session.write_line("nfc.magic probe")
        _, events = _drain_until(session, timeout=10.0, want={"nfc.status"})
        for ev in events:
            if ev.get("evt") in ("ack", "err"):
                print(f"{ev.get('evt')}: {ev.get('msg')}")
            if ev.get("evt") == "nfc.status":
                print(f"magic: {ev.get('state')}")
    input("Enter…")
    session.stop()
    return "quit"


def protocols_menu(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    print("1) ISO15693  2) DESFire  3) UL-C  4) FeliCa  5) Forced type")
    c = input("Proto: ").strip()
    if c == "1":
        session.write_line("nfc.iso15693 inventory")
        _, events = _drain_until(session, timeout=12.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "2":
        session.write_line("nfc.desfire")
        _, events = _drain_until(session, timeout=12.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "3":
        key = input("UL-C key 32 hex: ").strip().replace(" ", "")
        session.write_line(f"nfc.ulc key={key}")
        _, events = _drain_until(session, timeout=12.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "4":
        session.write_line("nfc.felica")
        _, events = _drain_until(session, timeout=12.0, want={"nfc.dump"})
        _print_tag_events(events)
    elif c == "5":
        t = input("type classic|ul|15693|felica|isodep: ").strip()
        session.write_line(f"nfc.read.as type={t}")
        _, events = _drain_until(session, timeout=12.0, want={"nfc.dump"})
        _print_tag_events(events)
    input("Enter…")
    session.stop()
    return "quit"


def write_ops(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    print("1) Write dump  2) Clone UID  3) Erase  4) Write NDEF URL")
    c = input("Op: ").strip()
    if c == "1":
        session.write_line("nfc.write")
    elif c == "2":
        session.write_line("nfc.clone")
    elif c == "3":
        session.write_line("nfc.erase")
    elif c == "4":
        url = input("URL: ").strip() or "https://github.com/kvx17/kvxputer"
        session.write_line(f"nfc.ndef.write type=url value={url}")
    else:
        session.stop()
        return "quit"
    print("Hold target tag…")
    _, events = _drain_until(session, timeout=20.0)
    _print_tag_events(events)
    for ev in events:
        if ev.get("evt") in ("ack", "err"):
            print(f"{ev.get('cmd')}: ok={ev.get('ok')} {ev.get('msg', '')}")
    input("Enter…")
    session.stop()
    return "quit"


def emulate_loaded(session: PcConnectSession) -> str:
    if not _ensure_manage(session):
        return "err"
    path = input("Dump path under /support_files/rfid/...: ").strip()
    session.write_line(f"nfc.load path={path}")
    _, events = _drain_until(session, timeout=8.0, want={"nfc.dump"})
    _print_tag_events(events)
    print("Emulating loaded dump… q / Esc stops")
    session.write_line("nfc.emulate start")
    keys = KeyReader()
    try:
        while session.alive:
            ch = keys.poll()
            if is_quit_key(ch):
                request_stop(session)
                drain_events(session, timeout=3.0, want={"nfc.status"}, allow_quit=False)
                break
            ev = session.read_event(timeout=0.2)
            if not ev:
                continue
            if ev.get("evt") == "bye":
                keys.close()
                return "bye"
            if ev.get("evt") == "nfc.status":
                print(f"  {ev.get('state')} {ev.get('detail') or ''}")
                if ev.get("state") in ("reader_hit", "stopped", "error"):
                    break
    finally:
        keys.close()
    input("Enter…")
    session.stop()
    return "quit"


def run(session: PcConnectSession) -> str:
    while session.alive:
        print("\n=== NFC Toolkit ===")
        print("  1) Live read")
        print("  2) Read once")
        print("  3) Emulate NDEF")
        print("  4) Emulate loaded dump")
        print("  5) Write / Clone / Erase / NDEF write")
        print("  6) Recorded files")
        print("  7) Lab (APDU / compare / field)")
        print("  8) Classic (dict / magic)")
        print("  9) Protocols")
        print("  q) back")
        raw = input("\nNFC: ").strip().lower()
        if raw in ("q", "quit", ""):
            return "quit"
        runners = {
            "1": live_read,
            "2": read_once,
            "3": emulate_ndef,
            "4": emulate_loaded,
            "5": write_ops,
            "6": files_browser,
            "7": lab_menu,
            "8": classic_menu,
            "9": protocols_menu,
        }
        fn = runners.get(raw)
        if not fn:
            continue
        reason = fn(session)
        if reason in ("bye", "disconnect", "err"):
            return reason
        # After a tool that called session.stop(), re-hello readiness is still open
        # but radio is idle — next tool calls nfc.manage start again.
    return "disconnect"
