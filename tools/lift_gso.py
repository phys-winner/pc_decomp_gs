"""Generate ARM64-ready C from the supplied GSOnline module and merge dispatch."""
import hashlib
import json
from pathlib import Path
import re

from lift_game import ROOT, collect_functions, emit_dispatch, emit_functions
from lift_griefsyndrome import SafeDisassembler, read_json, sha256, write_json
from tools.pe.pe_analyze import analyze_pe, build_iat_map

EXPECTED_SHA256 = "86afc9823ed85f5c2cdaf6f7252db6c536fa50d1316d1e29a61e20d5248df0f4"


def main():
    source = ROOT / "original/russian/gso.dll"
    raw = source.read_bytes()
    if sha256(raw) != EXPECTED_SHA256:
        raise ValueError("GSOnline module SHA-256 mismatch")
    output = ROOT / "generated/gso-x86"
    output.mkdir(parents=True, exist_ok=True)
    info = analyze_pe(str(source))
    decoder = SafeDisassembler(raw, info.image_base, info.sections)
    seeds = [info.image_base + info.entry_point_rva]
    functions = decoder.find_functions(info.code_start, info.code_end,
                                       build_iat_map(info), seeds=seeds)
    catalog = [{"address": address} for address in functions]
    print(f"GSOnline catalog: {len(catalog)} candidate entries", flush=True)
    info, imports, decoder, functions, rejected = collect_functions(
        source, catalog, label="gso", max_entries=10000, compact=True)
    emitted = emit_functions(output, imports, decoder, functions, label="gso",
                             direct_hooks={0x1000A7B0: ("grief_lan_check_cookie", 464)})
    report = {
        "status": "GENERATED",
        "module": source.name,
        "binary_sha256": EXPECTED_SHA256,
        "base": hex(info.image_base),
        "entry": hex(info.image_base + info.entry_point_rva),
        "rejected_non_catalog_entries": rejected,
        **emitted,
    }
    write_json(output / "report.json", report)
    image_lines = ["#include <stdint.h>", "const uint8_t void_gso_image[] = {"]
    for offset in range(0, len(raw), 16):
        image_lines.append("    " + ",".join(f"0x{value:02X}" for value in raw[offset:offset + 16]) + ",")
    image_lines += ["};", f"const uint32_t void_gso_image_size = {len(raw)}u;", ""]
    (output / "gso_image.c").write_text("\n".join(image_lines), encoding="utf-8")
    game_dispatch = (ROOT / "generated/game-x86/recomp_dispatch.c").read_text(encoding="utf-8")
    game_addresses = [int(value, 16) for value in re.findall(r"\{0x([0-9A-F]+)u,", game_dispatch)]
    emit_dispatch(ROOT / "generated/recomp_dispatch.c", [*game_addresses, *functions],
                  ["game-x86/recomp_funcs.h", "gso-x86/recomp_funcs.h"])
    print(json.dumps({key: report[key] for key in ("functions", "emitted_instructions_with_overlap")}), flush=True)


if __name__ == "__main__":
    main()
