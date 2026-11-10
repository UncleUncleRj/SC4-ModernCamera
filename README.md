# SC4-ModernCamera

A DLL plugin that gives SimCity 4 Deluxe Edition a modern, mouse-controlled city builder camera.

<a href="https://ko-fi.com/V6N721RG6K">
  <img src="https://rickeyjsimas.neocities.org/img/Ko-fi_FIRE.gif" width="48" alt="Ko-fi" align="left">
</a>
<br>
<a href="https://ko-fi.com/V6N721RG6K"><strong>Support Development!</strong></a>
<br clear="left">
<p><br></p>
Visit the <a href="https://simcity4-dev.neocities.org/"><strong>SC4 Engine Atlas</strong></a>!

## Requirements

Windows version of SimCity 4 Deluxe Edition, game version `641` (Steam, GOG, or EA App). Disc and retail builds are not supported.

## Installation

1. Download the latest package from the [releases page](https://github.com/UncleUncleRj/SC4-ModernCamera/releases) or from [Simtropolis](https://community.simtropolis.com/files/file/37462-sc4-modern-camera/).
2. Copy `SC4-ModernCamera.dll` and `SC4-ModernCamera.dat` to `%USERPROFILE%\Documents\SimCity 4\Plugins\`.
3. Start the game and load a city.

With [sc4pac](https://memo33.github.io/sc4pac/), install `rj-simas:modern-camera` from the [Simtropolis channel](https://sc4pac.simtropolis.com/).

## Controls

These are the Modern camera controls. Classic mode uses SimCity 4's own camera.

| Input | Action |
| --- | --- |
| WASD or arrow keys | Pan (hold Shift to pan faster) |
| Hold Q / E | Rotate |
| Middle mouse drag | Rotate and tilt |
| Mouse wheel | Zoom, smooth or in SimCity 4's steps |
| Hold Alt | Move to the nearest Classic angle and zoom, then let go to return |
| Ctrl+1 to Ctrl+8 | Save the current view as a bookmark |
| 1 to 8 | Jump to a saved bookmark |
| Ctrl+Alt+F8 | Write a camera report to the log |

The camera button in the upper-right corner opens the settings, where you can switch between Modern and Classic, change these controls, and reset the camera. Click the small light on the button to switch modes without opening the settings. **View Help** in the settings explains every option.

## Cheat codes

Press Ctrl+X to open SimCity 4's cheat console. Every code also works with the short prefix `SC4MC` in place of `ModernCamera`, so `SC4MCFix` resets the camera too. Codes ignore case.

| Code | Action |
| --- | --- |
| `ModernCamera` | Switch between Modern and Classic |
| `ModernCameraFix` | Reset the camera |
| `ModernCameraDefaults` | Restore every setting to its default |
| `ModernCameraDump` | Write a camera report to the log |
| `ModernCameraWASD` | Turn WASD movement on or off |

Type a value code without a number to see its current value. Codes work in both modes; in Classic they change the saved setting, which applies when you switch back to Modern.

## Themes

To reskin the mod's windows and camera button, see [RESKINNING.md](RESKINNING.md).

## Building

Open `SC4-ModernCamera.slnx` in Visual Studio and build the `SC4-ModernCamera` project for `Win32`. Python 3.10 or newer is required to generate the DAT. The build runs it with `py -3`; set the `PythonExe` property to use another Python.

Each build copies the DLL and DAT to `%USERPROFILE%\Documents\SimCity 4\Plugins\`. Set the `PLUGINS_FOLDER` property to copy them somewhere else.

## Acknowledgements

- [gzcom-dll](https://github.com/nsgomez/gzcom-dll): SimCity 4 plugin SDK classes, the basis of this project's SDK in `Dev/sdk`.
- [3D Camera DLL for SimCity 4](https://github.com/memo33/sc4-3d-camera-dll): camera pitch and yaw memory tables and camera overrides.
- [sc4-render-services](https://github.com/caspervg/sc4-render-services): camera control layout and update flow.
- [sc4-dll-utilities](https://github.com/0xC0000054/sc4-dll-utilities) and [SC4Fix](https://github.com/nsgomez/sc4fix): game version detection.

## License

Licensed under the [GNU General Public License version 3.0](LICENSE). The SDK in `Dev/sdk`, derived from [gzcom-dll](https://github.com/nsgomez/gzcom-dll), is licensed under the GNU Lesser General Public License version 2.1 or later.
