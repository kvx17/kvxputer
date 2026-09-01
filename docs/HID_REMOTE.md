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
| Keyboard | Live text mirror. Hold **FN** for an on-screen layer: `1`–`0`, `-`, `=` → F1–F12; Ins/PrtSc/Pause/Home/End/PgUp/PgDn/Esc/NumLk/ScrLk/Menu/Del; **t** Alt+Tab, **w** Win+Tab, **x** Ctrl+Shift+Esc, **d** Ctrl+Alt+Del |
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
- Forget BLE pairings (drops bonds; advertise so a host can pair again)
- Reconnect to a new BLE host (new MAC, wait for a different device)
- Mouse sensitivity, jiggler intervals, clicker delay/button, PTT preset

Settings persist in `kvxConfig` (`/conf.json`).

Mouse mode also reads **M5 Unit Joystick2** on Grove PORT.A (I2C 0x63) when present: stick moves the cursor, click is left button, hold is right click. Press **D** to invert up/down (persists). Keyboard arrows still work.

## Transport notes

- **USB**: Keyboard and mouse modes use separate TinyUSB HID devices; mode switch may re-init USB.
- **BLE**: Single `BleCompositeHid` session stays connected while switching modes.

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
