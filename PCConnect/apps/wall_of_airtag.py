"""Wall of Airtag — Apple Find My / AirTag advertisements."""

from __future__ import annotations

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import LiveTable, run_live_table


def approx_m(rssi: int) -> float:
    d = 10.0 ** ((-59.0 - float(rssi)) / 20.0)
    return max(0.1, min(99.0, d))


def run(session: PcConnectSession) -> str:
    def render_row(d: dict) -> str:
        rssi = int(d.get("rssi", -99))
        return (
            f"{rssi:5d}dBm  ~{approx_m(rssi):4.1f}m  {d.get('mac', ''):<17}  "
            f"batt={d.get('battery', '?')}  "
            f"{'sep' if d.get('separated') else 'near'}  "
            f"key={d.get('key_prefix') or '-'}"
        )

    table = LiveTable(
        title="Wall of Airtag",
        app_id="ble.airtag",
        columns=["rssi", "mac", "battery", "key_prefix"],
        key_field="mac",
        sort_keys=["rssi", "mac", "battery"],
        bell_on_new=True,
        render_row=render_row,
        text_fields=["mac", "battery", "key_prefix"],
    )
    return run_live_table(session, APP_COMMANDS["ble.airtag"], table, "airtag")
