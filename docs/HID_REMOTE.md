# kvxkeyboard HID

Unified USB/Bluetooth HID keyboard/mouse, inspired by
[flipper-hid-app](https://github.com/fidian/flipper-hid-app) (GPL-3.0).

On the host, the device presents as a generic HID keyboard/mouse:

- **USB:** product `HID Keyboard`, manufacturer `Generic`, VID `0x1209` (pid.codes).
  These are board USB descriptors (TinyUSB starts at boot), so they apply to the
  whole USB device, not only HID mode.
- **BLE:** advertised name from settings (default **kvxKeyboard**), manufacturer
  `HID`, Generic HID appearance `0x03C0`, no Apple/Espressif PnP IDs. The
  firmware menu is still labeled **kvxkeyboard HID**.

## Launch

- **Bluetooth → kvxkeyboard HID** — uses transport from settings (default USB; set to BLE in HID Settings)
- **USB → kvxkeyboard HID** — launches with USB transport

Legacy Bruce entries (`BLE Keyboard`, `Media Cmds`, `Presenter mode`, `USB Keyboard`, `USB Clicker`) remain for one release with `(legacy)` suffix.

## Modes

| Mode | Description |
|------|-------------|
| Presenter | `;`/`,` = previous, `.`/`/`/Enter = next, `5` = F5 (start slideshow); `[`/`]`/PgUp/PgDn, `h`/`e` Home/End, **P** play/pause; FN+`;`/`.`/`,`/`/` = arrow keys |
| Presenter Vertical | Portrait cluster: `/` up, `.` right, `;` left, `,` down; **P** play/pause in the center; Enter next, `5` = F5 |
| Keyboard | Live text mirror. Press **FN** for a sticky on-screen layer (until a key, or FN again): `1`–`0`, `-`, `=` → F1–F12; Ins/PrtSc/Pause/Home/End/PgUp/PgDn/Esc/NumLk/ScrLk/Menu/Del; **t** Alt+Tab, **w** Win+Tab, **x** Ctrl+Shift+Esc, **d** Ctrl+Alt+Del. Sent keys appear on the mirror. **Opt+Ok** opens System Shortcuts |
| System Shortcuts | Pick-and-send common Win/Mac/Linux shortcuts (Alt+Tab, Alt+F4, Ctrl+Alt+Shift+V, clipboard, window, browser, terminal, …) |
| Media | Space play/pause, volume, track skip |
| Apple Music | macOS-oriented media controls |
| Movie | Play/pause, seek, fullscreen |
| Mouse | Move cursor, click, scroll wheel |
| Shorts | On-screen up / Space / down keys. **Ok** remaps the physical up/down keys (saved in settings) |
| Mouse Clicker | Auto-click at configured interval |
| Mouse Jiggler | Periodic small movements |
| Stealth Jiggler | Random interval/distance movements |
| Push-to-Talk | Hold Space to talk; red muted mic / green talking mic |

## Settings (kvxkeyboard HID → Settings)

- Transport: USB or Bluetooth
- **Rename hosts…** — name each filled slot (also under BLE Hosts → host → Rename)
- Default new-host name: template only for newly paired hosts that have no name yet
- BLE device name (default **kvxKeyboard**)
- Forget BLE pairings (drops bonds and clears host slots; advertising stops until you pick a slot)
- Disconnect / **Host slots…** (same 1–6 selector as startup) / Connect to new device at the top of Settings
- **BLE Hosts…** — numbered slots 1–6; connect/switch, rename, disconnect, forget one host, or pair into an empty slot
- **Keyboard LED: On/Off** — HID status LED on the Cardputer RGB (does not change global LED brightness)
- Mouse sensitivity, jiggler intervals, clicker delay/button, PTT preset

Settings persist in `kvxConfig` (`/conf.json`), including `hidRemoteHostSlots[6]` and `hidRemoteLedEnabled`.

Mouse mode also reads **M5 Unit Joystick2** on Grove PORT.A (I2C 0x63) when present: stick moves the cursor, click is left button, hold is right click. Press **D** to invert up/down (persists). Keyboard arrows still work.

## Multi-host slots (BLE)

Stable **host slots 1–6** map to keyboard keys `1`–`6` (independent of NimBLE bond list order).

**Startup (Bluetooth launch)** shows a slot screen:

| Color | Meaning |
|-------|---------|
| Green | Slot’s host is currently connected |
| Orange | Slot has a saved address (remembered) but not connected |
| Red | Empty slot |

- Press **1–6**: filled slot → **tap** to connect (wait **only for that** bonded host); **hold 2 s** for that host’s options (Connect/switch, Rename, Disconnect, Forget). Empty slot → open discoverable advertising and pair a **new** host into that slot. Other remembered hosts that sneak in are dropped with a quiet gap so they cannot steal the link.
- **S**: open HID Settings (wipe pairings, LED, rename hosts). ESC returns to the slot screen.
- **Ok/Enter**: if already connected, continue to modes; otherwise reconnect the preferred/first filled slot (same exclusive-host wait as `1`–`6`)
- **ESC**: leave the app

The slot screen is **not connectable** until you pick a slot. That stops every remembered phone from racing in.

**Settings → Host slots…** opens the same selector (ESC returns to Settings).

**Settings → Rename hosts…** lists filled slots so you can name each host (e.g. “iPhone”, “Linux”).

**Settings → BLE Hosts…** lists the same slots (not raw bond indices). Empty slots start pairing; filled slots offer Connect/switch, Rename, Disconnect, Forget. **Connect to new device** uses the first empty slot.

Forget one host clears that slot + alias + bond. Forget all clears every slot.

## LED (Cardputer)

While kvxkeyboard is open it owns the RGB LED (firmware purple/green is paused):

| State | LED |
|-------|-----|
| Waiting to switch a remembered host | Slow blue blink |
| Pairing a new host into an empty slot | Faster blue blink |
| Host contacting / securing (GAP up, not HID-ready yet) | Fast cyan blink |
| Connected | Dim solid blue (~20% of global LED brightness) |
| Link lost / wait expired | Solid red |
| Wrong host rejected | Short red flash, then blue blink |
| Forget succeeded | Brief green flash |
| HID init failed | Red blink a few times |

**Keyboard LED: Off** in HID Settings disables this without changing Config → LED brightness. G0 fake-off still blanks the LED. Leaving the app restores firmware LED status.

StickS3 has no RGB LED and no number-key slots; BLE exclusive-host policy still applies. Settings stay on the mode-picker row.

## Multi-host notes

The Cardputer is a BLE **peripheral** (keyboard). Hosts (phone/PC) connect *to* it; the ESP cannot dial out or measure which idle phone is “closest.”

Practical workflow:

1. On the slot screen (or **BLE Hosts**), press an empty slot number and pair from the phone/PC.
2. Pair a second host into another empty slot without forgetting the first. Other phones may still *try* to reconnect; they are dropped and must not win. You do **not** need those clients to Forget the keyboard.
3. Switch with keys **1–6** or **Connect / switch** in Settings. Only that host is allowed.
4. On the host, open Bluetooth and tap the keyboard if it does not auto-reconnect. That reconnect should stay up.
5. Use Forget only when retiring a device.

## Transport notes

- **USB**: Keyboard and mouse modes use separate TinyUSB HID devices; mode switch may re-init USB. USB launch skips the host-slot screen.
- **BLE**: Single `BleCompositeHid` session stays connected while switching modes.
- **Idle / filled slot**: the slot screen stays non-connectable until you pick a slot. Switching to a remembered host uses **open undirected HID advertising** (BlueZ needs this) and accepts only that host in software. Wrong bonded hosts (e.g. iOS) are disconnected immediately with a quiet gap so Linux can finish reconnect. If Linux already has a GAP link to the selected host, the firmware **keeps** that link instead of dropping it (dropping caused connect/disconnect loops). Directed / whitelist-only ADV is kept as a helper API but is not the exclusive-switch path. The mode picker does not advertise.
- **Bonds**: advertising is stopped and the filter list cleared before any unpair — editing bonds while advertising aborts the NimBLE host.
- **New-pair**: Open pairing rebuilds a full HID advertisement (flags, appearance `0x03C0`, HID UUID `0x1812`, name + scan response). Known hosts that connect anyway are disconnected with a short advertising pause so a new host can get in.
- **Client OS notes**: After picking a slot, if the host does not auto-link within a few seconds, open Bluetooth settings and tap **kvxKeyboard** (common on Windows/Linux; sometimes Android). iOS usually auto-reconnects — and will be rejected while you are waiting for another slot.
- **Bond / slot limit**: Cardputer builds allow 8 stored NimBLE bonds (`CONFIG_BT_NIMBLE_MAX_BONDS`). kvxkeyboard uses **6 host slots** so other BLE tools keep bond headroom. HID keeps one active link at a time (ESP32-S3 can multi-connect in hardware; this app does not). Duplicate slot entries for the same bond are collapsed automatically. Legacy configs with 8 slots migrate extras into empty 1–6 on load.

## Files

```
src/menu/ble/hid_remote/
  hid_remote_menu.cpp      — entry, mode picker, settings
  hid_remote_transport.cpp — USB/BLE session
  hid_remote_modes.cpp     — all control modes
  hid_remote_ui.cpp        — shared kvx UI chrome
lib/Bad_Usb_Lib/
  BleCompositeHid.h        — alias for BleKeyboard with mouse reports
  BleKeyboard.*            — keyboard + media + BLE mouse
```
