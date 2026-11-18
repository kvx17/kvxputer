#!/bin/sh
# Install / uninstall scrolld on the Cardputer Zero.
#   sudo ./install.sh            install or update
#   sudo ./install.sh uninstall  remove everything (keeps nothing)
set -e
[ "$(id -u)" -eq 0 ] || { echo "Run with sudo: sudo ./install.sh"; exit 1; }
cd "$(dirname "$0")"

if [ "$1" = "uninstall" ]; then
    systemctl disable --now scrolld 2>/dev/null || true
    rm -rf /usr/local/lib/scrolld /usr/local/bin/scrollctl \
           /etc/systemd/system/scrolld.service /etc/modules-load.d/scrolld.conf /etc/scrolld.conf
    systemctl daemon-reload
    echo "scrolld removed."
    exit 0
fi

echo "==> Installing dependencies"
if ! apt-get install -y python3-evdev python3-smbus2 i2c-tools; then
    apt-get update && apt-get install -y python3-evdev python3-smbus2 i2c-tools \
    || pip3 install --break-system-packages evdev smbus2
fi

echo "==> Installing files"
install -Dm755 scrolld.py      /usr/local/lib/scrolld/scrolld.py
install -Dm755 scrollctl       /usr/local/bin/scrollctl
install -Dm644 scrolld.service /etc/systemd/system/scrolld.service
if [ -f /etc/scrolld.conf ] && ! grep -q '^cli_cw' /etc/scrolld.conf; then
    cp /etc/scrolld.conf /etc/scrolld.conf.old
    echo "    old config saved as /etc/scrolld.conf.old (new defaults installed)"
    rm /etc/scrolld.conf
fi
[ -f /etc/scrolld.conf ] || install -Dm644 scrolld.conf /etc/scrolld.conf
printf 'uinput\ni2c-dev\n' > /etc/modules-load.d/scrolld.conf
modprobe uinput  || true
modprobe i2c-dev || true

if ! ls /dev/i2c-* >/dev/null 2>&1; then
    echo "!! No I2C buses found. Enable I2C (raspi-config -> Interface Options -> I2C) and reboot."
fi

echo "==> Starting service"
systemctl daemon-reload
systemctl enable scrolld
systemctl restart scrolld
sleep 3
echo
scrollctl status || echo "\nUnit Scroll not detected yet - plug it in, the service picks it up automatically."
