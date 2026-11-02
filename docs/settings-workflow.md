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
- Updates: Show Changelogs on Update.

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

The changelog popup is the same baked Welcome/Greeting window resource used for the first-install/version notice. Its changelog body is generated from `docs/changelog.md`; camera-settings guidance and the `View Github` button live outside that scrollable changelog body. `View Github` opens the project page in the default browser; the controls popup now opens from `View Controls` in the settings window.

The body uses a read-only multiline `GZWinTextEdit` with `vscrollbar=yes` so changelog text can grow without increasing the window size. Visual Studio generates the companion DAT from `tools/build_sc4_ui_dat.py` during the project pre-build event; manual DAT edits should be avoided.
