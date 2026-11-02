# SimCity 4 Native UI Research

This document records native UI behavior, resource recipes, ABI constraints, and window-management conventions for SC4-ModernCamera.

## Architecture

The test window is a native SimCity 4 UI window. It does not use ImGui and does not require an additional runtime DLL.

- `Dev/ui/SC4-ModernCamera-TestUI.txt` contains the readable legacy UI script.
- `tools/build_sc4_ui_dat.py` packages that script into an uncompressed DBPF file.
- The Visual Studio pre-build step generates the companion DAT, and the post-build step copies it beside the plugin DLL.
- `Dev/src/SC4WindowManager.cpp` owns plugin windows, notification dialogs, the floating settings button, the production Settings window with its General and Advanced pages, and the control laboratory.
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
- the baked camera settings window, with General and Advanced pages;
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

The factory returns an `SC4WindowHandle`; zero (`InvalidSC4WindowHandle`) indicates failure. The manager retains ownership and accepts the handle in `CloseWindow`. Named templates are used for windows with specialized controls or behavior. The control laboratory, Settings window, greeting, controls popup, and floating menu button all use dedicated baked resources rather than dynamic layout.

Basic windows are instantiated from the generic `SC4-ModernCamera-BasicUI.txt` resource packed into the companion DAT. Runtime customization uses captions, `SetSize`, and relative `GZWinOffset` anchoring.

A dynamically populated basic window was observed as a blank shell or ignoring runtime size/caption changes in-game. Those observations predate the `cIGZWin.h` correction described under [Calling-convention and SDK hazards](#calling-convention-and-sdk-hazards), when `SetSize(width, height)` compiled to the game's `SetArea(rect)` slot, and have not been retested. The basic window has no production caller. User-facing windows use dedicated baked UI resources and the control-laboratory pattern: create from script, add to the SC4 parent, center using `rootWindow->GetW()`/`GetH()`, set the winproc, then show.

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
| `0x3D0C0903` | Camera settings window, both pages |

Instance `0x3D0C0905` held the separate Advanced Settings window and is retired.

The same DAT also carries custom UI image resources for the floating menu icon, selected/disabled button states, title-bar camera icon, and welcome arrow art. The packager substitutes the ordinance-style checkbox recipe and bakes `docs/changelog.md` into the greeting resource. The greeting heading is generated as `SC4-ModernCamera {PluginVersion::String} Installed!`, so the version number remains centralized in `Dev/src/PluginVersion.h`.

The settings option buttons use the custom button-stage image resource `0x856DDBAC / 0x3D0C0700 / 0x3D0C0907`, generated from `Dev/ui/menu-button-stages.png`. The image strip order is Disabled, Normal, Selected, Hovered. The selected state uses the green `#25DC80` fill baked into the asset, and selected-state coloring remains baked into button image resources. `cIGZWin::SetFillColor` and `SetFillColorRGB` now compile to their game slots, but they have not been called in game, and whether an image button honors its fill color is unknown.

Use `cIGZWinBtn::ToggleOn()` and `ToggleOff()` to select option buttons. Use `cIGZWin::SetFlag(cIGZWin::WinFlag_Enabled, false)` to switch a control to its disabled image and make it unclickable. Apply selection before the enabled flag when a disabled group still needs a logical selected state.

## Custom UI Images

Native UI scripts reference image resources with two IDs:

```text
image={group,instance}
```

The resource type is not written in the UI script. UI/button images use DBPF resources with:

```text
Type:  0x856DDBAC
Group: value from the first `image={...}` field
IID:   value from the second `image={...}` field
```

SC4 loads these resources through its GIMEX image path. PNG is the usual source format for UI icons. The plugin packages selected PNG assets as compatible uncompressed SHPI/FSH payloads.

The SHPI header is:

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

The Modern and Classic menu sources are
`Dev/ui/menu_glyphs_very_small.png` (green) and
`Dev/ui/menu_glyphs_red_very_small.png` (red). Each source is a 220-by-58 strip
of four 55-by-58 states in Disabled, Normal, Clicked, Hovered order. The builder
resamples each state to 60 by 72 with premultiplied-alpha bilinear filtering
and aligns every state's alpha-weighted center to the Normal frame before
packaging a 240-by-72 strip. This removes frame-to-frame movement baked into a
source atlas while retaining each state's color and artwork. Both encoded
dimensions remain divisible by four. The packaged DXT3 payload size for each
strip is:

```text
(240 / 4) * (72 / 4) * 16 = 17,280 bytes
```

A minimal uncompressed one-image FSH/SHPI payload for that strip is:

```text
16 bytes  SHPI header
 8 bytes  one directory entry
 8 bytes  metadata/padding
16 bytes  image block header
17280 bytes DXT3 payload
---------
17328 bytes total
```

`tools/build_sc4_ui_dat.py` packages both images as uncompressed SHPI/FSH
payloads:

```text
                         Modern/on       Classic/off
Type:                    0x856DDBAC      0x856DDBAC
Group:                   0x3D0C0700      0x3D0C0700
IID:                     0x3D0C0900      0x3D0C090A
```

The corresponding UI controls occupy the same 60-by-72 area. Only the control
for the active camera mode is visible, so hover and click replace the complete
button image rather than exposing a separate lower layer. The lower 16 pixels
remain a separate mode-switch click region; the rest opens Camera Settings.

```text
Modern: image={3d0c0700,3d0c0900}
Classic: image={3d0c0700,3d0c090a}
```

The generated companion DAT contains the UI script resources plus both image
TGIs with `SHPI` payloads.

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

## Native primitive recipes

These recipes come from the game's own scripts in `SimCity_1.dat` (reference implementation). All three were runtime-observed in this plugin on 641 on 2026-11-02.

The window frame `{46a006b0,6bb93cb5}` is not a plain background. Its image draws a 2-pixel divider at rows 184-185, which appears 62 pixels above the bottom edge at any window height (runtime-observed at heights 320 and 530). Windows using this frame must put their footer below that line or the line crosses their content. The settings window uses it as the footer divider and draws no rule of its own.

A moveable `GZWinGen` starts a drag only outside its client rectangle (binary-verified on 641: mouse handlers `0x99A361` and `0x99A3A7`, hit test `0x999C15`, rectangle `0x99922F`). With `titlebar=no`, that rectangle is `(gutterX, gutterY, width - gutterX, height - gutterY)`, so `gutters` sets the grabbable border, not just padding. The plugin's framed windows use `gutters=(4,34)`, which makes the 34-pixel title bar grabbable along with the matching band at the bottom edge. Controls take the mouse first, so anything in the title bar except the close button must set `winflag_mousetrans=yes winflag_ignoremouse=yes`.

Two native return values do not report success (binary-verified on 641), so the plugin does not treat them as results:

- `cIGZWinMgr::GZShowCursor` (`0x9DB72D`, forwarding to `cIGZGraphicSystem` slot 30, `0x98DFC0`) adds one to a hide count on hide, subtracts one on show, and returns the new count. A normal show returns zero. Reading that as failure left the plugin's hidden flag wrong and made the count drift, which by trace of the code left the cursor visible on two of every three M3 gestures.
- `cISC43DRender::ForceFullRedraw` (`0x7C7DD0`) ends in a tail jump to the void routine `0x7C6D80`, so its return value is the low byte of a leftover pointer. The redraw had been requested even when that byte was zero.

A divider is a thin `GZWinFlatRect` with no `style` key. It draws only its `fillcolor`; Maxis has 16 of these.

```text
clsid=GZWinFlatRect
iid=IGZWinFlatRect
fillcolor=(150,160,180)
colorleft=(0,0,0) colortop=(0,0,0) colorright=(0,0,0) colorbottom=(0,0,0)
```

A section outline is a `GZWinFlatRect` with one `style` key per drawn edge plus `style=nofill`. The edge colors come from the matching `color...` keys. The outline is unfilled and uses `winflag_mousetrans=yes winflag_ignoremouse=yes`, so it neither covers nor blocks the controls inside it, whichever way sibling order draws.

A static image is a `GZWinBMP`. Maxis has 845 of them. The title-bar icon and welcome arrow use a single-frame image whose `imagerect` covers all of it, so they do not depend on how `imagerect` selects from a larger image:

```text
clsid=GZWinBMP
iid=IGZWinBMP
image={group,instance}
imagerect=(0,0,width,height)
notify=no
transparentbkg=yes
edgeimage=no
```

The keys `winflag_enable` and `alphablend` that older generated scripts carried are not in the game's UI vocabulary and are no longer written.

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

Every laboratory interaction is serialized into `Plugins/SC4-ModernCamera/test.json`.
Its normal plugin-log record is emitted only when Diagnostics Logging permits
info-level output.

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

`GZWinMoveTo(x, y)` moves a window to an absolute position in its parent, and `GZWinOffset(dx, dy)` moves it by a delta (binary-verified: `0x99C8C5` and `0x99BD27`, vtable slots 56 and 57). The laboratory scrolls with `GZWinMoveTo(baseLeft, desiredTop)`.

Earlier versions of this document said `GZWinMoveTo` applied a delta. That was a header bug: the old `cIGZWin.h` had no `GZWinOffset`, so its `GZWinMoveTo` compiled to slot 57.

## Calling-convention and SDK hazards

SC4 is a 32-bit application, so a wrong calling convention or virtual signature corrupts the stack immediately. Debug builds report this as `_RTC_CheckEsp`; release builds may simply crash. Free functions in the Windows executable commonly use `__cdecl`; class methods reached through vtables commonly use `__thiscall`. Validate each non-SDK entry point and vtable slot against the target Windows executable before use.

Startup popup wrapper:

```cpp
bool (__cdecl*)(cIGZString const& caption, cIGZString const& message)
```

at game address `0x78DD80`. Ignore the Boolean return value. A `__stdcall` declaration caused an ESP mismatch after dismissing the popup.

MSVC places every virtual overload of a name at the vtable slot of the first one declared, in reverse declaration order. A header's declaration order is therefore not its slot order: reordering overloads to read like the game's slot numbers moves them to the wrong slots, and a second virtual with an existing name far away in the class shifts every slot in between.

`cIGZWin.h`, `cIGZWinGen.h`, `cIGZWinBtn.h`, `cIGZWinTextEdit.h`, and `cIGZWinCtrlMgr.h` in `Dev/vendor/gzcom` come from gzcom-dll `fe42ff7`. MSVC's `/d1reportSingleClassLayout` output for these and the other vendored window interfaces has no method-name mismatch against the SC4 Engine Atlas slot table. The earlier `cIGZWin.h` compiled `SetSize(width, height)` to the game's `SetArea(rect)` slot, `GZWinMoveTo` to `GZWinOffset`, the `SetFillColor` overloads to each other's slots, and everything from slot 118 upward one slot low, which is what made those calls look unsafe.

Matching slots do not prove a method's behavior. Production use of a UI virtual method not yet called in game still requires targeted Windows v641 validation. That includes `SetArea`, `CenterWindowInRect`, `SetFillColor`, `SetFillColorRGB`, and the `cIGZWinCtrlMgr` factories. Production windows use authored UI script resources instead of those factories.

Safe geometry queries used by the laboratory are `GetL`, `GetT`, `GetR`, and `GetB`.

## Lifecycle and input behavior

The first-install/changelog popup is a baked native SC4 window generated from `docs/changelog.md`. It is displayed during the first city load for each installed plugin version. Controls and camera-settings guidance are intentionally kept out of the changelog body; the settings window has a `View Controls` button that opens a smaller baked controls popup, and the greeting window has a `View Github` button and a separate top-right note pointing to the floating camera button. The changelog body uses a read-only multiline `GZWinTextEdit` with `vscrollbar=yes`, not a plain `GZWinText`, so release notes can grow without increasing the window size.

Creating the managed greeting during city-load notification can run before the city view and UI hierarchy finish initializing. A 3-second Win32 timer defers greeting creation until the native UI is ready.

Opening a plugin child window directly inside a settings-window button callback can leave the child behind the settings window after SC4 finishes its own command handling. The stable pattern is to schedule the child open on a short timer, let the button callback return, then send the settings window back and call `PullToFront()` on the child. This is used for the Show Changelog button.

The canvas Win32 filter remains installed for the full city lifetime in both
camera modes. In Classic mode it performs only plugin-window mouse routing;
Modern camera input handling still returns immediately. The floating menu
button participates in that hit test, refreshes its native button cursor while
hovered, and reapplies the cursor after a mode transition so a newly selected
native city tool cannot replace it with the circular world cursor. The settings
controls are synchronized before the camera-mode callback performs its
world-focus and native-tool handoff.

Show Changelog uses an additional z-order guard: when the user explicitly opens the child from Settings, destroy and recreate that child before showing it, then send Settings back and pull the child to the front. Reusing an existing native child can inherit stale z-order after the user changes settings, especially after switching to Classic and reopening a child window. Do not replace this with ordinary reuse unless a replacement z-order rule is validated in game. The Advanced page avoids the problem by staying in the same window.

The control laboratory is created only after the UI services and city view are available. While it is visible, camera input is suppressed so clicks and drags intended for controls cannot move the city camera.

Most root windows and controls should be reused by showing/hiding them. Avoid reconstructing the entire hierarchy during ordinary interaction unless a specific native UI behavior requires it. The changelog window is an exception because recreation prevents stale child-window z-order.

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
- The Advanced page remains available in Classic because diagnostics are not Modern-only.
- Camera Tuning only affects the Modern camera. Its buttons are disabled in Classic without changing the saved values.
- Every button option change should emit an explicit normal log entry. Slider movement can emit verbose per-step changes, with the delayed save logging the final values at normal level.

## Persistence files

Runtime files are located in the `SC4-ModernCamera` subfolder beside the plugin DLL. The plugin discovers that location dynamically rather than assuming a Documents path:

- `SC4-ModernCamera.json`: persistent user settings and installed-version marker.
- `test.json`: control-laboratory event/state output.
- `SC4-ModernCamera.log`: current plugin log.
- `SC4-ModernCamera.last`: previous plugin log retained when startup rotates the
  current log.

These files are generated at runtime and are not build artifacts. Legacy root-level files are migrated into the subfolder before the logger or settings system opens them. The DLL and companion UI DAT remain in the Plugins root.

After the framework lifecycle hook is registered, the logger moves file output
off the game thread. The startup and shutdown rules are documented in
[runtime-lifecycle.md](runtime-lifecycle.md).

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
- The control laboratory writes `test.json`; the production Settings window does not depend on it.
- gzcom-dll declares `BeginPaint`, `Paint`, and `EndPaint` window-procedure messages on `cIGZWinGen`, and `cIGZWin::GetDrawContext` returns a draw context with no SDK interface. Custom painting through them has not been tried.
- Whether a nested `GZWinGen` forwards child commands to its parent's procedure has not been established. The settings pages avoid the question by keeping every control a direct child of the root.
