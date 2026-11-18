#!/usr/bin/env python3
"""
scrolld - M5Stack Unit Scroll as a scroll wheel + button for the Cardputer Zero

- Finds the Unit Scroll on I2C at boot and keeps checking (hot-plug).
- Cardputer Zero: switches the Grove port to I2C and turns its 5V on.
- Output "inject": writes key events straight into the built-in keyboard's
  input device, so APPLaunch (menu, CLI, every app) sees them as real keys.
- Output "uinput": creates a separate virtual device (other Linux setups).
- Config: /etc/scrolld.conf   State: /run/scrolld/state.json   Reload: SIGHUP
"""
import configparser
import glob
import json
import os
import re
import signal
import time

from smbus2 import SMBus
from evdev import InputDevice, UInput, ecodes as e

CONF = os.environ.get("SCROLLD_CONF", "/etc/scrolld.conf")
STATE = os.environ.get("SCROLLD_STATE", "/run/scrolld/state.json")
SYSFS_LEDS = os.environ.get("SCROLLD_SYSFS", "/sys/class/leds")
DEVICE_NAME = "m5-unit-scroll"
# Cardputer Zero: Grove port defaults to UART with 5V off.
GROVE_MODE = "grove_fun"      # 0 = UART, 1 = I2C (I2C1 = /dev/i2c-1)
GROVE_5V = "grove_5v_out"     # 0 = off, 1 = on

# Unit Scroll registers (same family as M5 Unit Encoder)
ADDR, REG_BTN, REG_RGB, REG_INC, REG_FW = 0x40, 0x20, 0x30, 0x50, 0xFE

POLL_S = 0.01        # 100 Hz polling
RETRY_S = 2.0        # how often to look for the unit when it's not connected
MAX_ERRORS = 5       # consecutive I2C errors before treating it as unplugged
MAX_STEPS = 50       # ignore absurd readings (bus noise)

DEFAULTS = {
    "bus": "auto",
    "output": "auto",
    "target": "/dev/input/by-path/platform-3f804000.i2c-event",
    "mode": "custom",
    "orientation": "normal",
    "cw": "right shift+pagedown",
    "ccw": "left shift+pageup",
    "screen_detect": "yes",
    "launcher": "APPLaunch",
    "cli_cw": "shift+pagedown",
    "cli_ccw": "shift+pageup",
    "press": "enter",
    "hold": "esc",
    "hold_ms": "600",
    "led": "0,0,40",
    "grove_setup": "yes",
}
ORIENTATIONS = ("normal", "inverted", "horizontal", "horizontal-inverted")
MODES = ("custom", "keys", "wheel")
OUTPUTS = ("auto", "inject", "uinput")
ALIASES = {
    "MIDDLE": "BTN_MIDDLE", "MIDDLE_CLICK": "BTN_MIDDLE",
    "LEFT_CLICK": "BTN_LEFT", "RIGHT_CLICK": "BTN_RIGHT",
    "RETURN": "KEY_ENTER", "ESCAPE": "KEY_ESC",
    "SHIFT": "KEY_LEFTSHIFT", "CTRL": "KEY_LEFTCTRL", "CONTROL": "KEY_LEFTCTRL",
    "ALT": "KEY_LEFTALT", "META": "KEY_LEFTMETA", "SUPER": "KEY_LEFTMETA",
    "PGUP": "KEY_PAGEUP", "PGDN": "KEY_PAGEDOWN",
    "PAGE_UP": "KEY_PAGEUP", "PAGE_DOWN": "KEY_PAGEDOWN",
    "DEL": "KEY_DELETE", "PERIOD": "KEY_DOT",
}


def log(msg):
    print(msg, flush=True)


# ---------- key actions ----------
# An action is a sequence of combos: "shift+pagedown right" = Shift+PgDn, then Right.

def _key(name):
    n = name.strip().upper().replace("-", "_")
    n = ALIASES.get(n, n)
    if not n.startswith(("KEY_", "BTN_")):
        n = "KEY_" + n
    if n not in e.ecodes:
        raise ValueError(f"unknown key '{name}'")
    return n


def canonical_action(text):
    """'shift+pagedown right' -> 'KEY_LEFTSHIFT+KEY_PAGEDOWN KEY_RIGHT', 'none' -> 'none'."""
    t = str(text).strip()
    if t.lower() in ("", "none", "off"):
        return "none"
    combos = []
    for combo in re.split(r"[\s,]+", t):
        keys = [k for k in combo.split("+") if k]
        if keys:
            combos.append("+".join(_key(k) for k in keys))
    return " ".join(combos) if combos else "none"


def action_codes(text):
    n = canonical_action(text)
    return [] if n == "none" else [[e.ecodes[k] for k in c.split("+")] for c in n.split()]


def code_name(code):
    for table in (getattr(e, "KEY", {}), getattr(e, "BTN", {})):
        n = table.get(code)
        if n:
            return n[0] if isinstance(n, (list, tuple)) else n
    return str(code)


# ---------- config ----------

def load_config():
    cp = configparser.ConfigParser()
    cp.read_dict({"scroll": DEFAULTS})
    cp.read(CONF)
    c = cp["scroll"]

    def pick(key, allowed):
        v = c.get(key, DEFAULTS[key]).strip().lower()
        return v if v in allowed else DEFAULTS[key]

    cfg = {
        "bus": c.get("bus", "auto").strip().lower(),
        "output": pick("output", OUTPUTS),
        "target": c.get("target", DEFAULTS["target"]).strip(),
        "mode": pick("mode", MODES),
        "orientation": pick("orientation", ORIENTATIONS),
        "grove_setup": c.get("grove_setup", "yes").strip().lower() in ("yes", "true", "1", "on"),
        "screen_detect": c.get("screen_detect", "yes").strip().lower() in ("yes", "true", "1", "on"),
        "launcher": c.get("launcher", DEFAULTS["launcher"]).strip() or DEFAULTS["launcher"],
    }
    for key in ("cw", "ccw", "cli_cw", "cli_ccw", "press", "hold"):
        try:
            cfg[key] = action_codes(c.get(key))
        except ValueError as ex:
            log(f"config: bad '{key}' ({ex}), using default")
            cfg[key] = action_codes(DEFAULTS[key])
    try:
        cfg["hold_ms"] = max(100, int(c.get("hold_ms")))
    except ValueError:
        cfg["hold_ms"] = 600
    try:
        rgb = [max(0, min(255, int(x))) for x in c.get("led").split(",")]
        cfg["led"] = rgb if len(rgb) == 3 else [0, 0, 40]
    except ValueError:
        cfg["led"] = [0, 0, 40]
    return cfg


def resolve_output(cfg):
    """Return 'inject' or 'uinput' for the 'auto' setting."""
    if cfg["output"] != "auto":
        return cfg["output"]
    return "inject" if os.path.exists(cfg["target"]) else "uinput"


# ---------- screen detection ----------
# APPLaunch's built-in CLI starts a shell on a pseudo-terminal while it is open.
# If such a shell exists under the launcher process, the CLI is on screen.

SHELLS = {"bash", "sh", "dash", "zsh", "fish", "ash", "ksh", "busybox"}
PROC = os.environ.get("SCROLLD_PROC", "/proc")


def _proc_table():
    procs = {}
    for d in os.listdir(PROC):
        if not d.isdigit():
            continue
        try:
            with open(f"{PROC}/{d}/stat") as f:
                stat = f.read()
        except OSError:
            continue
        i, j = stat.find("("), stat.rfind(")")
        comm = stat[i + 1:j]
        fields = stat[j + 2:].split()
        try:
            procs[int(d)] = (int(fields[1]), comm, int(fields[4]))   # ppid, comm, tty_nr
        except (IndexError, ValueError):
            continue
    return procs


def _cmdline(pid):
    try:
        with open(f"{PROC}/{pid}/cmdline", "rb") as f:
            return f.read().replace(b"\0", b" ").decode(errors="replace")
    except OSError:
        return ""


def cli_shell(launcher_match):
    """Return (pid, name) of a shell the launcher runs on a pty, or None."""
    procs = _proc_table()
    launchers = {pid for pid, (_, comm, _) in procs.items()
                 if launcher_match in comm or launcher_match in _cmdline(pid)}
    if not launchers:
        return None
    for pid, (ppid, comm, tty) in procs.items():
        if comm not in SHELLS or not 136 <= (tty >> 8) & 0xFFF <= 143:   # 136-143 = /dev/pts
            continue
        p, hops = ppid, 0
        while p > 1 and hops < 10:
            if p in launchers:
                return pid, comm
            p = procs.get(p, (0,))[0]
            hops += 1
    return None


# ---------- hardware ----------

def _sysfs(name):
    return os.path.join(SYSFS_LEDS, name, "brightness")


def grove_read():
    """{'mode': 'i2c'|'uart'|None, '5v': bool|None}; None = not a Cardputer Zero."""
    out = {}
    for key, name in (("mode", GROVE_MODE), ("5v", GROVE_5V)):
        try:
            with open(_sysfs(name)) as f:
                v = int(f.read().strip() or 0) > 0
        except (OSError, ValueError):
            v = None
        out[key] = v
    if out["mode"] is not None:
        out["mode"] = "i2c" if out["mode"] else "uart"
    return out


def grove_setup():
    """Switch the Grove port to I2C and turn on its 5V. Returns True if anything changed."""
    changed = False
    for name in (GROVE_MODE, GROVE_5V):
        path = _sysfs(name)
        try:
            with open(path) as f:
                if int(f.read().strip() or 0) > 0:
                    continue
            with open(path, "w") as f:
                f.write("1")
            changed = True
        except (OSError, ValueError):
            pass
    return changed


def list_buses():
    nums = []
    for p in glob.glob("/dev/i2c-*"):
        m = re.search(r"(\d+)$", p)
        if m:
            nums.append(int(m.group(1)))
    return sorted(nums)


def probe(busnum):
    """Return an open SMBus if a Unit Scroll answers on this bus, else None."""
    try:
        bus = SMBus(busnum)
    except OSError:
        return None
    try:
        if bus.read_byte_data(ADDR, REG_BTN) in (0, 1):
            return bus
    except OSError:
        pass
    bus.close()
    return None


def find_scroll(bus_setting="auto"):
    candidates = list_buses() if bus_setting == "auto" else [int(bus_setting)]
    for n in candidates:
        bus = probe(n)
        if bus:
            return n, bus
    return None, None


def read_fw(bus):
    try:
        return bus.read_byte_data(ADDR, REG_FW)
    except OSError:
        return None


def s16(lo, hi):
    v = lo | (hi << 8)
    return v - 0x10000 if v & 0x8000 else v


# ---------- outputs ----------

class UinputOut:
    kind = "uinput"

    def __init__(self, keys):
        # REL_X/REL_Y + BTN_LEFT make udev/libinput treat it as a mouse (for the wheel).
        rel = [e.REL_X, e.REL_Y, e.REL_WHEEL, e.REL_HWHEEL]
        rel += [getattr(e, n) for n in ("REL_WHEEL_HI_RES", "REL_HWHEEL_HI_RES") if hasattr(e, n)]
        keys = set(keys) | {e.BTN_LEFT, e.BTN_RIGHT, e.BTN_MIDDLE}
        self.dev = UInput({e.EV_KEY: sorted(keys), e.EV_REL: rel}, name=DEVICE_NAME,
                          vendor=0x0001, product=0x0186, version=1)
        self.missing = []
        try:
            self.path = self.dev.device.path
        except AttributeError:
            self.path = None

    def write(self, t, c, v):
        self.dev.write(t, c, v)

    def syn(self):
        self.dev.syn()

    def close(self):
        self.dev.close()


class InjectOut:
    """Writes events into an existing input device (the built-in keyboard)."""
    kind = "inject"

    def __init__(self, path, keys):
        self.path = os.path.realpath(path)
        self.dev = InputDevice(self.path)
        supported = set(self.dev.capabilities().get(e.EV_KEY, []))
        # The kernel silently drops keys the device doesn't declare.
        self.missing = sorted(set(keys) - supported)

    def write(self, t, c, v):
        self.dev.write(t, c, v)

    def syn(self):
        self.dev.syn()

    def close(self):
        self.dev.close()


# ---------- daemon ----------

class Daemon:
    def __init__(self):
        self.cfg = load_config()
        self.bus = self.busnum = self.out = self.fw = None
        self.mode = None
        self.running, self.reload = True, False
        self.errors = 0
        self.btn_down, self.t_down, self.hold_fired = False, 0.0, False
        self.last_state = object()
        self.screen_t, self.screen_cli = 0.0, False

    def write_state(self, connected):
        key = (connected, self.busnum)
        if key == self.last_state:
            return
        self.last_state = key
        st = {"connected": connected, "bus": self.busnum, "firmware": self.fw,
              "output": self.out.kind if self.out else None,
              "target": self.out.path if self.out else None,
              "mode": self.mode,
              "missing": [code_name(c) for c in self.out.missing] if self.out else [],
              "pid": os.getpid(), "since": int(time.time())}
        try:
            os.makedirs(os.path.dirname(STATE), exist_ok=True)
            tmp = STATE + ".tmp"
            with open(tmp, "w") as f:
                json.dump(st, f)
            os.replace(tmp, STATE)
        except OSError as ex:
            log(f"cannot write state: {ex}")

    def used_keys(self):
        keys = {e.KEY_UP, e.KEY_DOWN, e.KEY_LEFT, e.KEY_RIGHT} if self.mode == "keys" else set()
        for k in ("press", "hold") + (("cw", "ccw", "cli_cw", "cli_ccw") if self.mode == "custom" else ()):
            for combo in self.cfg[k]:
                keys.update(combo)
        return keys

    def open_output(self):
        kind = resolve_output(self.cfg)
        self.mode = self.cfg["mode"]
        if kind == "inject":
            if self.mode == "wheel":
                log("mode 'wheel' needs uinput output; using 'custom' instead")
                self.mode = "custom"
            out = InjectOut(self.cfg["target"], self.used_keys())
        else:
            out = UinputOut(self.used_keys())
        if out.missing:
            log("warning: target device doesn't support "
                + ", ".join(code_name(c) for c in out.missing) + " - those keys will be ignored")
        log(f"output: {out.kind} {out.path or ''} mode: {self.mode}")
        return out

    def set_led(self, rgb):
        try:
            self.bus.write_i2c_block_data(ADDR, REG_RGB, [0] + list(rgb))
        except OSError:
            pass

    def connect(self):
        if self.cfg["grove_setup"] and grove_setup():
            log("Grove port switched to I2C with 5V on")
            time.sleep(0.5)
        num, bus = find_scroll(self.cfg["bus"])
        if bus is None:
            self.write_state(False)
            return False
        try:
            self.out = self.open_output()
        except OSError as ex:
            log(f"cannot open output: {ex}")
            bus.close()
            self.write_state(False)
            return False
        self.busnum, self.bus = num, bus
        self.fw = read_fw(bus)
        try:
            bus.read_i2c_block_data(ADDR, REG_INC, 2)   # discard steps from before
        except OSError:
            pass
        self.set_led(self.cfg["led"])
        self.errors, self.btn_down = 0, False
        log(f"Unit Scroll connected on i2c-{num} (fw {self.fw})")
        self.write_state(True)
        return True

    def disconnect(self, reason):
        if self.out:
            self.out.close()
        if self.bus:
            self.bus.close()
        self.out = self.bus = self.busnum = self.fw = None
        log(f"Unit Scroll disconnected ({reason})")
        self.write_state(False)

    def send(self, action):
        for combo in action:
            for k in combo:
                self.out.write(e.EV_KEY, k, 1)
            self.out.syn()
            for k in reversed(combo):
                self.out.write(e.EV_KEY, k, 0)
            self.out.syn()

    def rotate(self, steps):
        o = self.cfg["orientation"]
        if o.endswith("inverted"):
            steps = -steps
        horizontal = o.startswith("horizontal")
        if self.mode == "wheel":
            # clockwise (+) = scroll down / right
            if horizontal:
                axis, hires, val = e.REL_HWHEEL, getattr(e, "REL_HWHEEL_HI_RES", None), steps
            else:
                axis, hires, val = e.REL_WHEEL, getattr(e, "REL_WHEEL_HI_RES", None), -steps
            self.out.write(e.EV_REL, axis, val)
            if hires is not None:
                self.out.write(e.EV_REL, hires, val * 120)
            self.out.syn()
            return
        if self.mode == "keys":
            if horizontal:
                action = [[e.KEY_RIGHT if steps > 0 else e.KEY_LEFT]]
            else:
                action = [[e.KEY_DOWN if steps > 0 else e.KEY_UP]]
        else:
            prefix = "cli_" if self.in_cli() else ""
            action = self.cfg[prefix + ("cw" if steps > 0 else "ccw")]
        for _ in range(abs(steps)):
            self.send(action)

    def in_cli(self):
        """Is the launcher's CLI open? Checked only when the wheel turns, cached for 0.3 s."""
        if not self.cfg["screen_detect"]:
            return False
        now = time.monotonic()
        if now - self.screen_t > 0.3:
            try:
                cli = cli_shell(self.cfg["launcher"]) is not None
            except OSError:
                cli = False
            if cli != self.screen_cli:
                log(f"screen: {'CLI' if cli else 'menu/app'}")
            self.screen_t, self.screen_cli = now, cli
        return self.screen_cli

    def button(self, pressed, now):
        press, hold = self.cfg["press"], self.cfg["hold"]
        if pressed and not self.btn_down:
            self.btn_down, self.t_down, self.hold_fired = True, now, False
        elif pressed and self.btn_down:
            if hold and not self.hold_fired and (now - self.t_down) * 1000 >= self.cfg["hold_ms"]:
                self.send(hold)             # fires while still held
                self.hold_fired = True
        elif not pressed and self.btn_down:
            self.btn_down = False
            if not self.hold_fired and press:
                self.send(press)            # short press fires on release

    def nap(self, seconds):
        end = time.monotonic() + seconds
        while self.running and not self.reload and time.monotonic() < end:
            time.sleep(0.1)

    def poll_once(self):
        lo, hi = self.bus.read_i2c_block_data(ADDR, REG_INC, 2)
        steps = s16(lo, hi)
        if steps and abs(steps) <= MAX_STEPS:
            self.rotate(steps)
        self.button(self.bus.read_byte_data(ADDR, REG_BTN) == 0, time.monotonic())

    def run(self):
        signal.signal(signal.SIGHUP, lambda *_: setattr(self, "reload", True))
        for s in (signal.SIGTERM, signal.SIGINT):
            signal.signal(s, lambda *_: setattr(self, "running", False))
        log("scrolld started, looking for Unit Scroll...")
        while self.running:
            if self.reload:
                self.reload = False
                self.cfg = load_config()
                log("config reloaded")
                if self.out:
                    self.disconnect("reconfiguring")
            if self.out is None:
                if not self.connect():
                    self.nap(RETRY_S)
                continue
            try:
                self.poll_once()
                self.errors = 0
            except OSError:
                self.errors += 1
                if self.errors >= MAX_ERRORS:
                    self.disconnect("not responding")
                    continue
            time.sleep(POLL_S)
        if self.out:
            self.set_led([0, 0, 0])
            self.disconnect("stopping")


if __name__ == "__main__":
    Daemon().run()
