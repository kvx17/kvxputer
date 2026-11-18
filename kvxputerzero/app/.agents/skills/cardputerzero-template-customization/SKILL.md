---
name: cardputerzero-template-customization
description: Turn the CardputerZero Template repository into a named application or update its identity, version, icons, configuration paths, package metadata, and documentation consistently. Do not use for ordinary feature work.
---

# CardputerZero Template Customization

Use this skill when deriving an application from this template or changing an existing app's
identity. Read [references/identity-map.md](references/identity-map.md) before editing.

## Required Inputs

Resolve these values from the user's request or existing product metadata:

- display name;
- CMake/project and executable name;
- upload package name;
- semantic version;
- config directory and filename;
- icon basename and store screenshots;
- description, categories, author, source repository, and license;
- required permissions and whether a systemd service is needed.

Do not invent public identity, repository URLs, permissions, or upload identifiers. Ask only for
values that cannot be inferred safely.

## Workflow

1. Inventory every identity occurrence using the reference map and `rg` before editing.
2. Update source/build identity first, then runtime paths, package/install metadata, assets, upload
   metadata, deployment patterns, and documentation examples.
3. Use structured JSON editing for `app-builder.json`. Treat it as the final package-upload
   contract, not as the build system's source of truth.
4. Rename files only when their names are part of the application contract. Update all callers in
   the same change.
5. Search for stale template names, old versions, old icon basenames, old config paths, and old
   package patterns.
6. Configure and build the current host preset, then use the package-release skill to validate a
   release artifact when packaging is in scope.

## Boundaries

- Preserve the CardputerZero toolchain, MVVM structure, and CMake presets unless the requested
  product actually changes them.
- Do not rewrite generic third-party license text or rename unrelated demo classes automatically.
- Do not upload or deploy as part of identity customization.

