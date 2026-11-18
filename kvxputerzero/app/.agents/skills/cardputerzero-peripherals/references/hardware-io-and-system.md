# Hardware IO And System API Patterns

The paths in this reference are target-Linux interfaces. Never assume a connector, line number,
bus, address, voltage, or DRM device from an example; resolve them from the board contract and
current product configuration.

## GPIO

Prefer an owning, non-copyable line object:

~~~cpp
struct OutputLineConfig {
  std::string chip_path;
  unsigned int line_offset;
  std::string consumer;
};

class OutputLine {
 public:
  explicit OutputLine(OutputLineConfig config);
  bool set_value(bool active, std::string& error);
  bool set_input(std::string& error);
  bool get_value(bool& active, std::string& error);
  void release();
};
~~~

Use an application-specific consumer name and libgpiod for /dev/gpiochip*. Check line ownership
before requesting it, release it when the mode/page ends, and never drive an unconfirmed line.
Treat sysfs helpers as explicit board controls, not a generic GPIO substitute.

## I2C And SPI

Suggested I2C API:

~~~cpp
enum class I2cAddressAccess { NORMAL, FORCE_IF_BUSY };
std::vector<I2cAddressInfo> scan_i2c_bus(int bus, std::string& error);
bool read_i2c_byte_data(int bus, uint8_t address, uint8_t command,
                        uint8_t& value, std::string& error,
                        I2cAddressAccess access = I2cAddressAccess::NORMAL);
~~~

Use /dev/i2c-* with Linux ioctl. Preserve the distinction between absent addresses and addresses
owned by a kernel driver. FORCE_IF_BUSY is exceptional and must require a known-safe device
contract.

Suggested SPI API:

~~~cpp
struct DeviceConfig {
  std::string path;
  uint32_t speed_hz{1000000};
  uint8_t mode{0};
  uint8_t bits_per_word{8};
  bool lsb_first{false};
  bool no_chip_select{false};
};

class Device {
 public:
  static std::unique_ptr<Device> open(const DeviceConfig&, std::string& error);
  bool transfer(const std::vector<uint8_t>& tx,
                std::vector<uint8_t>& rx,
                std::string& error);
};
~~~

Enumerate /dev/spidev*, then open a configured RAII device and use SPI_IOC_* transfers. Validate
transfer sizes and release the file descriptor in the destructor.

## UART

Use a non-copyable session and distinguish console ownership:

~~~cpp
enum class UartOpenStatus { OK, OCCUPIED_BY_CONSOLE, OPEN_FAILED, UNSUPPORTED };

class UartDebugSession {
 public:
  static std::unique_ptr<UartDebugSession> open(
      const std::string& path, int baud_rate, UartOpenResult& result,
      bool flush_buffers = true);
  std::string read_available(std::string& error);
  bool write_text(const std::string& text, std::string& error);
  bool set_baud_rate(int baud_rate, std::string& error);
  void close(bool drain_output = true);
};
~~~

The backend uses libserialport. Do not detach a console or claim an occupied port without explicit
authorization. Poll nonblocking reads on a worker/timer and close the session during teardown.

## USB And Displays

USB enumeration can expose:

~~~cpp
std::vector<UsbDeviceInfo> list_usb_devices(std::string& error);
std::vector<UsbPortDeviceInfo> list_usb_bus_devices(std::string& error);
~~~

Use libudev/sysfs and keep bus/device, VID/PID, display name, and sysfs path as separate fields.

HDMI status can use HdmiInfo read_hdmi_info(std::string& error), backed by DRM connector sysfs.
Internal output uses the repository's fbdev or DRM/KMS display path. External rendering additionally
requires a valid DRM device/connector, LVGL display lifecycle, mode selection, and cleanup. Set
external_display only for shipped external-display behavior.

## Infrared

Use LIRC and owning sessions:

~~~cpp
IrDeviceInfo read_receiver_info();
IrDeviceInfo read_sender_info();
IrSendResult send_nec_packet(uint16_t address, uint16_t command);

class IrReceiverSession {
 public:
  bool start(uint16_t expected_address, bool filter_address = true);
  void stop();
  IrReceiveSnapshot poll();
};
~~~

Discover sender/receiver capabilities before use. Keep receive polling bounded, preserve queued
packets, and stop the file-descriptor session during teardown. Never assume NEC address/command
values are safe for an attached appliance.

## Power, Backlight, And Device Information

Battery reads should return a structured PowerSupplyInfo from power-supply sysfs, preserving
unknown values rather than replacing them with zero. safe_shutdown(error) and safe_reboot(error)
are privileged, externally visible actions and require explicit user approval.

Backlight access can use:

~~~cpp
bool read_brightness_percent(int32_t& percent);
bool write_brightness_percent(int32_t percent);
~~~

Clamp values, remember the prior hardware brightness when entering a temporary test, and restore it
on exit.

Device information can expose product model, SKU, serial number, firmware version, and labeled
fields through value-returning functions. Treat serial numbers as potentially sensitive and avoid
logging or transmitting them without a product requirement.

## Keyboard And Input Routing

Initialize LVGL input with the active display, attach the repository key router, and register
listeners with an owner pointer. Every listener registration must have a matching clear operation
during teardown. Global listeners should be able to decline an event so normal focused-widget
routing continues.

Input normally uses evdev on the target. Exclusive device grabs, modal capture, long-press state,
and navigation aliases must be explicit because they can prevent other applications or widgets from
receiving keys.
