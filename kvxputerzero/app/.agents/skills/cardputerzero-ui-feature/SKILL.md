---
name: cardputerzero-ui-feature
description: Implement or modify CardputerZero Template LVGL screens, widgets, state, navigation, themes, and keyboard interactions while preserving the repository's MVVM and reactive-binding architecture. Do not use for packaging or low-level peripheral integration.
---

# CardputerZero UI Feature

Implement UI work in the existing C++17/LVGL 9.5 structure. Read
[references/architecture.md](references/architecture.md) before changing a screen, widget,
navigation flow, theme behavior, or shared UI state.

## Workflow

1. Inspect the current screen, related model/view-model state, reactive bindings, and input routes.
2. Put durable application state in `src/model`, UI-facing actions and subjects in
   `src/viewmodel`, LVGL objects in `src/view`, and hardware-facing behavior in `src/platform`.
3. Prefer existing `reactive::bind_*`, `reactive::observe_obj`, theme roles, and shared widgets.
4. Keep LVGL ownership explicit. Bind observers to LVGL objects when possible and release any
   manually owned observer, timer, input listener, or resource during teardown.
5. Preserve the 320x170 device viewport, title/navigation regions, light and dark themes, and
   keyboard-only operation.
6. Build with the current host preset. Run the desktop simulator for visual or interaction work.
7. Exercise every changed state with keyboard input and verify both themes. For platform-guarded
   code, also configure or build the relevant device preset when its toolchain is available.

## Boundaries

- Do not place business state directly in LVGL callbacks when it belongs in the model or view model.
- Do not add a second reactive framework or UI toolkit.
- Do not edit generated files under `build/`, `.cache/`, `dist/`, or `temp/`.
- Do not claim hardware support from a desktop simulation. Use the peripheral skill for hardware
  behavior and the package-release skill for release metadata.

