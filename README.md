# SC4-ModernCamera

SC4-ModernCamera is a DLL plugin for SimCity 4 Deluxe Edition. It completely changes the SC4 Camera into a more modern City Builder style Mouse-Controlled camera.

## Requirements

- Windows version of SimCity 4 Deluxe Edition (Rush Hour)
- Game version `641`, used by the Steam, GOG, and EA App digital releases

Disc and retail builds are not supported.

## Installation

1. Download the release package from the releases page.
2. Copy `SC4-ModernCamera.dll` and `SC4-ModernCamera.dat` to your SimCity 4 Plugins folder, usually:

   ```text
   %USERPROFILE%\Documents\SimCity 4\Plugins\
   ```

3. Start the game and load a city.

## Controls

- - Hold SHIFT to move faster
- **WASD**: Moves the camera. This can be disabled in-game.
- **M3 + horizontal drag**: Rotates the camera.
- **M3 + vertical drag**: Adjusts the camera pitch.
- - In-game Option to Invert Vertical controls
- **Mouse wheel**: New smooth zoom experience.
- **F8**: Dumps the current camera and renderer state to `Plugins\SC4-ModernCamera\SC4-ModernCamera.log` for debugging.

Camera options are available in-game through the floating camera settings button in the upper-right corner of the screen.

The settings window can switch between Modern and Classic camera mode in real time, reset the camera location, show the changelog, and open Advanced Settings for redraw aggression and diagnostics logging.

## Debug Logging

The plugin writes debug logs to `Plugins\SC4-ModernCamera\`:

- `SC4-ModernCamera.log`
- `SC4-ModernCamera.last`

These files can be safely ignored or deleted.

## Technical Documentation

- [SimCity 4 default camera behavior](docs/default-camera.md) records native zoom, pitch, yaw, and rotation behavior.
- [Region preview save fix](docs/region-preview-save-fix.md) documents the save lifecycle integration, rejected hook approaches, and normalization design.
- [Settings window workflow](docs/settings-workflow.md) documents in-game settings behavior, Classic/Modern mode rules, WASD capture, redraw options, logging, and child-window z-order workflow.
- [SimCity 4 native UI research](docs/native-ui-research.md) records control recipes, message IDs, scrolling behavior, ABI constraints, and companion-DAT workflow.

## Building

Open `SC4-ModernCamera.slnx` in Visual Studio and build the `SC4-ModernCamera` project for `Win32`.

The project is configured as a DLL plugin and includes the required GZCOM source files and headers under `Dev/src` and `Dev/vendor/gzcom`.

Release builds statically link the MSVC runtime so released binaries do not normally require users to install a separate Visual C++ Redistributable.

## Acknowledgements

This project builds on work from all around the SimCity 4 DLL modding community.

- [gzcom-dll](https://github.com/nsgomez/gzcom-dll) provides the SimCity 4 GZCOM plugin SDK classes used by this DLL.
- [3D Camera DLL for SimCity 4](https://github.com/memo33/sc4-3d-camera-dll) demonstrated the camera pitch/yaw memory tables and cheat-code driven camera overrides that made this project possible.
- [sc4-render-services](https://github.com/caspervg/sc4-render-services), especially the camera-view-input sample, informed the reconstructed camera-control layout and cleaner camera update flow used here.
- [sc4-dll-utilities](https://github.com/0xC0000054/sc4-dll-utilities) and [SC4Fix](https://github.com/nsgomez/sc4fix) document the game-version detection fallback pattern used by this plugin.

## License

This project is licensed under the terms of the GNU General Public License version 3.0.
See [LICENSE](LICENSE) for more information.

Bundled [gzcom-dll](https://github.com/nsgomez/gzcom-dll) code is licensed under the GNU Lesser General Public License version 2.1 or later.
