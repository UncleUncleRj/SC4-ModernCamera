# SimCity 4 Native UI Research

This document records native UI behavior, resource recipes, ABI constraints, and window-management conventions for SC4-ModernCamera.

## Architecture

The test window is a native SimCity 4 UI window. It does not use ImGui and does not require an additional runtime DLL.

- `Dev/ui/SC4-ModernCamera-TestUI.txt` contains the readable legacy UI script.
- `tools/build_sc4_ui_dat.py` packages that script into an uncompressed DBPF file.
- The Visual Studio pre-build step generates the companion DAT, and the post-build step copies it beside the plugin DLL.
- `Dev/src/SC4WindowManager.cpp` owns plugin windows, notification dialogs, the floating settings button, the production Settings window, the Advanced Settings child window, and the control laboratory.
- `docs/changelog.md` is baked into the first-install greeting window by the DAT builder. The greeting version is read from `Dev/src/PluginVersion.h`, not from the changelog text.

The DBPF resource identifiers are:

| Field | Value |
| --- | --- |
| Type | `0x00000000` |
| Group | `0x3D0C0700` |
| Instance | `0x3D0C0701` |
| Root window CLSID/control ID | `0x3D0C0702` |

The window is instantiated through `cIGZUIScriptService::CreateWindowFromScript`. Controls are authored in the UI script and retrieved by ID with `GetChildWindowFromIDRecursive`.

## Window-manager architecture

`SC4WindowManager` is the plugin's single owner and integration point for UI. `Main.cpp` forwards lifecycle and input events to it instead of knowing how individual windows are constructed.

The manager owns:

- the baked first-install/version/changelog greeting window;
- the baked Controls help popup opened from the greeting window;
- the baked floating camera-settings menu button;
- the baked first-pass camera settings window;
- the baked Advanced Settings window;
- the native control-laboratory window.

Each window keeps its own control IDs, layout state, and notification handling, while the manager coordinates:

- showing windows and bringing existing instances to the front;
- closing every plugin window during city shutdown;
- reporting whether any managed window is visible;
- routing mouse-wheel input to the window beneath the pointer;
- creating basic SC4 notification dialogs through the native wrapper.

This keeps game lifecycle policy in one place and prevents `Main.cpp` from accumulating window-specific resource IDs or control behavior.

The public factory supports three creation forms:

```cpp
manager.CreateManagedWindow();
manager.CreateManagedWindow(SC4BasicWindowOptions{ /* ... */ });
manager.CreateManagedWindow(SC4WindowTemplate::ControlLaboratory);
```

The parameterless form creates a blank 420-by-240 window with only the native X close button. Basic-window options accept a clamped width and height, title, wrapped body text, and one of four button arrangements: X only, OK, Close, or Accept. The latter three retain the X and add the named footer button.

The factory returns an `SC4WindowHandle`; zero (`InvalidSC4WindowHandle`) indicates failure. The manager retains ownership and accepts the handle in `CloseWindow`. Named templates are used for windows with specialized controls or behavior. The control laboratory, Settings window, Advanced Settings window, greeting, controls popup, and floating menu button all use dedicated baked resources rather than dynamic layout.

Basic windows are instantiated from the generic `SC4-ModernCamera-BasicUI.txt` resource packed into the companion DAT. Runtime customization uses captions, `SetSize`, and relative `GZWinMoveTo` anchoring; it does not use either unsafe `SetArea` overload.

Runtime customization is less reliable than baked script layout. A dynamically populated basic window can appear as a blank shell or ignore runtime size/caption changes in-game. User-facing windows use dedicated baked UI resources and the control-laboratory pattern: create from script, add to the SC4 parent, center using `rootWindow->GetW()`/`GetH()`, set the winproc, then show.

## Resource and build behavior

The UI DAT is a required runtime resource, unlike the user settings JSON files. Visual Studio regenerates it with `tools/build_sc4_ui_dat.py` during the project pre-build event, then copies it to the Plugins folder during post-build. The DAT builder uses only Python's standard library so the build does not require Pillow or other Python packages. The JSON files are created by the plugin as needed and must not be copied by the build.

The DBPF writer emits:

- a 96-byte DBPF header;
- UTF-8 legacy UI scripts as uncompressed resources;
- one 20-byte index entry per resource.

The packager emits these UI script resources in the same type/group:

| Instance | Purpose |
| --- | --- |
| `0x3D0C0701` | Native control laboratory |
| `0x3D0C0703` | Generic basic-window template |
| `0x3D0C0705` | First-install greeting/changelog window |
| `0x3D0C0707` | Controls help popup |
| `0x3D0C0901` | Floating camera-settings menu button |
| `0x3D0C0903` | Camera settings window |
| `0x3D0C0905` | Advanced Settings window |

The same DAT also carries custom UI image resources for the floating menu icon, selected/disabled button states, title-bar camera icon, and welcome arrow art. The packager substitutes the ordinance-style checkbox recipe and bakes `docs/changelog.md` into the greeting resource. The greeting heading is generated as `SC4-ModernCamera {PluginVersion::String} Installed!`, so the version number remains centralized in `Dev/src/PluginVersion.h`.

The settings option buttons use the custom button-stage image resource `0x856DDBAC / 0x3D0C0700 / 0x3D0C0907`, generated from `Dev/ui/menu-button-stages.png`. The image strip order is Disabled, Normal, Selected, Hovered. The selected state uses the green `#25DC80` fill baked into the asset. Calls to `cIGZWin::SetFillColor` and `SetFillColorRGB` require in-game ABI validation before use, so selected-state coloring remains baked into button image resources.

Use `cIGZWinBtn::ToggleOn()` and `ToggleOff()` to select option buttons. Use `cIGZWin::SetFlag(cIGZWin::WinFlag_Enabled, false)` to switch a control to its disabled image and make it unclickable. Apply selection before the enabled flag when a disabled group still needs a logical selected state.

## Custom UI Images

Native UI scripts reference image resources with two IDs:

```text
image={group,instance}
```

The resource type is not written in the UI script. UI/button images are DBPF resources with:

```text
Type:  0x856DDBAC
Group: value from the first `image={...}` field
IID:   value from the second `image={...}` field
```

The builder emits uncompressed SHPI/FSH payloads. The SHPI header is:

| Offset | Size | Value | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `SHPI` | FSH magic |
| `0x04` | 4 | total FSH size | payload size |
| `0x08` | 4 | image count | number of images |
| `0x0C` | 4 | directory/group tag | usually `G264` |

Directory entries begin at `0x10`. Each entry is 8 bytes:

| Field | Size | Meaning |
| --- | ---: | --- |
| Name | 4 | image name/tag |
| Offset | 4 | little-endian offset to the image block |

After the directory is an 8-byte metadata/padding field.

Each image block begins with a 16-byte image header:

| Offset | Size | Value | Meaning |
| --- | ---: | --- | --- |
| `+0x00` | 1 | `0x61` | image format |
| `+0x01` | 3 | `0` | size/flags field in decoded file |
| `+0x04` | 2 | `512` | width |
| `+0x06` | 2 | `512` | height |
| `+0x08` | 2 | `0` | x offset/unknown |
| `+0x0A` | 2 | `0` | y offset/unknown |
| `+0x0C` | 4 | `0` | reserved/unknown |
| `+0x10` | variable | image bytes | payload |

The builder uses DXT3:

```text
FSH image format 0x61 = DXT3
```

DXT3 data is 16 bytes per 4-by-4 pixel block:

```text
(width / 4) * (height / 4) * 16
```

For `Dev/ui/moderncamera-menu-icon.png`, a 176-by-44 four-state strip, the DXT3 payload size is:

```text
(176 / 4) * (44 / 4) * 16 = 7,744 bytes
```

A minimal uncompressed one-image FSH/SHPI payload for that strip is:

```text
16 bytes  SHPI header
 8 bytes  one directory entry
 8 bytes  metadata/padding
16 bytes  image block header
7744 bytes DXT3 payload
---------
7792 bytes total
```

`tools/build_sc4_ui_dat.py` packages `Dev/ui/moderncamera-menu-icon.png` as an uncompressed SHPI/FSH payload with this TGI:

```text
Type:  0x856DDBAC
Group: 0x3D0C0700
IID:   0x3D0C0900
```

The corresponding UI script reference is:

```text
image={3d0c0700,3d0c0900}
```

The generated companion DAT contains the UI script resources plus `0x856DDBAC / 0x3D0C0700 / 0x3D0C0900` with an `SHPI` payload.

## UI script coordinates

Legacy UI `area` values are edges:

```text
area=(left, top, right, bottom)
```

They are not `(left, top, width, height)`. Treating the last two values as dimensions produced invalid layouts and, in some cases, parser or runtime crashes.

Child coordinates are relative to the root window. The control-laboratory root is 570 by 600 pixels.

Caption attributes are literal strings. SC4's UI parser does not HTML/XML-decode text entities in captions; `&amp;` displays as `&amp;`, not `&`. Preserve plain ampersands in authored text. Only avoid or replace characters that would break the quoted attribute itself, such as double quotes and angle brackets.

## Window layout convention

The test window has three logical bands:

- Header: title and the native X close control.
- Content viewport: vertically scrolling controls, from local Y 108 through 530.
- Footer: the background divider and a conventional full-width Close button.

The native root background contains a lower divider. It should be treated as a footer seam rather than allowing content to overlap it.

SC4 does not reliably clip child windows to a parent or viewport. A scrolling control is therefore shown only when its entire rectangle fits inside the content viewport. Partially visible controls are hidden. This avoids text, borders, and option-group outlines drawing through the header or footer.

## Verified control recipes

### Standard button

The laboratory uses this normal SC4 button atlas:

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

The pattern is a small bitmap button plus a separate text label:

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

The vertical scrollbar uses:

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

Buttons also emit additional state messages ending in F7, F8, and F9. These diagnostic messages should not be treated as completed clicks.

`GZWinOptGrp` also creates anonymous internal option buttons. They emit button-state notifications with control ID `0` (observed as `0x287259F7`) immediately before the owning option-group selection notification. The laboratory records these as `optionGroupInternalButton`.

Only `DoWinProcMessage` should perform event handling. Forwarding both `DoWinMsg` and `DoWinProcMessage` created duplicate interaction records.

Every laboratory interaction is written to the normal plugin log and serialized into `Plugins/SC4-ModernCamera/test.json`.

## Scrollbar handling

The GZCOM headers expose concrete declarations for `cIGZWinScrollbar` and `cIGZWinSlider`. Their methods require in-game validation before production code calls value accessors directly. The generic command message reports `dwData3 == 0`.

The laboratory maps the cursor position to a logical scroll offset:

- clicking the upper/lower arrow zones moves by 40 pixels;
- clicking or dragging the track maps its cursor ratio to the 0-600 content range;
- turning the mouse wheel while the pointer is inside the window activates the corresponding native scrollbar arrow;
- all scrolling inputs call the same `ApplyScrollPosition` path.

`WM_MOUSEWHEEL` coordinates are screen-relative. The canvas Win32 filter converts them to canvas-client coordinates, hit-tests the native root window, and forwards the wheel delta only when the pointer is inside it. Wheel input outside the window remains available to the city camera. Partial high-resolution wheel deltas are accumulated until they form a standard 120-unit notch.

Each completed wheel notch is translated into the same native arrow-button input used by the scrollbar itself. SC4 updates the thumb, and the resulting scrollbar notification moves the content by the matching 40-pixel line size. This avoids maintaining independent content and thumb positions.

This keeps the scrollbar thumb and content movement coupled without calling an unverified virtual method.

`GZWinMoveTo(x, y)` is misleadingly named: it applies a relative delta rather than moving to an absolute position. To make scrolling deterministic, calculate:

```text
deltaX = baseLeft - currentLeft
deltaY = desiredTop - currentTop
```

and pass those deltas to `GZWinMoveTo`. Passing absolute coordinates causes controls to drift farther on every scroll operation.

## Calling-convention and SDK hazards

SC4 is a 32-bit application, so a wrong calling convention or virtual signature corrupts the stack immediately. Debug builds report this as `_RTC_CheckEsp`; release builds may simply crash.

Startup popup wrapper:

```cpp
bool (__cdecl*)(cIGZString const& caption, cIGZString const& message)
```

at game address `0x78DD80`. Ignore the Boolean return value. A `__stdcall` declaration caused an ESP mismatch after dismissing the popup.

The following SDK declarations or reverse-engineered paths require ABI validation before use:

- `cIGZWinCtrlMgr` programmatic control factories; `CreateLabel` caused a stack-balance failure.
- `cIGZWin::SetArea(cRZRect)`, `cIGZWin::SetArea(left, top, right, bottom)`, and `CenterWindowInRect()`; production use requires targeted in-game validation.

Manual centering is safe when the required movement is expressed as a relative `GZWinMoveTo` delta.

Safe geometry queries used by the laboratory are `GetL`, `GetT`, `GetR`, and `GetB`.

## Lifecycle and input behavior

The first-install/changelog popup is a baked native SC4 window generated from `docs/changelog.md`. It is displayed during the first city load for each installed plugin version. Controls and camera-settings guidance are intentionally kept out of the changelog body; the greeting window has a `View Controls` button that opens a smaller baked controls popup and a separate top-right note pointing to the floating camera button. The changelog body uses a read-only multiline `GZWinTextEdit` with `vscrollbar=yes`, not a plain `GZWinText`, so release notes can grow without increasing the window size.

Creating the managed greeting during city-load notification can run before the city view and UI hierarchy finish initializing. A 3-second Win32 timer defers greeting creation until the native UI is ready.

Opening a plugin child window directly inside a settings-window button callback can leave the child behind the settings window after SC4 finishes its own command handling. The stable pattern is to schedule the child open on a short timer, let the button callback return, then send the settings window back and call `PullToFront()` on the child. This is used for the Advanced Settings and Show Changelog buttons.

Advanced Settings and Show Changelog use an additional z-order guard: when the user explicitly opens the child from Settings, destroy and recreate that child before showing it, then send Settings back and pull the child to the front. Reusing an existing native child can inherit stale z-order after the user changes settings, especially after switching to Classic and reopening a child window. Do not replace this with ordinary reuse unless a replacement z-order rule is validated in game.

The control laboratory is created only after the UI services and city view are available. While it is visible, camera input is suppressed so clicks and drags intended for controls cannot move the city camera.

Most root windows and controls should be reused by showing/hiding them. Avoid reconstructing the entire hierarchy during ordinary interaction unless a specific native UI behavior requires it. Advanced Settings is an exception because recreation prevents stale child-window z-order.

## Native hide/show UI synchronization

The production camera settings button follows SC4's native hide/show UI state through `cISC4View3DWin::MinimizeUI(bool)`.

Validated behavior:

- The city starts with SC4's native UI visible, so the plugin shows the floating camera settings button at city load.
- `cISC4View3DWin::MinimizeUI(true)` is called when SC4 hides/minimizes the native UI.
- `cISC4View3DWin::MinimizeUI(false)` is called when SC4 restores the native UI.
- The plugin vtable-hooks that method while a city is loaded, calls the original function, then hides or shows the floating camera button from the Boolean argument.
- The hook is installed after post-city-init once `cISC4View3DWin` can be resolved, and uninstalled during city shutdown before the city-scoped UI object is discarded.

The lower-left click area remains intentionally broad for diagnostics and correlation, but it is not the source of truth. It must not directly hide/show the camera button or call `MinimizeUI` itself. It only logs that a likely native UI toggle click happened; the camera button state changes only when the native `MinimizeUI(bool)` hook observes the real state transition.

Rejected approaches:

- Exact pixel hit boxes for the visible and restored native UI buttons were too fragile. The pressed and restored states move slightly, and adjacent clicks could leave the camera button out of sync.
- Fixed-location virtual mouse probes could detect the first minimize but did not reliably detect restore.
- Timer retries introduced latency and did not reliably detect restore.
- Native GZ window hit-testing of the restore button returned the full 3D surface (`0x6A5E44B6`) rather than a button/control ID, so there was no reliable `cIGZWinBtn::IsOn()` state to read.

Implementation notes:

- The Windows v641 `cISC4View3DWin` vtable slot used for `MinimizeUI(bool)` is `55`, counting the inherited `cIGZUnknown` entries.
- If the GZCOM header changes, re-count this slot before touching the hook.
- Keep the hook narrowly scoped and avoid adding pixel-probe heuristics unless the native method hook stops firing for a supported game executable.

## Production settings behavior

The production settings workflow is documented in [settings-workflow.md](settings-workflow.md). The important native UI invariants are:

- Camera mode buttons apply immediately while the settings window remains open.
- Classic mode disables Modern-only controls but does not overwrite their saved Modern values.
- Reset Camera Location stays enabled in Classic as a recovery action.
- Advanced Settings remains available in Classic because diagnostics are not Modern-only.
- Redraw Aggression only affects the Modern camera. While Classic camera mode is active, the Advanced redraw group visually selects Classic and disables the other redraw buttons without overwriting the saved Modern redraw value.
- Every button option change should emit an explicit normal log entry. Slider movement can emit verbose per-step changes, with the delayed save logging the final values at normal level.

## Persistence files

Runtime files are located in the `SC4-ModernCamera` subfolder beside the plugin DLL. The plugin discovers that location dynamically rather than assuming a Documents path:

- `SC4-ModernCamera.json`: persistent user settings and installed-version marker.
- `test.json`: control-laboratory event/state output.
- `SC4-ModernCamera.log`: plugin log.

These files are generated at runtime and are not build artifacts. Legacy root-level files are migrated into the subfolder before the logger or settings system opens them. The DLL and companion UI DAT remain in the Plugins root.

## Production-window recommendations

For the settings and diagnostics UI:

1. Author controls in a companion UI DAT and retrieve them by stable IDs.
2. Use a fixed header, fixed native X, fixed footer, and one content viewport.
3. Keep a full Close or Back button in the footer for discoverability.
4. Use custom baked button-stage assets for option buttons; do not color buttons at runtime.
5. Use `WinFlag_Enabled` for disabled controls so the native disabled state and disabled asset are used.
6. Keep Modern-only settings visually disabled while Classic camera mode is active, but do not overwrite their saved Modern values.
7. Keep Reset Camera Location available in both Modern and Classic as a recovery action.
8. Keep diagnostics controls available in both Modern and Classic.
9. Hide partially visible scrolling controls instead of depending on clipping.
10. Route every scroll input through one offset and layout function.
11. Log raw notifications for controls before assigning semantics.
12. Do not call an SDK virtual method until its 32-bit ABI has been validated against the game.

## Open questions

- The concrete slider, spinner, text-edit, option-group, and scrollbar declarations are vendored in `Dev/vendor/gzcom`; production code should only use their virtual methods after a targeted in-game validation pass.
- Direct ABI-safe access to a native scrollbar's numeric value is unconfirmed; wheel input activates its native arrow controls.
- Keyboard focus, tab order, and accessibility behavior need a dedicated pass.
- The control laboratory writes `test.json`; production Settings and Advanced Settings do not depend on it.
