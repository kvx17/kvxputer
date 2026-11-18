# Template UI Architecture

## Stable Constraints

- Language: C++17.
- UI: LVGL 9.5.
- Desktop preview: SDL2 compositor around a 320x170 LVGL display.
- Device display: Linux fbdev by default, optional DRM/KMS.
- Build entrypoint: repository `CMakePresets.json`.

## Layer Map

| Concern | Location | Rule |
| --- | --- | --- |
| Application state | `src/model` | Keep LVGL types out unless an existing contract requires them. |
| UI actions and observable state | `src/viewmodel` | Publish model state through the existing subject wrappers. |
| Subject and binding helpers | `src/reactive` | Reuse before writing custom observer plumbing. |
| Screens | `src/view/screens` | Derive from `BaseScreen`; build page content under its content root. |
| Reusable widgets | `src/view/widgets` | Derive from `BaseWidgets`; make ownership and teardown explicit. |
| Theme and layout constants | `src/view/theme.*`, `src/view/ui_const.h` | Preserve light/dark behavior and the fixed device viewport. |
| Lifecycle and screen routing | `src/app` | Keep deferred screen switching and application lifetime here. |
| Hardware-facing behavior | `src/platform` | Separate desktop and device behavior with existing compile guards. |

The intended data flow is:

```text
input -> widget callback -> BaseViewModel action -> BaseModel update
      -> LVGL subject -> reactive binding/observer -> rendered object
```

## Adding A Screen

Inspect the current implementations before editing. A normal screen addition touches:

1. `model::AppPage` and any durable state;
2. `BaseViewModel` actions and subjects;
3. a `BaseScreen` subclass under `src/view/screens`;
4. `ScreenManager` construction and deferred page dispatch;
5. navigation icons, callbacks, and keyboard behavior;
6. help text or screenshots when user-visible controls change.

`ScreenManager` uses deferred switching. Preserve that behavior instead of deleting the current
screen from inside an LVGL observer callback.

## Adding A Widget

- Build under `parent_` and store the top-level object in `core_obj_`.
- Prefer object-bound observers such as `reactive::observe_obj`.
- Use the shared theme roles and palette rather than duplicating colors.
- Unregister global input listeners, timers, or non-object-bound observers in teardown.
- Keep button labels and hit targets usable at 320x170.

## Verification

Select the existing host preset from `CMakePresets.json`, then configure and build its Debug
variant. For visual work, run the simulator and check:

- both light and dark themes;
- navigation with keys `4` through `8`;
- `ESC`, left/right, help, and screenshot behavior when relevant;
- page transitions, overlays, long press, and focus states;
- text clipping and layout at the native 320x170 viewport.

There is no general Template test suite at present. Compile and interactive verification are the
minimum evidence; platform work may additionally require `cp0-cross`.

