"""Wall of Flipper — Flipper Zero advertisements."""

from __future__ import annotations

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import LiveTable, run_live_table


def run(session: PcConnectSession) -> str:
    def render_row(d: dict) -> str:
        name = (d.get("name") or "Flipper")[:24]
        return (
            f"{d.get('rssi', 0):5d}dBm  {d.get('mac', ''):<17}  {name:<24}  "
            f"{d.get('services') or ''}"
        )

    table = LiveTable(
        title="Wall of Flipper",
        app_id="ble.flipper",
        columns=["rssi", "mac", "name", "services"],
        key_field="mac",
        sort_keys=["rssi", "name", "mac"],
        bell_on_new=True,
        render_row=render_row,
        text_fields=["name", "mac", "services", "mfg_hex"],
    )
    return run_live_table(session, APP_COMMANDS["ble.flipper"], table, "flipper")
