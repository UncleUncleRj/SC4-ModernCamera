# SC4-3DMouseCam

SC4-3DMouseCam is an experimental DLL plugin for SimCity 4 Deluxe Edition on Steam which will modify the camera to allow middle-mouse 3D Movement (RTS Style).

This project is currently an early alpha. It is intended for testing, feedback, and development acceleration rather than normal gameplay.

## Requirements

- Windows version of SimCity 4 Deluxe or Rush Hour
- Game version `1.1.641`, typically the Steam or GOG digital release

Older disc/retail builds are not supported, and at this time support is not planned.

## Installation

1. Download the `SC4-3DMouseCam.dll` file from the releases page.
2. Copy it to your SimCity 4 Plugins folder, on Windows native it's usually:

   ```text
   %USERPROFILE%\Documents\SimCity 4\Plugins\
   ```

3. Start the game and load a city.

## Current Controls

- **Middle Mouse Button (M3) + drag**: Adjusts the camera angle.
- **Mouse wheel**: Experimental smooth magnification/zoom while preserving the current camera angle.
- **F8**: Dumps the current camera and renderer state to `SC4-3DMouseCam.log` for debugging.


## Debug Logging

The plugin writes debug logs to the Plugins folder:

- `SC4-3DMouseCam.log`
- `SC4-3DMouseCam.last`

These files are for debugging only and can be safely ignored or deleted.

## Building

Open `SC4-3DMouseCam.slnx` in Visual Studio and build the `Dev` project for `Win32`.

The project is configured as a DLL plugin and includes the required GZCOM source files and headers under `Dev/src` and `Dev/vendor/gzcom`.

## Acknowledgements

This project builds on work from all around the SimCity 4 DLL modding community.

- [gzcom-dll](https://github.com/nsgomez/gzcom-dll) provides the SimCity 4 GZCOM plugin SDK classes used by this DLL.
- [3D Camera DLL for SimCity 4](https://github.com/memo33/sc4-3d-camera-dll) demonstrated the camera pitch/yaw memory tables and cheat-code driven camera overrides that made this project possible.
- [sc4-render-services](https://github.com/caspervg/sc4-render-services), especially the camera-view-input sample, informed the reconstructed camera-control layout and cleaner camera update flow used here.
- [sc4-dll-utilities](https://github.com/0xC0000054/sc4-dll-utilities) and [SC4Fix](https://github.com/nsgomez/sc4fix) informed the game-version detection fallback pattern.

## License

This project is licensed under the terms of the GNU General Public License version 3.0.
See [LICENSE](LICENSE) for more information.

## 3rd party code

[gzcom-dll](https://github.com/nsgomez/gzcom-dll) - GNU Lesser General Public License version 2.1 or later.

The GZCOM source files and headers used by this project are included under `Dev/src` and `Dev/vendor/gzcom`.

## Disclaimer

This alpha may cause visual glitches, camera oddities, game instability, or other unexpected behavior. It is unlikely to harm your PC, but save-game issues have not been ruled out. Back up important cities or regions before testing.

