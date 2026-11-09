"""Build, align, sign and inspect the ARM32/ARM64 APK without Gradle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

from android_tools import (ADB, ANDROID_JAR, APKSIGNER, BUILD_TOOLS, EXPECTED_CERT_SHA256,
                           JAVA_HOME, SIGNER_CERT, SIGNER_KEY, TOOLCHAIN)

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "org.griefsyndrome.port"
MIN_API = 24
TARGET_API = 37


def run(command, cwd=ROOT, env=None):
    result = subprocess.run([str(part) for part in command], cwd=cwd, env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError("Command failed:\n" + " ".join(map(str, command)) + "\n" + result.stdout)
    return result.stdout


def sha256(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(8 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def check_native(path):
    readelf = TOOLCHAIN / "bin/llvm-readelf.exe"
    if not readelf.is_file():
        raise ValueError(f"Required ELF inspector is missing: {readelf}")
    headers = run([readelf, "-lW", path])
    for line in headers.splitlines():
        fields = line.split()
        if fields and fields[0] == "TLS":
            raise ValueError(f"ELF TLS is not supported with minSdk {MIN_API}: {path}")
        if fields and fields[0] == "LOAD":
            alignment = int(fields[-1], 16)
            if alignment < 16384 or alignment % 16384:
                raise ValueError(f"ELF LOAD alignment is not 16 KiB compatible: {path}: {line}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-native", action="store_true")
    parser.add_argument("--arm64-only", action="store_true")
    args = parser.parse_args()
    out = ROOT / "build/apk"
    out.mkdir(parents=True, exist_ok=True)
    pub = ROOT / "pub"
    pub.mkdir(parents=True, exist_ok=True)
    aapt2 = BUILD_TOOLS / "aapt2.exe"
    zipalign = BUILD_TOOLS / "zipalign.exe"
    d8 = BUILD_TOOLS / "d8.bat"
    java = JAVA_HOME / "bin/java.exe"
    jar = JAVA_HOME / "bin/jar.exe"
    javac = JAVA_HOME / "bin/javac.exe"
    for path in (aapt2, zipalign, d8, java, jar, javac, ANDROID_JAR, APKSIGNER, SIGNER_CERT, SIGNER_KEY):
        if not path.is_file():
            raise ValueError(f"Required build input is missing: {path}")
    if not args.skip_native:
        run(["python", "-X", "utf8", "tools/build_android.py", "--shared", "--jobs", "4"])
        if not args.arm64_only:
            run(["python", "-X", "utf8", "tools/build_android.py", "--shared", "--abi", "armeabi-v7a", "--jobs", "4"])
    natives = {"arm64-v8a": ROOT / "build/arm64-v8a-shared/libgriefsyndrome.so"}
    if not args.arm64_only:
        natives["armeabi-v7a"] = ROOT / "build/armeabi-v7a-shared/libgriefsyndrome.so"
    for abi, native in natives.items():
        if not native.is_file():
            raise ValueError(f"{abi} library is missing: {native}")
        check_native(native)
    native = natives["arm64-v8a"]

    from grief_launcher import LAUNCHER, prepare as prepare_launcher
    run(["python", "-X", "utf8", str(LAUNCHER / "tools/icons.py"), "check", "app/res"])
    run(["python", "-X", "utf8", "tools/embed_assets.py"])
    launcher = prepare_launcher(out / "launcher")
    compiled = out / "resources.zip"
    run([aapt2, "compile", "--dir", "app/res", "-o", compiled])
    generated = out / "generated"
    generated.mkdir(exist_ok=True)
    base = out / "base.apk"
    run([aapt2, "link", "-I", ANDROID_JAR, "--manifest", launcher["manifest"],
         "--java", generated, "--min-sdk-version", str(MIN_API),
         "--target-sdk-version", str(TARGET_API), "-o", base,
         compiled])
    with zipfile.ZipFile(base, "a") as apk:
        launcher_assets = Path(launcher["assets"])
        for path in sorted(launcher_assets.rglob("*")):
            if path.is_file():
                apk.write(path, "assets/" + path.relative_to(launcher_assets).as_posix(), compress_type=zipfile.ZIP_DEFLATED)

    classes = out / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir()
    sources = sorted((ROOT / "app/src").rglob("*.java")) + [Path(path) for path in launcher["sources"]]
    run([javac, "-encoding", "UTF-8", "-source", "8", "-target", "8",
         "-classpath", ANDROID_JAR, "-d", classes, *sources])
    classes_jar = out / "classes.jar"
    run([jar, "--create", "--file", classes_jar, "-C", classes, "."])
    env = os.environ.copy()
    env["JAVA_HOME"] = str(JAVA_HOME)
    run([os.environ.get("COMSPEC", "cmd.exe"), "/c", d8, "--release",
         "--min-api", str(MIN_API), "--output", out, classes_jar], env=env)
    if not (out / "classes.dex").is_file():
        raise ValueError("d8 did not produce classes.dex")
    with zipfile.ZipFile(base, "a") as apk:
        apk.write(out / "classes.dex", "classes.dex", compress_type=zipfile.ZIP_DEFLATED)
        for abi, library in natives.items():
            apk.write(library, f"lib/{abi}/libgriefsyndrome.so", compress_type=zipfile.ZIP_STORED)

    aligned = out / "aligned.apk"
    run([zipalign, "-P", "16", "-f", "4", base, aligned])
    destination = pub / "grief-syndrome.apk"
    run([java, "-jar", APKSIGNER, "sign", "--key", SIGNER_KEY, "--cert", SIGNER_CERT,
         "--out", destination, aligned])
    certificates = run([java, "-jar", APKSIGNER, "verify", "--print-certs", destination])
    if EXPECTED_CERT_SHA256 not in certificates.lower():
        raise ValueError("Unexpected APK certificate:\n" + certificates)
    run([zipalign, "-c", "-P", "16", "4", destination])
    badging = run([aapt2, "dump", "badging", destination])
    permissions = run([aapt2, "dump", "permissions", destination])
    manifest = run([aapt2, "dump", "xmltree", "--file", "AndroidManifest.xml", destination])
    (out / "badging.txt").write_text(badging, encoding="utf-8")
    (out / "permissions.txt").write_text(permissions, encoding="utf-8")
    (out / "manifest.txt").write_text(manifest, encoding="utf-8")
    with zipfile.ZipFile(destination) as apk:
        for abi, library in natives.items():
            embedded = apk.read(f"lib/{abi}/libgriefsyndrome.so")
            if hashlib.sha256(embedded).hexdigest() != sha256(library):
                raise ValueError(f"Packaged {abi} library differs from the build")
        abis = sorted({name.split("/")[1] for name in apk.namelist() if name.startswith("lib/")})
        if abis != sorted(natives):
            raise ValueError(f"Unexpected ABI set: {abis}")
        embedded_assets = sorted(name for name in apk.namelist() if name.startswith("assets/"))
        expected_assets = ["assets/LICENSE", "assets/THIRD_PARTY_NOTICES.md",
                            "assets/launcher/build.json", "assets/launcher/port.json"]
        if embedded_assets != sorted(expected_assets):
            raise ValueError(f"Unexpected game asset set: {embedded_assets}")
        forbidden = [name for name in apk.namelist() if name.lower().endswith((".exe", ".dat", ".dll"))]
        if forbidden:
            raise ValueError(f"Game files must be installed externally: {forbidden}")
    digest = sha256(destination)
    (pub / "grief-syndrome.apk.sha256").write_text(digest + "  grief-syndrome.apk\n")
    receipt = {
        "apk": str(destination.relative_to(ROOT)),
        "apk_sha256": digest,
        "native_sha256": sha256(native),
        "native_abis": {abi: sha256(library) for abi, library in natives.items()},
        "package": PACKAGE,
        "min_sdk": MIN_API,
        "target_sdk": TARGET_API,
        "abis": sorted(natives),
        "build_tools": str(BUILD_TOOLS),
        "certificate_sha256": EXPECTED_CERT_SHA256,
    }
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
