# SC4-ModernCamera source-layout refactor

## Purpose

`Dev/src/Main.cpp` currently contains director lifetime, input-state tracking, Win32 timer bookkeeping, vtable patching, native-input policy, and canvas-message routing. The objective is to reduce it to the plugin runtime and director orchestration without changing SimCity 4 behavior.

This is a behavior-preserving refactor. Reverse-engineered IDs, vtable indices, calling conventions, hook order, and native-input pass-through rules are not to change as part of it.

## Current source boundaries

The project already has focused translation units for several responsibilities:

- `SC4CameraController.cpp`: camera state, bounds, projection, save-preview, and redraw operations.
- `SC4WindowManager.cpp`: the native/baked window implementations and settings UI.
- `PluginSettings.cpp`: settings persistence and serialization.
- `SC4VersionDetection.cpp` and `PluginPaths.cpp`: version and filesystem concerns.

The refactor must extend these modules rather than create duplicate camera, UI, or settings implementations.

## Target layout

New sources live beneath `Dev/src` and have matching Visual Studio filters.

- `Input/KeyStateManager.h/.cpp`
  - `KeyOwner`, `TrackedKeyState`, and the key state/ownership tracker.
  - The physical-key reader is injected, so the tracker can be exercised without Windows hooks.
- `Runtime/TimerManager.h/.cpp`
  - Owns the Win32 timer IDs and the matching `SetTimer`/`KillTimer` operations.
  - Timer callbacks remain thin and are still owned by the plugin runtime.
- `Hooks/VTableHook.h/.cpp`
  - Safely owns one patched vtable slot and restores it only if the slot still contains this plugin's replacement.
  - Game-specific vtable indices and hook policy remain in `Main.cpp`.
- `Main.cpp`
  - Keeps the COM director, city lifetime sequencing, camera/input policy, and the small callbacks that bridge the modules to SC4.

No `GlobalState.h` is introduced. A header of `extern` variables would distribute the current coupling instead of removing it. Each extracted module owns its own private state; `Main.cpp` retains only runtime state that genuinely belongs to the director lifecycle.

## Execution order

1. Extract the key tracker first. Keep the existing policy wrappers and logging in `Main.cpp`, so key ownership and pass-through behavior remain unchanged.
2. Replace the six raw global timer IDs with `TimerManager`. Preserve the existing timer procedures and their one-shot/repeating semantics.
3. Replace raw vtable/original-function globals with two `VTableHook` instances. The hook handlers stay in `Main.cpp` because they apply camera and UI policy.
4. Add every new source and header to both `SC4-ModernCamera.vcxproj` and `SC4-ModernCamera.vcxproj.filters`.
5. Remove dead declarations and verify there are no duplicate definitions, stale raw timer IDs, or stale vtable patch helpers.
6. Validate in Visual Studio and in-game. Command-line builds are intentionally not part of this workflow.

## Required manual validation

- Enter and leave a city in both Modern and Classic modes.
- Toggle Modern/Classic camera and native UI minimization.
- Test keyboard pan, held keys during focus changes, Alt classic-angle hold, and Ctrl/Alt/Win pass-through.
- Test middle-mouse rotation, wheel zoom, and settings-window focus restoration.
- At city bounds, verify native scroll stops safely rather than moving the camera beyond the city.
- Save a city and confirm save-preview normalization still starts and ends correctly.

## Follow-up boundaries

After this extraction has been built and manually validated, the next safe move is a runtime-owned input router. It must receive explicit references/callbacks for the camera controller, settings, window manager, and director lifetime state. Do not extract hooks, timers, or canvas input as free functions backed by shared `extern` globals.
