#!/usr/bin/env python3
"""Self-contained checks for the generated settings window layout."""

from __future__ import annotations

import re
from pathlib import Path

import build_sc4_ui_dat as base
import build_sc4_ui_theme as themes


REPO = Path(__file__).resolve().parents[1]
WINDOW_SOURCE = REPO / "Dev" / "src" / "SC4WindowManager.cpp"
OUTLINE_STYLE = "style=nofill"
CLOSE_X_ID = 0x3D0C0920


def parse_elements(script: str) -> dict[int, dict[str, object]]:
    """Return the area, class, visibility, and raw text of every child control."""
    elements: dict[int, dict[str, object]] = {}
    for line in script.splitlines():
        match = re.match(
            r"\s+<LEGACY clsid=(\w+) iid=\w+ id=0x([0-9A-F]{8}) area=\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)",
            line,
        )
        if match:
            control_id = int(match.group(2), 16)
            if control_id in elements:
                raise AssertionError(f"duplicate control 0x{control_id:08X}")
            elements[control_id] = {
                "clsid": match.group(1),
                "area": tuple(int(match.group(index)) for index in range(3, 7)),
                "visible": "winflag_visible=yes" in line,
                "line": line,
            }
    return elements


def in_ranges(control_id: int, ranges: tuple[tuple[int, int], ...]) -> bool:
    return any(first <= control_id <= last for first, last in ranges)


def overlaps(first: tuple[int, ...], second: tuple[int, ...]) -> bool:
    return first[0] < second[2] and second[0] < first[2] and first[1] < second[3] and second[1] < first[3]


def page_ranges_from_source(name: str) -> tuple[tuple[int, int], ...]:
    source = WINDOW_SOURCE.read_text(encoding="utf-8")
    block = re.search(rf"{name}\[\] = \{{(.*?)\n\t\}};", source, flags=re.S)
    if not block:
        raise AssertionError(f"{name} was not found in {WINDOW_SOURCE.name}")
    return tuple(
        (int(first, 16), int(last, 16))
        for first, last in re.findall(r"\{ 0x([0-9A-F]{8}), 0x([0-9A-F]{8}) \}", block.group(1))
    )


def main() -> None:
    script = base.build_settings_script(REPO / "Dev" / "src" / "PluginVersion.h").decode("utf-8")
    elements = parse_elements(script)
    width = base.SETTINGS_WINDOW_WIDTH
    height = base.SETTINGS_WINDOW_HEIGHT

    if page_ranges_from_source("kGeneralPageRanges") != base.GENERAL_PAGE_RANGES:
        raise AssertionError("General page ranges differ between the generator and the plugin")
    if page_ranges_from_source("kAdvancedPageRanges") != base.ADVANCED_PAGE_RANGES:
        raise AssertionError("Advanced page ranges differ between the generator and the plugin")

    pages: dict[str, dict[int, dict[str, object]]] = {"general": {}, "advanced": {}, "shared": {}}
    for control_id, element in elements.items():
        left, top, right, bottom = element["area"]
        if left < 0 or top < 0 or right > width or bottom > height or left >= right or top >= bottom:
            raise AssertionError(f"0x{control_id:08X} leaves the window: {element['area']}")
        if in_ranges(control_id, base.GENERAL_PAGE_RANGES):
            pages["general"][control_id] = element
            if not element["visible"]:
                raise AssertionError(f"General control 0x{control_id:08X} must start visible")
        elif in_ranges(control_id, base.ADVANCED_PAGE_RANGES):
            pages["advanced"][control_id] = element
            if element["visible"]:
                raise AssertionError(f"Advanced control 0x{control_id:08X} must start hidden")
        else:
            pages["shared"][control_id] = element

    for page in ("general", "advanced"):
        if not pages[page]:
            raise AssertionError(f"the {page} page has no controls")
        for control_id, element in pages[page].items():
            if element["area"][1] < base.SETTINGS_PAGE_TOP or element["area"][3] > base.SETTINGS_FOOTER_LINE_TOP:
                raise AssertionError(f"0x{control_id:08X} leaves the {page} page area: {element['area']}")
        # Section outlines deliberately surround their rows; everything else is one layer.
        solid = [
            (control_id, element)
            for control_id, element in {**pages[page], **pages["shared"]}.items()
            if OUTLINE_STYLE not in element["line"]
        ]
        for index, (first_id, first) in enumerate(solid):
            for second_id, second in solid[index + 1:]:
                if overlaps(first["area"], second["area"]):
                    raise AssertionError(
                        f"0x{first_id:08X} {first['area']} overlaps 0x{second_id:08X} {second['area']} on {page}"
                    )

    # The title bar only drags the window where the gutters reach and no control takes the mouse.
    if f"gutters=(4,{base.WINDOW_TITLE_BAR_HEIGHT})" not in script.splitlines()[1]:
        raise AssertionError("the settings window gutters do not cover the title bar")
    for control_id, element in elements.items():
        if element["area"][1] < base.WINDOW_TITLE_BAR_HEIGHT and control_id != CLOSE_X_ID:
            if "winflag_ignoremouse=yes" not in element["line"]:
                raise AssertionError(f"0x{control_id:08X} blocks dragging by the title bar")

    # Spellings the game does not read must not come back through a helper.
    for builder in (
        lambda: base.build_controls_script(),
        lambda: base.build_menu_button_script(),
        lambda: script.encode("utf-8"),
    ):
        generated = builder().decode("utf-8")
        if re.search(r"\b(winflag_enable|alphablend)=", generated):
            raise AssertionError("a generated script uses a key the game does not read")

    # The theme contract and the plugin must require the same settings controls.
    source = WINDOW_SOURCE.read_text(encoding="utf-8")
    constants = {
        name: int(value, 16)
        for name, value in re.findall(r"constexpr uint32_t (k\w+) = 0x([0-9A-F]{8});", source)
    }
    settings_case = re.search(
        r"case kSettingsWindowResourceInstance:\s*buttons = \{(.*?)\};\s*sliders = \{(.*?)\};",
        source,
        flags=re.S,
    )
    if not settings_case:
        raise AssertionError("the settings theme validation block was not found")
    runtime_required = {constants[name] for name in re.findall(r"k\w+", settings_case.group(1) + settings_case.group(2))}
    tool_required = set(themes.REQUIRED_CONTROLS["settings.txt"])
    if runtime_required != tool_required:
        raise AssertionError(
            "theme tool and plugin disagree on required settings controls: "
            + ", ".join(f"0x{value:08X}" for value in sorted(runtime_required ^ tool_required))
        )
    missing = tool_required - set(elements)
    if missing:
        raise AssertionError(
            "generated settings window omits required controls: "
            + ", ".join(f"0x{value:08X}" for value in sorted(missing))
        )

    print(
        f"Settings layout checks passed: {len(pages['general'])} General, "
        f"{len(pages['advanced'])} Advanced, {len(pages['shared'])} shared controls."
    )


if __name__ == "__main__":
    main()
