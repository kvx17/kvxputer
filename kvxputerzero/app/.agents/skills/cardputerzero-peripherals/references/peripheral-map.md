# Peripheral Capability Index

Use this file as a router. Read only the reference containing the requested capability. The API
patterns are self-contained guidance for implementing services under src/platform; inspect the
repository first because some services may already exist or use different names.

| Capability | Read next | Target backend | Upload permission |
| --- | --- | --- | --- |
| Camera preview | [media-and-sensors.md](media-and-sensors.md) | libcamera; RGB565 frames | camera |
| Recording, playback, UI sounds | [media-and-sensors.md](media-and-sensors.md) | miniaudio | microphone for capture |
| Headphone detection | [media-and-sensors.md](media-and-sensors.md) | GPIO or input-switch state | Confirm upload schema |
| BMI270/BMM150 IMU | [media-and-sensors.md](media-and-sensors.md) | Linux IIO and I2C sysfs | imu |
| Wi-Fi and Ethernet | [connectivity.md](connectivity.md) | NetworkManager/libnm | network |
| Internet and throughput test | [connectivity.md](connectivity.md) | HTTPS probe, libiperf, libsctp | network |
| Bluetooth scan | [connectivity.md](connectivity.md) | BlueZ over GIO/D-Bus | Confirm upload schema |
| GPIO | [hardware-io-and-system.md](hardware-io-and-system.md) | libgpiod; selected sysfs controls | additional_hardware |
| I2C | [hardware-io-and-system.md](hardware-io-and-system.md) | /dev/i2c-*; Linux ioctl | additional_hardware |
| SPI | [hardware-io-and-system.md](hardware-io-and-system.md) | /dev/spidev*; Linux ioctl | additional_hardware |
| UART | [hardware-io-and-system.md](hardware-io-and-system.md) | libserialport | additional_hardware |
| USB enumeration | [hardware-io-and-system.md](hardware-io-and-system.md) | libudev and sysfs | additional_hardware |
| HDMI status/output | [hardware-io-and-system.md](hardware-io-and-system.md) | DRM connector sysfs and LVGL DRM | external_display |
| IR send/receive | [hardware-io-and-system.md](hardware-io-and-system.md) | Linux LIRC | additional_hardware |
| Battery and power actions | [hardware-io-and-system.md](hardware-io-and-system.md) | power-supply sysfs; systemd/logind | Confirm upload schema |
| Backlight | [hardware-io-and-system.md](hardware-io-and-system.md) | backlight sysfs | Confirm upload schema |
| Device identity/status | [hardware-io-and-system.md](hardware-io-and-system.md) | device tree and sysfs | None by default |
| Keyboard/input routing | [hardware-io-and-system.md](hardware-io-and-system.md) | evdev and LVGL input | None |
| Internal LCD | [hardware-io-and-system.md](hardware-io-and-system.md) | fbdev or DRM/KMS | None |

Permission values describe shipped behavior. They do not install libraries, grant access to device
nodes, create udev/polkit rules, or prove physical support.
