---
name: cardputerzero-package-release
description: Configure, build, verify, package, deploy, or prepare upload metadata for the CardputerZero Template application. Use for CMake presets, aarch64 Debian artifacts, app-builder.json matching, and release diagnostics; do not use for ordinary source edits.
---

# CardputerZero Package Release

Produce a release artifact that matches the final upload contract. Read
[references/release-contract.md](references/release-contract.md) before packaging or upload work.

## Workflow

1. Inspect `git status` and preserve existing changes. Never clean caches or build directories as
   a first response to a build failure.
2. Select an existing configure/build preset for the current host. Use
   `cmake --workflow --preset cp0-cross-package` for the complete aarch64 Debian flow.
3. Treat configure-time downloads of LVGL or the BSP as network operations and surface them when
   the environment requires authorization.
4. Confirm the output is an ARM64 Debian package, not a desktop executable labeled as ARM64.
5. Run the manifest validator before upload:

   ```sh
   cmake -P .agents/skills/cardputerzero-package-release/scripts/validate_app_builder.cmake
   ```

   When a package exists, also pass its path:

   ```sh
   cmake -DPACKAGE_FILE=dist/<package>.deb \
     -P .agents/skills/cardputerzero-package-release/scripts/validate_app_builder.cmake
   ```

6. Inspect the package with `dpkg-deb -f` and `dpkg-deb -c` when available. Verify executable,
   config, assets, APPLaunch entry, optional service, version, and architecture.
7. Update `app-builder.json` only when the feature set and release artifact are final. Its paths
   must resolve from the repository root and its permissions must describe shipped behavior.
8. Stop after producing and validating artifacts unless the user explicitly requested deployment
   or upload. Use the provided uploader and credentials only when they are actually available; do
   not invent an upload command or destination.

## Failure Handling

- Report the failing phase: configure, compile, link, package, manifest validation, deploy, or
  upload.
- Do not delete `.cache/`, `build/`, or `dist/` without a demonstrated need and explicit approval.
- Do not deploy to a device until the remote user, host, directory, and exact package are known.

