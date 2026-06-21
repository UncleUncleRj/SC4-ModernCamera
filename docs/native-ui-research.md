# SimCity 4 Native UI Research

This document records the verified behavior, resource recipes, failures, and working conventions discovered while building the SC4-3DMouseCam native control laboratory. It is intended to prevent future work from repeating unsafe experiments and to provide a foundation for the production settings and diagnostics windows.

## Current architecture

The test window is a native SimCity 4 UI window. It does not use ImGui and does not require an additional runtime DLL.

- `Dev/ui/SC4-3DMouseCam-TestUI.txt` contains the readable legacy UI script.
- `tools/build_sc4_ui_dat.py` packages that script into an uncompressed DBPF file.
- `Dev/ui/SC4-3DMouseCam-TestUI.dat` is the generated companion resource.
- The Visual Studio post-build step copies the DAT beside the plugin DLL.
- `Dev/src/TestWindow.cpp` loads the resource, registers the controls, handles notifications, scrolls the content, logs interactions, and writes `test.json`.

The DBPF resource identifiers are:

| Field | Value |
| --- | --- |
| Type | `0x00000000` |
| Group | `0x3D0C0700` |
| Instance | `0x3D0C0701` |
| Root window CLSID/control ID | `0x3D0C0702` |

The window is instantiated through `cIGZUIScriptService::CreateWindowFromScript`. Controls should be authored in the UI script and then retrieved by ID with `GetChildWindowFromIDRecursive`.

## Resource and build behavior

The UI DAT is a required runtime resource, unlike the user settings JSON files. The JSON files are created by the plugin as needed and must not be copied by the build.

The DBPF writer currently emits:

- a 96-byte DBPF header;
- the UTF-8 legacy UI script as one uncompressed resource;
- one 20-byte index entry.

The packager also substitutes the verified ordinance-style checkbox recipe. Keeping this substitution in the build step allows the readable source to remain easy to edit while preserving the exact native bitmap configuration that SC4 expects.

## UI script coordinates

Legacy UI `area` values are edges:

```text
area=(left, top, right, bottom)
```

They are not `(left, top, width, height)`. Treating the last two values as dimensions produced invalid layouts and, in some cases, parser or runtime crashes.

Child coordinates are relative to the root window. The current root is 570 by 600 pixels.

## Window layout convention

The test window now has three logical bands:

- Header: title and the native X close control.
- Content viewport: vertically scrolling controls, from local Y 108 through 530.
- Footer: the background divider and a conventional full-width Close button.

The native root background contains a lower divider. It should be treated as a footer seam rather than allowing content to overlap it.

SC4 does not reliably clip child windows to a parent or viewport. A scrolling control is therefore shown only when its entire rectangle fits inside the content viewport. Partially visible controls are hidden. This avoids text, borders, and option-group outlines drawing through the header or footer.

## Verified control recipes

### Standard button

The normal SC4 button atlas currently used by the laboratory is:

```text
image={46a006b0,144161eb}
style=standard
```

### Native title-bar X button

The small close control used by native SC4 dialogs is:

```text
area=(..., ..., ...+22, ...+20)
image={46a006b0,144161f9}
showcaption=no
style=standard
tiptext="Close"
btnclicksnd={00000000,ca5c3239}
```

It is a normal `GZWinBtn`, not special window chrome. Give it its own unique control ID and handle it like any other close button.

### Ordinance-style checkbox

A checkbox is not a wide standard button with `style=radiocheck`. That combination indexes the wrong state atlas and can expose uninitialized or back-buffer pixels, producing scenery-dependent distortion.

The verified pattern is a small bitmap button plus a separate text label:

```text
clsid=GZWinBtn
area=(28,178,48,200)
image={46a006b0,14416245}
toggle=on
showcaption=no
style=radiocheck
```

The label is a mouse-transparent `GZWinText` beside the 20-by-22-pixel button. This matches the checkbox/X presentation used by the game's Ordinances window.

### Option group

`GZWinOptGrp` works with manually defined `option`, `optionmoveto`, and `optionsetsize` values. Its outline is drawn as part of the control and must remain wholly inside the content viewport; otherwise it visibly crosses fixed dividers.

### Sliders and scrollbars

Both horizontal and vertical native controls instantiate and render successfully from UI script resources. The laboratory's content scrollbar is a fixed control; the rest of the controls move beneath it.

The vertical scrollbar currently uses:

```text
minmaxvalue=(0,600)
direction=vertical
pagesize=80
linesize=20
image={46a006b0,46a006a6}
```

## Notifications observed

The root window receives command messages with `dwMessageType == 3`.

| Control/action | `dwData1` | `dwData2` | `dwData3` |
| --- | --- | --- | --- |
| Button click | `0x287259F6` | Control ID | varies/unused |
| Option-group selection | `0x88710F1C` | Control ID | Selected option (1, 2, ...) |
| Slider/scrollbar interaction | `0x887113A3` | Control ID | observed as 0 |

Buttons also emit additional state messages ending in F7, F8, and F9. These are useful diagnostic data but should not be treated as completed clicks.

Only `DoWinProcMessage` should perform event handling. Forwarding both `DoWinMsg` and `DoWinProcMessage` created duplicate interaction records.

Every laboratory interaction is written to the normal plugin log and serialized into `test.json` beside the DLL.

## Scrollbar handling

The SDK does not currently expose a verified concrete scrollbar interface that safely returns its value. The generic command message also reports `dwData3 == 0`.

The working laboratory implementation therefore maps the cursor position to a logical scroll offset:

- clicking the upper/lower arrow zones moves by 40 pixels;
- clicking or dragging the track maps its cursor ratio to the 0-600 content range;
- both actions call the same `ApplyScrollPosition` path.

This keeps the scrollbar thumb and content movement coupled without calling an unverified virtual method.

`GZWinMoveTo(x, y)` is misleadingly named: it applies a relative delta rather than moving to an absolute position. To make scrolling deterministic, calculate:

```text
deltaX = baseLeft - currentLeft
deltaY = desiredTop - currentTop
```

and pass those deltas to `GZWinMoveTo`. Passing absolute coordinates causes controls to drift farther on every scroll operation.

## Calling-convention and SDK hazards

SC4 is a 32-bit application, so a wrong calling convention or virtual signature corrupts the stack immediately. Debug builds report this as `_RTC_CheckEsp`; release builds may simply crash.

Verified startup popup wrapper:

```cpp
bool (__cdecl*)(cIGZString const& caption, cIGZString const& message)
```

at game address `0x78DD80`. Ignore the Boolean return value. A `__stdcall` declaration caused an ESP mismatch after dismissing the popup.

The following SDK declarations or reverse-engineered paths have proven unsafe in this context and must not be used until their ABI is verified:

- `cIGZWinCtrlMgr` programmatic control factories; `CreateLabel` caused a stack-balance failure.
- `cIGZWin::SetArea(cRZRect)`.
- `cIGZWin::SetArea(left, top, right, bottom)`.
- `CenterWindowInRect()`.

Manual centering is safe when the required movement is expressed as a relative `GZWinMoveTo` delta.

Safe geometry queries used by the laboratory are `GetL`, `GetT`, `GetR`, and `GetB`.

## Lifecycle and input behavior

The first-install/changelog popup is intentionally a basic SC4 popup for now. It is displayed during the first city load for a newly installed plugin version.

The control laboratory is created only after the UI services and city view are available. While it is visible, camera input is suppressed so clicks and drags intended for controls cannot move the city camera.

The root window and its controls should be reused by showing/hiding them. Avoid reconstructing the entire hierarchy during ordinary interaction.

## Persistence files

Runtime files are located beside the plugin DLL, which allows the plugin folder to be discovered dynamically rather than assuming a Documents path:

- `SC4-3DMouseCam.json`: persistent user settings and installed-version marker.
- `test.json`: control-laboratory event/state output.
- `SC4-3DMouseCam.log`: plugin log.

These files are generated at runtime and are not build artifacts.

## Research sources

The repositories and game resources that informed this work include:

- `0xC0000054/sc4-region-census`, especially its native UI DAT and close-button definition;
- the game's `SimCity_1.dat`, used to find native ordinance checkbox resources;
- `0xC0000054/sc4-dll-utilities` and the bundled GZCOM headers for service and plugin patterns.

## Production-window recommendations

For the settings and diagnostics UI:

1. Author controls in a companion UI DAT and retrieve them by stable IDs.
2. Use a fixed header, fixed native X, fixed footer, and one content viewport.
3. Keep a full Close or Back button in the footer for discoverability.
4. Use the verified small bitmap checkbox with a separate label.
5. Hide partially visible scrolling controls instead of depending on clipping.
6. Route every scroll input through one offset and layout function.
7. Log raw notifications for any newly introduced control before assigning semantics.
8. Do not call an SDK virtual method until its 32-bit ABI has been verified against the game.

## Open questions

- The concrete slider, spinner, text-edit, option-group, and scrollbar interfaces still need ABI-safe method declarations before values can be queried directly.
- Mouse-wheel routing to a native child scrollbar has not yet been confirmed.
- Keyboard focus, tab order, and accessibility behavior need a dedicated pass.
- The final settings window should replace laboratory-only controls and remove `test.json` event recording.
