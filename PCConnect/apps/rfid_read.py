"""RFID / NFC UID read stream."""

from __future__ import annotations

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import LiveTable, run_live_table


def run(session: PcConnectSession) -> str:
    def render_row(d: dict) -> str:
        return (
            f"{(d.get('uid') or ''):<24}  {(d.get('type') or '')[:20]:<20}  "
            f"SAK={d.get('sak', '-')}  ATQA={d.get('atqa', '-')}  pages={d.get('pages', '?')}"
        )

    table = LiveTable(
        title="RFID Read",
        app_id="rfid.read",
        columns=["uid", "type", "sak", "atqa", "pages"],
        key_field="uid",
        sort_keys=["uid", "type"],
        bell_on_new=True,
        render_row=render_row,
        text_fields=["uid", "type", "sak", "atqa"],
    )
    return run_live_table(session, APP_COMMANDS["rfid.read"], table, "rfid")
