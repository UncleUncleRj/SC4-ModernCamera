# Settings Window Workflow

This document records production behavior for the SC4-ModernCamera settings UI.

## Window Structure

The floating camera button in the upper-right corner opens the main Camera Settings window. It hides and shows with SC4's native UI by observing the native `cISC4View3DWin::MinimizeUI(bool)` transition.

The window has General and Advanced tabs under the title bar, a View Controls button at the far right of that row, and shared footer actions: Defaults, Show Changelog, and Close. It reopens on General after it is closed.

The General page contains:

- Camera: Camera Mode (Modern, Classic) and Reset Camera Location.
- Modern: WASD Movement, Q/E Rotation, Invert Vertical, On Alt Release, Bookmark Hotkeys, rotation sensitivity, zoom sensitivity, and panning speed.

The Advanced page contains:

- Camera Tuning: Q/E rotation speed, Shift pan boost, Invert Q/E, Mouse wheel zoom, and Invert wheel zoom.
- Diagnostics: Debug Logging, with a note about the log location and the Ctrl+Alt+F8 camera dump, which is always available.
- Updates: Don't notify me of updates in the news ticker.

Advanced is intentionally available in both Modern and Classic camera modes. Camera Tuning buttons are disabled in Classic, and Invert wheel zoom is also disabled while Mouse wheel zoom is Stepped, because SC4's stepped zoom cannot be inverted.

Every setting in `PluginSettings` has a control on one of these pages. Pages are shown by control ID range, as described in [ui-theme-contract.md](ui-theme-contract.md); every control stays a direct child of the root window so button and slider notifications keep reaching the window's procedure directly.

## Adding an Option

1. Add the field, default, and JSON read and write to `PluginSettings`.
2. Add one row to `GENERAL_SECTIONS` or `ADVANCED_SECTIONS` in `tools/build_sc4_ui_dat.py`, with IDs from that page's range.
3. Handle the control IDs in `SettingsWindow`: sync, click, the Defaults log, and the Modern-only list when it applies.
4. Add interactive controls to `REQUIRED_CONTROLS` and `ValidateThemeControls`.
5. Run `tools/test_sc4_ui_layout.py` and `tools/test_sc4_ui_theme.py`.

## Camera Mode Rules

Modern camera mode is the default when no saved settings file exists. Switching between Modern and Classic applies immediately while the settings window is open.

Classic mode:

- resets the camera location before disabling Modern camera control;
- disables Modern-only controls in the main settings window;
- keeps Reset Camera Location enabled as an emergency recovery action;
- leaves the Advanced page available for diagnostics, with Camera Tuning disabled.

Classic mode must not erase the user's saved Modern-only preferences. WASD, Q/E rotation, sensitivity, panning speed, invert vertical, On Alt Release, Bookmark Hotkeys, and Camera Tuning reappear with their saved values when the user switches back to Modern.

Modern mode:

- re-enables the Modern-only controls;
- moves an off-map camera target back near the city center when returning from Classic camera mode;
- clears the native view tools with the same call as SC4's Cancel command, `RemoveAllViewInputControls(false)`, so Modern camera input does not inherit a zoning, network, or other tool cursor selected while Classic controls were active. Passing `true` skips the cursor reset and leaves the old tool's cursor showing;
- resumes Modern camera input and redraw behavior immediately.

## Redraw Behavior

Redraw scheduling is fixed and has no setting. Classic mode leaves redraws to the game. Modern mode requests one full redraw 1 second after M3 rotation ends and 1.5 seconds after the last wheel zoom. The older High and Extreme levels, which also redrew on a repeating timer, are gone. A `redrawAggression` key in an older settings file is ignored and dropped on the next save.

## Camera Tuning

Q/E rotation speed is separate from Rotation sensitivity, which now only affects the mouse. A settings file from before this option starts Q/E rotation speed at its Rotation sensitivity, so Q/E keeps the speed it had. Invert Q/E also applies to the Classic steps made while Alt is held, and Invert wheel zoom to the Classic zoom steps. Stepped mouse wheel zoom leaves the wheel to SC4 in Modern mode, except during the Alt hold, which keeps its own Classic steps.

## WASD Movement

WASD Movement is a Modern camera feature. When Modern camera and WASD Movement are enabled:

- W/A/S/D key input is consumed only while the city view has keyboard focus and
  Ctrl, Alt, and the Windows keys are up.
- movement is frame-driven, not key-repeat driven, so it starts while held and stops immediately on key release;
- Shift + WASD applies the configured boost multiplier;
- the panning speed slider applies to WASD movement;
- movement uses the same camera bounds as the other Modern camera movement paths;
- movement is disabled while right-click scrolling is active;
- movement remains active during M3 camera maneuvering and zoom.

Holding Ctrl, Alt, or either Windows key stops any held Modern Camera movement
and returns W/A/S/D and arrow keys to SC4 for native shortcuts. Plain Alt still
has its separate Modern Camera classic-angle function when the city view has
keyboard focus and Ctrl or a Windows key is not held. While that hold owns Alt,
W/A/S/D and arrow keys keep panning, and Windows still sees Alt for chords such as
Alt+Tab and Alt+F4.

When WASD Movement is off, or Classic camera is active, the plugin stops consuming W/A/S/D and returns those keys to the game.

When WASD Movement is re-enabled while Modern camera is active, the plugin clears the current native view tool before consuming WASD again. This prevents a tool selected through SC4's native W/A/S/D shortcuts from staying active while Modern camera movement starts.

SC4's standard arrow-key panning remains available independently of the WASD Movement setting. In Modern camera mode, arrow-key panning and WASD Movement use the same internal `SetScrolling` path so panning speed, Shift boost, focus gating, modifier pass-through, and map-boundary handling stay consistent.

## U-Drive-It

While the player drives a U-Drive-It vehicle, Modern Camera gives its input to the game: W/A/S/D, the arrow keys, Q/E, Alt, bookmark hotkeys, the middle mouse button, and the wheel all reach SC4 exactly as they do in Classic mode. Modern controls come back when the drive ends.

This is a temporary handoff that does not run the Camera Mode switch. `g_Settings.cameraMode`, the current view, and the saved preferences are untouched, and the switch's camera reset and tool clearing do not run. `RunCameraFrame` checks once per frame whether the current view tool's ID is `0x2B709676`, the same test as `cISC4AutomataManager::IsDrivingMode` (binary-verified at `0x506940`). `IsModernInputActive` is the gate for the keyboard hook, the canvas filter, and the `SetScrolling` bounds check.

`ClearNativeView3DToolState` keeps this tool, because removing it would end the drive. Changing Camera Mode or WASD Movement during a drive leaves the player in the vehicle.

If another tool is stacked over the drive tool, it is no longer the current tool, so Modern input resumes until that tool is removed. `IsDrivingMode` also reports false in that state.

## On Alt Release

On Alt Release decides what releasing Alt does in Modern mode. Restore View, the default, returns to the view saved when Alt was pressed, and Keep View stays on the view Alt let go of. It is stored as `altCameraBungie` in the `camera` block of `SC4-ModernCamera.json`, `true` for Restore View. A change in the menu applies to the next Alt release. A file without the option gets it, at its default value, the next time the plugin loads. The Defaults button resets it to Restore View.

## Slider Saves

Sensitivity sliders update live but save with a short delay. This prevents a drag from writing many settings files in rapid succession.

The delayed save log line should include the saved rotation, zoom, and panning speed values. Per-step slider changes can remain verbose because they are noisy by nature.

## Option Change Logging

Settings changes should be explicit in the log. Button/toggle/menu changes belong at normal info level. Slider movement can be verbose, with the delayed save emitting the saved values at info level.

Important log patterns:

- `Settings UI: Camera Mode changed from Modern to Classic.`
- `Settings UI: WASD Movement changed from On to Off.`
- `Settings UI: Alt Release changed from Restore View to Keep View.`
- `Settings UI: Bookmark Hotkeys changed from On to Off.`
- `Advanced Settings UI: Mouse Wheel Zoom changed from Smooth to Stepped.`
- `Advanced Settings UI: Diagnostics Logging changed from Normal to Verbose.`
- `Settings UI: saved delayed settings after rotation sensitivity change; rotationSensitivity=... zoomSensitivity=... panSensitivity=...`

When diagnostics logging is set to Off, subsequent normal log lines may be intentionally suppressed.

## Native UI Synchronization

SC4 starts each city with the native UI visible, so the floating camera button is shown during city-load window setup. After that, the button follows SC4's native hide/show UI state through a vtable hook on `cISC4View3DWin::MinimizeUI(bool)`.

The lower-left corner click area is only diagnostic context. It logs that a likely native UI toggle click occurred, but it must not decide visibility from pixels or act as a separate show/hide button. The validated source of truth is the Boolean passed to `MinimizeUI`: `true` means the native UI is being hidden and the camera button should hide; `false` means the native UI is being restored and the camera button should show.

Probe-based synchronization is not used. Fixed lower-left UI probes and timer retries could detect hide, but restore was unreliable because the restored mini-button hit-tested as the full 3D surface rather than as a native button/control.

## Child Window Z-Order

The Advanced page needs none of this, because a tab click only changes which controls are visible.

Do not create child windows directly inside a settings button callback. SC4 can finish its command handling by restoring the parent window above the child.

The stable flow is:

1. The button callback schedules a short timer.
2. The callback returns to SC4.
3. The timer opens the requested child window.
4. The manager sends the settings window behind the child.
5. The manager pulls the child to front.

Show Changelog has one extra hardening step: destroy and recreate the child window when explicitly opened. Reusing an existing native child can inherit stale z-order after the user has changed settings, especially after switching to Classic and reopening a child window.

If this regresses, inspect `SC4WindowManager::ScheduleDeferredWindowOpen`, `SC4WindowManager::OnDeferredWindowOpenTimer`, and the `DeferredWindowOpen::Changelog` case first.

## Changelog Window

The changelog window opens from Show Changelog in Settings and from the update notice's See what's new link; nothing opens it automatically after an install. Its body shows the latest GitHub release at runtime, and no changelog text is baked into the DAT, so an out-of-date list can never appear. `Github` and `Simtropolis` open the project pages in the default browser. Installed comes from `Dev/src/PluginVersion.h`; Current and Update available! are set at runtime.

`ReleaseNotesDownloader` reads `https://github.com/UncleUncleRj/SC4-ModernCamera/releases/latest` on a worker. It takes the latest version from the URL the redirect ends at (`/releases/tag/v1.0.4` is `1.0.4`) and the notes from the `markdown-body` element inside the page's `Box-body`, which leaves out the title and author rows. A game-thread timer polls every 250 ms. A failed download shows a message pointing to `Github`, logs the reason, and is retried by the next city load or window open. Because this scrapes GitHub's page markup, a markup change appears as that message with `the release page had no notes inside Box-body` in the log; the version check does not depend on the markup.

## Update Notice

A city load starts the download unless the player turned notices off, and it compares the latest version with the installed one numerically. The notice is a real game advice, defined by the package script `Dev/ui/SC4-ModernCamera-UpdateNotice.lua`, which the DAT builder stores with the advisor script type and group. The script's trigger reads `sc4moderncamera.update_available`, and `SC4WindowManager::PublishUpdateNotification` sets that field through `cISCLua::DoString` in the city's advisor Lua state. The game then shows the title in the ticker and the message in its advice list. The message's link calls the native `sc4moderncamera_open_changelog`, which `OnCityLoaded` registers for each city's Lua state and which uses the deferred window open.

The advice's `title` and `message` are `text@` keys, not literal text. The game resolves every advice string as an LTEXT resource (type `0x2026960B`, group `0x6A231EA4`, binary-verified at `0x4178F0`) and prefixes `***_NOT_IN_TEXT_DATABASE_***` to anything it can't find, so the DAT builder also stores the two strings at instances `0x3D0C0A03` and `0x3D0C0A04`. The game substitutes `#s@...#` and `#link_id#` tokens after that lookup.

Priority is 59 and has to stay there. When any active advice is above 59 the ticker cycles only advice at 59 or higher (binary-verified at `0x774070`), and the lowest popup threshold is `ADVICE_PRIORITY_MED_HIGH` (60), so 59 reaches the ticker without opening a dialog. It uses the base advice's `frequency` of 720 days: a dismissed notice returns after two game years until the player updates. Turning notices off, or installing the new version, makes the trigger false and the game removes the advice at its next check. The game stores active advice in the city file by GUID (`0x3D0C0A02`).

An older settings file's `showChangelogsOnUpdate` value seeds `notifyUpdatesInTicker`, so someone who turned off the install popup also gets no ticker notice.

The body uses a read-only multiline `GZWinTextEdit` with `vscrollbar=yes` so changelog text can grow without increasing the window size. Visual Studio generates the companion DAT from `tools/build_sc4_ui_dat.py` during the project pre-build event; manual DAT edits should be avoided.
