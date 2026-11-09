"""Generate ARM64-ready C from the verified Grief Syndrome PE32 image."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from lift_game import ROOT, collect_functions, emit_dispatch, emit_functions
from tools.pe.pe_analyze import analyze_pe, build_iat_map
from tools.disasm.disasm32 import Disassembler


class SafeDisassembler(Disassembler):
    def read_bytes(self, address, size):
        return super().read_bytes(address, size) or b""


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def executable_sections(path):
    data = path.read_bytes()
    info = analyze_pe(str(path))
    ranges = [section for section in info.sections if section.is_code]
    return b"".join(data[section.raw_offset:section.raw_offset + section.raw_size]
                    for section in ranges)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog-only", action="store_true")
    args = parser.parse_args()
    inputs = read_json(ROOT / "config/inputs.json")
    jp_path = ROOT / inputs["executable"]
    jp = jp_path.read_bytes()
    if sha256(jp) != inputs["executable_sha256"]:
        raise ValueError("Original executable SHA-256 mismatch")
    jp_code = executable_sections(jp_path)

    output = ROOT / "generated/game-x86"
    output.mkdir(parents=True, exist_ok=True)
    catalog_path = output / "catalog.json"
    receipt_path = output / "catalog.sha256"
    digest = sha256(jp)
    if catalog_path.exists() and receipt_path.exists() and receipt_path.read_text().strip() == digest:
        catalog = read_json(catalog_path)
    else:
        info = analyze_pe(str(jp_path))
        decoder = SafeDisassembler(jp, info.image_base, info.sections)
        if decoder.read_bytes(0xFFFFFFFF, 16) != b"":
            raise ValueError("Out-of-image reads must be empty")
        seeds = [info.image_base + info.entry_point_rva]
        seeds += [info.image_base + export.rva for export in info.exports if export.rva]
        functions = decoder.find_functions(info.code_start, info.code_end,
                                           build_iat_map(info), seeds=seeds)
        catalog = [{"address": address} for address in functions]
        write_json(catalog_path, catalog)
        receipt_path.write_text(digest + "\n", encoding="utf-8")
        del functions, decoder
    for address in (0x5AA060,):
        if all(entry["address"] != address for entry in catalog):
            catalog.append({"address": address})
    catalog.sort(key=lambda entry: entry["address"])
    print(f"Catalog: {len(catalog)} candidate entries", flush=True)
    if args.catalog_only:
        return

    table_rows = read_json(ROOT / "config/jump-tables.json")
    tables = {int(row["address"], 0): (row["first_index"], row["count"]) for row in table_rows}
    if len(tables) != len(table_rows):
        raise ValueError("Duplicate declared jump table")
    info, imports, decoder, functions, rejected = collect_functions(
        jp_path, catalog, label="griefsyndrome", max_entries=100000,
        compact=True, jump_tables=tables)
    emitted = emit_functions(output, imports, decoder, functions,
                             label="griefsyndrome",
                             direct_hooks={0x52E270: ("void_grief_compile_buffer", 3),
                                           0x52C260: ("grief_read_closure", 5)},
                             callsite_hooks={0x4B98C4: (0x4ACB80, "void_grief_lan_zlib_hook"),
                                             0x42E77B: (0x52BC90, "void_grief_lan_squirrel_hook")})
    emit_dispatch(output / "recomp_dispatch.c", functions,
                  ["recomp_funcs.h"])
    traps = set()
    trap_pattern = re.compile(r"void_lift_trap\(0x([0-9A-F]+)u,")
    for source in emitted["sources"]:
        traps.update(trap_pattern.findall((output / source).read_text(encoding="utf-8")))
    report = {
        "status": "GENERATED",
        "module": jp_path.name,
        "binary_sha256": digest,
        "shared_executable_code_sha256": sha256(jp_code),
        "base": hex(info.image_base),
        "entry": hex(info.image_base + info.entry_point_rva),
        "rejected_non_catalog_entries": rejected,
        "declared_jump_tables": len(tables),
        "unique_static_trap_sites": len(traps),
        "runtime_status": "UNVERIFIED",
        **emitted,
    }
    write_json(output / "report.json", report)
    write_json(output / "static-traps.json", sorted("0x" + address for address in traps))
    (output / "game_entry.h").write_text(
        f"#define GAME_BASE 0x{info.image_base:08X}u\n"
        f"#define GAME_ENTRY 0x{info.image_base + info.entry_point_rva:08X}u\n",
        encoding="utf-8")
    print(json.dumps({key: report[key] for key in
                      ("functions", "emitted_instructions_with_overlap", "unique_static_trap_sites")}),
          flush=True)


if __name__ == "__main__":
    main()
