# HID / BadUSB / BLE keyboard (Cardputer ADV)

## HID Remote (preferred)

Use **HID Remote** for unified USB/Bluetooth control (presenter, keyboard, media, mouse, jiggler, push-to-talk, etc.):

- **Bluetooth → HID Remote**
- **Others → BadUSB & HID → HID Remote**

See [HID_REMOTE.md](HID_REMOTE.md).

## Legacy paths

Bruce Cardputer env enables:

- `-DUSB_as_HID=1`
- BadUSB under **Others → BadUSB & HID**
- **BLE → Bad BLE** and legacy `(legacy)` keyboard/media/presenter entries

kvxputer **keeps Bruce** for BadUSB ducky scripts. Evil’s `Bad_Usb_Lib` core replace is **not** required unless Bruce HID regresses on ADV.

## Unit Scroll

Scroll is navigation only (Prev/Next/Sel). It does not inject HID keystrokes; Cardputer keyboard remains the typing source for BadUSB/BLE keyboard apps.

## Validation checklist

- [ ] Bluetooth → HID Remote: presenter + keyboard over BLE
- [ ] Others → BadUSB & HID → HID Remote: mouse/clicker over USB
- [ ] Others → BadUSB: run a small ducky script over USB
- [ ] With Unit Scroll connected: navigate menus; scroll wheel in Mouse mode sends wheel
- [ ] Without Unit Scroll: same HID flows still work
