# Region preview save fix

## Problem

SC4's engine can save distorted city preview tiles in the region view when the
active camera uses non-native pitch or yaw. The plugin normalizes camera state
during preview generation. Distortions produced by other methods are outside the
validated scope of this fix.

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
conflict with other UI plugins. The implementation leaves game menus intact.

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

## Design

SimCity 4 publishes a pre-save message through its message server:

```text
kSC4MessagePreSave = 0x26C63343
```

The director subscribes to it alongside city-load and city-shutdown messages.
When pre-save is received, the controller:

1. Saves the active free-camera angles, view target, rotation base target, and
   process-global pitch/yaw tables.
2. Replaces the global tables with native values.
3. Selects native pitch for the current discrete zoom.
4. Restores native yaw while preserving discrete rotation and view target.
5. Refreshes the camera, rebuilds the render grid, and requests a redraw
   immediately, because no frame runs before the preview is rendered.
6. Holds native state throughout SC4's internal save and preview generation.

The preview renders inside `cSC4City`'s own pre-save handler (`0x45C1A0` ->
`0x459B20` -> `0x5DDEC0`). Subscribers are called in subscription order, and
`cSC4City` subscribes in its `Init` (`0x45BD20`), after the plugin subscribes in
`OnStart`, so the plugin normalizes first. Moving the plugin's subscription later
would break this order.

SC4 also publishes a post-save message, sent after the preview is finished but
before the object records are written and the file is closed:

```text
kSC4MessagePostSave = 0x26C63345
```

Restoration follows from it. This refers to save completion, not the framework
`PreAppShutdown()` callback, which runs only during application teardown and is
used to stop the logger:

- After a normal save, the post-save message schedules the restore for the next
  camera frame, when the save has finished. Any keyboard or mouse interaction that
  arrives first still restores free-camera state and custom tables before
  processing that interaction.
- During save-and-exit, pre-city-shutdown discards the saved free-camera state and
  deliberately leaves the global tables native. If a frame does run first, city
  shutdown's reset still writes native tables, so the next city starts from clean
  defaults either way.

The `g_IsModernCamEnabled` flag remains available. When false, modern camera input
is bypassed while diagnostics and native SC4 input remain operational.

## Validation

The path is validated by moving the free camera and using save-and-exit to
return to region view. Preview tiles retained native orientation, the game
remained stable, and no custom menu or binary hook is required.

The post-save message and subscription order are binary-verified. Restoring on the
post-save message still needs runtime validation: after a normal save the free
camera should return with no input, and after save-and-exit another city should
open with native angles.
