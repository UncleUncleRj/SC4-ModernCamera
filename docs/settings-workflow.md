# Settings Window Workflow

This document records the current production behavior of the SC4-ModernCamera settings UI. It is meant as a quick reference for future settings-window, input, redraw, and diagnostics work.

## Window Structure

The floating camera button in the upper-right corner opens the main Camera Settings window.

The main window contains:

- Camera Mode: Modern and Classic.
- Modern settings: WASD Movement, sensitivity sliders, Invert Vertical, Reset Camera Location, and Advanced Settings.
- Footer actions: Defaults, Show Changelog, and Close.

Advanced Settings opens a child window for:

- Redraw Aggression.
- Diagnostics logging.

Diagnostics are intentionally available in both Modern and Classic camera modes.

## Camera Mode Rules

Modern camera mode is the default for new installs. Switching between Modern and Classic applies immediately while the settings window is still open.

Classic mode:

- resets the camera location before disabling Modern camera control;
- disables Modern-only controls in the main settings window;
- keeps Reset Camera Location enabled as an emergency recovery action;
- leaves Advanced Settings available for diagnostics;
- visually forces Advanced Settings redraw aggression to Classic.

Classic mode must not erase the user's saved Modern-only preferences. WASD, sensitivity, invert vertical, and redraw aggression should reappear with their previous values when the user switches back to Modern.

Modern mode:

- re-enables the Modern-only controls;
- restores the actual saved redraw aggression selection in Advanced Settings;
- resumes Modern camera input and redraw behavior immediately.

## Redraw Aggression

Redraw Aggression only affects the Modern camera. The buttons are:

- Classic: standard classic-camera behavior; no Modern redraw aggression.
- Normal: normal Modern redraw behavior.
- High: previous aggressive behavior; periodic redraw every 1000 ms and idle redraw delay divided by 4, minimum 100 ms.
- Extreme: stress-test behavior; periodic redraw every 100 ms and idle redraw delay divided by 20, minimum 25 ms.

When Classic camera mode is selected, the Advanced Settings redraw group shows Classic as selected and leaves Normal, High, and Extreme disabled. This is visual state only; it does not overwrite the saved Modern redraw preference.

## WASD Movement

WASD movement is a Modern camera feature. When Modern camera and WASD Movement are enabled:

- W/A/S/D key input is consumed before the game can process its default actions.
- movement is timer-driven, not key-repeat driven, so it starts while held and stops immediately on key release;
- Shift + WASD applies the configured boost multiplier;
- movement uses the same camera bounds as the other Modern camera movement paths;
- movement is disabled while right-click scrolling is active;
- movement remains active during M3 camera maneuvering and zoom.

When WASD Movement is off, or Classic camera is active, the plugin stops consuming those keys and returns them to the game.

## Slider Saves

Sensitivity sliders update live but save with a short delay. This prevents a drag from writing many settings files in rapid succession.

The delayed save log line should include the final rotation and zoom sensitivity values. Per-step slider changes can remain verbose because they are noisy by nature.

## Option Change Logging

Settings changes should be explicit in the log. Button/toggle/menu changes belong at normal info level. Slider movement can be verbose, with the delayed save emitting the final values at info level.

Important log patterns:

- `Settings UI: Camera Mode changed from Modern to Classic.`
- `Settings UI: WASD Movement changed from On to Off.`
- `Advanced Settings UI: Redraw Aggression changed from Normal to Extreme.`
- `Advanced Settings UI: Diagnostics Logging changed from Normal to Verbose.`
- `Settings UI: saved delayed settings after rotation sensitivity change; rotationSensitivity=... zoomSensitivity=...`

When diagnostics logging is set to Off, subsequent normal log lines may be intentionally suppressed.

## Child Window Z-Order

Do not create child windows directly inside a settings button callback. SC4 can finish its command handling by restoring the parent window above the newly opened child.

The stable flow is:

1. The button callback schedules a short timer.
2. The callback returns to SC4.
3. The timer opens the requested child window.
4. The manager sends the settings window behind the child.
5. The manager pulls the child to front.

Advanced Settings and Show Changelog both have one extra hardening step: destroy and recreate the child window when explicitly opened. Reusing an existing native child can inherit stale z-order after the user has changed settings, especially after switching to Classic and reopening a child window.

If this regresses, inspect `SC4WindowManager::ScheduleDeferredWindowOpen`, `SC4WindowManager::OnDeferredWindowOpenTimer`, `SC4WindowManager::ShowAdvancedSettingsWindow`, and the `DeferredWindowOpen::Changelog` case first.

## Changelog Window

The changelog popup is the same baked Welcome/Greeting window resource used for the first-install/version notice. Its changelog body is generated from `docs/changelog.md`; camera-settings guidance and the `View Controls` affordance live outside that scrollable changelog body.

The body uses a read-only multiline `GZWinTextEdit` with `vscrollbar=yes` so changelog text can grow without increasing the window size. Visual Studio regenerates the ignored generated file `Dev/ui/SC4-ModernCamera.dat` from `tools/build_sc4_ui_dat.py` during the project pre-build event; manual DAT edits should be avoided.
