#!/usr/bin/env python3
"""Export, validate, and build SC4-ModernCamera UI theme packages."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
from pathlib import Path

import build_sc4_ui_dat as base


CONTRACT_VERSION = "2.0"
CONTRACT_MAJOR = 2
SCRIPT_RESOURCES = {
    "greeting.txt": (base.GREETING_INSTANCE_ID, 0x3D0C0706),
    "controls.txt": (base.CONTROLS_INSTANCE_ID, 0x3D0C0708),
    "menu-button.txt": (base.MENU_BUTTON_INSTANCE_ID, 0x3D0C0902),
    "settings.txt": (base.SETTINGS_INSTANCE_ID, 0x3D0C0904),
}
IMAGE_RESOURCES = {
    base.MENU_ICON_ON_SOURCE_NAME: (
        base.MENU_ICON_ON_INSTANCE_ID,
        lambda path: base.build_menu_icon_fsh_from_png(path, b"MODCMION"),
    ),
    base.MENU_ICON_OFF_SOURCE_NAME: (
        base.MENU_ICON_OFF_INSTANCE_ID,
        lambda path: base.build_menu_icon_fsh_from_png(path, b"MODCMIOF"),
    ),
    "menu-button-stages.png": (
        base.CHOICE_BUTTON_IMAGE_INSTANCE_ID,
        lambda path: base.build_fsh_dxt3_from_png(path, b"MODCMBTN"),
    ),
    "camicon.png": (
        base.CAM_ICON_INSTANCE_ID,
        lambda path: base.build_fsh_dxt3_from_png(path, b"MODCMIC2"),
    ),
    "camera_point.png": (
        base.CAMERA_POINT_INSTANCE_ID,
        lambda path: base.build_fsh_dxt3_from_png(path, b"MODCMARW"),
    ),
}
GENERATED_IMAGE_RESOURCES = {
    base.MENU_MODE_TOGGLE_HIT_TARGET_INSTANCE_ID: base.build_menu_mode_toggle_hit_target_fsh,
}
REQUIRED_CONTROLS = {
    "greeting.txt": {
        0x3D0C0840: "GZWinBtn",
        0x3D0C0843: "GZWinBtn",
        0x3D0C0844: "GZWinBtn",
        0x3D0C0847: "GZWinBtn",
    },
    "controls.txt": {
        0x3D0C0850: "GZWinBtn",
        0x3D0C0853: "GZWinBtn",
    },
    "menu-button.txt": {
        0x3D0C0910: "GZWinBtn",
        0x3D0C0914: "GZWinBtn",
    },
    "settings.txt": {
        **{control_id: "GZWinBtn" for control_id in (
            0x3D0C0920, 0x3D0C0930, 0x3D0C0931, 0x3D0C0932, 0x3D0C0933,
            0x3D0C0936, 0x3D0C0937, 0x3D0C0938, 0x3D0C093A, 0x3D0C093B,
            0x3D0C093C, 0x3D0C093D, 0x3D0C093E, 0x3D0C093F,
            0x3D0C0950, 0x3D0C0951, 0x3D0C0952, 0x3D0C0953, 0x3D0C0955, 0x3D0C095A,
            0x3D0C0940, 0x3D0C0941, 0x3D0C0942, 0x3D0C0943, 0x3D0C094C, 0x3D0C094D,
            0x3D0C0968, 0x3D0C0969, 0x3D0C096A, 0x3D0C096E,
        )},
        0x3D0C0934: "GZWinSlider",
        0x3D0C0935: "GZWinSlider",
        0x3D0C0939: "GZWinSlider",
        0x3D0C0946: "GZWinSlider",
        0x3D0C0948: "GZWinSlider",
    },
}


def parse_group(value: object) -> int:
    if isinstance(value, int):
        group = value
    elif isinstance(value, str):
        group = int(value, 0)
    else:
        raise ValueError("resourceGroup must be an integer or hexadecimal string")
    if not 0 <= group <= 0xFFFFFFFF:
        raise ValueError("resourceGroup must fit in an unsigned 32-bit value")
    if group == base.PLUGIN_GROUP_ID:
        raise ValueError("resourceGroup must not reuse the built-in UI resource group")
    return group


def read_manifest(theme_dir: Path) -> tuple[dict[str, object], int]:
    path = theme_dir / "theme.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    for field in ("name", "contractVersion", "resourceGroup"):
        if field not in manifest:
            raise ValueError(f"theme.json is missing {field}")
    if not isinstance(manifest["name"], str) or not manifest["name"].strip():
        raise ValueError("theme.json name must be a non-empty string")
    version = str(manifest["contractVersion"])
    match = re.fullmatch(r"(\d+)\.(\d+)", version)
    if not match or int(match.group(1)) != CONTRACT_MAJOR:
        raise ValueError(
            f"theme contract {version!r} is incompatible with supported contract {CONTRACT_VERSION}"
        )
    return manifest, parse_group(manifest["resourceGroup"])


def parse_controls(script: str) -> tuple[dict[int, str], list[int]]:
    controls: dict[int, str] = {}
    duplicates: list[int] = []
    for match in re.finditer(
        r"<LEGACY\s+clsid=([A-Za-z0-9_]+)[^\r\n>]*\sid=0x([0-9A-Fa-f]{8})\b",
        script,
    ):
        control_id = int(match.group(2), 16)
        if control_id in controls:
            duplicates.append(control_id)
        controls[control_id] = match.group(1)
    return controls, duplicates


def validate(theme_dir: Path) -> tuple[dict[str, object], int]:
    manifest, group = read_manifest(theme_dir)
    available_images = {
        instance_id for instance_id, _ in IMAGE_RESOURCES.values()
    } | set(GENERATED_IMAGE_RESOURCES)
    errors: list[str] = []
    for filename, (_, root_id) in SCRIPT_RESOURCES.items():
        path = theme_dir / "ui" / filename
        if not path.is_file():
            errors.append(f"missing UI script: ui/{filename}")
            continue
        script = path.read_text(encoding="utf-8")
        controls, duplicates = parse_controls(script)
        if controls.get(root_id) != "GZWinGen":
            errors.append(
                f"ui/{filename}: root 0x{root_id:08X} must be a GZWinGen"
            )
        for control_id in duplicates:
            errors.append(f"ui/{filename}: duplicate ID 0x{control_id:08X}")
        for control_id, expected_type in REQUIRED_CONTROLS[filename].items():
            actual_type = controls.get(control_id)
            if actual_type != expected_type:
                errors.append(
                    f"ui/{filename}: 0x{control_id:08X} must be {expected_type}, "
                    f"found {actual_type or 'missing'}"
                )
        for image_group, image_instance in re.findall(
            r"image=\{([0-9A-Fa-f]{8}),([0-9A-Fa-f]{8})\}",
            script,
        ):
            parsed_group = int(image_group, 16)
            parsed_instance = int(image_instance, 16)
            if parsed_group in (base.PLUGIN_GROUP_ID, group) and parsed_instance not in available_images:
                errors.append(
                    f"ui/{filename}: referenced theme image 0x{parsed_instance:08X} has no source file"
                )
    for filename in IMAGE_RESOURCES:
        if not (theme_dir / "images" / filename).is_file():
            errors.append(f"missing source image: images/{filename}")
    if errors:
        raise ValueError("\n".join(errors))
    return manifest, group


def write_dbpf(resources: list[tuple[int, int, int, bytes]], destination: Path) -> None:
    payload = bytearray()
    index_entries = []
    for type_id, group_id, instance_id, resource_data in resources:
        offset = base.HEADER_SIZE + len(payload)
        payload.extend(resource_data)
        index_entries.append(
            struct.pack("<IIIII", type_id, group_id, instance_id, offset, len(resource_data))
        )
    index_offset = base.HEADER_SIZE + len(payload)
    header = bytearray(base.HEADER_SIZE)
    header[0:4] = b"DBPF"
    struct.pack_into("<II", header, 4, 1, 0)
    struct.pack_into("<II", header, 0x18, 0, 0)
    struct.pack_into(
        "<IIII",
        header,
        0x20,
        7,
        len(resources),
        index_offset,
        20 * len(resources),
    )
    struct.pack_into("<III", header, 0x30, 0, 0, 0)
    struct.pack_into("<I", header, 0x3C, 0)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + payload + b"".join(index_entries))


def build(theme_dir: Path, destination: Path) -> None:
    manifest, group = validate(theme_dir)
    resources: list[tuple[int, int, int, bytes]] = []
    group_text = f"{group:08x}"
    for filename, (instance_id, _) in SCRIPT_RESOURCES.items():
        script = (theme_dir / "ui" / filename).read_text(encoding="utf-8")
        script = script.replace("{THEME_GROUP}", group_text)
        script = re.sub(
            rf"(?i)\b{base.PLUGIN_GROUP_ID:08x}\b",
            group_text,
            script,
        )
        resources.append(
            (base.UI_SCRIPT_TYPE_ID, group, instance_id, script.encode("utf-8"))
        )
    for filename, (instance_id, encoder) in IMAGE_RESOURCES.items():
        resources.append(
            (
                base.UI_IMAGE_TYPE_ID,
                group,
                instance_id,
                encoder(theme_dir / "images" / filename),
            )
        )
    for instance_id, build_image in GENERATED_IMAGE_RESOURCES.items():
        resources.append(
            (
                base.UI_IMAGE_TYPE_ID,
                group,
                instance_id,
                build_image(),
            )
        )
    write_dbpf(resources, destination)
    print(
        f"Built theme {manifest['name']!r} ({manifest['contractVersion']}) "
        f"at {destination} with resource group 0x{group:08X}"
    )


def export_template(destination: Path, group: int) -> None:
    repo = Path(__file__).resolve().parents[1]
    ui_source = repo / "Dev" / "ui" / "SC4-ModernCamera-ControlLaboratory.txt"
    changelog = repo / "docs" / "changelog.md"
    version_header = repo / "Dev" / "src" / "PluginVersion.h"
    scripts = {
        "greeting.txt": base.build_greeting_script(changelog, version_header),
        "controls.txt": base.build_controls_script(),
        "menu-button.txt": base.build_menu_button_script(),
        "settings.txt": base.build_settings_script(version_header),
    }
    (destination / "ui").mkdir(parents=True, exist_ok=True)
    (destination / "images").mkdir(parents=True, exist_ok=True)
    legacy_menu_icon = destination / "images" / "moderncamera-menu-icon.png"
    if legacy_menu_icon.exists():
        legacy_menu_icon.unlink()
    for filename, data in scripts.items():
        text = data.decode("utf-8").replace(
            f"{base.PLUGIN_GROUP_ID:08x}",
            "{THEME_GROUP}",
        )
        (destination / "ui" / filename).write_text(text, encoding="utf-8")
    for filename in IMAGE_RESOURCES:
        shutil.copy2(ui_source.with_name(filename), destination / "images" / filename)
    manifest = {
        "name": "SC4-ModernCamera Reference Theme",
        "contractVersion": CONTRACT_VERSION,
        "resourceGroup": f"0x{group:08X}",
    }
    (destination / "theme.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Exported theme template to {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    export_parser = subparsers.add_parser("export-template")
    export_parser.add_argument("destination", type=Path)
    export_parser.add_argument("--group", type=lambda value: int(value, 0), default=0x3D0C1700)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("theme_dir", type=Path)
    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("theme_dir", type=Path)
    build_parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.command == "export-template":
        export_template(args.destination, args.group)
    elif args.command == "validate":
        manifest, group = validate(args.theme_dir)
        print(
            f"Theme {manifest['name']!r} is valid for contract "
            f"{manifest['contractVersion']} (0x{group:08X})"
        )
    else:
        build(args.theme_dir, args.destination)


if __name__ == "__main__":
    main()
