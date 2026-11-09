"""Build the ARM64 /dev/uinput acceptance helper with the pinned local NDK."""
import struct
import subprocess
from pathlib import Path

from android_tools import CLANG, TOOLCHAIN

ROOT = Path(__file__).resolve().parents[1]


def build(name):
    output = ROOT / "build/tools" / name
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(CLANG), "--target=aarch64-linux-android24", "-static", "-O2", "-std=gnu11",
        "-Wall", "-Wextra", "-Werror", f"tools/{name}.c", "-o", str(output),
    ], cwd=ROOT, check=True)
    data = bytearray(output.read_bytes())
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("The helper is not a little-endian ELF64 image")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 183:
        raise ValueError(f"Expected AArch64, got e_machine={machine}")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 54)
    tls = []
    for index in range(phnum):
        at = phoff + index * phentsize
        if struct.unpack_from("<I", data, at)[0] == 7:
            tls.append(at)
    if len(tls) != 1:
        raise ValueError(f"Expected one PT_TLS segment, got {len(tls)}")
    if struct.unpack_from("<Q", data, tls[0] + 48)[0] == 8:
        struct.pack_into("<Q", data, tls[0] + 48, 64)
        output.write_bytes(data)
    if struct.unpack_from("<Q", data, tls[0] + 48)[0] != 64:
        raise ValueError("Unexpected PT_TLS alignment")
    subprocess.run([str(TOOLCHAIN / "bin/llvm-readelf.exe"), "-h", "-l", str(output)], check=True)
    print(output.relative_to(ROOT))


def main():
    build("virtual_hid")
    build("virtual_touch")


if __name__ == "__main__":
    main()
