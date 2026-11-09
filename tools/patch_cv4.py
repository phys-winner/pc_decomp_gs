"""Patch Title's serialized selector and emit its native ARM64 data image."""
import hashlib
import json
from pathlib import Path
import struct

from decompile_cv4 import parse, flatten

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "work/evidence/closures/closure_031.cv4"
PATCHED = ROOT / "work/reference-config/title-patched.cv4"
OUTPUT = ROOT / "generated/title-patched.c"


def u32(value):
    return struct.pack("<I", value)


def write_object(value):
    if value is None:
        return u32(0x01000001)
    if isinstance(value, bool):
        return u32(0x08000001) + struct.pack("<i", int(value))
    if isinstance(value, int):
        return u32(0x05000002) + struct.pack("<i", value)
    if isinstance(value, float):
        return u32(0x05000004) + struct.pack("<f", value)
    if isinstance(value, str):
        encoded = value.encode("utf-8")
        return u32(0x08000010) + u32(len(encoded)) + encoded
    raise TypeError(type(value))


def write_function(function):
    result = [u32(0x50415254), write_object(function["source"]), write_object(function["name"]),
              u32(0x50415254)]
    result.append(u32(len(function["literals"])))
    result.append(u32(len(function["parameters"])))
    result.append(u32(len(function["outers"])))
    result.append(u32(len(function["locals"])))
    result.append(u32(len(function["lines"])))
    result.append(u32(len(function["defaults"])))
    result.append(u32(len(function["instructions"])))
    result.append(u32(len(function["children"])))
    result.append(u32(0x50415254))
    result.extend(write_object(value) for value in function["literals"])
    result.append(u32(0x50415254))
    result.extend(write_object(value) for value in function["parameters"])
    result.append(u32(0x50415254))
    for outer, name, value in function["outers"]:
        result += [u32(outer), write_object(name), write_object(value)]
    result.append(u32(0x50415254))
    for name, start, end, value in function["locals"]:
        result += [write_object(name), u32(start), u32(end), u32(value)]
    result.append(u32(0x50415254))
    for line, instruction in function["lines"]:
        result.append(struct.pack("<II", line, instruction))
    result.append(u32(0x50415254))
    for value in function["defaults"]:
        result.append(struct.pack("<i", value))
    result.append(u32(0x50415254))
    for instruction in function["instructions"]:
        result.append(struct.pack("<iBBBB", *instruction))
    result.append(u32(0x50415254))
    result.extend(write_function(child) for child in function["children"])
    trailer = function["trailer"]
    result += [u32(trailer[0]), bytes(trailer[1:])]
    return b"".join(result)


def emit(function):
    return b"\xfa\xfa" + u32(0x53514952) + u32(1) + write_function(function) + u32(0x5441494C)


def ins(arg1, opcode, arg0=0, arg2=0, arg3=0):
    return (arg1, opcode, arg0, arg2, arg3)


def append_item(image, index, selected):
    return [
        ins(3, 31, 3),
        ins(image, 9, 4), ins(4, 32, 3),
        ins(index, 2, 4), ins(4, 32, 3),
        ins(selected, 9, 4), ins(4, 32, 3),
        ins(3, 32, 2),
    ]


def insert_preserving_jumps(function, offset, instructions):
    original = function["instructions"]
    for index, instruction in enumerate(original):
        arg1, opcode, arg0, arg2, arg3 = instruction
        branch = opcode in (24, 25, 26, 49, 50) or (opcode in (43, 44) and arg3 == 0)
        if branch and index < offset <= index + arg1:
            original[index] = (arg1 + len(instructions), opcode, arg0, arg2, arg3)
    function["instructions"][offset:offset] = instructions


def main():
    root = parse(SOURCE)
    functions = {function["path"]: function for function in flatten(root)}
    common = functions["main.data/system/Component/common.nut"]
    assert common["instructions"][63:70] == [
        ins(5, 9, 4), ins(4, 32, 3), ins(0, 2, 4), ins(4, 32, 3),
        ins(6, 9, 4), ins(4, 32, 3), ins(3, 32, 2)]
    assert common["instructions"][22][1] == 31
    assert common["instructions"][70][1] == 31
    items = append_item(6, 1, 6) + append_item(8, 3, 8)
    insert_preserving_jumps(common, 22, items)
    assert common["instructions"][25 + len(items)] == ins(2, 2, 4)
    assert common["instructions"][73 + len(items)] == ins(1, 2, 4)
    assert common["instructions"][81 + len(items)] == ins(2, 2, 4)
    common["instructions"][25 + len(items)] = ins(4, 2, 4)
    common["instructions"][73 + len(items)] = ins(3, 2, 4)
    common["instructions"][81 + len(items)] = ins(4, 2, 4)
    insert_preserving_jumps(common, 70 + len(items), items)

    activation = functions["main.CreateAct"]
    activation["literals"].append("menu_exitS")
    exit_selected = len(activation["literals"]) - 1
    end = len(activation["instructions"]) - 1
    offset = 329
    assert activation["instructions"][offset:offset + 3] == [
        ins(24, 9, 2), ins(2, 9, 3), ins(4, 9, 3, 3)]
    assert activation["instructions"][end] == ins(0, 19, 255)
    special = [
        ins(24, 9, 2), ins(2, 9, 3), ins(4, 9, 3, 3),
        ins(2, 14, 2, 3), ins(0, 2, 3), ins(2, 14, 2, 3),
        ins(6, 9, 3), ins(2, 15, 3, 2), ins(4, 26, 3),
        ins(6, 8, 3, 0, 4), ins(90, 2, 5), ins(3, 6, 3, 4, 2),
        ins(end - offset - 13, 24),
        ins(exit_selected, 9, 3), ins(2, 15, 3, 2), ins(4, 26, 3),
        ins(6, 8, 3, 0, 4), ins(91, 2, 5), ins(3, 6, 3, 4, 2),
        ins(end - offset - 20, 24),
    ]
    insert_preserving_jumps(activation, offset, special)

    raw = emit(root)
    PATCHED.parent.mkdir(parents=True, exist_ok=True)
    PATCHED.write_bytes(raw)
    check = parse(PATCHED)
    counts = {function["path"]: len(function["instructions"]) for function in flatten(check)}
    if counts["main.data/system/Component/common.nut"] != 166 + 2 * len(items):
        raise ValueError("Unexpected Title common size")
    if counts["main.CreateAct"] != end + 1 + len(special):
        raise ValueError("Unexpected Title activation size")
    lines = ["#include <stdint.h>", "const uint8_t void_title_patch[] = {"]
    for offset in range(0, len(raw), 16):
        lines.append("    " + ",".join(f"0x{value:02X}" for value in raw[offset:offset + 16]) + ",")
    lines += ["};", f"const uint32_t void_title_patch_size = {len(raw)}u;", ""]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw),
                      "activation_instructions": counts["main.CreateAct"]}, indent=2))


if __name__ == "__main__":
    main()
