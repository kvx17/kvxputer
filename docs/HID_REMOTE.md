# kvxkeyboard HID

Unified USB/Bluetooth HID keyboard/mouse, inspired by
[flipper-hid-app](https://github.com/fidian/flipper-hid-app) (GPL-3.0).

On the host, the device presents as a generic HID keyboard/mouse:

- **USB:** product `HID Keyboard`, manufacturer `Generic`, VID `0x1209` (pid.codes).
  These are board USB descriptors (TinyUSB starts at boot), so they apply to the
  whole USB device, not only HID mode.
- **BLE:** advertised name from settings (default **Keyboard**), manufacturer
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
| Presenter Vertical | Same pad and keys, rotated 180° from the previous portrait layout; **P** in the center |
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
- Host name: header label when connected (Settings → Host name). USB cannot auto-detect the PC name; set it manually or rely on the BLE peer-address fallback.
- BLE device name (default **Keyboard**)
- Forget BLE pairings (drops bonds and clears host slots; advertise so a host can pair again)
- Disconnect / **Host slots…** (same 1–8 selector as startup) / Connect to new device at the top of Settings
- **BLE Hosts…** — numbered slots 1–8; connect/switch, rename, disconnect, forget one host, or pair into an empty slot
- Mouse sensitivity, jiggler intervals, clicker delay/button, PTT preset

Settings persist in `kvxConfig` (`/conf.json`), including `hidRemoteHostSlots[8]`.

Mouse mode also reads **M5 Unit Joystick2** on Grove PORT.A (I2C 0x63) when present: stick moves the cursor, click is left button, hold is right click. Press **D** to invert up/down (persists). Keyboard arrows still work.

## Multi-host slots (BLE)

Stable **host slots 1–8** map to keyboard keys `1`–`8` (independent of NimBLE bond list order).

**Startup (Bluetooth launch)** shows a slot screen:

| Color | Meaning |
|-------|---------|
| Green | Slot’s host is currently connected |
| Orange | Slot has a saved address (remembered) but not connected |
| Red | Empty slot |

- Press **1–8**: filled slot → disconnect and reconnect **only that** host; empty → open discoverable advertising and pair a **new** host into that slot (already-remembered hosts are rejected until Forgotten)
- **Ok/Enter**: if already connected, continue to modes; otherwise briefly try the preferred/first filled slot
- **ESC**: leave the app

**Settings → Host slots…** opens the same selector (ESC returns to Settings).

**Settings → BLE Hosts…** lists the same slots (not raw bond indices). Empty slots start pairing; filled slots offer Connect/switch, Rename, Disconnect, Forget. **Connect to new device** uses the first empty slot.

Forget one host clears that slot + alias + bond. Forget all clears every slot.

## Multi-host notes

The Cardputer is a BLE **peripheral** (keyboard). Hosts (phone/PC) connect *to* it; the ESP cannot dial out or measure which idle phone is “closest.”

Practical workflow:

1. On the slot screen (or **BLE Hosts**), press an empty slot number and pair from the phone/PC.
2. Pair a second host into another empty slot without forgetting the first.
3. Switch with keys **1–8** or **Connect / switch** in Settings.
4. On the host, open Bluetooth and tap the keyboard if it does not auto-reconnect.
5. You do **not** need to forget bonds to switch — only use Forget when retiring a device.

## Transport notes

- **USB**: Keyboard and mouse modes use separate TinyUSB HID devices; mode switch may re-init USB. USB launch skips the host-slot screen.
- **BLE**: Single `BleCompositeHid` session stays connected while switching modes.
- **Discoverable advertising**: Open pairing rebuilds a full HID advertisement (flags, appearance `0x03C0`, HID UUID `0x1812`, name + scan response) so phones can find the keyboard after reconnect/new-device flows.
- **Bond limit**: Cardputer builds allow 8 stored bonds (`CONFIG_BT_NIMBLE_MAX_BONDS`). HID keeps one active link at a time.

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
