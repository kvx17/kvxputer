# Application Identity Map

Treat these values as related but distinct:

| Value | Typical form | Purpose |
| --- | --- | --- |
| Display name | `kvxputerzero` | Launcher and package filename. |
| Project/executable name | `kvxputerzero` | CMake target and installed binary. |
| Upload package name | `kvxputerzero` | `app-builder.json` upload identifier. |
| Config identity | `kvxputerzero` | Config filename and per-user directory. |
| Icon basename | `template` | Runtime and APPLaunch images. |

Do not force one spelling onto every surface. Resolve the intended value for each contract.

## Required Search And Update Areas

### Build and version

- `CMakeLists.txt`: `project(<name> VERSION <version>)`, `APP_NAME`, config defaults, compile
  definitions, and logger-visible identity.
- `CMakePresets.json`: only paths or labels that embed the application identity.
- `vcpkg.json`: change package identity only when it represents this application rather than a
  reusable dependency manifest.

### Runtime paths

- `config/<app>.conf` and every `APP_CONFIG_FILE` default.
- Per-user config directories in `src/app/app.cpp`.
- Asset paths resolved by `src/app/asset_manager.*`.
- Screenshot filename prefixes and log tags when they are product-facing.

### Debian and APPLaunch packaging

- `cmake/cm0-package.cmake`: display name, maintainer, descriptions, config install path, icon
  pattern, package filename, architecture, service choice, and dependencies.
- `cmake/templates/app.desktop.in`: executable and icon contracts.
- `cmake/templates/app.service.in`: executable and service description.
- `cmake/templates/conffiles.in`: installed config path.
- `deploy.sh`: package glob and examples.

### Upload contract

- `app-builder.json`: package name, app name, version at the top level, plus the nested `store`
  object (summary, locales, icon, screenshots, categories, permissions, author, source
  repository, license, and share code).
- Treat this file as final package/upload metadata. Do not use it to drive source architecture or
  silently rewrite it during ordinary feature work.

### Assets and documentation

- `assets/images`: application icon variants and any filename filters used by packaging.
- `screenshot`: store/release screenshots referenced by `app-builder.json`.
- `README.md`: commands, binary paths, package filenames, config paths, install layout, and examples.

## Completion Checks

Run focused searches for every old identity value and version. Classify remaining hits before
changing them: a demo class name or historical changelog can remain, while runtime paths, package
metadata, commands, and current screenshots must be consistent.

Then run:

```sh
cmake -P .agents/skills/cardputerzero-package-release/scripts/validate_app_builder.cmake
```

