"""BLE Scan — live advertisement table."""

from __future__ import annotations

from typing import Dict, List, Optional

from protocol import APP_COMMANDS, PcConnectSession

from apps._common import LiveTable, run_live_table

# Bluetooth SIG company identifiers (little-endian first two bytes of mfg).
_COMPANY_IDS: Dict[int, str] = {
    0x004C: "Apple",
    0x0006: "Microsoft",
    0x0075: "Samsung",
    0x00E0: "Google",
}


def decode_company(mfg_hex: Optional[str]) -> str:
    if not mfg_hex:
        return ""
    parts = mfg_hex.replace(",", " ").split()
    if len(parts) < 2:
        return ""
    try:
        lo = int(parts[0], 16)
        hi = int(parts[1], 16)
    except ValueError:
        return ""
    cid = lo | (hi << 8)
    name = _COMPANY_IDS.get(cid)
    if name:
        return f"{name} (0x{cid:04X})"
    return f"0x{cid:04X}"


def run(session: PcConnectSession) -> str:
    min_rssi = -999
    name_filter = ""

    def row_filter(r: dict) -> bool:
        if int(r.get("rssi", -999)) < min_rssi:
            return False
        if name_filter:
            n = (r.get("name") or "").lower()
            if name_filter.lower() not in n:
                return False
        return True

    def render_row(d: dict) -> str:
        name = (d.get("name") or "")[:22]
        return (
            f"{d.get('rssi', 0):5d}  {d.get('mac', ''):<17}  {name:<22}  "
            f"{(d.get('addr_type') or '')[:6]:<6}  {(d.get('services') or '')[:20]}"
        )

    def render_detail(d: dict) -> List[str]:
        company = decode_company(d.get("mfg_hex"))
        lines = [
            f"  name:       {d.get('name') or '(none)'}",
            f"  mac:        {d.get('mac')}",
            f"  addr_type:  {d.get('addr_type')}",
            f"  rssi:       {d.get('rssi')} dBm",
            f"  services:   {d.get('services') or '-'}",
            f"  tx_power:   {d.get('tx_power')}",
            f"  appearance: {d.get('appearance')}",
            f"  mfg_hex:    {d.get('mfg_hex') or '-'}",
            f"  company:    {company or '-'}",
        ]
        return lines

    table = LiveTable(
        title="BLE Scan",
        app_id="ble.scan",
        columns=["rssi", "mac", "name", "addr_type", "services"],
        key_field="mac",
        sort_keys=["rssi", "name", "mac"],
        render_row=render_row,
        render_detail=render_detail,
        row_filter=row_filter,
        text_fields=["name", "mac", "services", "mfg_hex"],
        help_extra="n=name-filter  r=min-rssi",
    )

    def on_key_extra(ch: str, tbl: LiveTable, keys) -> Optional[str]:
        nonlocal min_rssi, name_filter
        if ch == "n" and keys is not None:
            name_filter = keys.read_line("name contains (empty=clear): ").strip()
            tbl._dirty = True
            return "redraw"
        if ch == "r" and keys is not None:
            raw = keys.read_line("min RSSI (e.g. -70, empty=off): ").strip()
            if not raw:
                min_rssi = -999
            else:
                try:
                    min_rssi = int(raw)
                except ValueError:
                    pass
            tbl._dirty = True
            return "redraw"
        return None

    return run_live_table(
        session,
        APP_COMMANDS["ble.scan"],
        table,
        "ble",
        on_key_extra=on_key_extra,
    )
