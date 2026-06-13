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
- **Mouse wheel**: WIP - A true smooth virtual dolly/zoom system is planned, but it is not complete in this alpha.


## Debug Logging

The plugin writes debug logs to the Plugins folder:

- `SC4-3DMouseCam.log`
- `SC4-3DMouseCam.last`

These files are for debugging only and can be safely ignored or deleted.

## Building

Open `SC4-3DMouseCam.slnx` in Visual Studio and build the `Dev` project for `Win32`.

The project is configured as a DLL plugin and includes the required GZCOM source files and headers under `Dev/src` and `Dev/vendor/gzcom`.

## Disclaimer

This alpha may cause visual glitches, camera oddities, game instability, or other unexpected behavior. It is unlikely to harm your PC, but save-game issues have not been ruled out. Back up important cities or regions before testing.

