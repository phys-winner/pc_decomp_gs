"""Compile the production-code regressions and run both ABIs on the selected device."""
import argparse
from pathlib import Path
import subprocess
from android_tools import ADB, CLANG
from build_android import ARM32_FLAGS

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError("Command failed:\n" + " ".join(map(str, command)) + "\n" + result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial")
    args = parser.parse_args()
    out = ROOT / "build/tests"
    out.mkdir(parents=True, exist_ok=True)
    supported = run([ADB, "-s", args.serial, "shell", "getprop", "ro.product.cpu.abilist"])
    title_bg = ROOT / "app/artwork/widescreen/TitleBG.original.act"
    remote_title_bg = "/data/local/tmp/grief-titlebg-original.act"
    run([ADB, "-s", args.serial, "push", title_bg, remote_title_bg])
    for abi, target in (("arm64-v8a", "aarch64-linux-android24"), ("armeabi-v7a", "armv7a-linux-androideabi24")):
        for name in ("touch_menu_selftest", "lan_menu_selftest", "graphics_selftest", "camera_selftest", "memcpy_selftest", "config_qa_selftest", "widescreen_selftest", "resource_lifetime_selftest", "jni_resource_selftest", "present_resources_selftest", "resource_gl_selftest", "fx_stateblock_selftest", "display_size_selftest", "heap_unmap_selftest"):
            executable = out / (name + "-" + abi)
            architecture = ARM32_FLAGS if abi == "armeabi-v7a" else []
            print(run([CLANG, "--target=" + target, *architecture, "-O2", "-std=gnu11", "-Wall",
                       "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-label", "-DHOMM5_LEGACY_UI",
                       "-ffunction-sections", "-fdata-sections", "-Iapp/native/host", "-Iapp/native/runtime",
                       "tools/" + name + ".c", "-Wl,--gc-sections", "-lm", "-o", executable]), end="")
            if abi not in supported:
                print(abi + ": compiled, device does not support this ABI")
                continue
            remote = "/data/local/tmp/" + executable.name
            run([ADB, "-s", args.serial, "push", executable, remote])
            run([ADB, "-s", args.serial, "shell", "chmod", "755", remote])
            fixture = [remote_title_bg] if name == "widescreen_selftest" else []
            print(abi + ": " + run([ADB, "-s", args.serial, "shell", remote, *fixture]).strip())


if __name__ == "__main__":
    main()
