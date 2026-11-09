"""Build the x86 Microsoft-D3DX effect extraction oracle."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VSWHERE = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
OUT = ROOT / "build/effect-oracle"
OUT.mkdir(parents=True, exist_ok=True)
installation = Path(subprocess.check_output([str(VSWHERE), "-latest", "-products", "*",
                                             "-property", "installationPath"], text=True).strip())
msvc = sorted((installation / "VC/Tools/MSVC").iterdir())[-1]
sdk = Path("C:/Program Files (x86)/Windows Kits/10")
version = sorted((sdk / "Lib").iterdir())[-1].name
env = os.environ.copy()
env["PATH"] = str(msvc / "bin/Hostx64/x86") + os.pathsep + env["PATH"]
env["INCLUDE"] = ";".join(map(str, [
    ROOT / "reference/dxvk/include/native/directx",
    msvc / "include",
    sdk / f"Include/{version}/ucrt",
    sdk / f"Include/{version}/shared",
    sdk / f"Include/{version}/um",
]))
env["LIB"] = ";".join(map(str, [
    msvc / "lib/x86",
    sdk / f"Lib/{version}/ucrt/x86",
    sdk / f"Lib/{version}/um/x86",
]))
compiler = msvc / "bin/Hostx64/x86/cl.exe"
command = [str(compiler), "/EHsc", "/std:c++17", "/O2", "/DUNICODE=0",
           "tools/effect_oracle.cpp", "/Fe" + str(OUT / "effect_oracle.exe")]
print(" ".join(command))
subprocess.run(command, cwd=ROOT, env=env, check=True)
print(OUT / "effect_oracle.exe")
