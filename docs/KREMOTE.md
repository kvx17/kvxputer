# kvxputer universal remote (kremote)

Infrared → **kvxputer universal remote** — learn 12 IR buttons from any TV remote and replay them from a concentric-circle pad on Cardputer ADV.

## Menu

Infrared → kvxputer universal remote:

| Entry | Action |
|-------|--------|
| Learn Remote | New or update a named profile |
| Use Remote | Pad control for a saved profile |
| Delete Remote | Remove a profile |
| Button Map | Short / hold reference |
| Settings | Portrait/Landscape, Normal/Swapped buttons |
| About | License note |

## Learned buttons

Fixed Flipper-compatible `name:` keys:

`POWER`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `OK`, `BACK`, `HOME`, `VOL+`, `VOL-`, `CHA+`, `CHA-`

Learn prompts: **OK** accept, **Right** skip, **Left** retry, **Esc** stop early and save what you have.

## Gestures (Use Remote)

| Physical | Short (Normal) | Hold (Normal) |
|----------|----------------|---------------|
| Up | UP | VOL+ |
| Down | DOWN | VOL− |
| Left | LEFT | CHA− |
| Right | RIGHT | CHA+ |
| OK | OK | HOME |
| Back | BACK | Exit pad |
| Back ×2 | POWER | — |

**Swapped** inverts short/hold for the six dual-action keys (directions ↔ Vol/Ch, OK ↔ Home). Back behavior is unchanged.

## Storage

- Profiles: `/support_files/infrared/remotes/kremote_<name>.ir` (SD preferred, LittleFS fallback)
- Settings: `kremotePortrait`, `kremoteButtonsSwapped` in `/root/kvxputer.conf`

`.ir` files use `Filetype: IR signals file` / Flipper body fields and open in Custom IR and Flipper Zero.
