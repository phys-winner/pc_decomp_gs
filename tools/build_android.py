"""Compile the lifted game and native host for ARM32/ARM64 Android."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from android_tools import CLANG

ROOT = Path(__file__).resolve().parents[1]
API = 24
TARGET = f"aarch64-linux-android{API}"
ARM32_FLAGS = ["-march=armv7-a", "-mthumb", "-mfpu=neon", "-mfloat-abi=softfp"]
COMMON = [
    "--target=" + TARGET,
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fvisibility=hidden",
    "-fPIC",
    "-ffp-contract=off",
    "-fno-math-errno",
    "-ffunction-sections",
    "-fdata-sections",
    "-g0",
    "-DRECOMP_X87_WINDOW",
    "-ffile-prefix-map=" + str(ROOT) + "=.",
]
GENERATED_FLAGS = COMMON + [
    "-O2", "-w", "-DRECOMP_GENERATED_CODE", "-Iapp/native/runtime",
    "-DRECOMP_LOCAL_REGS", "-DRECOMP_PLAIN_MEMORY",
    "-DRECOMP_GUEST_STATE_SEPARATE", "-mllvm", "-basic-aa-separate-storage",
    "-DRECOMP_GUEST_PROVENANCE", "-DRECOMP_CALL_DIAG",
]
HOST_FLAGS = COMMON + [
    "-O2", "-std=gnu11", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
    "-DRECOMP_TRACE", "-DHOMM5_LEGACY_UI", "-Iapp/native/runtime", "-Iapp/native/host",
]
MOJO_DIR = "reference/mojoshader"
MOJO_SOURCES = [
    "mojoshader.c", "mojoshader_common.c", "mojoshader_assembler.c",
    "mojoshader_preprocessor.c", "mojoshader_lexer.c", "mojoshader_effects.c",
    "profiles/mojoshader_profile_common.c", "profiles/mojoshader_profile_glsl.c",
    "profiles/mojoshader_profile_d3d.c", "profiles/mojoshader_profile_bytecode.c",
]
MOJO_FLAGS = COMMON + [
    "-O2", "-w", "-DMOJOSHADER_EFFECT_SUPPORT", "-DMOJOSHADER_FLIP_RENDERTARGET", "-DMOJOSHADER_DEPTH_CLIPPING",
    "-DSUPPORT_PROFILE_HLSL=0", "-DSUPPORT_PROFILE_ARB1=0", "-DSUPPORT_PROFILE_ARB1_NV=0",
    "-DSUPPORT_PROFILE_METAL=0", "-DSUPPORT_PROFILE_SPIRV=0", "-DSUPPORT_PROFILE_GLSPIRV=0",
    "-DSUPPORT_PROFILE_GLSL120=0", "-I" + MOJO_DIR, "-Iapp/native/runtime",
]


def sha_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def generated_sources():
    directory = ROOT / "generated/game-x86"
    report = json.loads((directory / "report.json").read_text(encoding="utf-8"))
    if report.get("status") != "GENERATED":
        raise ValueError("Run tools/lift_griefsyndrome.py first")
    gso_directory = ROOT / "generated/gso-x86"
    gso_report = json.loads((gso_directory / "report.json").read_text(encoding="utf-8"))
    if gso_report.get("status") != "GENERATED":
        raise ValueError("Run tools/lift_gso.py first")
    sources = [(directory / name, GENERATED_FLAGS) for name in report["sources"]]
    sources += [(gso_directory / name, GENERATED_FLAGS) for name in gso_report["sources"]]
    sources += [(gso_directory / "gso_image.c", GENERATED_FLAGS),
                (ROOT / "generated/recomp_dispatch.c", GENERATED_FLAGS)]
    return sources


def host_sources(shared):
    flags = HOST_FLAGS + (["-DVOID_ANDROID_APP"] if shared else [])
    return [(path, flags) for path in sorted((ROOT / "app/native/host").glob("*.c"))]


def mojo_sources():
    return [(ROOT / MOJO_DIR / name, MOJO_FLAGS) for name in MOJO_SOURCES]


def main():
    global TARGET, COMMON, GENERATED_FLAGS, HOST_FLAGS, MOJO_FLAGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shared", action="store_true")
    parser.add_argument("--host-only", action="store_true")
    parser.add_argument("--abi", choices=("arm64-v8a", "armeabi-v7a"), default="arm64-v8a")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 32:
        parser.error("--jobs must be in 1..32")
    if not CLANG.is_file():
        raise ValueError(f"Pinned NDK clang is missing: {CLANG}")
    script_include = ROOT / "generated/port_scripts.inc"
    script_include.write_text("\n".join(
        f"static const char {p.stem}_script[] = " + json.dumps(p.read_text(encoding="utf-8"), ensure_ascii=True) + ";"
        for p in sorted((ROOT / "app/native/scripts").glob("*.nut"))) + "\n", encoding="utf-8")
    TARGET = ("aarch64" if args.abi == "arm64-v8a" else "armv7a") + f"-linux-androideabi{API}"
    architecture = [] if args.abi == "arm64-v8a" else ARM32_FLAGS
    for flags in (COMMON, GENERATED_FLAGS, HOST_FLAGS, MOJO_FLAGS):
        flags[0] = "--target=" + TARGET
        flags.extend(architecture)
    out = ROOT / (f"build/{args.abi}-shared" if args.shared else f"build/{args.abi}-shell")
    objects_dir = out / "objects"
    objects_dir.mkdir(parents=True, exist_ok=True)
    generated = [] if args.host_only else generated_sources()
    jobs = generated + host_sources(args.shared) + mojo_sources()
    header_paths = list((ROOT / "app/native").rglob("*.h"))
    header_paths += list((ROOT / "reference/mojoshader").rglob("*.h"))
    header_paths += list((ROOT / "reference/dlmalloc").rglob("*.c"))
    header_hasher = hashlib.sha256()
    for path in sorted(header_paths):
        header_hasher.update(str(path.relative_to(ROOT)).encode())
        header_hasher.update(path.read_bytes())
    compiler_hash = sha_file(CLANG)
    header_hash = header_hasher.hexdigest()

    def object_for(source, flags):
        relative = source.relative_to(ROOT)
        hasher = hashlib.sha256()
        hasher.update(str(relative).encode())
        hasher.update(source.read_bytes())
        if source.name == "grief_script.c":
            hasher.update(script_include.read_bytes())
        if source.name == "grief_tv.c":
            hasher.update((source.parent / "grief_tv_kernel.inc").read_bytes())
        hasher.update(header_hash.encode())
        hasher.update(compiler_hash.encode())
        hasher.update("\0".join(flags).encode())
        digest = hasher.hexdigest()
        stem = str(relative).replace("/", "_").replace("\\", "_")
        return objects_dir / f"{stem}-{digest[:24]}.o", digest

    def compile_one(job):
        source, flags = job
        obj, digest = object_for(source, flags)
        receipt = obj.with_suffix(".json")
        if obj.exists() and receipt.exists() and json.loads(receipt.read_text()).get("fingerprint") == digest:
            return obj, True
        started = time.monotonic()
        command = [str(CLANG), *flags, "-c", str(source), "-o", str(obj)]
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"Compilation failed for {source.relative_to(ROOT)}:\n{result.stdout}")
        receipt.write_text(json.dumps({"fingerprint": digest, "seconds": time.monotonic() - started}) + "\n")
        prefix = obj.name[:-(24 + 3)]
        for stale in objects_dir.glob(prefix + "*.o"):
            if stale != obj:
                stale.unlink()
                stale.with_suffix(".json").unlink(missing_ok=True)
        print(f"Compiled {source.relative_to(ROOT)} in {time.monotonic() - started:.1f}s", flush=True)
        return obj, False

    started = time.monotonic()
    objects = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, job) for job in jobs]
        for future in as_completed(futures):
            objects.append(future.result()[0])
    if args.host_only:
        for source, flags in generated_sources():
            objects.append(object_for(source, flags)[0])
    missing = [str(path) for path in objects if not path.is_file()]
    if missing:
        raise ValueError("Missing cached objects: " + ", ".join(missing[:10]))
    output = out / ("libgriefsyndrome.so" if args.shared else "griefsyndrome-" + ("arm64" if args.abi == "arm64-v8a" else "arm32"))
    response = out / "objects.rsp"
    response.write_text("\n".join('"' + str(path).replace("\\", "/") + '"' for path in objects) + "\n")
    command = [str(CLANG), "--target=" + TARGET, *architecture, "-o", str(output), "@" + str(response)]
    if args.shared:
        command += ["-shared", "-Wl,-soname,libgriefsyndrome.so"]
    else:
        command += ["-pie"]
    command += [
        "-Wl,-z,max-page-size=16384", "-Wl,-z,common-page-size=16384",
        "-Wl,--gc-sections", "-Wl,-z,nostart-stop-gc", "-Wl,--no-undefined", "-Wl,--build-id=sha1",
        "-landroid", "-lEGL", "-lGLESv3", "-llog", "-lz", "-lm", "-ldl", "-pthread",
    ]
    print("Linking", output, flush=True)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError("Link failed:\n" + result.stdout)
    receipt = {
        "output": str(output.relative_to(ROOT)),
        "sha256": sha_file(output),
        "target": TARGET,
        "shared": args.shared,
        "objects": len(objects),
        "seconds": time.monotonic() - started,
    }
    (out / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == "__main__":
    main()
