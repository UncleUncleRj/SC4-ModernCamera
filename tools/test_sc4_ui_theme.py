#!/usr/bin/env python3
"""Self-contained validation checks for the SC4-ModernCamera theme contract."""

from __future__ import annotations

import json
import shutil
import tempfile
from pathlib import Path

import build_sc4_ui_theme as themes


def expect_invalid(theme_dir: Path, description: str) -> None:
    try:
        themes.validate(theme_dir)
    except ValueError:
        return
    raise AssertionError(f"validator accepted {description}")


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sc4-moderncamera-theme-") as temp:
        root = Path(temp)
        reference = root / "reference"
        themes.export_template(reference, 0x3D0C1700)
        themes.validate(reference)

        missing_control = root / "missing-control"
        shutil.copytree(reference, missing_control)
        settings_path = missing_control / "ui" / "settings.txt"
        settings = settings_path.read_text(encoding="utf-8")
        settings_path.write_text(
            "\n".join(
                line for line in settings.splitlines()
                if "id=0x3D0C093A" not in line
            ),
            encoding="utf-8",
        )
        expect_invalid(missing_control, "a missing required control")

        wrong_type = root / "wrong-type"
        shutil.copytree(reference, wrong_type)
        settings_path = wrong_type / "ui" / "settings.txt"
        settings_path.write_text(
            settings_path.read_text(encoding="utf-8").replace(
                "clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C093A",
                "clsid=GZWinText iid=IGZWinText id=0x3D0C093A",
                1,
            ),
            encoding="utf-8",
        )
        expect_invalid(wrong_type, "a required control with the wrong type")

        wrong_contract = root / "wrong-contract"
        shutil.copytree(reference, wrong_contract)
        manifest_path = wrong_contract / "theme.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for version in ("1.0", "3.0"):
            manifest["contractVersion"] = version
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            expect_invalid(wrong_contract, f"unsupported contract version {version}")

        missing_image = root / "missing-image"
        shutil.copytree(reference, missing_image)
        (missing_image / "images" / "camicon.png").unlink()
        expect_invalid(missing_image, "an incomplete image set")

        duplicate_id = root / "duplicate-id"
        shutil.copytree(reference, duplicate_id)
        settings_path = duplicate_id / "ui" / "settings.txt"
        settings = settings_path.read_text(encoding="utf-8")
        duplicate_line = next(
            line for line in settings.splitlines()
            if "id=0x3D0C093A" in line
        )
        settings_path.write_text(
            settings.replace("</CHILDREN>", duplicate_line + "\n</CHILDREN>"),
            encoding="utf-8",
        )
        expect_invalid(duplicate_id, "a duplicate control ID")

    print("Theme validator negative cases passed.")


if __name__ == "__main__":
    main()
