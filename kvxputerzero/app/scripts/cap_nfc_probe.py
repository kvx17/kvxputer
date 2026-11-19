#!/usr/bin/env python3
"""Cap-CC1101 ST25R3916 field / REQA probe (Cardputer Zero).

  sudo python3 cap_nfc_probe.py
  sudo python3 cap_nfc_probe.py --hold
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

NFC_CS, CE1, POWER_EN = 22, 7, 26
SPI_SPEED = 1_000_000

REG_IO1, REG_IO2 = 0x00, 0x01
REG_OP, REG_MODE, REG_BITRATE = 0x02, 0x03, 0x04
REG_ISOA, REG_AUX = 0x05, 0x0A
REG_RX1, REG_RX2, REG_RX3, REG_RX4 = 0x0B, 0x0C, 0x0D, 0x0E
REG_NRT1, REG_NRT2 = 0x10, 0x11
REG_IRQ_MAIN, REG_FIFO_ST1 = 0x1A, 0x1E
REG_ANT_A, REG_ANT_B = 0x26, 0x27
REG_TXDRV, REG_PTMOD = 0x28, 0x29
REG_FTH_A, REG_FTH_D = 0x2A, 0x2B
REG_REG_CTRL, REG_AD_RESULT = 0x2C, 0x25  # AD converter output (Measure Amplitude)
REG_AUXDISP, REG_ICID = 0x31, 0x3F

CMD_READ = 0x40
CMD_SET_DEFAULT, CMD_STOP = 0xC1, 0xC2
CMD_TX_REQA, CMD_FIELD_ON = 0xC6, 0xC8
CMD_MEASURE_AMP = 0xD3
CMD_RST_RXGAIN, CMD_ADJ_REG, CMD_CLR_FIFO = 0xD5, 0xD6, 0xDB
CMD_SPACE_B, CMD_TEST_ACCESS = 0xFB, 0xFC
FIFO_READ = 0x9F

TEST_OVERHEAT_REG = 0x04
TEST_OVERHEAT_BIT = 0x10

# Cap 3.3V SPI: miso_pd1|pd2|io_drv|sup3v|aat_en
IO2_CAP = 0xBC
# M5 default tx_am_modulation=13 → TX_DRIVER bits[7:4]; d_res=0
TXDRV_M5 = 0xD0
# Same AM, max driver resistance (lowest current) — probe thermal/antenna shorts
TXDRV_GENTLE = 0xDF


def sh(args: list[str]) -> None:
    subprocess.run(args, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def pin(offset: int, high: bool) -> None:
    sh(["pinctrl", "set", str(offset), "op", "dh" if high else "dl"])


class St25:
    def __init__(self) -> None:
        import spidev

        pin(CE1, True)
        pin(NFC_CS, True)
        self.spi = spidev.SpiDev()
        bus, dev = (0, 0)
        if not os.path.exists("/dev/spidev0.0"):
            bus, dev = (0, 1)
        self.spi.open(bus, dev)
        self.spi.mode = 1
        self.spi.max_speed_hz = SPI_SPEED
        self.spi.bits_per_word = 8
        try:
            self.spi.no_cs = True
        except Exception as e:
            print(f"warn: no_cs failed: {e}")
        pin(CE1, True)
        print(f"spi=/dev/spidev{bus}.{dev} soft-CS=GPIO{NFC_CS} CE1=GPIO{CE1} held high")

    def xfer(self, tx: list[int]) -> list[int]:
        pin(CE1, True)
        pin(NFC_CS, False)
        time.sleep(0.00002)
        rx = self.spi.xfer2([t & 0xFF for t in tx])
        pin(NFC_CS, True)
        return rx

    def wr(self, addr: int, val: int) -> None:
        self.xfer([addr & 0x3F, val & 0xFF])

    def rd(self, addr: int) -> int:
        return self.xfer([(addr & 0x3F) | CMD_READ, 0])[1]

    def wr_b(self, addr: int, val: int) -> None:
        self.xfer([CMD_SPACE_B, addr & 0x3F, val & 0xFF])

    def rd_b(self, addr: int) -> int:
        return self.xfer([CMD_SPACE_B, (addr & 0x3F) | CMD_READ, 0])[2]

    def rd_test(self, addr: int) -> int:
        # RFAL: CS-low [0xFC, reg|READ_MODE, 0]
        return self.xfer([CMD_TEST_ACCESS, (addr & 0x3F) | CMD_READ, 0])[2]

    def wr_test(self, addr: int, val: int) -> None:
        # RFAL WriteTestRegister: CS-low [0xFC, reg|WRITE_MODE, val]
        self.xfer([CMD_TEST_ACCESS, addr & 0x3F, val & 0xFF])

    def unlock_overheat(self) -> tuple[int, int, bool]:
        """RFAL ChangeTestRegisterBits(0x04, 0x10, 0x10) — RMW + verify."""
        before = self.rd_test(TEST_OVERHEAT_REG)
        after_val = (before & ~TEST_OVERHEAT_BIT) | TEST_OVERHEAT_BIT
        self.wr_test(TEST_OVERHEAT_REG, after_val)
        after = self.rd_test(TEST_OVERHEAT_REG)
        ok = bool(after & TEST_OVERHEAT_BIT)
        return before, after, ok

    def cmd(self, c: int) -> None:
        self.xfer([c & 0xFF])

    def read_irq(self) -> tuple[int, int, int, int]:
        # Burst-read MAIN..TIMER (1A..1D); error is 1C
        rx = self.xfer([REG_IRQ_MAIN | CMD_READ, 0, 0, 0, 0])
        return rx[1], rx[2], rx[3], rx[4]

    def dump(self, label: str) -> None:
        io1, io2 = self.rd(REG_IO1), self.rd(REG_IO2)
        op, mode = self.rd(REG_OP), self.rd(REG_MODE)
        aux, txd = self.rd(REG_AUXDISP), self.rd(REG_TXDRV)
        try:
            reg_b = self.rd_b(0x2C)
        except Exception:
            reg_b = -1
        try:
            t04 = self.rd_test(TEST_OVERHEAT_REG)
        except Exception:
            t04 = -1
        print(
            f"  [{label}] IO1=0x{io1:02X} IO2=0x{io2:02X} OP=0x{op:02X} MODE=0x{mode:02X} "
            f"TXDRV=0x{txd:02X} AUX=0x{aux:02X} osc={bool(aux & 0x10)} tx_on={bool(aux & 0x20)} "
            f"regB=0x{reg_b:02X} test04=0x{t04:02X}"
        )

    def close(self) -> None:
        try:
            self.wr(REG_OP, 0x00)
        except Exception:
            pass
        pin(NFC_CS, True)
        self.spi.close()


def wait_osc(st: St25, ms: int = 100) -> bool:
    end = time.time() + ms / 1000
    while time.time() < end:
        if st.rd(REG_AUXDISP) & 0x10:
            return True
        time.sleep(0.002)
    return False


def base_init(st: St25, txdrv: int = TXDRV_M5) -> bool:
    st.cmd(CMD_STOP)
    st.wr(REG_OP, 0x00)
    time.sleep(0.002)
    st.cmd(CMD_SET_DEFAULT)
    time.sleep(0.003)
    before, after, ok = st.unlock_overheat()
    print(f"  unlock overheat: test04 0x{before:02X} -> 0x{after:02X} bit4={'OK' if ok else 'FAIL'}")
    if not ok:
        # Fallback absolute write (M5 one-shot FC/04/10)
        st.xfer([CMD_TEST_ACCESS, TEST_OVERHEAT_REG, TEST_OVERHEAT_BIT])
        after2 = st.rd_test(TEST_OVERHEAT_REG)
        print(f"  fallback absolute write: test04=0x{after2:02X} bit4={bool(after2 & TEST_OVERHEAT_BIT)}")
        ok = bool(after2 & TEST_OVERHEAT_BIT)
    st.wr(REG_IO1, 0x07)
    st.wr(REG_IO2, IO2_CAP)
    st.wr(REG_TXDRV, txdrv)
    st.wr(REG_FTH_A, 0x13)
    st.wr(REG_FTH_D, 0x02)
    st.wr(REG_PTMOD, 0x5F)
    st.wr(REG_ANT_A, 0x82)
    st.wr(REG_ANT_B, 0x82)
    st.wr(REG_REG_CTRL, 0x00)  # auto regulators
    st.wr(REG_OP, 0x83)  # en | en_fd auto (M5 begin)
    if not wait_osc(st):
        raise SystemExit("FAIL: oscillator not ready")
    st.cmd(CMD_ADJ_REG)
    time.sleep(0.01)
    # Re-assert unlock after adj (paranoia — SET_DEFAULT is the documented reset point)
    _, after, ok2 = st.unlock_overheat()
    print(f"  re-unlock after adj: test04=0x{after:02X} bit4={'OK' if ok2 else 'FAIL'}")
    st.dump("after adj_reg")
    return ok and ok2


def configure_nfca_rx(st: St25) -> None:
    st.wr(REG_MODE, 0x09)
    st.wr(REG_BITRATE, 0x00)
    st.wr(REG_ISOA, 0x00)
    st.wr(REG_RX1, 0x08)
    st.wr(REG_RX2, 0x2D)
    st.wr(REG_RX3, 0xD8)
    st.wr(REG_RX4, 0x22)
    st.cmd(CMD_RST_RXGAIN)
    st.wr(REG_NRT1, 0x00)
    st.wr(REG_NRT2, 0x80)


def try_field(st: St25, name: str, use_collision_cmd: bool, use_aat: bool, txdrv: int) -> bool:
    print(f"\n== strategy: {name} ==")
    unlock_ok = base_init(st, txdrv=txdrv)
    if not unlock_ok:
        print("  WARN: overheat unlock bit did not stick")
    if not use_aat:
        st.wr(REG_IO2, (IO2_CAP & ~0x20) | 0x80)  # keep sup3v, drop aat
        st.dump("aat off")
    configure_nfca_rx(st)
    st.wr(REG_OP, 0x83)  # en|en_fd
    if use_collision_cmd:
        st.cmd(CMD_FIELD_ON)
        time.sleep(0.005)
        st.dump("after FIELD_ON cmd")
        # M5 nfc_initial_field_on: SET tx_en|rx_en (keep en)
        op = st.rd(REG_OP)
        st.wr(REG_OP, op | 0x48)  # rx_en|tx_en
        time.sleep(0.01)
        st.dump("m5 set tx|rx")
    else:
        # Staged TX
        st.wr(REG_OP, 0x88)  # en|tx
        time.sleep(0.01)
        st.dump("en|tx")
        st.wr(REG_OP, 0xCB)  # en|rx|tx|en_fd
        time.sleep(0.01)
        st.dump("en|rx|tx")

    main, nfc, err, timer = st.read_irq()
    print(f"  IRQ main=0x{main:02X} nfc=0x{nfc:02X} err=0x{err:02X} pta=0x{timer:02X}")
    if use_collision_cmd:
        if nfc & 0x02:
            print("  I_cat: collision avoidance passed, field switched on")
        if nfc & 0x04:
            print("  I_cac: external field collision — field NOT switched on")
    # AUXDISP.tx_on = frame in flight only; field presence is judged by REQA below.
    return True


def reqa(st: St25) -> bytes | None:
    st.wr(REG_ISOA, 0x01)  # antcl: 7-bit short frame
    st.wr(REG_AUX, st.rd(REG_AUX) | 0x80)  # no_crc_rx
    st.read_irq()  # clear
    st.cmd(CMD_CLR_FIFO)
    st.cmd(CMD_TX_REQA)
    seen = [0, 0, 0, 0]
    data = None
    end = time.time() + 0.12
    while time.time() < end:
        irq = st.read_irq()
        seen = [a | b for a, b in zip(seen, irq)]
        if st.rd(REG_FIFO_ST1) >= 2:
            data = bytes(st.xfer([FIFO_READ, 0, 0])[1:])
            break
        if seen[0] & 0x10 or seen[1] & 0x40:  # I_rxe or I_nre
            break
        time.sleep(0.001)
    main, nfc, err, _ = seen
    flags = []
    if main & 0x08:
        flags.append("txe(REQA sent)")
    if main & 0x20:
        flags.append("rxs(tag started reply)")
    if main & 0x10:
        flags.append("rxe")
    if main & 0x04:
        flags.append("col")
    if nfc & 0x40:
        flags.append("nre(no response)")
    if err:
        flags.append(f"err=0x{err:02X}")
    print(f"  REQA IRQs: main=0x{main:02X} nfc=0x{nfc:02X} -> {', '.join(flags) or 'none'}")
    return data


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hold", action="store_true")
    args = ap.parse_args()

    print(f"probe Cap ST25 soft-CS=GPIO{NFC_CS} mode1")
    try:
        with open("/sys/class/leds/ext_5v_out/brightness", "w") as f:
            f.write("1")
        print("ext_5v_out=1")
    except OSError as e:
        print(f"ext_5v_out: {e}")
    pin(POWER_EN, True)
    time.sleep(0.5)

    st = St25()
    try:
        ic = st.rd(REG_ICID)
        typ = (ic >> 3) & 0x1F
        print(f"IC_IDENTITY=0x{ic:02X} type={typ} {'OK' if typ in (5, 6) else 'BAD'}")
        if typ not in (5, 6):
            return 1

        # Quick test-reg R/W sanity before strategies
        t0 = st.rd_test(TEST_OVERHEAT_REG)
        st.wr_test(TEST_OVERHEAT_REG, t0 | TEST_OVERHEAT_BIT)
        t1 = st.rd_test(TEST_OVERHEAT_REG)
        print(f"test-reg smoke: 0x{t0:02X} -> write|0x10 -> 0x{t1:02X} sticky={bool(t1 & TEST_OVERHEAT_BIT)}")

        strategies = [
            ("collision_m5", True, True, TXDRV_M5),
            ("staged_tx", False, True, TXDRV_M5),
        ]
        print("\nPlace a tag / Flipper (Emulate NFC-A) flat on the Cap PCB NFC coil now.")
        time.sleep(2)
        got = None
        for name, coll, aat, txdrv in strategies:
            try_field(st, name, coll, aat, txdrv)
            for i in range(5):
                atqa = reqa(st)
                if atqa:
                    print(f"  ATQA: {atqa.hex(' ').upper()} OK (try {i + 1})")
                    got = atqa
                    break
                time.sleep(0.05)
            if got:
                break
            print("  ATQA: (none)")

        if args.hold:
            print("holding field 10s…")
            time.sleep(10)
        if not got:
            print("\nFAIL: no ATQA. txe without rxs = field/TX OK but no tag reply (placement/antenna).")
            print("      no txe at all = TX path not running.")
            return 3
        return 0
    finally:
        st.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ModuleNotFoundError as e:
        print(f"FAIL: {e} — sudo apt-get install -y python3-spidev", file=sys.stderr)
        raise SystemExit(2)
