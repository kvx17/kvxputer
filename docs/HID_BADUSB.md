# HID / BadUSB / BLE keyboard (Cardputer ADV)

## kvxkeyboard HID (preferred)

Use **kvxkeyboard HID** for unified USB/Bluetooth control (presenter, keyboard, media, mouse, jiggler, push-to-talk, etc.):

- **Bluetooth → kvxkeyboard HID**
- **USB → kvxkeyboard HID**

See [HID_REMOTE.md](HID_REMOTE.md).

## Legacy paths

Bruce Cardputer env enables:

- `-DUSB_as_HID=1`
- BadUSB under **USB**
- **BLE → Bad BLE** and legacy `(legacy)` keyboard/media/presenter entries

kvxputer **keeps Bruce** for BadUSB ducky scripts. Evil’s `Bad_Usb_Lib` core replace is **not** required unless Bruce HID regresses on ADV.

## Unit Scroll

Scroll is navigation only (Prev/Next/Sel). It does not inject HID keystrokes; Cardputer keyboard remains the typing source for BadUSB/BLE keyboard apps.

## Validation checklist

- [ ] Bluetooth → kvxkeyboard HID: presenter + keyboard over BLE
- [ ] USB → kvxkeyboard HID: mouse/clicker over USB
- [ ] USB → BadUSB: run a small ducky script over USB
- [ ] With Unit Scroll connected: navigate menus; scroll wheel in Mouse mode sends wheel
- [ ] With Unit Joystick2 on Grove PORT.A: Mouse mode stick moves cursor; click = left button
- [ ] HID Settings → Forget BLE pairings, then pair again
- [ ] HID Settings → Reconnect new BLE host (advertise under a new MAC)
