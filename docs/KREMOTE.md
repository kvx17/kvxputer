# kvxputer universal remote (kremote)

Infrared → **kvxputer universal remote** — learn 12 IR buttons from any TV remote and replay them from Virtual Remote on Cardputer.

## Menu

Infrared → kvxputer universal remote:

| Entry | Action |
|-------|--------|
| Learn Remote | Hardware-specific RX reminder (OK continues / Esc cancels); then new or update a named profile |
| Use Remote | Pick a saved profile (`F` stars/unstars); opens **Virtual Remote** directly |
| Favorites | List only favorited remotes; opens the same file-action menu as Browse IR (does not open the pad automatically) |
| Browse IR | SD / LittleFS / Recent `.ir` picker starting in the Settings IR folder (default learned remotes) |
| Delete Remote | Remove a profile (also drops it from favorites) |
| Button Map | Virtual Remote key reference |
| Settings | Portrait/Landscape, Normal/Swapped buttons, IR hardware, Browse IR folder |
| About | License note |

### File-action menu (Favorites and Browse IR)

After a `.ir` file is chosen:

1. Choose cmd
2. Spam all
3. Virtual Remote
4. Add Favorite / Remove Favorite (when the path has a slug)
5. Menu (returns to the caller: file list or favorites list)

## Settings

| Option | Effect |
|--------|--------|
| Orientation | Portrait / Landscape (stored; **does not affect Virtual Remote**) |
| Buttons | Normal / Swapped short-hold map (stored; **does not affect Virtual Remote**) |
| IR hardware | **Cardputer IR** (onboard TX LED), **Unit IR** ([M5 Unit IR](https://docs.m5stack.com/en/unit/ir) on Grove: yellow TX / white RX), or **Both** |
| IR folder | SD start folder for **Browse IR** only. Learned/generated profiles still save to `/kvxputer/infrared/remotes`. **Reset default** restores that path. |

**Both** fires the onboard LED and Unit IR TX one after the other (two GPIOs; the driver is one pin at a time). That is a back-to-back repeat, not a merged carrier. Custom IR / TV-B-Gone keep using Infrared → Config pins; this setting applies only while kremote is sending or learning.

Learn always needs an RX. Cardputer ADV has none onboard — plug Unit IR (or another Grove RX) even if TX is set to Cardputer IR. With Unit IR / Both, Learn uses Grove white (SCL).

## Learned buttons

Fixed Flipper-compatible `name:` keys:

`POWER`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `OK`, `BACK`, `HOME`, `VOL+`, `VOL-`, `CHA+`, `CHA-`

Learn prompts: **OK** accept, **Right** skip, **Left** retry, **Esc** stop early and save what you have.

## Virtual Remote keys

Fixed Cardputer map. Commands are matched from the `.ir` file by `name:` (case-insensitive; spaces/underscores ignored). First matching alias wins. Missing commands are dimmed and do not transmit.

| Cardputer key | Remote command aliases |
|---------------|------------------------|
| `1`–`9`, `0` | `1`–`9`, `0`, `NUM_*`, `DIGIT_*` |
| `` ` `` (Esc) | Power off: `POWER_OFF`, `OFF`, `SHUTDOWN`, `POWER OFF` (or `POWER` if that is the only power name) |
| `o` / `O` | Power on: `POWER_ON`, `ON`, `POWER ON` (or `POWER` if single) |
| `;` | `UP` |
| `.` | `DOWN` |
| `,` | `LEFT` |
| `/` | `RIGHT` |
| Enter / OK | `OK`, `ENTER`, `SELECT` |
| `-` | `VOL-`, `VOL_DOWN`, `VOLUME_DOWN`, `V-` |
| `=` | `VOL+`, `VOL_UP`, `VOLUME_UP`, `V+` |
| `[` | `CHA-`, `CH-`, `CH_DOWN`, `CHANNEL_DOWN` |
| `]` | `CHA+`, `CH+`, `CH_UP`, `CHANNEL_UP` |
| `m` | `MENU` |
| `b` | `BACK` |
| `h` | `HOME` |
| `u` | `MUTE` |
| `f` | `FASTFORWARD`, `FF`, `FORWARD`, `NEXT` |
| `r` | `REWIND`, `RW`, `PREV`, `PREVIOUS` |
| Space / `p` | `PLAY`, `PAUSE`, `PLAYPAUSE`, `PLAY_PAUSE` |

**Exit:** Del / Backspace. Esc only sends power-off (does not leave the view). G0 / Home still leaves immediately.

Held keys repeat IR every 180 ms. The UI uses the system top bar (remote name, clock, icons, battery) plus a concentric pad and a strip of the other bindings.

A learned 12-name profile lights arrows, OK, volume, channel, Back, Home, and power. Digits / Menu / Mute / transport stay dim until those names exist in the file.

## Storage

- Profiles: `/kvxputer/infrared/remotes/kremote_<name>.ir` (SD preferred, LittleFS fallback; legacy `/support_files/infrared/remotes/`)
- Device settings in `/root/kvxputer.conf`: `kremotePortrait`, `kremoteButtonsSwapped`, `kremoteIrHw` (0 onboard / 1 Unit IR / 2 both), `kremoteBrowseFolder` (empty = learned remotes)
- Favorites (SD only): `/kvxputer/kvxuniversalremote/userSettings.json`  
  Shape: `{ "favorites": ["living_room", "bedroom"] }` — profile slugs. No LittleFS fallback; missing SD blocks save.

`.ir` files use `Filetype: IR signals file` / Flipper body fields and open in Custom IR and Flipper Zero.
