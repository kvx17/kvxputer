# scrolld: M5Stack Unit Scroll for the Cardputer Zero

This lets you use the [M5Stack Unit Scroll](https://docs.m5stack.com/en/unit/Unit_Scroll) as a scroll wheel with a clickable button on the Cardputer Zero. A background service reads the Scroll over I2C and sends its input to Linux as a virtual input device.

## Install over SSH

```bash
scp scrolld.tar.gz user@cardputer.local:~
ssh user@cardputer.local
tar xzf scrolld.tar.gz && cd scrolld
sudo ./install.sh
```

The installer adds the dependencies, sets the service to start at boot, starts it and prints the current status. To remove everything, run `sudo ./install.sh uninstall`.

## After installing

The service is already running and starts automatically at every boot. Check it with:

```bash
scrollctl status
```

The output should include `Service : running` and `Unit Scroll : yes (i2c-N, ...)`. Next, check that the wheel's input is getting through:

```bash
sudo scrollctl monitor
```

Turn and press the wheel. Each action should print a line such as `REL_WHEEL -1`, `KEY_ENTER down` or `KEY_ESC down`. Press Ctrl+C to stop.

### Managing the service

| Action | Command |
|---|---|
| Start | `sudo systemctl start scrolld` |
| Stop | `sudo systemctl stop scrolld` |
| Restart | `sudo systemctl restart scrolld` |
| Reload the config without restarting | `sudo systemctl reload scrolld` |
| Turn off start at boot | `sudo systemctl disable scrolld` |
| Turn on start at boot | `sudo systemctl enable scrolld` |
| Show service state | `systemctl status scrolld` |
| Watch the logs live | `journalctl -u scrolld -f` |

## Detection

- The Cardputer Zero's Grove port starts in UART (serial) mode with its 5V power off, so the Scroll can't be seen until both are changed. The service switches the port to I2C and turns the 5V on every time it starts. In I2C mode the Scroll is on bus 1 (`/dev/i2c-1`). To turn this off, run `sudo scrollctl grove-setup no`.
- At boot the service checks every I2C bus for the Scroll, so you don't need to set a bus number.
- The virtual device only exists while the Scroll is connected. If the Scroll isn't plugged in, the service waits and checks again every 2 seconds.
- You can plug it in or unplug it while the Cardputer is on. The LED turns dim blue when the Scroll is detected.

## Behaviour

By default the service sends its key presses straight into the Cardputer's built-in keyboard device (`output = inject`). The launcher, the CLI and every app then see them as normal key presses, so nothing has to be rebuilt.

| Action | Main menu and apps | CLI |
|---|---|---|
| Turn clockwise | Right + Shift+PgDn (next app) | Shift+PgDn (scroll down) |
| Turn counter-clockwise | Left + Shift+PgUp (previous app) | Shift+PgUp (scroll up) |
| Short press | Enter | Enter |
| Hold (600 ms) | Esc | Esc |

**How it knows the CLI is open:** the launcher's CLI runs a shell on a terminal while it's open. Each time you turn the wheel, the service checks for that shell under the launcher process. If it finds one, it sends the `cli_cw` / `cli_ccw` keys. Otherwise it sends `cw` / `ccw`. The CLI only gets the scroll keys, because arrow keys would snap its view back to the bottom. `scrollctl status` shows what it detects in the `Screen now` line.

- A short press fires when you let go, because that's the only way to tell a tap from a hold.
- Every action can be changed. Keys joined with `+` are pressed together (`shift+pageup`), and several actions separated by spaces are sent one after another (`right shift+pagedown`).

### Modes

| Mode | What turning the wheel does |
|---|---|
| `custom` (default) | sends the `cw` / `ccw` keys |
| `keys` | sends ↑/↓, or ←/→ with a horizontal orientation |
| `wheel` | sends real mouse-wheel events. Needs `output uinput`, which apps that only read the built-in keyboard will ignore. |

## `scrollctl`

- `scrollctl` with nothing after it opens an interactive menu.
- `scrollctl status` shows whether the Scroll is connected and your current settings.
- `sudo scrollctl monitor` shows key events live, so you can check it's working. With `inject` output, keys you type on the built-in keyboard show up too.
- `sudo scrollctl cw "right shift+pagedown"` and `sudo scrollctl ccw "left shift+pageup"` change what turning the wheel sends in the menu and apps.
- `sudo scrollctl cli-cw "shift+pagedown"` and `sudo scrollctl cli-ccw "shift+pageup"` change what it sends while the CLI is open.
- `sudo scrollctl screen-detect no` turns CLI detection off, so `cw` / `ccw` are always used.
- `sudo scrollctl press enter`, `sudo scrollctl hold esc`, `sudo scrollctl hold-time 800`
- `sudo scrollctl mode custom|keys|wheel`
- `sudo scrollctl orientation inverted` (also `normal`, `horizontal`, `horizontal-inverted`) applies to `keys` and `wheel` modes. In `custom` mode, swap `cw` and `ccw` instead.
- `sudo scrollctl output auto|inject|uinput` and `sudo scrollctl target /dev/input/...` choose where the keys are sent.
- `sudo scrollctl led 0,40,0` sets the LED colour. `0,0,0` turns it off.
- `sudo scrollctl grove-setup yes|no` turns automatic Grove port setup on or off.
- `scrollctl keys` lists common key names.
- Changes take effect straight away, with no reboot.

The settings are saved in `/etc/scrolld.conf`. You can also edit that file by hand, then run `sudo systemctl reload scrolld`. To go back to the defaults, run `sudo cp scrolld.conf /etc/scrolld.conf && sudo systemctl reload scrolld`.

## Troubleshooting

**`Unit Scroll : no`**
1. Check the `Grove port` line in `scrollctl status`. It must say `I2C, 5V on`. If it doesn't, switch it by hand:
   ```bash
   echo 1 | sudo tee /sys/class/leds/grove_fun/brightness      # I2C mode
   echo 1 | sudo tee /sys/class/leds/grove_5v_out/brightness   # 5V on
   ```
   You can also turn on 5V from the device's **Settings > ExtPort** menu.
2. Check the cable. The Scroll has to be in the Grove port (HY2.0-4P).
3. Check that I2C buses exist: `ls /dev/i2c-*`. If none are listed, run `sudo raspi-config`, go to **Interface Options → I2C**, turn it on and reboot. You can instead add `dtparam=i2c_arm=on` to `/boot/firmware/config.txt`.
4. Scan bus 1 by hand: `sudo i2cdetect -y 1`. The Scroll shows up as `40`.
5. If `40` appears on a bus but the Scroll still isn't detected, set that bus by hand: `sudo scrollctl bus N`.

**`40` shows up but reads fail, or the keyboard misbehaves while the Scroll is plugged in**
The Grove port shares bus 1 with the keyboard and other internal chips. The Pi's I2C hardware also has trouble with STM32-based units like the Scroll. Try lowering the bus speed: add `dtparam=i2c_arm_baudrate=50000` to `/boot/firmware/config.txt` and reboot.

**`Service : stopped` or the service keeps restarting**
- Read the error with `journalctl -u scrolld -n 50`.
- `ModuleNotFoundError: evdev` or `smbus2` means a dependency is missing. Run `sudo ./install.sh` again, or `sudo apt install python3-evdev python3-smbus2`.
- An error about `/dev/uinput` means the uinput module isn't loaded. Run `sudo modprobe uinput`, then `sudo systemctl restart scrolld`.

**`scrollctl monitor` says the Scroll isn't active**
Run `scrollctl status` first. The Scroll has to be connected.

**The monitor shows events but nothing happens on screen**
- If `status` shows `! Not supported by the target device`, the built-in keyboard can't send those keys. Pick other keys.
- Find which keys a screen really uses: run `sudo evtest`, choose the built-in keyboard, and press the keys that work there (for example the ones that move the main menu). Then set them with `cw` and `ccw`. If the menu moves with `,` and `.`, use `sudo scrollctl cw "dot shift+pagedown"`, but note that `.` will then also be typed in the CLI.

**The CLI doesn't scroll, or the menu stops moving**
Check the `Screen now` line in `scrollctl status`. With the CLI open it should say `CLI (bash, pid ...)`, and on the main menu it should say `menu/app`.
- **It says `menu/app` while the CLI is open:** check the launcher's process name with `ps -eo pid,comm,args | grep -i launch`, then set it, for example `sudo scrollctl launcher M5CardputerZero`.
- **It still says `CLI` after you leave the CLI:** the launcher keeps the shell running in the background, so the service can't tell the screens apart. Run `sudo scrollctl screen-detect no` and pick one set of keys.

**The wheel scrolls the wrong way**
Run `sudo scrollctl orientation inverted`.

**Presses register as holds, or holds as presses**
Change the hold time, for example `sudo scrollctl hold-time 800`.

**`permission denied`**
Commands that change settings, and `monitor`, need `sudo`.

**Start from scratch**
```bash
sudo ./install.sh uninstall
sudo ./install.sh
```

## Limitations

- **Only the CLI is detected.** Every other screen gets the menu keys (`cw` / `ccw`). If an app needs different keys, change them for everything, or tell me which app it is.
- **The CLI scrolls one step of its scrollback per detent.** Each detent sends one PgUp/PgDn to the terminal.
- **The plain Linux text console** (outside the launcher) has no scrollback at all on current kernels.

Run `sudo scrollctl monitor` first to confirm the hardware side works.
