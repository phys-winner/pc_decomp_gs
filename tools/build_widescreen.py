"""Pack generated panorama PNGs into an original-format external GS/CV2 archive."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
IMAGES = ("title", "stage_select", "clear1", "clear2", "clear3", "clear4", "clear5")
PANORAMAS = tuple("data/widescreen/" + name + ".cv2" for name in IMAGES)
OVERRIDES = (
    ("title", "data/system/Title/title_back.cv2"),
    ("title", "data/system/Title/title_back_B.cv2"),
    ("stage_select", "data/system/StageSelect/back.cv2"),
    ("clear1", "data/actor/boss/clear1.dds"),
    ("clear2", "data/actor/boss2/clear2.dds"),
    ("clear3", "data/actor/boss3/clear3.dds"),
    ("clear4", "data/actor/boss4/clear4.dds"),
    ("clear5", "data/actor/boss5/clear5.dds"),
)
TITLE_BG_NAME = "data/system/Title/TitleBG.act"
TITLE_BG_SOURCE = ROOT / "app/artwork/widescreen/TitleBG.original.act"
TITLE_BG_SHA256 = "d79a2b7bdb28ab248b5cfdbdd81d08cd1cbebc2ae433491e4e7c42fe582bb6d4"
TITLE_BG_ALPHA_OFFSET = 1208
NAMES = PANORAMAS + tuple(name for _, name in OVERRIDES) + (TITLE_BG_NAME,)
IMAGE_MAX = 64 << 20


def mt19937(seed):
    state = [seed & 0xFFFFFFFF]
    for i in range(1, 624):
        state.append((1812433253 * (state[-1] ^ (state[-1] >> 30)) + i) & 0xFFFFFFFF)
    while True:
        for i in range(624):
            y = (state[i] & 0x80000000) | (state[(i + 1) % 624] & 0x7FFFFFFF)
            state[i] = state[(i + 397) % 624] ^ (y >> 1) ^ (0x9908B0DF if y & 1 else 0)
        for value in state:
            value ^= value >> 11
            value ^= (value << 7) & 0x9D2C5680
            value ^= (value << 15) & 0xEFC60000
            yield value ^ (value >> 18)


def crypt_index(data):
    result = bytearray(data)
    random = mt19937(len(result) + 6)
    key, delta = 0xC5, 0x89
    for i in range(len(result)):
        result[i] ^= (next(random) & 255) ^ key
        key = (key + delta) & 255
        delta = (delta + 0x49) & 255
    return result


def cv2_info(data):
    if len(data) < 17:
        raise ValueError("Truncated CV2 header")
    bits, width, height, pitch, reserved = struct.unpack_from("<B4I", data)
    if bits != 32 or not 0 < width <= 8192 or not 0 < height <= 4096 or pitch != width or reserved:
        raise ValueError("Invalid panorama CV2 dimensions")
    if len(data) != 17 + width * height * 4 or len(data) > IMAGE_MAX:
        raise ValueError("Invalid panorama CV2 pixel length")
    return width, height


def dds_header():
    # Standard uncompressed A8R8G8B8, one 800x600 level, top-down BGRA payload.
    # The old engine checks FourCC even without DDPF_FOURCC; D3DX correctly uses
    # the RGB flags and masks. Keep its unused compatibility tag without compression.
    header = bytearray(128)
    header[:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x100F, 600, 800, 3200, 0, 1)
    struct.pack_into("<8I", header, 76, 32, 0x41, 0x35545844, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)
    struct.pack_into("<I", header, 108, 0x1000)
    return bytes(header)


def image_info(name, data):
    if name == TITLE_BG_NAME:
        if data != title_background_patch():
            raise ValueError("Invalid panorama title background metadata")
        return None
    if name.endswith(".dds"):
        if len(data) != 128 + 800 * 600 * 4 or data[:128] != dds_header():
            raise ValueError("Invalid panorama center DDS")
        return 800, 600
    dimensions = cv2_info(data)
    if name not in PANORAMAS and dimensions != (800, 600):
        raise ValueError("Invalid panorama center CV2 dimensions")
    return dimensions


def title_background_patch():
    original = TITLE_BG_SOURCE.read_bytes()
    if len(original) != 5534 or hashlib.sha256(original).hexdigest() != TITLE_BG_SHA256:
        raise ValueError("Unexpected original TitleBG ACT metadata")
    at = TITLE_BG_ALPHA_OFFSET
    if original[at:at + 4] != struct.pack("<f", 0.05):
        raise ValueError("Unexpected TitleBG layer 2 subtractive alpha")
    return original[:at] + struct.pack("<f", 0.0) + original[at + 4:]


def pack(images):
    if set(images) != set(NAMES):
        raise ValueError("The panorama archive requires exactly sixteen known entries")
    index_size = sum(9 + len(name.encode("ascii")) for name in NAMES)
    offset = index_size + 6
    index, payload = bytearray(), bytearray()
    for name in NAMES:
        data = images[name]
        image_info(name, data)
        encoded = name.encode("ascii")
        index += struct.pack("<IIB", offset, len(data), len(encoded)) + encoded
        key = ((offset >> 1) | 0x23) & 255
        payload += bytes(value ^ key for value in data)
        offset += len(data)
    return struct.pack("<HI", len(NAMES), index_size) + crypt_index(index) + payload


def unpack(data):
    if len(data) < 6:
        raise ValueError("Truncated GS header")
    count, index_size = struct.unpack_from("<HI", data)
    if count != len(NAMES) or index_size > 4096 or index_size > len(data) - 6:
        raise ValueError("Invalid panorama GS index")
    index = crypt_index(data[6:6 + index_size])
    at, images, ranges = 0, {}, []
    for _ in range(count):
        if at + 9 > len(index):
            raise ValueError("Truncated GS index record")
        offset, size, length = struct.unpack_from("<IIB", index, at)
        at += 9
        if length > len(index) - at:
            raise ValueError("Truncated GS path")
        name = index[at:at + length].decode("ascii")
        at += length
        if name not in NAMES or name in images or offset < 6 + index_size or size > IMAGE_MAX or offset + size > len(data):
            raise ValueError("Invalid panorama GS record")
        if any(offset < end and begin < offset + size for begin, end in ranges):
            raise ValueError("Overlapping panorama GS entries")
        ranges.append((offset, offset + size))
        key = ((offset >> 1) | 0x23) & 255
        decoded = bytes(value ^ key for value in data[offset:offset + size])
        image_info(name, decoded)
        images[name] = decoded
    if at != len(index):
        raise ValueError("Trailing GS index data")
    return images


def build(source=None, output=None):
    from PIL import Image
    source = Path(source or ROOT / "app/artwork/widescreen")
    output = Path(output or ROOT / "pub/gs_widescreen.dat")
    images, records = {}, []
    for stem, name in zip(IMAGES, PANORAMAS):
        path = source / (stem + ".png")
        with Image.open(path) as image:
            source_width, source_height = image.size
            if not 0 < source_width <= 8192 or not 0 < source_height <= 4096:
                raise ValueError(f"Panorama dimensions exceed the runtime limits: {path}")
            width, height = max(800, 2 * round(source_width * 600 / source_height / 2)), 600
            if width > 8192 or 17 + width * height * 4 > IMAGE_MAX:
                raise ValueError(f"Normalized panorama exceeds the runtime limits: {path}")
            # Normalize once to the engine's pixel grid, then use the identical center pixels.
            # This is an asset-format conversion; generated source PNGs are unchanged.
            panorama = image.convert("RGBA").resize((width, height), Image.Resampling.BILINEAR)
            data = struct.pack("<B4I", 32, width, height, width, 0) + panorama.tobytes("raw", "BGRA")
            center = panorama.crop(((width - 800) // 2, 0, (width + 800) // 2, 600))
            center_bgra = center.tobytes("raw", "BGRA")
            for source_stem, target in OVERRIDES:
                if source_stem == stem:
                    images[target] = (dds_header() if target.endswith(".dds") else
                        struct.pack("<B4I", 32, 800, 600, 800, 0)) + center_bgra
        images[name] = data
        records.append({"name": name, "source": str(path), "width": width, "height": height,
                        "source_width": source_width, "source_height": source_height,
                        "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "cv2_sha256": hashlib.sha256(data).hexdigest()})
    images[TITLE_BG_NAME] = title_background_patch()
    records.append({"name": TITLE_BG_NAME, "source": str(TITLE_BG_SOURCE),
                    "source_sha256": TITLE_BG_SHA256, "bytes": len(images[TITLE_BG_NAME]),
                    "patch": {"offset": TITLE_BG_ALPHA_OFFSET, "old_hex": "cdcc4c3d", "new_hex": "00000000"}})
    archive = pack(images)
    if unpack(archive) != images:
        raise ValueError("Panorama GS/CV2 round-trip failed")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_bytes() != archive:
        temporary = output.with_suffix(output.suffix + ".part")
        temporary.write_bytes(archive)
        temporary.replace(output)
    digest = hashlib.sha256(archive).hexdigest()
    output.with_suffix(output.suffix + ".sha256").write_text(digest + "  " + output.name + "\n", encoding="utf-8")
    output.with_suffix(output.suffix + ".json").write_text(json.dumps({
        "format": "original-gs-dat/raw-cv2-bgra32-dds-argb8888-act", "bytes": len(archive), "sha256": digest, "images": records
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {output.name}: 7 panoramas + 8 matching center overrides + title dimmer metadata, {len(archive)} bytes, SHA-256 {digest}")
    return output


def selftest():
    random = mt19937(5489)
    assert [next(random) for _ in range(3)] == [3499211612, 581869302, 3890346734]
    cv2 = struct.pack("<B4I", 32, 2, 1, 2, 0) + bytes.fromhex("01 02 03 ff 10 20 30 80")
    images = {name: cv2 for name in PANORAMAS}
    center = bytes(800 * 600 * 4)
    for _, name in OVERRIDES:
        images[name] = (dds_header() if name.endswith(".dds") else struct.pack("<B4I", 32, 800, 600, 800, 0)) + center
    images[TITLE_BG_NAME] = title_background_patch()
    data = pack(images)
    assert unpack(data) == images
    for invalid in (data[:5], data[:8], data[:-1], b"\x00\x00" + data[2:]):
        try:
            unpack(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed GS archive was accepted")
    corrupted = bytearray(images[TITLE_BG_NAME])
    corrupted[1000] ^= 1
    try:
        image_info(TITLE_BG_NAME, corrupted)
    except ValueError:
        pass
    else:
        raise AssertionError("Corrupt ACT metadata was accepted")
    assert len(NAMES) == 16 and sum(a != b for a, b in zip(TITLE_BG_SOURCE.read_bytes(), images[TITLE_BG_NAME])) == 4
    print("PASS: standard MT19937, original GS index/payload XOR, seven panoramas/eight CV2/DDS overrides, exact four-byte ACT patch and malformed-input rejection")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
    else:
        build(args.input, args.output)


if __name__ == "__main__":
    main()
