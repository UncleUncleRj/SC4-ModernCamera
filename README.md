# SC4-3DMouseCam

SC4-3DMouseCam is a DLL plugin for SimCity 4 Deluxe Edition. It completely changes the SC4 Camera into a more modern City Builder style Mouse-Controlled camera.

This project is currently in BETA. Feel free to play with it, but __WARNING - backup your important cities and regions first!__

## Requirements

- Windows version of SimCity 4 Deluxe Edition (Rush Hour)
- Game version `641`, used by the Steam, GOG, and EA App digital releases

Older disc/retail builds are not supported, and at this time support is not planned.

## Installation

1. Download the `SC4-3DMouseCam.dll` file from the releases page.
2. Copy it to your SimCity 4 Plugins folder, usually:

   ```text
   %USERPROFILE%\Documents\SimCity 4\Plugins\
   ```

3. Start the game and load a city.

## Current Controls

- **M3 + horizontal drag**: Rotates the camera.
- **M3 + vertical drag**: Adjusts the camera pitch.
- **Mouse wheel**: Smoothly zooms while preserving the current camera angle.
- **F8**: Dumps the current camera and renderer state to a file `SC4-3DMouseCam.log` located in the plugins folder for debugging.

## Debug Logging

The plugin writes debug logs to the Plugins folder:

- `SC4-3DMouseCam.log`
- `SC4-3DMouseCam.last`

These files can be safely ignored or deleted.

## Technical Documentation

- [SimCity 4 default camera behavior](docs/default-camera.md) records the verified native zoom, pitch, yaw, and rotation behavior.
- [Region preview save fix](docs/region-preview-save-fix.md) documents the save lifecycle integration, rejected hook approaches, and final normalization design.

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

Bundled [gzcom-dll](https://github.com/nsgomez/gzcom-dll) code is licensed under the GNU Lesser General Public License version 2.1 or later.
