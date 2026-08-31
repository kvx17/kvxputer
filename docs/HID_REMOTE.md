# HID Remote

Unified USB/Bluetooth HID remote for Cardputer ADV, inspired by
[flipper-hid-app](https://github.com/fidian/flipper-hid-app) (GPL-3.0).

## Launch

- **Bluetooth → HID Remote** — uses transport from settings (default USB; set to BLE in HID Settings)
- **Others → BadUSB & HID → HID Remote** — launches with USB transport

Legacy Bruce entries (`BLE Keyboard`, `Media Cmds`, `Presenter mode`, `USB Keyboard`, `USB Clicker`) remain for one release with `(legacy)` suffix.

## Modes

| Mode | Description |
|------|-------------|
| Presenter | Arrow keys, Page Up/Down, Home, End; scroll navigates list |
| Presenter Vertical | Same keys; ;/. send Left/Right, ,/ send Up/Down (portrait) |
| Keyboard | Full Cardputer keyboard with live on-screen text mirror |
| Media | Play/pause, volume, track skip |
| Apple Music | macOS-oriented media controls |
| Movie | Play/pause, seek, fullscreen |
| Mouse | Move cursor, click, scroll wheel |
| Shorts | TikTok / YouTube Shorts navigation |
| Mouse Clicker | Auto-click at configured interval |
| Mouse Jiggler | Periodic small movements |
| Stealth Jiggler | Random interval/distance movements |
| Push-to-Talk | Hold SEL to unmute (Ctrl+Shift+M) |

## Settings (HID Remote → Settings)

- Transport: USB or Bluetooth
- BLE device name
- Mouse sensitivity, jiggler intervals, clicker delay/button, PTT preset

Settings persist in `kvxConfig` (`/conf.json`).

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
