"""Build the external installation ZIP from verified, unmodified game files."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


def main():
    inputs = json.loads((ROOT / "config/inputs.json").read_text(encoding="utf-8"))
    files = {"griefsyndrome.exe": ("jp", inputs["executable_sha256"])}
    for edition in ("jp", "ru"):
        for name, expected in inputs["editions"][edition]["containers"].items():
            files[name] = (edition, expected)
    manifest = {"layout": "external-game-data", "files": []}
    sources = {}
    for name, (edition, expected) in files.items():
        source = ROOT / "original" / ("russian" if edition == "ru" else "japanese") / name
        if not source.is_file():
            raise ValueError(f"Missing original input: {source}")
        digest = sha256(source)
        if digest != expected:
            raise ValueError(f"Unexpected SHA-256 for {source}: {digest}")
        sources[name] = source
        manifest["files"].append({"name": name, "bytes": source.stat().st_size, "sha256": expected})
    pub = ROOT / "pub"
    pub.mkdir(exist_ok=True)
    from build_widescreen import IMAGES, build, unpack
    artwork = ROOT / "app/artwork/widescreen"
    if all((artwork / (name + ".png")).is_file() for name in IMAGES):
        build(artwork)
    widescreen = pub / "gs_widescreen.dat"
    if widescreen.is_file():
        unpack(widescreen.read_bytes())
        sources[widescreen.name] = widescreen
        manifest["files"].append({"name": widescreen.name, "bytes": widescreen.stat().st_size,
                                  "sha256": sha256(widescreen)})
    destination = pub / "grief-syndrome-game-data.zip"
    valid = False
    if destination.is_file():
        try:
            with zipfile.ZipFile(destination) as archive:
                valid = sorted(archive.namelist()) == sorted(sources)
                for item in manifest["files"]:
                    with archive.open(item["name"]) as stream:
                        valid = valid and hashlib.file_digest(stream, "sha256").hexdigest() == item["sha256"]
        except (OSError, KeyError, zipfile.BadZipFile):
            valid = False
    if not valid:
        temporary = destination.with_suffix(".zip.part")
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, source in sources.items():
                archive.write(source, name)
        with zipfile.ZipFile(temporary) as archive:
            assert sorted(archive.namelist()) == sorted(sources)
            for item in manifest["files"]:
                with archive.open(item["name"]) as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != item["sha256"]:
                        raise ValueError(f"ZIP verification failed: {item['name']}")
        temporary.replace(destination)
    manifest["archive"] = destination.name
    manifest["archive_sha256"] = sha256(destination)
    destination.with_suffix(".zip.sha256").write_text(manifest["archive_sha256"] + "  " + destination.name + "\n")
    (ROOT / "config/assets.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Verified external installation ZIP: {destination.name}, {destination.stat().st_size} bytes")


if __name__ == "__main__":
    main()
