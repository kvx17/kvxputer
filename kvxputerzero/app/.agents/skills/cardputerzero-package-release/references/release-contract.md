# Release And Upload Contract

## Build Outputs

Use repository presets rather than custom build directories:

- desktop configure presets: `darwin-arm64`, `darwin-x86-64`, `linux-x86-64`, `win32-msvc`, and
  `win32-mingw64`;
- device presets: `cp0-native` and `cp0-cross`;
- release workflow: `cp0-cross-package`;
- Debian package output: `dist/<APP_DISPLAY_NAME>_<version>_<revision>_arm64.deb`.

`cmake/cm0-package.cmake` is the installation/package contract. `app-builder.json` is the final
upload/store contract. Neither replaces the other.

## Current app-builder.json Fields

Store-facing metadata lives in the nested `store` object; the top level keeps
packaging identity only. This matches what `czdev publish` uploads and what
the store's CI validates.

| Field | Release rule |
| --- | --- |
| `app_name` | User-facing app name; becomes the store display title. |
| `package_name` | Stable upload/application identifier; currently expected to match the CMake project name. Must be a valid Debian package name when published (lowercase letters, digits, `.+-`). |
| `version` | Must match `project(... VERSION ...)`. |
| `store.summary` | One-line store summary (required). |
| `store.description` | Longer store description (optional). |
| `store.locales` | Localized `summary`/`description` overrides keyed by locale, e.g. `ja`, `zh-CN` (optional). |
| `store.icon` | Repository-relative square PNG, 128–512 px (256×256 recommended); must exist. |
| `store.screenshots` | Non-empty repository-relative list of 320×170 PNGs; every file must exist. |
| `store.categories` | 1–2 entries from the store category enum (see AppBuilder `docs/APP_BUILDER_JSON.md`). |
| `store.permissions` | All seven boolean keys declaring shipped behavior, not driver enablement. |
| `store.author` | Object with required `display_name`; `github`, `email`, `website` optional. |
| `store.source_repo` | Actual source repository for this application. |
| `store.license` | SPDX license identifier (e.g. `MIT`, `GPL-3.0-only`). |
| `store.share_code` | 4-char A–Z/0–9 code, globally unique in the store; do not invent or rotate it after first publish. |

The current permission keys are `camera`, `microphone`, `imu`, `network`,
`additional_hardware`, `background_service`, and `external_display`.

The template ships deliberately invalid `TODO:` placeholders in the `store`
section and a placeholder `APP_MAINTAINER` in `cmake/cm0-package.cmake`. The
store validator auto-rejects unedited placeholders (including any M5Stack
identity), so every one of them must be replaced before publishing.

## Permission Mapping

- Camera capture: `camera`.
- Microphone/recording: `microphone`.
- IMU reads: `imu`.
- Network access or scans: `network`.
- GPIO, I2C, SPI, UART, USB accessory, IR, or other external buses: `additional_hardware` when the
  upload policy defines them that way.
- Installed background service: `background_service`.
- HDMI or other external display output: `external_display`.

Confirm ambiguous mappings against the uploader's current schema. A `true` value does not install
a library, grant Linux device permissions, add polkit/udev rules, or prove physical support.

## Pre-upload Checklist

1. Working tree and requested release scope are understood.
2. Host build succeeds for changes that can run on desktop.
3. `cp0-cross-package` succeeds.
4. Artifact is an ARM64 Debian package.
5. Package control metadata and installed file list are correct.
6. `app-builder.json` version, identity, permissions, and asset paths match the shipped artifact.
7. Store icon and screenshots describe the current UI.
8. Upload destination, tool, credentials, and exact artifact are explicit.

`deploy.sh` copies a package to a device and is not the package-store upload flow.

