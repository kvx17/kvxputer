"""Skimmer Detector — BLE GAP names / MAC prefixes."""

from __future__ import annotations

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import LiveTable, run_live_table


def run(session: PcConnectSession) -> str:
    def render_row(d: dict) -> str:
        flag = "! " if d.get("matched") else "  "
        name = (d.get("name") or d.get("mac") or "")[:24]
        rule = d.get("rule") or ""
        return f"{flag}{d.get('rssi', 0):5d}dBm  {d.get('mac', ''):<17}  {name:<24}  {rule}"

    table = LiveTable(
        title="Skimmer Detector",
        app_id="ble.skimmer",
        columns=["matched", "rssi", "mac", "name", "rule"],
        key_field="mac",
        sort_keys=["rssi", "name", "mac"],
        bell_on_new=True,
        bell_predicate=lambda r: bool(r.get("matched")),
        render_row=render_row,
        text_fields=["name", "mac", "rule"],
        help_extra="m=matched-only",
    )
    return run_live_table(session, APP_COMMANDS["ble.skimmer"], table, "skimmer")
