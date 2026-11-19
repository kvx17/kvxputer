#!/usr/bin/env python3
import os,time,subprocess
NFC_CS,CE1,POWER_EN=22,7,26
def sh(a): subprocess.run(a,check=False,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def pin(n,h): sh(["pinctrl","set",str(n),"op","dh" if h else "dl"])
import spidev
try:
  open("/sys/class/leds/ext_5v_out/brightness","w").write("1")
except Exception: pass
pin(POWER_EN,True); pin(CE1,True); pin(NFC_CS,True); time.sleep(0.3)
spi=spidev.SpiDev(); bus,dev=(0,1) if not os.path.exists("/dev/spidev0.0") else (0,0)
spi.open(bus,dev); spi.mode=1; spi.max_speed_hz=1_000_000
try: spi.no_cs=True
except Exception: pass
pin(CE1,True)
def xfer(tx):
  pin(CE1,True); pin(NFC_CS,False); time.sleep(2e-5); rx=spi.xfer2([t&0xFF for t in tx]); pin(NFC_CS,True); return rx
def rd(a): return xfer([(a&0x3F)|0x40,0])[1]
def wr(a,v): xfer([a&0x3F,v&0xFF])
def cmd(c): xfer([c&0xFF])
def rd_t(a): return xfer([0xFC,(a&0x3F)|0x40,0])[2]
def wr_t(a,v): xfer([0xFC,a&0x3F,v&0xFF])
ic=rd(0x3F); print(f"spi={bus}.{dev} IC=0x{ic:02X}")
cmd(0xC2); wr(0x02,0); time.sleep(0.002); cmd(0xC1); time.sleep(0.003)
b=rd_t(4); wr_t(4,(b&~0x10)|0x10); a=rd_t(4)
print(f"test04 0x{b:02X}->0x{a:02X} unlock={bool(a&0x10)}")
wr(0x00,0x07); wr(0x01,0xBC); wr(0x28,0xD0); wr(0x2A,0x13); wr(0x2B,0x02)
wr(0x29,0x5F); wr(0x26,0x82); wr(0x27,0x82); wr(0x2C,0x00); wr(0x02,0x83)
ok=False
for _ in range(50):
  if rd(0x31)&0x10: ok=True; break
  time.sleep(0.002)
print(f"osc={ok} AUX=0x{rd(0x31):02X}")
cmd(0xD6); time.sleep(0.01)
_,_,ok2= (lambda: (None,None,bool(rd_t(4)&0x10)))(); wr_t(4,rd_t(4)|0x10); print(f"re-unlock test04=0x{rd_t(4):02X}")
wr(0x03,0x09); wr(0x04,0x00); wr(0x05,0x00)
wr(0x0B,0x08); wr(0x0C,0x2D); wr(0x0D,0xD8); wr(0x0E,0x22); cmd(0xD5)
wr(0x02,0x88); time.sleep(0.01); aux=rd(0x31); print(f"en|tx OP=0x{rd(0x02):02X} AUX=0x{aux:02X} tx_on={bool(aux&0x20)}")
wr(0x02,0xCB); time.sleep(0.01); aux=rd(0x31); print(f"en|rx|tx OP=0x{rd(0x02):02X} AUX=0x{aux:02X} tx_on={bool(aux&0x20)}")
irq=xfer([0x1A|0x40,0,0,0,0]); print(f"IRQ {[hex(x) for x in irq[1:]]}")
cmd(0xD3); time.sleep(0.005); print(f"AD=0x{rd(0x25):02X} AUX=0x{rd(0x31):02X}")
wr(0x02,0); spi.close(); pin(NFC_CS,True)
