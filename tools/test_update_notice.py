"""Regression checks for the update notice's advice script and its text resources."""

from __future__ import annotations

from pathlib import Path
import re
import struct

import build_sc4_ui_dat as dat


def main() -> None:
    script_path = Path(__file__).parents[1] / "Dev" / "ui" / dat.UPDATE_NOTICE_SCRIPT_NAME
    script = script_path.read_text(encoding="utf-8")

    keys = {int(key, 16) for key in re.findall(r'"text@([0-9a-f]{8})"', script)}
    expected = {dat.UPDATE_NOTICE_TITLE_INSTANCE_ID, dat.UPDATE_NOTICE_MESSAGE_INSTANCE_ID}
    if keys != expected:
        raise AssertionError(f"script keys {keys} do not match the built resources {expected}")

    # The ticker's cutoff is 59 and the lowest popup threshold is 60.
    if not re.search(r"^a\.priority = 59\s*$", script, re.MULTILINE):
        raise AssertionError("the notice must stay at priority 59 to reach the ticker without a popup")

    resource = dat.build_ltext(dat.UPDATE_NOTICE_TITLE)
    count, marker = struct.unpack_from("<HH", resource)
    if marker != 0x1000 or count * 2 + 4 != len(resource):
        raise AssertionError("LTEXT header does not match the game's layout")
    if resource[4:].decode("utf-16-le") != dat.UPDATE_NOTICE_TITLE:
        raise AssertionError("LTEXT text did not round-trip")

    print("Update notice checks passed.")


if __name__ == "__main__":
    main()
