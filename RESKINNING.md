# Reskinning SC4-ModernCamera

A theme changes how the mod's four windows look:

- the Settings window, with its General and Advanced pages
- the Help window
- the changelog window
- the camera button in the top-right corner of the city view

You can change window sizes, layout, fonts, colors, button art and icons. You can't change what the controls do.

The mod's own look always stays installed. If part of your theme is broken, only the affected window falls back to the built-in look, and the rest of your theme keeps working.

## What you need

- **Python 3.10 or newer.** On Windows, `py -3` runs it.
- **The source code of the release you're theming.** Download *Source code (zip)* from that release's page, or clone the repository at the release's tag. The theme tool, `tools/build_sc4_ui_theme.py`, comes with it. The tool exports its starting files from this source, so the version needs to match the one your players run.

Version 2.0.0's release source doesn't include the theme tool. Until the next release, download the source from the repository page with *Code › Download ZIP* instead.

## Quick start

Run these commands from the top folder of the source code.

1. Export a copy of the built-in look to start from:

   ```
   py -3 tools/build_sc4_ui_theme.py export-template my-theme --group 0x5A3C91E4
   ```

   Replace `0x5A3C91E4` with a resource group of your own, which can be any 8-digit hex number. Don't use `0x3D0C0700`, because that's the mod's own group (the tool refuses it). Also avoid `0x3D0C1700`, the tool's default, so your theme can't collide with someone else's.

2. Edit `my-theme/theme.json`, the window scripts in `my-theme/ui/`, and the images in `my-theme/images/`.

3. Check your theme for mistakes:

   ```
   py -3 tools/build_sc4_ui_theme.py validate my-theme
   ```

4. Build your theme's DAT:

   ```
   py -3 tools/build_sc4_ui_theme.py build my-theme MyTheme.dat
   ```

5. Install the theme as described in [Installing a theme](#installing-a-theme).

The validator catches a missing file, a missing or retyped required control, a repeated control ID, and an image your scripts use that your theme doesn't provide. It can't tell you whether a window looks right, so test every window in the game.

## Inside a theme folder

```
my-theme/
  theme.json
  ui/
    settings.txt       Settings window, both pages
    controls.txt       Help window
    greeting.txt       Changelog window
    menu-button.txt    Camera button
  images/
    menu_glyphs_very_small.png       Camera button in Modern mode
    menu_glyphs_red_very_small.png   Camera button in Classic mode
    menu-button-stages.png           Option buttons and tabs
    camicon.png                      Title-bar icon
    camera_point.png                 Picture in the changelog window
```

`theme.json` tells the mod which theme to use:

```json
{
  "name": "My Theme",
  "contractVersion": "2.0",
  "resourceGroup": "0x5A3C91E4"
}
```

| Field | Meaning |
| --- | --- |
| `name` | Your theme's name. The exported file says "SC4-ModernCamera Reference Theme", so change it. |
| `contractVersion` | The theme format version. Leave it at `2.0`. |
| `resourceGroup` | Your resource group. It must match the group your DAT was built with. |

## Editing the window scripts

Each file in `ui/` is a SimCity 4 UI script. The first `<LEGACY ...>` line is the window itself. Each line between `<CHILDREN>` and `</CHILDREN>` is one control in that window. Here is the Help window's General tab, shortened:

```
<LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0859 area=(24,44,144,72) caption="General"
  image={{THEME_GROUP},3d0c0907} font=GenButton colorfontnormal=(63,73,103) style=toggle ... >
```

The attributes you'll change most often:

| Attribute | What it does |
| --- | --- |
| `area=(left,top,right,bottom)` | Position and size in pixels, measured from the window's top-left corner. On the window's own line, this sets the window's size. |
| `caption="..."` | The control's text. It can't contain double quotes or angle brackets. |
| `font=` | A SimCity 4 font style, such as `GenHeader`, `GenBodyMedium`, `GenBodyLight` or `GenButton`. |
| `forecolor=`, `fillcolor=`, `colorfontnormal=` and other `color...` keys | Colors, written as `(red,green,blue)`. |
| `image={group,instance}` | The art a control draws. `{THEME_GROUP}` is replaced with your resource group when you build, so `{{THEME_GROUP},3d0c0907}` is your theme's own button art. Any other group is SimCity 4's own UI art. For example, `{46a006b0,6bb93cb5}` is the game's window frame. |
| `winflag_visible=` | Whether the control starts out shown. |

Rules that keep your theme working:

- **Keep the required controls.** Each one needs its ID and its control type (`clsid`) unchanged. See [Required controls](#required-controls). You can move, resize, recolor and relabel them freely.
- **Keep every control directly in its window.** Don't put a `<CHILDREN>` block inside a control. The mod only hears clicks and slider moves from controls that sit directly in the window.
- **Leave the title bar draggable.** The top 34 pixels of the Settings, Help and changelog windows are the area you drag them by. That's set by `gutters=(4,34)` on the window's line. Any control there other than the close button needs `winflag_mousetrans=yes winflag_ignoremouse=yes`, or it will block dragging.
- **Give new controls their own IDs.** For art on one settings page, use an unused ID from that page's range (see [Settings window](#settings-window)). For anything else, use an ID that doesn't start with `0x3D0C`, so a later version of the mod can't claim it.
- **Don't patch anything else.** A theme is a DAT of UI resources. It must not change the mod's DLL, the mod's own DAT, or SimCity 4's files.

## Settings window

The window shows one page at a time. The mod decides which control belongs to which page by its ID:

| Page | Control IDs | How it starts in the script |
| --- | --- | --- |
| General | `0x3D0C0922`-`0x3D0C093F` | `winflag_visible=yes` |
| Advanced | `0x3D0C0940`-`0x3D0C094F` and `0x3D0C0961`-`0x3D0C097F` | `winflag_visible=no` |
| Both pages (title bar, tabs, footer) | any other ID | `winflag_visible=yes` |

Also note:

- **Leave each slider's `minmaxvalue` as exported.** The mod sets each slider to its option's value times 100. That gives 10-300 for rotation, zoom and Q/E speed, 25-275 for panning speed, and 100-500 for Shift boost.
- **Make the Disabled button state clearly readable.** Modern-only options are disabled while the camera is in Classic mode.
- **Leave `0x3D0C0969` out.** It was the old Normal logging button. The mod hides it if a theme still has it.

## Help window

The Help window has three tabs, General, Advanced and Controls, and a scrolling text pane for each one. The mod shows one pane at a time: `0x3D0C085C` for General, `0x3D0C085D` for Advanced and `0x3D0C085E` for Controls. The help text is part of your `controls.txt`, so you can restyle it or rewrite it. To keep a pane scrollable, keep it a `GZWinTextEdit` with `editable=no` and `vscrollbar=yes`.

## Changelog window

The mod fills some of this window in while the game runs:

| ID | Filled in with |
| --- | --- |
| `0x3D0C0842` | The latest release notes. Keep it a scrolling `GZWinTextEdit`. |
| `0x3D0C084F` | The "Latest :" version line. |
| `0x3D0C0854` | The "Update available!" label, shown only when a newer release exists. |

## Camera button

The mod places the button in the top-right corner of the city view, so a theme can't move it. It has two faces. The mod shows `0x3D0C0910` in Modern mode and `0x3D0C0914` in Classic mode.

Clicking the small light switches camera modes. The light's click area is the invisible button `0x3D0C0911`, which by default covers `(40,46,60,72)`. If your art moves the light, move that button's `area` to match. If your theme leaves `0x3D0C0911` out, the mod uses the default corner instead.

## Required controls

If a window is missing one of these controls, or one has the wrong type, that window uses the built-in look. Every control in this table is a `GZWinBtn` except the five sliders.

| Script | Control IDs |
| --- | --- |
| `settings.txt` | Title bar and footer: `0x3D0C0920` close (X), `0x3D0C0955` General tab, `0x3D0C0951` Advanced tab, `0x3D0C095A` View Help, `0x3D0C0950` Defaults, `0x3D0C0952` Show Changelog, `0x3D0C0953` Close |
| | General page: `0x3D0C0930` Modern, `0x3D0C0931` Classic, `0x3D0C0938` Reset Camera Location, `0x3D0C0932`/`0x3D0C0933` WASD Movement On/Off, `0x3D0C093A`/`0x3D0C093B` Q/E Rotation On/Off, `0x3D0C0936`/`0x3D0C0937` Invert vertical rotation On/Off, `0x3D0C093C`/`0x3D0C093D` Restore View/Keep View, `0x3D0C093E`/`0x3D0C093F` Bookmark Hotkeys On/Off |
| | General page sliders (`GZWinSlider`): `0x3D0C0934` Rotation sensitivity, `0x3D0C0935` Zoom sensitivity, `0x3D0C0939` Panning speed |
| | Advanced page: `0x3D0C0940`/`0x3D0C0941` Invert Q/E On/Off, `0x3D0C0942`/`0x3D0C0943` Smooth/Stepped wheel zoom, `0x3D0C094C`/`0x3D0C094D` Invert wheel zoom On/Off, `0x3D0C096A`/`0x3D0C0968` Write Logs Enabled/Disabled, `0x3D0C096E` update notice checkbox |
| | Advanced page sliders (`GZWinSlider`): `0x3D0C0946` Q/E rotation speed, `0x3D0C0948` Shift pan boost |
| `controls.txt` | `0x3D0C0850` close (X), `0x3D0C0853` Close, `0x3D0C0859` General tab, `0x3D0C085A` Advanced tab, `0x3D0C085B` Controls tab |
| `greeting.txt` | `0x3D0C0840` close (X), `0x3D0C0843` Dismiss, `0x3D0C0844` Github, `0x3D0C0855` Simtropolis |
| `menu-button.txt` | `0x3D0C0910` Modern face, `0x3D0C0914` Classic face |

## Images

| File | Used for | Size and layout |
| --- | --- | --- |
| `menu_glyphs_very_small.png` | Camera button in Modern mode | Exactly 220×58: four 55×58 states side by side, in the order Disabled, Normal, Clicked, Hovered. Each state is scaled to the button's 60×72 size, and each one is lined up with the Normal state by the center of its art. |
| `menu_glyphs_red_very_small.png` | Camera button in Classic mode | The same as above. |
| `menu-button-stages.png` | Option buttons and tabs | Four equal states side by side, in the order Disabled, Normal, Selected, Hovered. The default is 480×32. Players tell which option is on by the Selected state, so make it stand out. |
| `camicon.png` | Title-bar icon in the Settings, Help and changelog windows | 32×32. If you change its size, change the picture control's `area` and `imagerect` to match. |
| `camera_point.png` | Picture in the changelog window | 102×68 by default. The same `area` and `imagerect` rule applies. |

Your PNGs must be 8 bits per channel and not interlaced. RGBA, RGB, grayscale and paletted PNGs all work. The tool pads an image whose width or height isn't a multiple of 4 with transparent pixels, except the two camera button strips, which must be exactly 220×58.

Images are stored in SimCity 4's DXT3 format, which keeps only 16 levels of transparency, so soft edges and shadows may look slightly banded in the game.

A theme can only replace these five images. Any other art you use has to be an image already in SimCity 4, referenced by its group and instance.

## Text the mod doesn't update

The version number on the Settings window (`0x3D0C0957`) and the "Installed :" line in the changelog window (`0x3D0C0846`) are written into your scripts when you export. The Help window's text is too. After a mod update, export a fresh template, as described in [When the mod updates](#when-the-mod-updates), so these stay current.

## Installing a theme

1. Put your DAT, for example `MyTheme.dat`, in the SimCity 4 `Plugins` folder. Keeping it next to `SC4-ModernCamera.dat` is recommended.
2. Copy your `theme.json` into the `SC4-ModernCamera` folder that sits next to the mod's DLL. That's usually `Documents\SimCity 4\Plugins\SC4-ModernCamera\theme.json`.
3. Start the game and load a city.

The mod reads `theme.json` each time a city loads. SimCity 4 only loads DATs when the game starts, so restart the game after you rebuild your DAT.

To go back to the built-in look, delete `theme.json`. Only one theme can be active at a time.

## Troubleshooting

If a window keeps its built-in look, the log says why:

1. In Settings, open the Advanced page and set **Write Logs** to Enabled. You can also type `ModernCameraLog` in the cheat box (Ctrl+X), which switches logging on or off.
2. Load a city and open the window that isn't themed.
3. Open `Plugins\SC4-ModernCamera\SC4-ModernCamera.log` and look for lines that start with `UI Theme:`.

| Log line | Meaning |
| --- | --- |
| `activated '<name>' contract 2.0 resource group ...` | `theme.json` was read and the theme is on. |
| `theme.json is missing name, contractVersion, or resourceGroup` | A field is missing or misspelled. |
| `incompatible contract version for '<name>'` | `contractVersion` isn't a `2.x` version. |
| `invalid contractVersion or resourceGroup for '<name>'` | The version or the group isn't a valid number. |
| `'<name>' failed for <window>: required button 0x... is missing or has the wrong type` | That window's script lacks a required control. The same message names a slider when a slider is the problem. |
| `'<name>' failed for <window>.` (no reason given) | The game couldn't build the window from your DAT. Usually the DAT isn't installed, its group doesn't match `resourceGroup`, or the script has a syntax error. |

Set Write Logs back to Disabled when you're done.

## When the mod updates

Within version 2 of the theme format, the controls your theme already has keep their IDs. A new version of the mod can still add required controls, though. If it does, the affected window uses the built-in look until you update your theme.

Each time the theme tool runs, it asks GitHub for the latest release. If that release is newer than your source code, the tool warns you, then finishes the command anyway. Without an internet connection, it says it couldn't check.

After each mod release:

1. Get that release's source code, which includes the theme tool.
2. Export a fresh template.
3. Compare the new `ui/*.txt` files with yours and carry your changes over.

If the theme format's major version ever changes, older themes are ignored and the built-in look is used.
