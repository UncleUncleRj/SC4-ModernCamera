# UI Theme Contract

SC4-ModernCamera supports one optional UI theme. The plugin's own resources
always remain installed as the fallback.

## Installation

A theme installation contains:

- a standalone DAT with the themed UI scripts and images;
- `Plugins\SC4-ModernCamera\theme.json`.

The manifest uses this shape:

```json
{
  "name": "Example Theme",
  "contractVersion": "1.0",
  "resourceGroup": "0x3D0C1700"
}
```

The themed DAT must load before the plugin tries to open its first window.
Keeping the theme DAT next to the plugin DAT is recommended. The manifest
selects the theme's resource group, so the DAT filename does not select or
identify a theme.

Removing `theme.json` selects the built-in UI on the next game start.

## Compatibility

Contract `1.x` freezes the production window instance IDs, root window IDs,
interactive control IDs, and required control types. Theme authors can change
window dimensions, layout, fonts, colors, backgrounds, buttons, icons, and
sprites without changing those identifiers.

Required controls are listed in `tools/build_sc4_ui_theme.py`. If a themed
window cannot be instantiated, omits a required control, or uses the wrong
interactive control type, the plugin destroys that window and opens the
built-in version. A failure in one window does not disable the theme for the
other windows.

The current `1.x` implementation treats every control listed by the validator
as required. A theme that omits one falls back to the built-in version of that
window. When extending the contract, update both the validator and the runtime
control checks before describing a new control as optional. Themes with an
incompatible major version are ignored.

## SDK workflow

Export the current production scripts and source images:

```powershell
python tools/build_sc4_ui_theme.py export-template theme-sdk\my-theme --group 0x3D0C1700
```

Edit the files under `ui` and `images`, then validate and build:

```powershell
python tools/build_sc4_ui_theme.py validate theme-sdk\my-theme
python tools/build_sc4_ui_theme.py build theme-sdk\my-theme MyTheme.dat
```

The validator checks manifest fields, contract compatibility, root resources,
required IDs, control classes, duplicate IDs, and referenced theme images.
The resulting DAT contains UI resources only; it does not bundle or overwrite
the plugin DLL or the base UI DAT.

Theme packages must not patch the SC4-ModernCamera DLL or vanilla game files.
