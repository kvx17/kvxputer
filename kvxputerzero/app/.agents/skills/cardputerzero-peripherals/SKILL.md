---
name: cardputerzero-peripherals
description: Explain or integrate CardputerZero display, keyboard, camera, audio, IMU, network, Bluetooth, GPIO, I2C, SPI, UART, USB, HDMI, IR, power, backlight, and device-status APIs using repository-contained interface and lifecycle guidance. Do not infer hardware support from app-builder.json permissions alone.
---

# CardputerZero Peripherals

Use the current repository as the source of truth. Read
[references/peripheral-map.md](references/peripheral-map.md), then load only the bundled reference
for the requested capability.

## Portability

- Refer only to files in this Skill or paths relative to the repository root.
- Do not require another checkout, a sibling repository, a user home directory, or an absolute
  workstation path.
- Target Linux interfaces such as /dev, /sys, and D-Bus names are runtime contracts, not
  workstation paths. Label them as device-only when explaining them.

## Source Priority

1. Existing implementation and callers in the current repository.
2. The matching API pattern and lifecycle guidance bundled under references/.
3. Current CMake dependency detection and Debian runtime setup in this repository.
4. The target device's actual interfaces and runtime state when on-device diagnosis is requested.

Bundled API patterns describe known integration shapes. They are not proof that the corresponding
headers, libraries, kernel nodes, or physical peripherals already exist in the current application.

## Explain A Peripheral

Describe:

- support status: implemented, reference-pattern, simulated, unknown, or unsupported;
- the target Linux interface and physical connection;
- the public C++ API and ownership model;
- build and runtime dependencies;
- initialization, normal operation, error handling, and cleanup;
- desktop behavior versus real-device behavior;
- a minimal usage sequence based on the bundled reference;
- package permission and service implications.

## Integrate A Peripheral

1. Inspect src/platform, its callers, and the relevant CMake/package files before adding code.
2. Read only the matching bundled reference and reconcile its API pattern with existing repository
   conventions.
3. Add the smallest platform abstraction and only the required system library or Linux interface.
4. Provide a desktop implementation or an explicit unsupported result. Never return fake hardware
   success.
5. Keep blocking I/O and long scans out of the LVGL event path. Define ownership and cancellation
   for sessions, threads, file descriptors, GPIO lines, requests, and callbacks.
6. Verify the desktop build when applicable and the cp0-cross build for device-only code. Use a
   real device only when requested and available.
7. Record the required upload permission, but update app-builder.json only during final release
   preparation after the feature is shipped.

## Safety Boundaries

- Camera and microphone use requires clear user-facing privacy behavior.
- Confirm board revision, connector, pin, voltage, direction, bus, address, and ownership before
  driving GPIO, I2C, SPI, UART, IR, or external modules.
- Poweroff, reboot, firmware updates, destructive writes, network transmission, deployment, and
  upload require separate explicit authorization.
- A header, library, device-tree node, or permission flag alone does not prove that physical
  hardware is present or usable.
