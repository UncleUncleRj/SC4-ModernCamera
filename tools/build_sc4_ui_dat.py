"""Build the dependency-free SC4 DBPF file containing our UI resources."""

from dataclasses import dataclass
from pathlib import Path
import re
import struct
import sys
from typing import Optional
import zlib


UI_SCRIPT_TYPE_ID = 0x00000000
UI_IMAGE_TYPE_ID = 0x856DDBAC
PLUGIN_GROUP_ID = 0x3D0C0700
CONTROL_LAB_INSTANCE_ID = 0x3D0C0701
BASIC_INSTANCE_ID = 0x3D0C0703
GREETING_INSTANCE_ID = 0x3D0C0705
CONTROLS_INSTANCE_ID = 0x3D0C0707
MENU_ICON_ON_INSTANCE_ID = 0x3D0C0900
MENU_ICON_OFF_INSTANCE_ID = 0x3D0C090A
MENU_ICON_ON_SOURCE_NAME = "menu_glyphs_very_small.png"
MENU_ICON_OFF_SOURCE_NAME = "menu_glyphs_red_very_small.png"
MENU_MODE_TOGGLE_HIT_TARGET_INSTANCE_ID = 0x3D0C090B
# 0x3D0C090C was used by the layered lip prototype.
CHOICE_BUTTON_IMAGE_INSTANCE_ID = 0x3D0C0907
CAM_ICON_INSTANCE_ID = 0x3D0C0908
CAMERA_POINT_INSTANCE_ID = 0x3D0C0909
MENU_BUTTON_INSTANCE_ID = 0x3D0C0901
SETTINGS_INSTANCE_ID = 0x3D0C0903
# 0x3D0C0905 held the separate Advanced Settings window, now a page of the settings window.
HEADER_SIZE = 96


def get_build_inputs(source: Path) -> list[Path]:
    """Return every existing file whose content can change the built DAT."""
    changelog_source = source.parents[2] / "docs" / "changelog.md"
    version_header = source.parents[1] / "src" / "PluginVersion.h"
    candidates = [
        Path(__file__),
        source,
        source.with_name("SC4-ModernCamera-BasicUI.txt"),
        source.with_name(MENU_ICON_ON_SOURCE_NAME),
        source.with_name(MENU_ICON_OFF_SOURCE_NAME),
        source.with_name("menu-button-stages.png"),
        source.with_name("camicon.png"),
        source.with_name("camera_point.png"),
    ]
    if changelog_source.is_file():
        candidates.extend((changelog_source, version_header))
    return [path for path in candidates if path.is_file()]


def get_build_reason(source: Path, destination: Path) -> Optional[str]:
    """Return why the DAT must be rebuilt, or None when it is current."""
    if not destination.is_file():
        return "output file is missing"
    if not source.is_file():
        return f"source file is missing: {source}"

    destination_time = destination.stat().st_mtime_ns
    for input_path in get_build_inputs(source):
        if input_path.stat().st_mtime_ns > destination_time:
            return f"input is newer: {input_path}"
    return None


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb_from_565(value: int) -> tuple[int, int, int]:
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def encode_dxt3_color_block(pixels: list[tuple[int, int, int, int]]) -> bytes:
    visible_pixels = [(r, g, b, a) for r, g, b, a in pixels if a > 8]
    if not visible_pixels:
        opaque_pixels = [(0, 0, 0)]
    else:
        opaque_pixels = [(r, g, b) for r, g, b, _a in visible_pixels]

    endpoint_values = sorted({rgb_to_565(*color) for color in opaque_pixels})
    if len(endpoint_values) == 1:
        endpoint_values.append(endpoint_values[0])

    best_error = None
    best_color0 = endpoint_values[0]
    best_color1 = endpoint_values[-1]
    best_palette = []

    for first in endpoint_values:
        for second in endpoint_values:
            color0, color1 = (first, second) if first > second else (second, first)
            c0 = rgb_from_565(color0)
            c1 = rgb_from_565(color1)
            palette = [
                c0,
                c1,
                tuple((2 * c0[i] + c1[i]) // 3 for i in range(3)),
                tuple((c0[i] + 2 * c1[i]) // 3 for i in range(3)),
            ]
            error = 0.0
            for r, g, b, a in visible_pixels:
                distance = min(
                    (palette[index][0] - r) ** 2
                    + (palette[index][1] - g) ** 2
                    + (palette[index][2] - b) ** 2
                    for index in range(4)
                )
                error += distance * (a / 255.0)

            if best_error is None or error < best_error:
                best_error = error
                best_color0 = color0
                best_color1 = color1
                best_palette = palette

    color0 = best_color0
    color1 = best_color1
    palette = best_palette
    if not palette:
        c0 = rgb_from_565(color0)
        c1 = rgb_from_565(color1)
        palette = [
            c0,
            c1,
            tuple((2 * c0[i] + c1[i]) // 3 for i in range(3)),
            tuple((c0[i] + 2 * c1[i]) // 3 for i in range(3)),
        ]

    indices = 0
    for i, (r, g, b, _a) in enumerate(pixels):
        best_index = min(
            range(4),
            key=lambda index: (
                (palette[index][0] - r) ** 2
                + (palette[index][1] - g) ** 2
                + (palette[index][2] - b) ** 2
            ),
        )
        indices |= best_index << (2 * i)

    return struct.pack("<HHI", color0, color1, indices)


def encode_dxt3_block(pixels: list[tuple[int, int, int, int]]) -> bytes:
    alpha_bits = 0
    for i, (_r, _g, _b, a) in enumerate(pixels):
        alpha_bits |= max(0, min(15, round(a / 17))) << (4 * i)
    return alpha_bits.to_bytes(8, "little") + encode_dxt3_color_block(pixels)


def encode_dxt3_rgba(width: int, height: int, pixels: list[tuple[int, int, int, int]]) -> bytes:
    if width % 4 != 0 or height % 4 != 0:
        raise RuntimeError(f"DXT3 images must be multiples of 4 pixels, got {width}x{height}")

    output = bytearray()
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            block = []
            for row in range(4):
                for col in range(4):
                    block.append(pixels[(y + row) * width + x + col])
            output.extend(encode_dxt3_block(block))
    return bytes(output)


def read_png_rgba(png_path: Path) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    data = png_path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError(f"{png_path} is not a PNG file")

    offset = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    interlace = 0
    palette: list[tuple[int, int, int]] = []
    transparency: bytes = b""
    compressed = bytearray()

    while offset < len(data):
        if offset + 8 > len(data):
            raise RuntimeError(f"Truncated PNG chunk header in {png_path}")
        chunk_length = struct.unpack(">I", data[offset:offset + 4])[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk_start = offset + 8
        chunk_end = chunk_start + chunk_length
        if chunk_end + 4 > len(data):
            raise RuntimeError(f"Truncated PNG chunk {chunk_type!r} in {png_path}")
        chunk_data = data[chunk_start:chunk_end]
        offset = chunk_end + 4

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _compression, _filter, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
        elif chunk_type == b"PLTE":
            palette = [
                tuple(chunk_data[index:index + 3])
                for index in range(0, len(chunk_data), 3)
            ]
        elif chunk_type == b"tRNS":
            transparency = chunk_data
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width <= 0 or height <= 0:
        raise RuntimeError(f"{png_path} is missing a valid IHDR")
    if bit_depth != 8:
        raise RuntimeError(f"{png_path} uses unsupported PNG bit depth {bit_depth}; expected 8")
    if interlace != 0:
        raise RuntimeError(f"{png_path} uses unsupported interlacing; expected non-interlaced PNG")

    channels_by_type = {
        0: 1,  # grayscale
        2: 3,  # RGB
        3: 1,  # palette index
        4: 2,  # grayscale + alpha
        6: 4,  # RGBA
    }
    if color_type not in channels_by_type:
        raise RuntimeError(f"{png_path} uses unsupported PNG color type {color_type}")

    channels = channels_by_type[color_type]
    bytes_per_pixel = channels
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise RuntimeError(f"{png_path} has unexpected decompressed size {len(raw)}; expected {expected}")

    rows: list[bytes] = []
    raw_offset = 0
    previous = bytearray(stride)
    for _row in range(height):
        filter_type = raw[raw_offset]
        raw_offset += 1
        scanline = bytearray(raw[raw_offset:raw_offset + stride])
        raw_offset += stride

        for index in range(stride):
            left = scanline[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            up = previous[index]
            up_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0

            if filter_type == 1:
                scanline[index] = (scanline[index] + left) & 0xFF
            elif filter_type == 2:
                scanline[index] = (scanline[index] + up) & 0xFF
            elif filter_type == 3:
                scanline[index] = (scanline[index] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                predictor = left + up - up_left
                distance_left = abs(predictor - left)
                distance_up = abs(predictor - up)
                distance_up_left = abs(predictor - up_left)
                if distance_left <= distance_up and distance_left <= distance_up_left:
                    paeth = left
                elif distance_up <= distance_up_left:
                    paeth = up
                else:
                    paeth = up_left
                scanline[index] = (scanline[index] + paeth) & 0xFF
            elif filter_type != 0:
                raise RuntimeError(f"{png_path} uses unsupported PNG filter {filter_type}")

        rows.append(bytes(scanline))
        previous = scanline

    pixels: list[tuple[int, int, int, int]] = []
    for row in rows:
        for x in range(width):
            index = x * channels
            if color_type == 0:
                gray = row[index]
                pixels.append((gray, gray, gray, 255))
            elif color_type == 2:
                r, g, b = row[index:index + 3]
                pixels.append((r, g, b, 255))
            elif color_type == 3:
                palette_index = row[index]
                if palette_index >= len(palette):
                    raise RuntimeError(f"{png_path} references missing palette index {palette_index}")
                r, g, b = palette[palette_index]
                a = transparency[palette_index] if palette_index < len(transparency) else 255
                pixels.append((r, g, b, a))
            elif color_type == 4:
                gray, alpha = row[index:index + 2]
                pixels.append((gray, gray, gray, alpha))
            elif color_type == 6:
                r, g, b, a = row[index:index + 4]
                pixels.append((r, g, b, a))

    return width, height, pixels


def build_fsh_dxt3(width: int, height: int, pixels: list[tuple[int, int, int, int]], image_id: bytes) -> bytes:
    if len(image_id) != 8:
        raise RuntimeError("FSH image IDs must be exactly 8 bytes")

    payload = encode_dxt3_rgba(width, height, pixels)

    # Minimal one-image SHPI/FSH payload. Format 0x61 was verified against a
    # Reader-exported decoded FSH sample as DXT3.
    image_offset = 0x20
    total_size = image_offset + 16 + len(payload)
    header = bytearray()
    header.extend(b"SHPI")
    header.extend(struct.pack("<I", total_size))
    header.extend(struct.pack("<I", 1))
    header.extend(b"G264")
    header.extend(b"NONE")
    header.extend(struct.pack("<I", image_offset))
    header.extend(image_id)
    header.extend(struct.pack("<B3sHHHHI", 0x61, b"\0\0\0", width, height, 0, 0, 0))
    header.extend(payload)

    if len(header) != total_size:
        raise RuntimeError(f"Internal FSH size mismatch: expected {total_size}, got {len(header)}")
    return bytes(header)


def build_fsh_dxt3_from_png(png_path: Path, image_id: bytes = b"MODCAMIC") -> bytes:
    width, height, pixels = read_png_rgba(png_path)
    return build_fsh_dxt3(width, height, pixels, image_id)


def build_menu_icon_fsh_from_png(png_path: Path, image_id: bytes) -> bytes:
    width, height, pixels = read_png_rgba(png_path)
    if width % 4 != 0:
        raise RuntimeError(f"{png_path} must contain four equal horizontal button states")
    state_width = width // 4
    if state_width != 55 or height != 58:
        raise RuntimeError(
            f"{png_path} must be a 220x58 Disabled/Normal/Clicked/Hovered strip"
        )

    output_state_width = 60
    output_height = 72
    output_width = output_state_width * 4
    output_pixels: list[tuple[int, int, int, int]] = []

    state_centers: list[tuple[float, float]] = []
    for state in range(4):
        alpha_total = 0
        weighted_x = 0
        weighted_y = 0
        for y in range(height):
            for x in range(state_width):
                alpha = pixels[(y * width) + (state * state_width) + x][3]
                alpha_total += alpha
                weighted_x += x * alpha
                weighted_y += y * alpha
        if alpha_total:
            state_centers.append(
                (weighted_x / alpha_total, weighted_y / alpha_total)
            )
        else:
            state_centers.append(
                ((state_width - 1) / 2.0, (height - 1) / 2.0)
            )
    normal_center_x, normal_center_y = state_centers[1]

    def sample(state: int, x: int, y: int) -> tuple[int, int, int, int]:
        return pixels[(y * width) + (state * state_width) + x]

    for output_y in range(output_height):
        output_rows = [[] for _ in range(4)]
        for state in range(4):
            center_x, center_y = state_centers[state]
            source_y = (
                ((output_y + 0.5) * height / output_height)
                - 0.5
                + center_y
                - normal_center_y
            )
            y0 = max(0, min(height - 1, int(source_y)))
            y1 = min(height - 1, y0 + 1)
            fy = max(0.0, min(1.0, source_y - y0))
            for output_x in range(output_state_width):
                source_x = (
                    ((output_x + 0.5) * state_width / output_state_width)
                    - 0.5
                    + center_x
                    - normal_center_x
                )
                x0 = max(0, min(state_width - 1, int(source_x)))
                x1 = min(state_width - 1, x0 + 1)
                fx = max(0.0, min(1.0, source_x - x0))
                samples = (
                    (sample(state, x0, y0), (1.0 - fx) * (1.0 - fy)),
                    (sample(state, x1, y0), fx * (1.0 - fy)),
                    (sample(state, x0, y1), (1.0 - fx) * fy),
                    (sample(state, x1, y1), fx * fy),
                )
                alpha = sum(color[3] * weight for color, weight in samples)
                if alpha <= 0.0:
                    output_rows[state].append((0, 0, 0, 0))
                    continue
                channels = [
                    round(sum(color[channel] * color[3] * weight for color, weight in samples) / alpha)
                    for channel in range(3)
                ]
                output_rows[state].append(
                    (channels[0], channels[1], channels[2], round(alpha))
                )
        for state in range(4):
            output_pixels.extend(output_rows[state])

    return build_fsh_dxt3(output_width, output_height, output_pixels, image_id)


def build_menu_mode_toggle_hit_target_fsh() -> bytes:
    # Four transparent 60x72 button states let the light's native hit target
    # receive mouse input without drawing over the menu glyph.
    return build_fsh_dxt3(240, 72, [(0, 0, 0, 0)] * (240 * 72), b"MODCMTGT")


def escape_ui_caption(text: str) -> str:
    return (
        text.replace('"', "'")
        .replace("<", "[")
        .replace(">", "]")
        .replace("\r\n", "\n")
        .replace("\r", "\n")
    )


def read_plugin_version(version_header: Path) -> str:
    text = version_header.read_text(encoding="utf-8")
    match = re.search(r'inline\s+constexpr\s+char\s+String\[\]\s*=\s*"([^"]+)"', text)
    if not match:
        raise RuntimeError(f"Could not read PluginVersion::String from {version_header}")
    return match.group(1)


# ---------------------------------------------------------------------------
# LEGACY script emitters
#
# Every generated window is described by the control helpers and the row and
# section layout below. Each helper writes the keys the game's own UI scripts
# use for that control class.
# ---------------------------------------------------------------------------

Area = tuple[int, int, int, int]
Color = tuple[int, int, int]

MAXIS_UI_IMAGE_GROUP = 0x46A006B0
STANDARD_BUTTON_IMAGE = (MAXIS_UI_IMAGE_GROUP, 0x144161EB)
CLOSE_X_BUTTON_IMAGE = (MAXIS_UI_IMAGE_GROUP, 0x144161F9)
CHECKBOX_IMAGE = (MAXIS_UI_IMAGE_GROUP, 0x14416245)
SLIDER_IMAGE = (MAXIS_UI_IMAGE_GROUP, 0x46A006A7)
WINDOW_FRAME_IMAGE = (MAXIS_UI_IMAGE_GROUP, 0x6BB93CB5)
# GZWinGen only starts a window drag outside the rectangle its gutters leave, so the
# vertical gutter is what makes the title bar drawn by WINDOW_FRAME_IMAGE grabbable.
WINDOW_TITLE_BAR_HEIGHT = 34
CHOICE_BUTTON_IMAGE = (PLUGIN_GROUP_ID, CHOICE_BUTTON_IMAGE_INSTANCE_ID)
CAM_ICON_IMAGE = (PLUGIN_GROUP_ID, CAM_ICON_INSTANCE_ID)
CAM_ICON_SIZE = (32, 32)
CAMERA_POINT_IMAGE = (PLUGIN_GROUP_ID, CAMERA_POINT_INSTANCE_ID)
CAMERA_POINT_SIZE = (84, 56)
BUTTON_CLICK_SOUND = (0xCA47EFD9, 0x4A5C31D7)
CLOSE_X_CLICK_SOUND = (0x00000000, 0xCA5C3239)
CHECKBOX_CLICK_SOUND = (0xCA4D1943, 0x8A5C324A)

WINDOW_FILL = (228, 231, 238)
BUTTON_FILL = (204, 204, 204)
NO_COLOR = (0, 0, 0)
RULE_COLOR = (150, 160, 180)
TEXT_COLOR = (32, 40, 80)
MUTED_TEXT_COLOR = (72, 82, 112)
NOTE_TEXT_COLOR = (96, 64, 32)
CHECKBOX_LABEL_COLOR = (63, 73, 103)
BUTTON_FONT_COLORS = "colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103)"
BUTTON_TIP_KEYS = "tipoffsets=(0,0) tipflag=0x01000000"


def yes_no(value: bool) -> str:
    return "yes" if value else "no"


def format_tuple(values: tuple[int, ...]) -> str:
    return "(" + ",".join(str(value) for value in values) + ")"


def format_resource(resource: tuple[int, int]) -> str:
    return f"{{{resource[0]:08x},{resource[1]:08x}}}"


def window_flags(
    visible: bool = True,
    enabled: bool = True,
    moveable: bool = True,
    pbuff: bool = False,
    pbufftrans: bool = True,
    alphablend: Optional[bool] = False,
    acceptfocus: Optional[bool] = False,
    inert: bool = False,
) -> str:
    """Return the winflag keys. An inert control never receives the mouse."""
    keys = [
        ("visible", visible),
        ("enabled", enabled),
        ("moveable", moveable),
        ("sizeable", False),
        ("sortable", False),
        ("pbuff", pbuff),
        ("pbufftrans", pbufftrans),
        ("pbufferase", True),
        ("pbuffvid", False),
        ("alphablend", alphablend),
        ("acceptfocus", acceptfocus),
        ("mousetrans", inert),
        ("ignoremouse", inert),
    ]
    return " ".join(
        f"winflag_{name}={yes_no(value)}" for name, value in keys if value is not None
    )


def control_line(clsid: str, control_id: int, area: Area, fill: Color, *attributes: str) -> str:
    return (
        f"   <LEGACY clsid={clsid} iid=I{clsid} id=0x{control_id:08X} "
        f"area={format_tuple(area)} fillcolor={format_tuple(fill)} "
        + " ".join(attributes)
        + " >"
    )


def button(
    control_id: int,
    area: Area,
    caption: str = "",
    image: tuple[int, int] = STANDARD_BUTTON_IMAGE,
    style: str = "standard",
    tip: str = "",
    visible: bool = True,
    wrap_caption: bool = False,
) -> str:
    return control_line(
        "GZWinBtn",
        control_id,
        area,
        BUTTON_FILL,
        f'caption="{caption}"',
        window_flags(visible=visible, pbuff=True, pbufftrans=False, alphablend=True, acceptfocus=True),
        f"image={format_resource(image)}",
        "font=GenButton",
        BUTTON_FONT_COLORS,
        f"toggle={'on' if style == 'toggle' else 'off'} triggerondown=off",
        "showcaption=yes fill=yes autosize=no",
        f"wrapcaption={yes_no(wrap_caption)} shiftcaption=yes",
        f"tips={yes_no(bool(tip))} tipsdelay=no tipstimeout=no",
        f"style={style} gutters=(0,0,0,0)",
        f'tiptext="{tip}" {BUTTON_TIP_KEYS} align=center',
        f"btnclicksnd={format_resource(BUTTON_CLICK_SOUND)}",
    )


def choice_button(control_id: int, area: Area, caption: str, tip: str = "", visible: bool = True) -> str:
    """Return one option of a choice group, drawn with the selected-state strip."""
    return button(control_id, area, caption, CHOICE_BUTTON_IMAGE, "toggle", tip, visible)


def image_button(
    control_id: int,
    area: Area,
    image: tuple[int, int],
    visible: bool = True,
    fill: bool = True,
) -> str:
    """Return a captionless button whose whole face comes from its image strip."""
    return control_line(
        "GZWinBtn",
        control_id,
        area,
        NO_COLOR,
        'caption=""',
        window_flags(visible=visible, moveable=False, pbuff=True, alphablend=True, acceptfocus=True),
        f"image={format_resource(image)}",
        "font=GenButton",
        BUTTON_FONT_COLORS,
        "toggle=off triggerondown=off",
        f"showcaption=no fill={yes_no(fill)} autosize=no",
        "wrapcaption=no shiftcaption=no",
        "tips=no tipsdelay=no tipstimeout=no",
        "style=standard gutters=(0,0,0,0)",
        f'tiptext="" {BUTTON_TIP_KEYS} align=center',
        f"btnclicksnd={format_resource(BUTTON_CLICK_SOUND)}",
    )


def close_x_button(control_id: int, right: int) -> str:
    """Return the native title-bar X, which is an ordinary 22x20 button."""
    return control_line(
        "GZWinBtn",
        control_id,
        (right - 22, 10, right, 30),
        BUTTON_FILL,
        'caption=""',
        window_flags(pbuff=True, pbufftrans=False, alphablend=True, acceptfocus=True),
        f"image={format_resource(CLOSE_X_BUTTON_IMAGE)}",
        "font=GenButton",
        BUTTON_FONT_COLORS,
        "toggle=off triggerondown=off",
        "showcaption=no fill=yes autosize=no",
        "wrapcaption=no shiftcaption=no",
        "tips=yes tipsdelay=no tipstimeout=no",
        "style=standard gutters=(0,0,0,0)",
        f'tiptext="Close" {BUTTON_TIP_KEYS} align=center',
        f"btnclicksnd={format_resource(CLOSE_X_CLICK_SOUND)}",
    )


def checkbox(control_id: int, left: int, top: int, visible: bool = True) -> str:
    """Return the Ordinances-style 20x22 checkbox; its label is a separate control."""
    return control_line(
        "GZWinBtn",
        control_id,
        (left, top, left + 20, top + 22),
        BUTTON_FILL,
        'caption=""',
        window_flags(visible=visible, acceptfocus=True),
        f"image={format_resource(CHECKBOX_IMAGE)}",
        "font=GenBodyMedium",
        BUTTON_FONT_COLORS,
        "toggle=on triggerondown=off",
        "showcaption=no fill=yes autosize=no",
        "wrapcaption=no shiftcaption=no",
        "tips=no tipsdelay=no tipstimeout=no",
        "style=radiocheck gutters=(10,3,10,3)",
        f'tiptext="" {BUTTON_TIP_KEYS} align=right',
        f"btnclicksnd={format_resource(CHECKBOX_CLICK_SOUND)}",
    )


def text(
    control_id: int,
    area: Area,
    caption: str,
    font: str = "GenBodyMedium",
    align: str = "leftcenter",
    color: Color = TEXT_COLOR,
    wrapped: bool = False,
    visible: bool = True,
) -> str:
    return control_line(
        "GZWinText",
        control_id,
        area,
        NO_COLOR,
        f'caption="{caption}"',
        window_flags(visible=visible, inert=True),
        f"font={font} align={align} notify=no wrapped={yes_no(wrapped)} opaque=no",
        f"forecolor={format_tuple(color)} bkgcolor=(0,0,0) gutters=(2,2)",
    )


def slider(
    control_id: int,
    area: Area,
    minimum: int,
    maximum: int,
    initial: int = 100,
    visible: bool = True,
) -> str:
    return control_line(
        "GZWinSlider",
        control_id,
        area,
        NO_COLOR,
        window_flags(visible=visible, alphablend=True, acceptfocus=True),
        f"minmaxvalue=({minimum},{maximum}) direction=horizontal",
        f"image={format_resource(SLIDER_IMAGE)} initvalue={initial}",
    )


def picture(control_id: int, left: int, top: int, image: tuple[int, int], size: tuple[int, int]) -> str:
    """Return a static image; imagerect covers the whole single-frame resource."""
    width, height = size
    return control_line(
        "GZWinBMP",
        control_id,
        (left, top, left + width, top + height),
        NO_COLOR,
        window_flags(moveable=False, pbuff=True, alphablend=True, inert=True),
        f"image={format_resource(image)} imagerect=(0,0,{width},{height})",
        "notify=no transparentbkg=yes edgeimage=no",
    )


def rule(control_id: int, area: Area) -> str:
    """Return a divider: a thin GZWinFlatRect that draws only its fill color."""
    return control_line(
        "GZWinFlatRect",
        control_id,
        area,
        RULE_COLOR,
        window_flags(moveable=False, inert=True),
        "colorleft=(0,0,0) colortop=(0,0,0) colorright=(0,0,0) colorbottom=(0,0,0)",
    )


def section_box(control_id: int, area: Area, visible: bool = True) -> str:
    """Return an unfilled outline that ignores the mouse, so sibling order does not matter."""
    edge = format_tuple(RULE_COLOR)
    return control_line(
        "GZWinFlatRect",
        control_id,
        area,
        NO_COLOR,
        window_flags(visible=visible, moveable=False, inert=True),
        f"colorleft={edge} colortop={edge} colorright={edge} colorbottom={edge}",
        "style=left style=top style=right style=bottom style=nofill",
    )


def changelog_view(control_id: int, area: Area, changelog: str, max_text: int) -> str:
    """Return the read-only multiline GZWinTextEdit that scrolls the changelog."""
    return control_line(
        "GZWinTextEdit",
        control_id,
        area,
        (242, 244, 248),
        f'caption="{changelog}"',
        "transparent",
        window_flags(pbuff=True, alphablend=None, acceptfocus=None),
        "editable=no wrapped=yes hscrollbar=no vscrollbar=yes outline=no opaque=yes",
        "caretvisible=no allowinsert=no allowundo=no singleline=no",
        f'initvalue="{changelog}"',
        "colorfontnormal=(32,40,80) colorfontdisabled=(32,40,80) colorfonthilited=(255,255,255)",
        "colorfontnormalbkg=(242,244,248) colorfontdisabledbkg=(242,244,248) colorfonthilitedbkg=(0,0,128)",
        "caretcolor=(32,40,80) highlightcolor=(0,0,128) outlinecolor=(112,124,152)",
        f"maxtext={max_text} gutters=(5,4) overwrite=no insertindex=0 insertpos=(0,0)",
        "caretperiod=1000 maxundo=0",
    )


def assemble_script(description: str, root: str, children: list[str]) -> bytes:
    lines = [f"# Generated for SC4-ModernCamera's {description}", root, "<CHILDREN>"]
    lines.extend(children)
    lines.extend(("</CHILDREN>", "</LEGACY>", ""))
    return "\n".join(lines).encode("utf-8")


def framed_window(
    control_id: int,
    width: int,
    height: int,
    caption: str,
    description: str,
    children: list[str],
) -> bytes:
    """Return a complete script for a moveable window with the native frame."""
    root = (
        f"<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x{control_id:08X} "
        f"area=(0,0,{width},{height}) fillcolor={format_tuple(WINDOW_FILL)} "
        f'caption="{caption}" '
        + window_flags(alphablend=True, acceptfocus=True)
        + f" image={format_resource(WINDOW_FRAME_IMAGE)} blttype=edge userdata=0 "
        "moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no "
        "minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no "
        f"titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,{WINDOW_TITLE_BAR_HEIGHT}) >"
    )
    return assemble_script(description, root, children)


# ---------------------------------------------------------------------------
# Settings layout
#
# A row draws itself at a given top edge. A section stacks its rows inside an
# outline under a heading. To add an option, add one row here and handle its
# control IDs in SC4WindowManager.cpp. Page controls must keep their IDs inside
# the page ranges below, because the plugin shows and hides pages by range.
# ---------------------------------------------------------------------------

GENERAL_PAGE_RANGES = ((0x3D0C0922, 0x3D0C093F),)
ADVANCED_PAGE_RANGES = ((0x3D0C0940, 0x3D0C094F), (0x3D0C0961, 0x3D0C097F))
SECTION_LEFT = 24
SECTION_RIGHT = 496
SECTION_GAP = 8
ROW_LEFT = 36
ROW_LABEL_RIGHT = 216
ROW_RIGHT = 472
TWO_BUTTONS = ((220, 340), (352, 472))
THREE_BUTTONS = ((172, 252), (264, 364), (376, 484))
FOUR_BUTTONS = ((36, 140), (150, 254), (264, 368), (378, 484))


@dataclass(frozen=True)
class Choice:
    control_id: int
    caption: str
    tip: str = ""


@dataclass(frozen=True)
class ChoiceRow:
    """One group of mutually exclusive buttons, with an optional label."""

    choices: tuple[Choice, ...]
    label_id: int = 0
    label: str = ""
    columns: tuple[tuple[int, int], ...] = TWO_BUTTONS
    height: int = 32

    def render(self, top: int, visible: bool) -> list[str]:
        lines = []
        if self.label_id:
            lines.append(
                text(
                    self.label_id,
                    (ROW_LEFT, top + 4, self.columns[0][0] - 4, top + 26),
                    self.label,
                    visible=visible,
                )
            )
        for choice, (left, right) in zip(self.choices, self.columns, strict=True):
            lines.append(
                choice_button(choice.control_id, (left, top, right, top + 28), choice.caption, choice.tip, visible)
            )
        return lines


@dataclass(frozen=True)
class SliderRow:
    label_id: int
    label: str
    control_id: int
    minimum: int
    maximum: int
    height: int = 30

    def render(self, top: int, visible: bool) -> list[str]:
        return [
            text(self.label_id, (ROW_LEFT, top + 2, ROW_LABEL_RIGHT, top + 24), self.label, visible=visible),
            slider(self.control_id, (220, top, ROW_RIGHT, top + 22), self.minimum, self.maximum, visible=visible),
        ]


@dataclass(frozen=True)
class ActionRow:
    """One button that runs an action rather than selecting a value."""

    control_id: int
    caption: str
    height: int = 32

    def render(self, top: int, visible: bool) -> list[str]:
        return [button(self.control_id, (220, top, ROW_RIGHT, top + 28), self.caption, visible=visible)]


@dataclass(frozen=True)
class CheckboxRow:
    control_id: int
    label_id: int
    label: str
    height: int = 28

    def render(self, top: int, visible: bool) -> list[str]:
        return [
            checkbox(self.control_id, ROW_LEFT, top, visible),
            text(self.label_id, (ROW_LEFT + 26, top, ROW_RIGHT, top + 22), self.label, visible=visible),
        ]


@dataclass(frozen=True)
class NoteRow:
    control_id: int
    caption: str
    height: int
    color: Color = TEXT_COLOR

    def render(self, top: int, visible: bool) -> list[str]:
        return [
            text(
                self.control_id,
                (ROW_LEFT, top, SECTION_RIGHT - 12, top + self.height - 2),
                self.caption,
                align="lefttop",
                color=self.color,
                wrapped=True,
                visible=visible,
            )
        ]


@dataclass(frozen=True)
class Section:
    box_id: int
    heading_id: int
    heading: str
    rows: tuple[object, ...]


def layout_sections(sections: tuple[Section, ...], top: int, visible: bool) -> list[str]:
    """Stack sections downward from top and return their controls."""
    lines: list[str] = []
    for section in sections:
        section_lines = [
            text(
                section.heading_id,
                (ROW_LEFT - 4, top + 4, SECTION_RIGHT - 8, top + 26),
                section.heading,
                font="GenHeader",
                align="lefttop",
                visible=visible,
            )
        ]
        row_top = top + 30
        for row in section.rows:
            section_lines.extend(row.render(row_top, visible))
            row_top += row.height
        bottom = row_top + 2
        lines.append(section_box(section.box_id, (SECTION_LEFT, top, SECTION_RIGHT, bottom), visible))
        lines.extend(section_lines)
        top = bottom + SECTION_GAP
    return lines


def on_off_row(label_id: int, label: str, on_id: int, off_id: int, on_tip: str = "", off_tip: str = "") -> ChoiceRow:
    return ChoiceRow((Choice(on_id, "On", on_tip), Choice(off_id, "Off", off_tip)), label_id, label)


# Vertical budget shared by both pages of the settings window.
SETTINGS_WINDOW_WIDTH = 520
SETTINGS_WINDOW_HEIGHT = 542
SETTINGS_PAGE_TOP = 86
# WINDOW_FRAME_IMAGE draws its own 2-pixel divider at this distance above the window's bottom edge.
FRAME_FOOTER_LINE_OFFSET = 62
SETTINGS_FOOTER_LINE_TOP = SETTINGS_WINDOW_HEIGHT - FRAME_FOOTER_LINE_OFFSET
SETTINGS_FOOTER_TOP = 498

GENERAL_SECTIONS = (
    Section(
        0x3D0C0922,
        0x3D0C092D,
        "Camera",
        (
            ChoiceRow(
                (Choice(0x3D0C0930, "Modern"), Choice(0x3D0C0931, "Classic")),
                0x3D0C0923,
                "Camera mode:",
            ),
            ActionRow(0x3D0C0938, "Reset Camera Location"),
        ),
    ),
    Section(
        0x3D0C092A,
        0x3D0C092B,
        "Modern",
        (
            on_off_row(0x3D0C0924, "WASD Movement:", 0x3D0C0932, 0x3D0C0933),
            on_off_row(0x3D0C0929, "Q/E Rotation:", 0x3D0C093A, 0x3D0C093B),
            on_off_row(0x3D0C0927, "Invert vertical rotation:", 0x3D0C0936, 0x3D0C0937),
            ChoiceRow(
                (
                    Choice(0x3D0C093C, "Restore View", "Return to the saved view when Alt is released"),
                    Choice(0x3D0C093D, "Keep View", "Stay on the current view when Alt is released"),
                ),
                0x3D0C0928,
                "On Alt release:",
            ),
            on_off_row(
                0x3D0C092C,
                "Bookmark Hotkeys:",
                0x3D0C093E,
                0x3D0C093F,
                "Ctrl+1-8 saves a camera view and 1-8 recalls it",
                "Leave the number keys to SimCity 4",
            ),
            SliderRow(0x3D0C0925, "Rotation sensitivity:", 0x3D0C0934, 10, 300),
            SliderRow(0x3D0C0926, "Zoom sensitivity:", 0x3D0C0935, 10, 300),
            SliderRow(0x3D0C092E, "Panning speed:", 0x3D0C0939, 25, 275),
        ),
    ),
)

ADVANCED_SECTIONS = (
    Section(
        0x3D0C0944,
        0x3D0C0962,
        "Camera Tuning",
        (
            SliderRow(0x3D0C0945, "Q/E rotation speed:", 0x3D0C0946, 10, 300),
            SliderRow(0x3D0C0947, "Shift pan boost:", 0x3D0C0948, 100, 500),
            on_off_row(0x3D0C0949, "Invert Q/E:", 0x3D0C0940, 0x3D0C0941),
            ChoiceRow(
                (
                    Choice(0x3D0C0942, "Smooth", "Zoom continuously with the mouse wheel"),
                    Choice(0x3D0C0943, "Stepped", "Use SimCity 4's own zoom levels"),
                ),
                0x3D0C094A,
                "Mouse wheel zoom:",
            ),
            on_off_row(0x3D0C094B, "Invert wheel zoom:", 0x3D0C094C, 0x3D0C094D),
        ),
    ),
    Section(
        0x3D0C0961,
        0x3D0C0966,
        "Diagnostics",
        (
            ChoiceRow(
                (Choice(0x3D0C0968, "Off"), Choice(0x3D0C0969, "Normal"), Choice(0x3D0C096A, "Verbose")),
                0x3D0C0967,
                "Debug Logging:",
                THREE_BUTTONS,
            ),
            NoteRow(
                0x3D0C096B,
                escape_ui_caption(
                    "Normal logs settings and camera changes. Verbose adds input traces.\n"
                    "Ctrl+Alt+F8 writes a camera dump to the log.\n"
                    "Log: Plugins/SC4-ModernCamera/SC4-ModernCamera.log"
                ),
                54,
            ),
        ),
    ),
    Section(
        0x3D0C0965,
        0x3D0C0971,
        "Updates",
        (CheckboxRow(0x3D0C096E, 0x3D0C0970, "Show Changelogs on Update"),),
    ),
)


def build_greeting_script(changelog_path: Path, version_header: Path) -> bytes:
    version = read_plugin_version(version_header)
    changelog_body = changelog_path.read_text(encoding="utf-8").strip()
    changelog = escape_ui_caption(changelog_body)
    camera_note = escape_ui_caption(
        "Use the upper-right camera button for camera options."
    )
    changelog_max_text = max(4096, len(changelog_body) + 512)
    return framed_window(
        0x3D0C0706,
        520,
        320,
        "SC4-ModernCamera Greeting",
        "first-install greeting window",
        [
            close_x_button(0x3D0C0840, 512),
            picture(0x3D0C0845, 18, 2, CAM_ICON_IMAGE, CAM_ICON_SIZE),
            text(0x3D0C0841, (58, 4, 478, 30), "Welcome!", font="GenHeader"),
            text(0x3D0C0846, (24, 50, 286, 74), f"SC4-ModernCamera {version} Installed!"),
            rule(0x3D0C0849, (292, 42, 294, 116)),
            picture(0x3D0C084A, 400, 40, CAMERA_POINT_IMAGE, CAMERA_POINT_SIZE),
            text(
                0x3D0C084B,
                (304, 96, 496, 122),
                camera_note,
                font="GenBodyLight",
                align="lefttop",
                color=MUTED_TEXT_COLOR,
                wrapped=True,
            ),
            text(0x3D0C084C, (24, 112, 292, 134), "Changelog:"),
            changelog_view(0x3D0C0842, (24, 132, 496, 258), changelog, changelog_max_text),
            rule(0x3D0C084D, (24, 266, 496, 268)),
            checkbox(0x3D0C0847, 190, 279),
            text(
                0x3D0C0848,
                (212, 276, 334, 304),
                "Don't show these again",
                font="GenBodyLight",
                color=CHECKBOX_LABEL_COLOR,
                wrapped=True,
            ),
            button(0x3D0C0844, (24, 276, 184, 304), "View Github"),
            button(0x3D0C0843, (340, 276, 500, 304), "OK"),
        ],
    )


def build_controls_script() -> bytes:
    controls = escape_ui_caption(
        "Keyboard Controls:\n"
        "WASD: Pan the camera\n"
        "Shift + WASD: Pan faster\n\n"
        "Q / E: Rotate the camera\n\n"
        "Mouse Controls:\n"
        "Mouse Wheel: Zoom\n"
        "Middle Mouse Button + Drag: Pan & Tilt\n"
        "Alt: Hold Classic angle\n"
        "Camera Button: Opens camera settings"
    )
    return framed_window(
        0x3D0C0708,
        440,
        280,
        "SC4-ModernCamera Controls",
        "controls help window",
        [
            close_x_button(0x3D0C0850, 432),
            text(0x3D0C0851, (24, 4, 398, 30), "Controls", font="GenHeader"),
            text(0x3D0C0852, (24, 48, 416, 220), controls, align="lefttop", wrapped=True),
            button(0x3D0C0853, (256, 236, 416, 264), "OK"),
        ],
    )


def build_menu_button_script() -> bytes:
    root = (
        "<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0902 area=(0,0,60,72) fillcolor=(0,0,0) "
        'caption="SC4-ModernCamera Menu Button" '
        + window_flags(moveable=False, alphablend=True)
        + " userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no "
        "minmaxvisible=no titlebar=no fill=no outline=no paint=no sidebar=no gutters=(0,0) >"
    )
    return assemble_script(
        "floating settings menu button",
        root,
        [
            image_button(0x3D0C0910, (0, 0, 60, 72), (PLUGIN_GROUP_ID, MENU_ICON_ON_INSTANCE_ID)),
            image_button(0x3D0C0914, (0, 0, 60, 72), (PLUGIN_GROUP_ID, MENU_ICON_OFF_INSTANCE_ID), visible=False),
            image_button(
                0x3D0C0911,
                (40, 46, 60, 72),
                (PLUGIN_GROUP_ID, MENU_MODE_TOGGLE_HIT_TARGET_INSTANCE_ID),
                fill=False,
            ),
        ],
    )


def build_settings_script(version_header: Path) -> bytes:
    version = read_plugin_version(version_header)
    return framed_window(
        0x3D0C0904,
        SETTINGS_WINDOW_WIDTH,
        SETTINGS_WINDOW_HEIGHT,
        "SC4-ModernCamera Settings",
        "camera settings window",
        [
            close_x_button(0x3D0C0920, 512),
            picture(0x3D0C0956, 18, 2, CAM_ICON_IMAGE, CAM_ICON_SIZE),
            text(0x3D0C0921, (58, 4, 410, 30), "SC4-ModernCamera Settings", font="GenHeader"),
            text(
                0x3D0C0957,
                (414, 6, 484, 28),
                f"v{version}",
                font="GenBodyLight",
                align="rightcenter",
                color=MUTED_TEXT_COLOR,
            ),
            choice_button(0x3D0C0955, (24, 44, 144, 72), "General"),
            choice_button(0x3D0C0951, (152, 44, 272, 72), "Advanced"),
            button(0x3D0C095A, (356, 44, SECTION_RIGHT, 72), "View Controls", tip="Show the camera controls"),
            rule(0x3D0C0959, (SECTION_LEFT, 78, SECTION_RIGHT, 80)),
            *layout_sections(GENERAL_SECTIONS, SETTINGS_PAGE_TOP, True),
            *layout_sections(ADVANCED_SECTIONS, SETTINGS_PAGE_TOP, False),
            button(
                0x3D0C0950,
                (24, SETTINGS_FOOTER_TOP, 144, SETTINGS_FOOTER_TOP + 28),
                "Defaults",
                tip="Restore default settings",
            ),
            button(
                0x3D0C0952,
                (246, SETTINGS_FOOTER_TOP, 386, SETTINGS_FOOTER_TOP + 28),
                "Show Changelog",
                tip="Read changelog",
                wrap_caption=True,
            ),
            button(
                0x3D0C0953,
                (402, SETTINGS_FOOTER_TOP, 492, SETTINGS_FOOTER_TOP + 28),
                "Close",
                wrap_caption=True,
            ),
        ],
    )


def build(source: Path, destination: Path) -> None:
    script = source.read_text(encoding="utf-8")
    # Ordinance-style checkboxes are a small radio-check bitmap button with a
    # separate label. Using the standard wide-button bitmap with radiocheck
    # makes SC4 read the wrong state atlas and display back-buffer garbage.
    checkbox = (
        '   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0734 area=(28,178,48,200) '
        'fillcolor=(204,204,204) winflag_visible=yes winflag_enabled=yes winflag_moveable=yes '
        'winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes '
        'winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=yes '
        'winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,14416245} font=GenBodyMedium '
        'colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) '
        'colorfontnormalbkg=(0,0,0) colorfontdisabledbkg=(0,0,0) colorfonthilitedbkg=(0,0,0) '
        'toggle=on triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no '
        'tips=no tipsdelay=no tipstimeout=no style=radiocheck gutters=(10,3,10,3) tiptext="" '
        'tipoffsets=(0,0) tipflag=0x01000000 align=right btnclicksnd={ca4d1943,8a5c324a} >\n'
        '   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0738 area=(54,178,208,200) '
        'fillcolor=(0,0,0) caption="Checkbox Test" winflag_visible=yes winflag_enabled=yes '
        'winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no '
        'winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no '
        'winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium '
        'align=leftcenter notify=no wrapped=no opaque=no forecolor=(63,73,103) bkgcolor=(0,0,0) '
        'gutters=(2,2) >'
    )
    script = re.sub(
        r"^.*<LEGACY clsid=GZWinBtn[^\r\n]*id=0x3D0C0734[^\r\n]*$",
        checkbox,
        script,
        flags=re.MULTILINE,
    )
    resources = [(UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, CONTROL_LAB_INSTANCE_ID, script.encode("utf-8"))]
    basic_source = source.with_name("SC4-ModernCamera-BasicUI.txt")
    if basic_source.exists():
        resources.append((UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, BASIC_INSTANCE_ID, basic_source.read_bytes()))
    changelog_source = source.parents[2] / "docs" / "changelog.md"
    version_header = source.parents[1] / "src" / "PluginVersion.h"
    if changelog_source.exists():
        resources.append(
            (UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, GREETING_INSTANCE_ID, build_greeting_script(changelog_source, version_header))
        )
        resources.append((UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, CONTROLS_INSTANCE_ID, build_controls_script()))
        resources.append((UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, MENU_BUTTON_INSTANCE_ID, build_menu_button_script()))
        resources.append(
            (UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, SETTINGS_INSTANCE_ID, build_settings_script(version_header))
        )

    menu_icon_on_source = source.with_name(MENU_ICON_ON_SOURCE_NAME)
    if menu_icon_on_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                MENU_ICON_ON_INSTANCE_ID,
                build_menu_icon_fsh_from_png(menu_icon_on_source, b"MODCMION"),
            )
        )

    menu_icon_off_source = source.with_name(MENU_ICON_OFF_SOURCE_NAME)
    if menu_icon_off_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                MENU_ICON_OFF_INSTANCE_ID,
                build_menu_icon_fsh_from_png(menu_icon_off_source, b"MODCMIOF"),
            )
        )

    resources.append(
        (
            UI_IMAGE_TYPE_ID,
            PLUGIN_GROUP_ID,
            MENU_MODE_TOGGLE_HIT_TARGET_INSTANCE_ID,
            build_menu_mode_toggle_hit_target_fsh(),
        )
    )

    choice_button_source = source.with_name("menu-button-stages.png")
    if choice_button_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                CHOICE_BUTTON_IMAGE_INSTANCE_ID,
                build_fsh_dxt3_from_png(choice_button_source, b"MODCMBTN"),
            )
        )

    cam_icon_source = source.with_name("camicon.png")
    if cam_icon_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                CAM_ICON_INSTANCE_ID,
                build_fsh_dxt3_from_png(cam_icon_source, b"MODCMIC2"),
            )
        )

    camera_point_source = source.with_name("camera_point.png")
    if camera_point_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                CAMERA_POINT_INSTANCE_ID,
                build_fsh_dxt3_from_png(camera_point_source, b"MODCMARW"),
            )
        )

    payload = bytearray()
    index_entries = []
    for type_id, group_id, instance_id, resource_data in resources:
        offset = HEADER_SIZE + len(payload)
        payload.extend(resource_data)
        index_entries.append(
            struct.pack("<IIIII", type_id, group_id, instance_id, offset, len(resource_data))
        )

    index_offset = HEADER_SIZE + len(payload)
    header = bytearray(HEADER_SIZE)
    header[0:4] = b"DBPF"
    struct.pack_into("<II", header, 4, 1, 0)
    struct.pack_into("<II", header, 0x18, 0, 0)
    struct.pack_into("<IIII", header, 0x20, 7, len(resources), index_offset, 20 * len(resources))
    struct.pack_into("<III", header, 0x30, 0, 0, 0)
    struct.pack_into("<I", header, 0x3C, 0)
    index = b"".join(index_entries)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + payload + index)
    print(f"Wrote {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_sc4_ui_dat.py SOURCE DESTINATION")
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    reason = get_build_reason(source, destination)
    if reason:
        print(f"Native UI DAT needs generation ({reason}).")
        build(source, destination)
    else:
        print(f"Native UI DAT is up to date; generation skipped: {destination}")
