# SC4-ModernCamera

A DLL plugin that gives SimCity 4 Deluxe Edition a modern, mouse-controlled city builder camera.

<a href="https://ko-fi.com/V6N721RG6K">
  <img src="https://rickeyjsimas.neocities.org/img/Ko-fi_FIRE.gif" width="48" alt="Ko-fi" align="left">
</a>
<br>
<a href="https://ko-fi.com/V6N721RG6K"><strong>Support Development!</strong></a>
<br clear="left">

## Requirements

Windows version of SimCity 4 Deluxe Edition, game version `641` (Steam, GOG, or EA App). Disc and retail builds are not supported.

## Installation

1. Download the latest package from the [releases page](https://github.com/UncleUncleRj/SC4-ModernCamera/releases).
2. Copy `SC4-ModernCamera.dll` and `SC4-ModernCamera.dat` to `%USERPROFILE%\Documents\SimCity 4\Plugins\`.
3. Start the game and load a city.

## Controls

| Input | Action |
| --- | --- |
| WASD or arrow keys | Pan (hold Shift to pan faster) |
| Q / E | Rotate |
| Middle mouse drag | Rotate and change pitch |
| Mouse wheel | Smooth zoom |
| Hold Alt | Move to the nearest Classic camera angle |
| Ctrl + 1 - 8 | Bookmark a Camera View |
| Press: 1 - 8 | Return to a View |


The camera button in the upper-right corner opens the settings, where you can switch between Modern and Classic camera modes, change these controls, and reset the camera.

## Building

Open `SC4-ModernCamera.slnx` in Visual Studio and build the `SC4-ModernCamera` project for `Win32`. Python 3 is required to generate the DAT.

## Acknowledgements

- [gzcom-dll](https://github.com/nsgomez/gzcom-dll): SimCity 4 plugin SDK classes.
- [3D Camera DLL for SimCity 4](https://github.com/memo33/sc4-3d-camera-dll): camera pitch and yaw memory tables and camera overrides.
- [sc4-render-services](https://github.com/caspervg/sc4-render-services): camera control layout and update flow.
- [sc4-dll-utilities](https://github.com/0xC0000054/sc4-dll-utilities) and [SC4Fix](https://github.com/nsgomez/sc4fix): game version detection.

## License

Licensed under the [GNU General Public License version 3.0](LICENSE). Bundled [gzcom-dll](https://github.com/nsgomez/gzcom-dll) code is licensed under the GNU Lesser General Public License version 2.1 or later.
