"""Build the dependency-free SC4 DBPF file containing our UI resources."""

from pathlib import Path
import re
import struct
import sys
import zlib


UI_SCRIPT_TYPE_ID = 0x00000000
UI_IMAGE_TYPE_ID = 0x856DDBAC
PLUGIN_GROUP_ID = 0x3D0C0700
CONTROL_LAB_INSTANCE_ID = 0x3D0C0701
BASIC_INSTANCE_ID = 0x3D0C0703
GREETING_INSTANCE_ID = 0x3D0C0705
CONTROLS_INSTANCE_ID = 0x3D0C0707
MENU_ICON_INSTANCE_ID = 0x3D0C0900
CHOICE_BUTTON_IMAGE_INSTANCE_ID = 0x3D0C0907
CAM_ICON_INSTANCE_ID = 0x3D0C0908
CAMERA_POINT_INSTANCE_ID = 0x3D0C0909
MENU_BUTTON_INSTANCE_ID = 0x3D0C0901
SETTINGS_INSTANCE_ID = 0x3D0C0903
DIAGNOSTICS_INSTANCE_ID = 0x3D0C0905
HEADER_SIZE = 96
CHOICE_BUTTON_IDS = (
    "0x3D0C0930",
    "0x3D0C0931",
    "0x3D0C0932",
    "0x3D0C0933",
    "0x3D0C0936",
    "0x3D0C0937",
    "0x3D0C0940",
    "0x3D0C0941",
    "0x3D0C0942",
    "0x3D0C0943",
    "0x3D0C0968",
    "0x3D0C0969",
    "0x3D0C096A",
)


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


def build_button_strip_fsh_from_png(png_path: Path, image_id: bytes) -> bytes:
    width, height, pixels = read_png_rgba(png_path)
    strip_width = width * 4
    strip_pixels: list[tuple[int, int, int, int]] = []
    for row in range(height):
        source_row = pixels[row * width:(row + 1) * width]
        strip_pixels.extend(source_row * 4)
    return build_fsh_dxt3(strip_width, height, strip_pixels, image_id)


def escape_ui_caption(text: str) -> str:
    return (
        text.replace('"', "'")
        .replace("<", "[")
        .replace(">", "]")
        .replace("\r\n", "\n")
        .replace("\r", "\n")
    )


def apply_choice_button_image(script: str) -> str:
    for control_id in CHOICE_BUTTON_IDS:
        script = re.sub(
            rf"(<LEGACY clsid=GZWinBtn[^\n]*id={control_id}[^\n]*?)image=\{{46a006b0,144161eb\}}",
            rf"\1image={{{PLUGIN_GROUP_ID:08x},{CHOICE_BUTTON_IMAGE_INSTANCE_ID:08x}}}",
            script,
        )
    return script


def read_plugin_version(version_header: Path) -> str:
    text = version_header.read_text(encoding="utf-8")
    match = re.search(r'inline\s+constexpr\s+char\s+String\[\]\s*=\s*"([^"]+)"', text)
    if not match:
        raise RuntimeError(f"Could not read PluginVersion::String from {version_header}")
    return match.group(1)


def build_greeting_script(changelog_path: Path, version_header: Path) -> bytes:
    version = read_plugin_version(version_header)
    changelog_body = changelog_path.read_text(encoding="utf-8").strip()
    changelog = escape_ui_caption(changelog_body)
    camera_note = escape_ui_caption(
        "Use the upper-right camera button for camera options."
    )
    changelog_max_text = max(4096, len(changelog_body) + 512)
    script = f"""# Generated for SC4-ModernCamera's first-install greeting window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0706 area=(0,0,520,320) fillcolor=(228,231,238) caption="SC4-ModernCamera Greeting" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,6bb93cb5}} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0840 area=(490,10,512,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161f9}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0845 area=(18,2,50,34) fillcolor=(0,0,0) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes image={{3d0c0700,3d0c0908}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0841 area=(58,4,478,30) fillcolor=(0,0,0) caption="Welcome!" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0846 area=(24,50,286,74) fillcolor=(0,0,0) caption="SC4-ModernCamera {version} Installed!" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0849 area=(292,42,294,116) fillcolor=(150,160,180) caption="" winflag_visible=yes winflag_enabled=no winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C084A area=(400,40,484,96) fillcolor=(0,0,0) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes image={{3d0c0700,3d0c0909}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C084B area=(304,96,496,122) fillcolor=(0,0,0) caption="{camera_note}" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyLight align=lefttop notify=no wrapped=yes opaque=no forecolor=(72,82,112) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C084C area=(24,112,292,134) fillcolor=(0,0,0) caption="Changelog:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinTextEdit iid=IGZWinTextEdit id=0x3D0C0842 area=(24,132,496,258) fillcolor=(242,244,248) caption="{changelog}" transparent winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_mousetrans=no winflag_ignoremouse=no editable=no wrapped=yes hscrollbar=no vscrollbar=yes outline=no opaque=yes caretvisible=no allowinsert=no allowundo=no singleline=no initvalue="{changelog}" colorfontnormal=(32,40,80) colorfontdisabled=(32,40,80) colorfonthilited=(255,255,255) colorfontnormalbkg=(242,244,248) colorfontdisabledbkg=(242,244,248) colorfonthilitedbkg=(0,0,128) caretcolor=(32,40,80) highlightcolor=(0,0,128) outlinecolor=(112,124,152) maxtext={changelog_max_text} gutters=(5,4) overwrite=no insertindex=0 insertpos=(0,0) caretperiod=1000 maxundo=0 >
   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C084D area=(24,266,496,268) fillcolor=(150,160,180) caption="" winflag_visible=yes winflag_enabled=no winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0844 area=(24,276,184,304) fillcolor=(204,204,204) caption="View Controls" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0843 area=(340,276,500,304) fillcolor=(204,204,204) caption="OK" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
</CHILDREN>
</LEGACY>
"""
    return apply_choice_button_image(script).encode("utf-8")


def build_controls_script() -> bytes:
    controls = escape_ui_caption(
        "Controls:\n"
        "WASD: Move Camera (optional)\n"
        "Scroll Wheel: Zoom\n"
        "Mouse 3 + Drag: Pan & Tilt"
    )
    script = f"""# Generated for SC4-ModernCamera's controls help window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0708 area=(0,0,360,210) fillcolor=(228,231,238) caption="SC4-ModernCamera Controls" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,6bb93cb5}} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0850 area=(330,10,352,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161f9}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0851 area=(20,4,318,30) fillcolor=(0,0,0) caption="Controls" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0852 area=(24,62,336,142) fillcolor=(0,0,0) caption="{controls}" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=lefttop notify=no wrapped=yes opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0853 area=(180,164,340,192) fillcolor=(204,204,204) caption="OK" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
</CHILDREN>
</LEGACY>
"""
    return apply_choice_button_image(script).encode("utf-8")


def build_menu_button_script() -> bytes:
    script = """# Generated for SC4-ModernCamera's floating settings menu button
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0902 area=(0,0,44,44) fillcolor=(0,0,0) caption="SC4-ModernCamera Menu Button" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=no winflag_mousetrans=no winflag_ignoremouse=no userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=no outline=no paint=no sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0910 area=(0,0,44,44) fillcolor=(0,0,0) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={3d0c0700,3d0c0900} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="SC4-ModernCamera Settings" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
</CHILDREN>
</LEGACY>
"""
    return script.encode("utf-8")


def build_settings_script_legacy() -> bytes:
    script = """# Generated for SC4-ModernCamera's camera settings window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0904 area=(0,0,520,480) fillcolor=(228,231,238) caption="SC4-ModernCamera Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,6bb93cb5} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0920 area=(490,10,512,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161f9} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={00000000,ca5c3239} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C092D area=(18,2,50,34) fillcolor=(0,0,0) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes image={3d0c0700,3d0c0908} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={00000000,ca5c3239} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0921 area=(58,4,478,30) fillcolor=(0,0,0) caption="SC4-ModernCamera Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0922 area=(24,48,496,70) fillcolor=(0,0,0) caption="Camera" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0923 area=(36,82,210,104) fillcolor=(0,0,0) caption="Camera mode:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0930 area=(220,78,340,106) fillcolor=(204,204,204) caption="Modern" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0931 area=(352,78,472,106) fillcolor=(204,204,204) caption="Classic" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0924 area=(36,122,220,144) fillcolor=(0,0,0) caption="WASD Movement:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0932 area=(220,118,340,146) fillcolor=(204,204,204) caption="On" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0933 area=(352,118,472,146) fillcolor=(204,204,204) caption="Off" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0925 area=(36,162,220,184) fillcolor=(0,0,0) caption="Rotation sensitivity:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinSlider iid=IGZWinSlider id=0x3D0C0934 area=(220,160,472,182) fillcolor=(0,0,0) winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no minmaxvalue=(10,300) direction=horizontal image={46a006b0,46a006a7} initvalue=100 >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0926 area=(36,202,220,224) fillcolor=(0,0,0) caption="Zoom sensitivity:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinSlider iid=IGZWinSlider id=0x3D0C0935 area=(220,200,472,222) fillcolor=(0,0,0) winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no minmaxvalue=(10,300) direction=horizontal image={46a006b0,46a006a7} initvalue=100 >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0927 area=(36,242,220,264) fillcolor=(0,0,0) caption="Invert vertical rotation:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0937 area=(220,238,340,266) fillcolor=(204,204,204) caption="Off" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0936 area=(352,238,472,266) fillcolor=(204,204,204) caption="On" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0938 area=(220,278,472,306) fillcolor=(204,204,204) caption="Reset Camera Location" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0928 area=(24,324,496,346) fillcolor=(0,0,0) caption="Redraw Aggression" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0940 area=(36,356,136,384) fillcolor=(204,204,204) caption="Classic" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0941 area=(148,356,248,384) fillcolor=(204,204,204) caption="Normal" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0942 area=(260,356,360,384) fillcolor=(204,204,204) caption="High" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0943 area=(372,356,492,384) fillcolor=(204,204,204) caption="Extreme" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=yes shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="Extreme redraw heavily stresses the engine." tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0929 area=(36,396,492,420) fillcolor=(0,0,0) caption="Only affects Modern camera" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=lefttop notify=no wrapped=yes opaque=no forecolor=(96,64,32) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0950 area=(24,438,144,466) fillcolor=(204,204,204) caption="Defaults" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Restore default settings" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0951 area=(150,438,270,466) fillcolor=(204,204,204) caption="Diagnostics" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0952 area=(276,438,396,466) fillcolor=(204,204,204) caption="Changelog" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Read changelog" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0953 area=(402,438,492,466) fillcolor=(204,204,204) caption="Close" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
</CHILDREN>
</LEGACY>
"""
    return script.encode("utf-8")


def build_diagnostics_script_legacy() -> bytes:
    body = escape_ui_caption(
        "Diagnostics\n\n"
        "Debug Logging:\n"
        "Off > Normal > Verbose\n\n"
        "Log Location:\n"
        "Plugins/SC4-ModernCamera/SC4-ModernCamera.log\n\n"
        "A full diagnostics submenu will be wired here next."
    )
    script = f"""# Generated for SC4-ModernCamera's diagnostics window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0906 area=(0,0,460,300) fillcolor=(228,231,238) caption="SC4-ModernCamera Diagnostics" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,6bb93cb5}} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0960 area=(430,10,452,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161f9}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0961 area=(20,4,418,30) fillcolor=(0,0,0) caption="Diagnostics" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0962 area=(24,62,436,220) fillcolor=(0,0,0) caption="{body}" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=lefttop notify=no wrapped=yes opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0963 area=(276,254,436,282) fillcolor=(204,204,204) caption="Close" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
</CHILDREN>
</LEGACY>
"""
    return script.encode("utf-8")


def build_settings_script() -> bytes:
    script = """# Generated for SC4-ModernCamera's camera settings window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0904 area=(0,0,520,480) fillcolor=(228,231,238) caption="SC4-ModernCamera Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,6bb93cb5} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0920 area=(490,10,512,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161f9} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={00000000,ca5c3239} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C092D area=(18,2,50,34) fillcolor=(0,0,0) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes image={3d0c0700,3d0c0908} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={00000000,ca5c3239} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0921 area=(58,4,478,30) fillcolor=(0,0,0) caption="SC4-ModernCamera Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0922 area=(24,48,496,70) fillcolor=(0,0,0) caption="Camera" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0923 area=(36,82,210,104) fillcolor=(0,0,0) caption="Camera mode:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0930 area=(220,78,340,106) fillcolor=(204,204,204) caption="Modern" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0931 area=(352,78,472,106) fillcolor=(204,204,204) caption="Classic" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C092A area=(24,120,496,122) fillcolor=(150,160,180) caption="" winflag_visible=yes winflag_enabled=no winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C092B area=(24,136,496,158) fillcolor=(0,0,0) caption="Modern" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0924 area=(36,174,220,196) fillcolor=(0,0,0) caption="WASD Movement:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0932 area=(220,170,340,198) fillcolor=(204,204,204) caption="On" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0933 area=(352,170,472,198) fillcolor=(204,204,204) caption="Off" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0925 area=(36,214,220,236) fillcolor=(0,0,0) caption="Rotation sensitivity:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinSlider iid=IGZWinSlider id=0x3D0C0934 area=(220,212,472,234) fillcolor=(0,0,0) winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no minmaxvalue=(10,300) direction=horizontal image={46a006b0,46a006a7} initvalue=100 >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0926 area=(36,254,220,276) fillcolor=(0,0,0) caption="Zoom sensitivity:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinSlider iid=IGZWinSlider id=0x3D0C0935 area=(220,252,472,274) fillcolor=(0,0,0) winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no minmaxvalue=(10,300) direction=horizontal image={46a006b0,46a006a7} initvalue=100 >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0927 area=(36,294,220,316) fillcolor=(0,0,0) caption="Invert vertical rotation:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0937 area=(220,290,340,318) fillcolor=(204,204,204) caption="Off" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0936 area=(352,290,472,318) fillcolor=(204,204,204) caption="On" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0938 area=(220,330,472,358) fillcolor=(204,204,204) caption="Reset Camera Location" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0951 area=(220,370,472,398) fillcolor=(204,204,204) caption="Advanced Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C092C area=(24,420,496,422) fillcolor=(150,160,180) caption="" winflag_visible=yes winflag_enabled=no winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0950 area=(24,438,144,466) fillcolor=(204,204,204) caption="Defaults" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Restore default settings" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0952 area=(246,438,386,466) fillcolor=(204,204,204) caption="Show Changelog" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=yes shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Read changelog" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0953 area=(402,438,492,466) fillcolor=(204,204,204) caption="Close" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={46a006b0,144161eb} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=yes shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={ca47efd9,4a5c31d7} >
</CHILDREN>
</LEGACY>
"""
    return apply_choice_button_image(script).encode("utf-8")


def build_diagnostics_script() -> bytes:
    diagnostics_note = escape_ui_caption(
        "Normal logs settings and camera state changes. Verbose adds input and redraw timer traces.\n"
        "Log: Plugins/SC4-ModernCamera/SC4-ModernCamera.log"
    )
    script = f"""# Generated for SC4-ModernCamera's advanced settings window
<LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0906 area=(0,0,520,400) fillcolor=(228,231,238) caption="SC4-ModernCamera Advanced Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,6bb93cb5}} blttype=edge userdata=0 moveable=yes sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no closedisabled=no gobackdisabled=no minmaxdisabled=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(4,4) winflag_enable=no alphablend=no >
<CHILDREN>
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0960 area=(490,10,512,30) fillcolor=(204,204,204) caption="" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161f9}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=no fill=yes autosize=no wrapcaption=no shiftcaption=no tips=yes tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="Close" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{00000000,ca5c3239}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0961 area=(20,4,478,30) fillcolor=(0,0,0) caption="Advanced Settings" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0962 area=(24,48,496,70) fillcolor=(0,0,0) caption="Redraw Aggression" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0940 area=(24,86,124,114) fillcolor=(204,204,204) caption="Classic" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0941 area=(136,86,236,114) fillcolor=(204,204,204) caption="Normal" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0942 area=(248,86,348,114) fillcolor=(204,204,204) caption="High" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0943 area=(360,86,496,114) fillcolor=(204,204,204) caption="Extreme" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=yes shiftcaption=yes tips=yes tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="Extreme redraw heavily stresses the engine." tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0964 area=(24,126,496,154) fillcolor=(0,0,0) caption="Only affects Modern camera" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=lefttop notify=no wrapped=yes opaque=no forecolor=(96,64,32) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinGen iid=IGZWinGen id=0x3D0C0965 area=(24,172,496,174) fillcolor=(150,160,180) caption="" winflag_visible=yes winflag_enabled=no winflag_moveable=no winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes userdata=0 moveable=no sizeable=no defaultkeys=no closevisible=no gobackvisible=no minmaxvisible=no titlebar=no fill=yes outline=no paint=yes sidebar=no gutters=(0,0) winflag_enable=no alphablend=no >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0966 area=(24,192,496,214) fillcolor=(0,0,0) caption="Diagnostics" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenHeader align=lefttop notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C0967 area=(36,224,160,246) fillcolor=(0,0,0) caption="Debug Logging:" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=leftcenter notify=no wrapped=no opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0968 area=(172,220,252,248) fillcolor=(204,204,204) caption="Off" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0969 area=(264,220,364,248) fillcolor=(204,204,204) caption="Normal" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C096A area=(376,220,496,248) fillcolor=(204,204,204) caption="Verbose" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=on triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=yes shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=toggle gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
   <LEGACY clsid=GZWinText iid=IGZWinText id=0x3D0C096B area=(24,268,496,330) fillcolor=(0,0,0) caption="{diagnostics_note}" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=no winflag_pbufftrans=yes winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=no winflag_acceptfocus=no winflag_mousetrans=yes winflag_ignoremouse=yes font=GenBodyMedium align=lefttop notify=no wrapped=yes opaque=no forecolor=(32,40,80) bkgcolor=(0,0,0) gutters=(2,2) >
   <LEGACY clsid=GZWinBtn iid=IGZWinBtn id=0x3D0C0963 area=(402,356,492,384) fillcolor=(204,204,204) caption="Close" winflag_visible=yes winflag_enabled=yes winflag_moveable=yes winflag_sizeable=no winflag_sortable=no winflag_pbuff=yes winflag_pbufftrans=no winflag_pbufferase=yes winflag_pbuffvid=no winflag_alphablend=yes winflag_acceptfocus=yes winflag_mousetrans=no winflag_ignoremouse=no image={{46a006b0,144161eb}} font=GenButton colorfontnormal=(63,73,103) colorfontdisabled=(102,102,102) colorfonthilited=(63,73,103) toggle=off triggerondown=off showcaption=yes fill=yes autosize=no wrapcaption=no shiftcaption=yes tips=no tipsdelay=no tipstimeout=no style=standard gutters=(0,0,0,0) tiptext="" tipoffsets=(0,0) tipflag=0x01000000 align=center btnclicksnd={{ca47efd9,4a5c31d7}} >
</CHILDREN>
</LEGACY>
"""
    return apply_choice_button_image(script).encode("utf-8")


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
        resources.append((UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, SETTINGS_INSTANCE_ID, build_settings_script()))
        resources.append((UI_SCRIPT_TYPE_ID, PLUGIN_GROUP_ID, DIAGNOSTICS_INSTANCE_ID, build_diagnostics_script()))

    menu_icon_source = source.with_name("moderncamera-menu-icon.png")
    if menu_icon_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                MENU_ICON_INSTANCE_ID,
                build_fsh_dxt3_from_png(menu_icon_source),
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
                build_button_strip_fsh_from_png(cam_icon_source, b"MODCMIC2"),
            )
        )

    camera_point_source = source.with_name("camera_point.png")
    if camera_point_source.exists():
        resources.append(
            (
                UI_IMAGE_TYPE_ID,
                PLUGIN_GROUP_ID,
                CAMERA_POINT_INSTANCE_ID,
                build_button_strip_fsh_from_png(camera_point_source, b"MODCMARW"),
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
    build(Path(sys.argv[1]), Path(sys.argv[2]))
