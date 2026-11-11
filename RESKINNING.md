# Reskinning SC4-ModernCamera

A theme changes the look of the mod's four windows:

- the Settings window, with its General and Advanced pages
- the Help window
- the changelog window
- the camera button in the top-right corner of the city view

You can change window sizes, layout, fonts, colors, button art and icons. You can't change what the controls do.

Your theme is installed alongside the mod's built-in look, not over it. If one of your windows has a problem, only that window falls back to the built-in look, and the rest of your theme keeps working.

## What you need

- **Python 3.10 or newer.** On Windows, the command `py -3` runs it. If `py` isn't found, use `python` instead.
- **The latest theme kit.** Download `theme-kit_<version>.zip`, for example `theme-kit_2_0_0.zip`, from the [latest release](https://github.com/UncleUncleRj/SC4-ModernCamera/releases/latest). It holds the theme tool and a copy of the mod's built-in look, which you'll use as your starting point.

## Making a theme

1. Create a new folder for your theme and extract the theme kit into it.

2. Open a terminal in that folder, the one that holds this file. On Windows 11, right-click inside the folder and choose **Open in Terminal**. On Windows 10, hold Shift, right-click, and choose **Open PowerShell window here**.

   Then make a copy of the built-in look to start from:

   ```
   py -3 tools/build_sc4_ui_theme.py export-template my-theme-name --group 0x5A3C91E4
   ```

   Replace `my-theme-name` with your theme's name, here and in the commands below. Use a name without spaces. Your finished DAT gets the same name.

   Replace `0x5A3C91E4` with a number you make up: `0x` followed by any 8 digits or letters from A to F. This is your theme's resource group, a label that keeps your theme's files from clashing with the mod's or another theme's. Don't use `0x3D0C0700`, which belongs to the mod, or `0x3D0C1700`, the tool's default that other people may also use.

   Run this only once per theme. Running it again on the same folder overwrites your changes.

3. Make your changes in the `my-theme-name` folder:
   - the images in `images/` (see [Changing the art](#changing-the-art))
   - the window layouts in `ui/` (see [Editing the window scripts](#editing-the-window-scripts))
   - your theme's name in `theme.json`

   If you only want new art, you only need to replace the images. Always edit the files in your theme folder, not the ones in the kit's `Dev` folder. The tool only copies from `Dev` when you export, so changes there won't reach your theme.

4. Check your theme for mistakes:

   ```
   py -3 tools/build_sc4_ui_theme.py validate my-theme-name
   ```

   This catches missing files, missing or changed required controls, the same ID used twice, and images your windows use that your theme doesn't include. It can't tell whether a window looks right, so open every window in the game to check.

5. Build your theme:

   ```
   py -3 tools/build_sc4_ui_theme.py build my-theme-name
   ```

   This puts `my-theme-name.dat` in your theme folder, next to its `theme.json`. Those two files are your finished theme, ready to install and share. The tool won't build a theme named `SC4-ModernCamera`, because its DAT would replace the mod's own.

6. [Install your theme](#installing-a-theme) and try it in the game, then [share it](#sharing-a-theme).

## Inside your theme folder

```
my-theme-name/
  theme.json           Tells the mod to use your theme
  my-theme-name.dat    Your finished theme, once you build it
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
  "name": "My Cool Ash theme!",
  "contractVersion": "2.0",
  "resourceGroup": "0x5A3C91E4"
}
```

| Field | What it's for |
| --- | --- |
| `name` | Your theme's name. The exported file says "SC4-ModernCamera Reference Theme", so change it. |
| `contractVersion` | The version of the theme format. Leave it at `2.0`, even when the mod's own version number is different. |
| `resourceGroup` | Your resource group from step 2. Your DAT is built with this group, so if you change it, build again. |

## Changing the art

| File | Where it shows | Size and layout |
| --- | --- | --- |
| `menu_glyphs_very_small.png` | Camera button in Modern mode | Exactly 220×58 pixels: four 55×58 pictures side by side, for Disabled, Normal, Clicked and Hovered. Each picture is scaled to the button's 60×72 size and lined up with the Normal picture by the center of its art. |
| `menu_glyphs_red_very_small.png` | Camera button in Classic mode | The same as above. |
| `menu-button-stages.png` | Option buttons and tabs | Four equal pictures side by side, for Disabled, Normal, Selected and Hovered. 480×32 by default. Players see which option is on from the Selected picture, so make it stand out. |
| `camicon.png` | Icon in the title bar of the Settings, Help and changelog windows | 32×32. If you change its size, change the icon control's `area` and `imagerect` to match. |
| `camera_point.png` | Picture in the changelog window | 102×68 by default. If you change its size, change its `area` and `imagerect` too. |

Save your images as ordinary PNGs: 8 bits per channel and not interlaced, which is what most image editors do by default. Color, grayscale and paletted PNGs all work, with or without transparency. If an image's width or height isn't a multiple of 4, the tool pads it with transparent pixels. The two camera button images are the exception: they must be exactly 220×58.

The game keeps only 16 levels of transparency (it stores images in DXT3 format), so soft edges, glows and shadows can look slightly banded.

A theme can only replace these five images. For any other picture, use art that's already in SimCity 4, referenced by its group and instance.

The kit's `Dev/ui` folder also holds `menu_glyphs.af`, the editable Affinity file the camera button art was made from. The tool doesn't use it; it's there for reference. If you work from it, export your camera button images at exactly 220×58 into your theme's `images` folder.

## Editing the window scripts

Each file in `ui/` describes one window in SimCity 4's UI script format. The first `<LEGACY ...>` line is the window itself. Each line between `<CHILDREN>` and `</CHILDREN>` is one control in that window, such as a button, slider, label or picture. Here's the Help window's General tab, shortened:

```
<LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0859 area=(24,44,144,72) caption="General"
  image={{THEME_GROUP},3d0c0907} font=GenButton colorfontnormal=(63,73,103) style=toggle ... >
```

The settings you'll change most often:

| Setting | What it does |
| --- | --- |
| `area=(left,top,right,bottom)` | Where the control sits and how big it is, in pixels from the window's top-left corner. On the window's own line, it sets the window's size. |
| `caption="..."` | The control's text. Don't use double quotes or angle brackets in it. |
| `font=` | A SimCity 4 text style, such as `GenHeader`, `GenBodyMedium`, `GenBodyLight` or `GenButton`. |
| `forecolor=`, `fillcolor=`, `colorfontnormal=` and the other `color...` settings | Colors, written as `(red,green,blue)`. |
| `image={group,instance}` | The picture the control shows. `{THEME_GROUP}` stands for your resource group, so `{{THEME_GROUP},3d0c0907}` is your theme's own button art. Any other group points to art built into SimCity 4. For example, `{46a006b0,6bb93cb5}` is the game's window frame. |
| `winflag_visible=` | Whether the control is showing when the window opens. |

To keep your theme working:

- **Keep the required controls.** The mod needs certain buttons and sliders. Keep each one's `id` and control type (its `clsid`) exactly as they are. You can move, resize, recolor and relabel them as much as you like. They're listed under [Required controls](#required-controls).
- **Don't put controls inside other controls.** Every control has to sit directly in its window, so don't add a `<CHILDREN>` block inside a control. The mod only notices clicks and slider moves on controls placed directly in the window.
- **Keep the title bar draggable.** Players drag the Settings, Help and changelog windows by their top 34 pixels, which `gutters=(4,34)` on the window's line sets. Anything you put there, apart from the close button, needs `winflag_mousetrans=yes winflag_ignoremouse=yes`, or it will stop the window from being dragged.
- **Give each new control its own ID.** For art on one Settings page, use an unused ID from that page's range (see [Settings window](#settings-window)). For anything else, use an ID that doesn't start with `0x3D0C`, because a later version of the mod may use those.
- **Only add your own DAT.** A theme must never change the mod's DLL, the mod's own DAT or SimCity 4's files.

## Settings window

The Settings window shows one page at a time. The mod works out which page a control belongs to from its ID:

| Page | Control IDs | Showing when the window opens? |
| --- | --- | --- |
| General | `0x3D0C0922`-`0x3D0C093F` | Yes: `winflag_visible=yes` |
| Advanced | `0x3D0C0940`-`0x3D0C094F` and `0x3D0C0961`-`0x3D0C097F` | No: `winflag_visible=no` |
| Both pages (title bar, tabs, footer) | any other ID | Yes: `winflag_visible=yes` |

Also:

- **Leave each slider's `minmaxvalue` as exported.** The mod sets each slider to its option's value times 100, which gives 10-300 for rotation, zoom and Q/E speed, 25-275 for panning speed, and 100-500 for Shift boost.
- **Make the Disabled look easy to read.** Modern-only options are disabled while the camera is in Classic mode.
- **Leave out `0x3D0C0969`.** It was the old Normal logging button, and the mod hides it if a theme still has it.

## Help window

The Help window has three tabs, General, Advanced and Controls, each with its own scrolling text area: `0x3D0C085C`, `0x3D0C085D` and `0x3D0C085E`. The mod shows one at a time. The help text is part of your `controls.txt`, so you can restyle it or rewrite it. To keep a text area scrollable, leave it a `GZWinTextEdit` with `editable=no` and `vscrollbar=yes`.

## Changelog window

The mod fills in parts of this window while the game runs:

| ID | Filled in with |
| --- | --- |
| `0x3D0C0842` | The latest release notes. Keep it a scrolling `GZWinTextEdit`. |
| `0x3D0C084F` | The "Latest :" version line. |
| `0x3D0C0854` | The "Update available!" label, shown only when a newer release is out. |

## Camera button

The mod always places the camera button in the top-right corner of the city view, so a theme can't move it. The button has two looks: the mod shows `0x3D0C0910` in Modern mode and `0x3D0C0914` in Classic mode.

Clicking the small light on the button switches camera modes. The clickable spot is an invisible button, `0x3D0C0911`, which covers `(40,46,60,72)` by default. If your art moves the light, move that button's `area` to match. If your theme leaves it out, the mod uses the default spot.

## Required controls

If a window is missing any of these, or one has the wrong type, that window falls back to the built-in look. They're all buttons (`GZWinBtn`) except the five sliders (`GZWinSlider`).

| Script | Control IDs |
| --- | --- |
| `settings.txt` | Title bar and footer: `0x3D0C0920` close (X), `0x3D0C0955` General tab, `0x3D0C0951` Advanced tab, `0x3D0C095A` View Help, `0x3D0C0950` Defaults, `0x3D0C0952` Show Changelog, `0x3D0C0953` Close |
| | General page: `0x3D0C0930` Modern, `0x3D0C0931` Classic, `0x3D0C0938` Reset Camera Location, `0x3D0C0932`/`0x3D0C0933` WASD Movement On/Off, `0x3D0C093A`/`0x3D0C093B` Q/E Rotation On/Off, `0x3D0C0936`/`0x3D0C0937` Invert vertical rotation On/Off, `0x3D0C093C`/`0x3D0C093D` Restore View/Keep View, `0x3D0C093E`/`0x3D0C093F` Bookmark Hotkeys On/Off |
| | General page sliders: `0x3D0C0934` Rotation sensitivity, `0x3D0C0935` Zoom sensitivity, `0x3D0C0939` Panning speed |
| | Advanced page: `0x3D0C0940`/`0x3D0C0941` Invert Q/E On/Off, `0x3D0C0942`/`0x3D0C0943` Smooth/Stepped wheel zoom, `0x3D0C094C`/`0x3D0C094D` Invert wheel zoom On/Off, `0x3D0C096A`/`0x3D0C0968` Write Logs Enabled/Disabled, `0x3D0C096E` update notice checkbox |
| | Advanced page sliders: `0x3D0C0946` Q/E rotation speed, `0x3D0C0948` Shift pan boost |
| `controls.txt` | `0x3D0C0850` close (X), `0x3D0C0853` Close, `0x3D0C0859` General tab, `0x3D0C085A` Advanced tab, `0x3D0C085B` Controls tab |
| `greeting.txt` | `0x3D0C0840` close (X), `0x3D0C0843` Dismiss, `0x3D0C0844` Github, `0x3D0C0855` Simtropolis |
| `menu-button.txt` | `0x3D0C0910` Modern look, `0x3D0C0914` Classic look |

## Text that doesn't update itself

Some text is copied into your scripts when you export: the version number on the Settings window (`0x3D0C0957`), the "Installed :" line in the changelog window (`0x3D0C0846`) and all of the Help text. It stays the same when the mod updates, so export a fresh template after each update, as described in [When the mod updates](#when-the-mod-updates).

## Installing a theme

1. Put `my-theme-name.dat` from your theme folder into your SimCity 4 `Plugins` folder. Next to `SC4-ModernCamera.dat` is a good spot.
2. Copy the `theme.json` from your theme folder into the `SC4-ModernCamera` folder next to the mod's DLL. That's usually `Documents\SimCity 4\Plugins\SC4-ModernCamera\theme.json`.
3. Start the game and load a city.

The mod checks `theme.json` every time you load a city, but SimCity 4 only reads DATs when the game starts. After you rebuild your DAT, restart the game to see the changes.

To go back to the built-in look, delete `theme.json`. Only one theme can be on at a time.

## Sharing a theme

Share `my-theme-name.dat` and the `theme.json` from your theme folder together, along with the steps in [Installing a theme](#installing-a-theme). The DAT does nothing on its own, because `theme.json` is what tells the mod to use it. Don't include the mod's own DLL or DAT.

## Troubleshooting

If a window keeps the built-in look, the mod's log says why:

1. In Settings, open the Advanced page and set **Write Logs** to Enabled. You can also type `ModernCameraLog` in the cheat box (Ctrl+X), which turns logging on or off.
2. Load a city and open the window that isn't themed.
3. Open `Plugins\SC4-ModernCamera\SC4-ModernCamera.log` and look for lines that start with `UI Theme:`.

| Log line | What it means |
| --- | --- |
| `activated '<name>' contract 2.0 resource group ...` | Your `theme.json` was read and the theme is on. |
| `theme.json is missing name, contractVersion, or resourceGroup` | A field in `theme.json` is missing or misspelled. |
| `incompatible contract version for '<name>'` | `contractVersion` isn't a `2.x` version. |
| `invalid contractVersion or resourceGroup for '<name>'` | `contractVersion` or `resourceGroup` isn't a valid number. |
| `'<name>' failed for <window>: required button 0x... is missing or has the wrong type` | That window's script is missing a required control, or the control has the wrong type. When a slider is the problem, the message names the slider. |
| `'<name>' failed for <window>.` (no reason given) | The game couldn't build the window from your DAT. Usually the DAT isn't installed, its group doesn't match `resourceGroup` in `theme.json`, or the script has a typo. |

Set Write Logs back to Disabled when you're done.

## When the mod updates

In version 2 of the theme format, every control your theme already has keeps its ID. A mod update can still add new required controls, though. If it does, the affected window falls back to the built-in look until you update your theme.

Each time you run the theme tool, it checks GitHub for a newer release of the mod. If one is out, the tool warns you but still finishes what you asked. If it can't connect, it says it couldn't check.

After each mod release:

1. Download the new theme kit and extract it into a new folder.
2. Export a fresh template there, using your theme's name and resource group.
3. Copy your images into the new template, set the `name` in its `theme.json`, and redo your changes to the `ui/*.txt` files. Comparing your old scripts with the new ones shows what changed.
4. Check, build and share your theme again.

If the theme format's main version number ever changes, for example to 3.0, older themes are ignored and the built-in look is used.
