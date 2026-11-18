# Connectivity API Patterns

These are repository-contained integration patterns. Check src/platform and current CMake feature
guards before adding a service.

## Shared Results

Use value types that separate display text from machine state:

~~~cpp
struct WirelessScanItem {
  std::string name;
  std::string detail;
  int32_t strength_percent{-1};
  std::string bssid;
};

struct EthernetInfo {
  std::string interface_name;
  std::string state;
  std::string hw_address;
  std::string ip4_address;
  std::string connection_name;
  bool connected{false};
};
~~~

Do not encode an error as an empty result alone; return a separate error string or structured
status.

## Wi-Fi And Ethernet

Suggested APIs:

~~~cpp
std::vector<WirelessScanItem> scan_wifi(std::string& error);
EthernetInfo read_ethernet_info(std::string& error);
~~~

The device backend uses NetworkManager through libnm, GLib, GObject, and GIO. Prefer its object API
over parsing command output. A Wi-Fi scan may enable an adapter or request a new scan, so expose
progress and cancellation and do not run it on the LVGL event thread. Ethernet reads should report
interface state separately from IP assignment.

## Bluetooth

Suggested API:

~~~cpp
std::vector<WirelessScanItem> scan_bluetooth(std::string& error);
~~~

Use BlueZ on the system D-Bus through GIO. Resolve an adapter, power it only when the product
behavior permits that change, start discovery, collect org.bluez.Device1 objects, and report
whether failure occurred during bus connection, adapter selection, or discovery. Scans are
asynchronous by nature; a synchronous wrapper belongs on a worker with a bounded timeout.

## Internet And Throughput

Suggested result shape:

~~~cpp
struct LinkTestSettings {
  std::string iperf_host;
  int iperf_port{5201};
  int iperf_duration_seconds{3};
};

struct LinkTestResult {
  LinkInternetResult internet;
  LinkIperfResult wifi;
  LinkIperfResult ethernet;
};

LinkTestResult run_link_test(const LinkTestSettings& settings);
~~~

Require the probe and iperf destinations as user or product configuration; never embed a private
test endpoint in reusable code. The backend may use HTTPS plus libiperf and libsctp. Report internet,
Wi-Fi, and Ethernet outcomes independently. Provide timeout/cancellation and avoid silently changing
routes or active connections.

All network transmission requires user-visible intent appropriate to the application. Set upload
permission network. Confirm the current upload schema before mapping Bluetooth separately.
