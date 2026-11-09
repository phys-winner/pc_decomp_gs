"""Pinned local Android and signing toolchains."""
import os
from pathlib import Path

SDK = Path(os.environ.get("ANDROID_SDK") or os.environ.get("ANDROID_HOME") or
           os.environ.get("ANDROID_SDK_ROOT") or Path.home() / "AppData/Local/Android/Sdk")
NDK = Path(os.environ.get("ANDROID_NDK") or SDK / "ndk/27.2.12479018")
TOOLCHAIN = NDK / "toolchains/llvm/prebuilt/windows-x86_64"
CLANG = TOOLCHAIN / "bin/clang.exe"
BUILD_TOOLS = SDK / "build-tools/37.0.0"
if not BUILD_TOOLS.is_dir():
    BUILD_TOOLS = SDK / "build-tools/36.1.0"
ANDROID_JAR = SDK / "platforms/android-37.0/android.jar"
if not ANDROID_JAR.is_file():
    ANDROID_JAR = SDK / "platforms/android-36/android.jar"
JAVA_HOME = Path(os.environ.get("JAVA_HOME") or "C:/Portable/jdk-24.0.1")
ADB = SDK / "platform-tools/adb.exe"
SIGNER_DIR = Path("C:/Portable/Total.Commander/Utilites/Just Another Tool for APK/bin")
APKSIGNER = Path(os.environ.get("APKSIGNER_JAR") or SIGNER_DIR / "apksigner.jar")
SIGNER_KEY = Path(os.environ.get("SIGNER_KEY") or SIGNER_DIR / "testkey.pk8")
SIGNER_CERT = Path(os.environ.get("SIGNER_CERT") or SIGNER_DIR / "testkey.x509.pem")
EXPECTED_CERT_SHA256 = os.environ.get("SIGNER_CERT_SHA256", "a40da80a59d170caa950cf15c18c454d47a39b26989d8b640ecd745ba71bf5dc").lower()
