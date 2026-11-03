2.0.0:
- Added camera bookmarks with 8 slots:
   - Ctrl+1 through Ctrl+8 saves the current view, and 1 through 8 returns to it.
   - Bookmark hotkeys can be turned off in the menu.
- Reworked the ALT camera hold:
   - Holding ALT now moves smoothly to the nearest Classic rotation, zoom, and angle.
   - While ALT is held, Q/E, the mouse wheel, and middle mouse drag move in Classic steps.
   - New setting: **On Alt Release** - return to the view you had when you pressed ALT, or stay where you are.
- Rebuilt the settings menu:
   - Advanced settings are now a tab in the main window.
   - Added a View Controls button.
   - The settings button artwork now shows the active camera mode.
- New Camera Tuning settings:
   - **Q/E rotation speed**
   - **Shift pan boost**
   - **Invert Q/E**
   - **Mouse wheel zoom** - Smooth or Stepped.
   - **Invert wheel zoom**
- Removed the Redraw Aggression options. Classic leaves redraws to the game, and Modern uses the former Normal timing.
- Removed the Camera Dump setting. The Ctrl+Alt+F8 camera dump no longer needs to be turned on.
- Added an update notice in the news ticker, which can be turned off in the menu.
- The changelog window now shows the notes from the latest GitHub release.
- THEMES: The theme format is now 2.0, and 1.x themes are ignored.
- BUGFIX: U-Drive-It vehicles now respond to WASD and the other game controls. Modern Camera controls return when the drive ends.
- BUGFIX: No Power, No Water, No Work, and No Car icons keep their shape at any camera angle.
- BUGFIX: The cursor now stays hidden while rotating the camera with the middle mouse button.
- BUGFIX: The settings window title bar can now be used to drag the window.
- BUGFIX: The settings window now shows current values when it is reopened.
- Performance improvements

1.5.0:
- New Menu Layouts
- Added new camera rotation controls for Modern Camera mode:
   - Hold Q/E to rotate the camera left/right.
   - Option to disable camera rotation in menu.
- New Settings Menu Icon
- Bug fixes
- Performance improvements

1.0.4:
- Hold ALT to lock the camera to the nearest Classic Mode camera angle.
   - NOTE: ALT lock is still experimental, if you lose your camera,
           goto the Modern Camera menu and click "RESET CAMERA LOCATION".
           
- Re-written input manager for better WASD/Arrow key handling - again.
- Bug fixes

1.0.3:
- WASD and Arrow keys are now linked in Modern Camera mode.
- New setting available:
   - **Panning speed** - affects WASD/Arrow key movement speed in modern camera mode.
- Performance improvements
- Bug fixes

1.0.2:
- COMPATIBILITY PATCH: Added full support for the SimCity 4 Extra Cheats plugin.
- BUGFIX: Native SimCity 4 keyboard bindings now function correctly alongside the plugin.
- BUGFIX: The plugin will now safely abort initialization if it detects an unsupported game version.
- UI: The Modern Camera settings button now automatically hides when the interface is minimized.

MAJOR RELEASE 1.0:
- Toolbars and other UI elements (advisor panels, etc) now allow you to use the mouse wheel to scroll in Modern camera mode.
- Polished the welcome/changelog window and UI image handling.
- Added persistent JSON settings.
- Added version-aware first-install notice.
- Added native SC4 window manager foundation.
- Added optional smooth WASD camera movement with Shift boost.
- Added in-game Camera Settings and Advanced Settings windows.
- Added Classic and Modern camera mode switching from the settings window.
- Added Modern-only redraw aggression controls: Classic, Normal, High, and Extreme.
- Added diagnostics logging controls.
- Added Reset Camera Location.
- Added plugin data folder: Plugins/SC4-ModernCamera/
- Added 3D camera movement and rotation controls.
