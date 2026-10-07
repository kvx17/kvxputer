"""USB NDJSON protocol client for kvxputer PC Connect.

IMPORTANT: On ESP32-S3 USB-Serial-JTAG (303a:1001), pyserial's Serial.open()
triggers USB_UART_CHIP_RESET even with DTR/RTS cleared. We open the tty with
os.open + termios instead and never call pyserial open().
"""

from __future__ import annotations

import json
import os
import select
import subprocess
import sys
import time
from typing import Any, Dict, Generator, List, Optional, Tuple

from serial.tools import list_ports

try:
    import termios
    import fcntl
except ImportError:
    termios = None  # type: ignore
    fcntl = None  # type: ignore


class PcConnectError(Exception):
    pass


class NeedPcConnectApp(PcConnectError):
    """Device answered hello but PC Connect app is not open yet."""

    def __init__(self, hello: Dict[str, Any]):
        super().__init__(hello.get("hint") or "open USB → PC Connect on device")
        self.hello = hello


TIOCMGET = 0x5415
TIOCMBIC = 0x5417
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004


class RawTty:
    """Minimal serial port using os.open (no USB chip reset on ESP32-S3)."""

    def __init__(self, port: str, timeout: float = 0.2):
        self.port = port
        self.timeout = timeout
        self._fd = -1
        self._buf = bytearray()
        self._open()

    def _open(self) -> None:
        if sys.platform.startswith("linux"):
            try:
                subprocess.run(
                    ["stty", "-F", self.port, "-hupcl", "raw", "-echo", "115200"],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=1.0,
                )
            except Exception:
                pass

        self._fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

        if termios is not None:
            attrs = termios.tcgetattr(self._fd)
            # iflag
            attrs[0] = attrs[0] & ~(
                termios.IGNBRK
                | termios.BRKINT
                | termios.PARMRK
                | termios.ISTRIP
                | termios.INLCR
                | termios.IGNCR
                | termios.ICRNL
                | termios.IXON
            )
            # oflag
            attrs[1] = attrs[1] & ~termios.OPOST
            # cflag
            attrs[2] = attrs[2] | (termios.CLOCAL | termios.CREAD)
            attrs[2] = attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
            attrs[2] = attrs[2] | termios.CS8
            attrs[2] = attrs[2] & ~getattr(termios, "HUPCL", 0)
            # lflag
            attrs[3] = attrs[3] & ~(
                termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN
            )
            try:
                termios.cfsetispeed(attrs, termios.B115200)
                termios.cfsetospeed(attrs, termios.B115200)
            except Exception:
                pass
            termios.tcsetattr(self._fd, termios.TCSANOW, attrs)

        if fcntl is not None:
            try:
                buf = __import__("array").array("i", [TIOCM_DTR | TIOCM_RTS])
                fcntl.ioctl(self._fd, TIOCMBIC, buf)
            except Exception:
                pass

    @property
    def is_open(self) -> bool:
        return self._fd >= 0

    def close(self) -> None:
        if self._fd >= 0:
            try:
                if fcntl is not None:
                    buf = __import__("array").array("i", [TIOCM_DTR | TIOCM_RTS])
                    fcntl.ioctl(self._fd, TIOCMBIC, buf)
            except Exception:
                pass
            try:
                os.close(self._fd)
            except Exception:
                pass
            self._fd = -1

    def write(self, data: bytes) -> int:
        if self._fd < 0:
            raise PcConnectError("port closed")
        return os.write(self._fd, data)

    def flush(self) -> None:
        if self._fd >= 0 and termios is not None:
            try:
                termios.tcdrain(self._fd)
            except Exception:
                pass

    def _fill(self, timeout: Optional[float]) -> None:
        if self._fd < 0:
            return
        wait = self.timeout if timeout is None else timeout
        if wait is None or wait < 0:
            wait = 0.0
        r, _, _ = select.select([self._fd], [], [], wait)
        if not r:
            return
        try:
            chunk = os.read(self._fd, 4096)
            if chunk:
                self._buf.extend(chunk)
        except BlockingIOError:
            pass

    def readline(self, timeout: Optional[float] = None) -> bytes:
        deadline = time.time() + (self.timeout if timeout is None else max(0.0, timeout))
        while True:
            nl = self._buf.find(b"\n")
            if nl >= 0:
                line = bytes(self._buf[: nl + 1])
                del self._buf[: nl + 1]
                return line
            remaining = deadline - time.time()
            if remaining <= 0:
                return b""
            self._fill(min(0.1, remaining))

    def reset_input_buffer(self) -> None:
        self._buf.clear()
        if self._fd < 0:
            return
        # Drain kernel buffer.
        end = time.time() + 0.05
        while time.time() < end:
            try:
                chunk = os.read(self._fd, 4096)
                if not chunk:
                    break
            except BlockingIOError:
                break


class PcConnectSession:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.2, settle: float = 0.2):
        self.port = port
        self._ser = RawTty(port, timeout=timeout)
        if settle > 0:
            time.sleep(settle)
        try:
            self._ser.reset_input_buffer()
        except Exception:
            pass
        self.hello: Dict[str, Any] = {}
        self._alive = True

    def close(self) -> None:
        self._alive = False
        try:
            self._ser.close()
        except Exception:
            pass

    @property
    def alive(self) -> bool:
        return self._alive and self._ser.is_open

    def write_line(self, line: str) -> None:
        if not self.alive:
            raise PcConnectError("port closed")
        self._ser.write((line.strip() + "\n").encode("utf-8", errors="replace"))
        self._ser.flush()

    def read_event(self, timeout: Optional[float] = None) -> Optional[Dict[str, Any]]:
        if not self.alive:
            return None
        try:
            raw = self._ser.readline(timeout=timeout)
        except OSError:
            self._alive = False
            return None
        if not raw:
            return None
        try:
            text = raw.decode("utf-8", errors="replace").strip()
        except Exception:
            return None
        if not text or not text.startswith("{"):
            return None
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            return None
        if not isinstance(obj, dict):
            return None
        if obj.get("evt") == "bye":
            self._alive = False
        return obj

    def do_hello(self, timeout: float = 4.0, require_ready: bool = True) -> Dict[str, Any]:
        deadline = time.time() + timeout
        saw_not_ready: Optional[Dict[str, Any]] = None
        while time.time() < deadline:
            try:
                self.write_line("hello")
            except PcConnectError:
                raise
            slice_end = min(time.time() + 1.0, deadline)
            while time.time() < slice_end:
                ev = self.read_event(timeout=0.15)
                if not ev or ev.get("evt") != "hello":
                    continue
                self.hello = ev
                if ev.get("ready") is False:
                    saw_not_ready = ev
                    if require_ready:
                        raise NeedPcConnectApp(ev)
                    return ev
                return ev
        if saw_not_ready is not None:
            raise NeedPcConnectApp(saw_not_ready)
        raise PcConnectError("no hello response (flash firmware + open USB → PC Connect)")

    def wait_until_ready(self, prompt: bool = True, timeout: float = 120.0) -> Dict[str, Any]:
        deadline = time.time() + timeout
        if prompt:
            print("Device found, but PC Connect is not open yet.")
            print("On the Cardputer: USB → PC Connect")
            print("Waiting", end="", flush=True)
        while time.time() < deadline:
            try:
                return self.do_hello(timeout=2.0, require_ready=True)
            except NeedPcConnectApp:
                if prompt:
                    print(".", end="", flush=True)
                time.sleep(0.4)
            except PcConnectError:
                if prompt:
                    print(".", end="", flush=True)
                time.sleep(0.4)
        if prompt:
            print()
        raise PcConnectError("timed out waiting for PC Connect app")

    def stop(self, timeout: float = 2.0) -> bool:
        if not self.alive:
            return False
        try:
            self.write_line("stop")
        except PcConnectError:
            return False
        deadline = time.time() + timeout
        while time.time() < deadline:
            ev = self.read_event(timeout=0.2)
            if not ev:
                if not self.alive:
                    return False
                continue
            if ev.get("evt") == "bye":
                return False
            if ev.get("evt") == "ack" and ev.get("cmd") == "stop":
                return True
            if ev.get("evt") == "err" and ev.get("cmd") == "stop":
                return False
        return True

    def start(self, command: str) -> Generator[Dict[str, Any], None, None]:
        self.write_line(command)
        while self.alive:
            ev = self.read_event(timeout=0.25)
            if ev is None:
                continue
            evt = ev.get("evt")
            if evt == "bye":
                yield ev
                return
            if evt == "err":
                yield ev
                return
            if evt == "ack":
                yield ev
                continue
            yield ev


def _port_priority(info) -> Tuple[int, str]:
    dev = info.device or ""
    vid = info.vid
    if vid == 0x303A:
        return (0, dev)
    if "ACM" in dev or "USB" in dev.upper() or "usbmodem" in dev.lower():
        return (1, dev)
    if dev.startswith("/dev/ttyS"):
        return (9, dev)
    return (5, dev)


def discover(timeout: float = 3.0, verbose: bool = False) -> List[Dict[str, Any]]:
    found: List[Dict[str, Any]] = []
    ports = sorted(list_ports.comports(), key=_port_priority)
    for info in ports:
        if (info.device or "").startswith("/dev/ttyS"):
            continue
        port = info.device
        sess: Optional[PcConnectSession] = None
        try:
            if verbose:
                print(f"  probing {port} ({info.description or ''}) …", flush=True)
            # Short settle — os.open does not reboot the chip.
            sess = PcConnectSession(port, timeout=0.2, settle=0.15)
            try:
                hello = sess.do_hello(timeout=timeout, require_ready=False)
            except NeedPcConnectApp as e:
                hello = e.hello
            found.append(
                {
                    "port": port,
                    "name": hello.get("name", "kvxputer"),
                    "mac": hello.get("mac", "?"),
                    "apps": list(hello.get("apps") or []),
                    "ready": hello.get("ready", True),
                    "hello": hello,
                    "desc": info.description or "",
                }
            )
            if verbose:
                state = (
                    "PC Connect ready"
                    if hello.get("ready", True) is not False
                    else "CLI (open PC Connect)"
                )
                print(f"    found: {hello.get('name')} {hello.get('mac')} [{state}]", flush=True)
            sess.close()
        except Exception as e:
            if verbose:
                print(f"    skip: {e}", flush=True)
            if sess is not None:
                try:
                    sess.close()
                except Exception:
                    pass
    return found


APP_LABELS = {
    "wifi.analyzer": "kvx wifi analyzer",
    "ble.scan": "BLE Scan",
    "ble.flipper": "Wall of Flipper",
    "ble.airtag": "Wall of Airtag",
    "ble.skimmer": "Skimmer Detector",
}

APP_COMMANDS = {
    "wifi.analyzer": "wifi.analyzer start",
    "ble.scan": "ble.scan start",
    "ble.flipper": "ble.flipper start",
    "ble.airtag": "ble.airtag start",
    "ble.skimmer": "ble.skimmer start",
}
