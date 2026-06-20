# Region preview save fix

## Problem

SC4's engine has a known problem where city preview tiles in the region view can
become distorted by various methods. One of the ways a preview tile can become
distorted is via using this plugin - however there is now a fix. And this fix
may also be helpful in fixing distortions made by other methods - however,
that claim is untested.

## Verified native behavior

The native measurements used by the fix are documented in
[default-camera.md](default-camera.md). In summary:

- Native pitch by zoom is 30, 35, 40, 45, and 45 degrees.
- Native yaw remains at -22.5 degrees.
- Quarter-turn rotation uses the discrete `rotation` field and derived transforms,
  not yaw changes.
- These orientation rules are identical for all city tile sizes.

## Rejected approaches

### Custom save menu

Replacing the save menu could intercept user actions, but would unnecessarily
conflict with other UI plugins. The final implementation leaves game menus intact.

### `cISC4App::SaveCity` vtable hooks

An experimental hook normalized the camera before calling the presumed original
`SaveCity` entry. Normalization succeeded, but the game crashed when that entry
was invoked. The exception occurred at `0x00454BD4` in SC4's string-comparison
code, showing that the selected vtable entry or signature did not match the ABI.

All `SaveCity` vtable-hook code was removed.

### `cISC43DRender::TakeSnapshot` vtable hooks

Both public snapshot slots were instrumented. They installed without crashing but
were never called during region-preview generation, proving that saving uses a
different internal rendering path.

All renderer snapshot-hook code was removed.

## Final design

SimCity 4 publishes a pre-save message through its message server:

```text
kSC4MessagePreSave = 0x26C63343
```

The director subscribes to it alongside city-load and city-shutdown messages.
When pre-save is received, the controller:

1. Saves the active free-camera angles, view target, rotation base target, and
   process-global pitch/yaw tables.
2. Replaces the global tables with verified native values.
3. Selects native pitch for the current discrete zoom.
4. Restores native yaw while preserving discrete rotation and view target.
5. Refreshes the camera and requests a redraw.
6. Holds native state throughout SC4's internal save and preview generation.

There is no known post-save notification, so restoration is lifecycle-driven:

- After a normal save, the first subsequent keyboard or mouse interaction restores
  free-camera state and custom tables before processing that interaction.
- During save-and-exit, pre-city-shutdown discards the saved free-camera state and
  deliberately leaves the global tables native. The next city therefore starts
  from clean defaults.

The `g_IsModernCamEnabled` flag remains available. When false, modern camera input
is bypassed while diagnostics and native SC4 input remain operational.

## Validation

The final path is validated by moving the free camera and using save-and-exit to
region view. Preview tiles retained native orientation, the game remained stable,
and no custom menu or binary hook is required.
