#!/usr/bin/env python3
"""Apply the Windows-safe recursive-nested-path fix to build_corpus.py.

This temporary migration helper is intentionally idempotent. It exists because the
first Stage 2 builder commit could try to create a directory underneath a file such
as `cache.mix/foo.mix`. The fixed layout uses `nested-recursive/*.contents/`.
"""
from __future__ import annotations

import argparse
from pathlib import Path

OLD = '''            if extension == ".mix":\n                nested_destination = corpus / "nested" / Path(*chain) / relative_name\n                if not nested_destination.exists():\n                    write_bytes(raw, nested_destination)\n                    record(\n                        nested_destination,\n                        resolution.filename,\n                        "nested-mix-resolved",\n                        " -> ".join(chain),\n                        mixHash=f"0x{file_hash:08X}",\n                        hashType=hash_type,\n                        resolutionSource=resolution.source,\n                        containerChain=list(chain),\n                    )\n                    resolved_nested_count += 1\n                child_chain = chain + tuple(relative_name.parts)\n                jobs.append((nested_destination, child_chain))\n                continue\n'''

NEW = '''            if extension == ".mix":\n                child_chain = chain + tuple(relative_name.parts)\n                audited_destination = corpus / "nested" / Path(*child_chain)\n                if audited_destination.is_file():\n                    if audited_destination.read_bytes() != raw:\n                        raise ValueError(\n                            f"Audited nested MIX bytes disagree with resolved parent entry: "\n                            f"{' -> '.join(child_chain)}"\n                        )\n                    nested_destination = audited_destination\n                else:\n                    nested_base = corpus / "nested-recursive"\n                    for part in chain:\n                        nested_base /= f"{part}.contents"\n                    nested_destination = nested_base / relative_name\n                    if nested_destination.exists():\n                        if nested_destination.read_bytes() != raw:\n                            raise ValueError(\n                                f"Recursive nested MIX changed while resolving: "\n                                f"{' -> '.join(child_chain)}"\n                            )\n                    else:\n                        write_bytes(raw, nested_destination)\n                        record(\n                            nested_destination,\n                            resolution.filename,\n                            "nested-mix-resolved",\n                            " -> ".join(chain),\n                            mixHash=f"0x{file_hash:08X}",\n                            hashType=hash_type,\n                            resolutionSource=resolution.source,\n                            containerChain=list(chain),\n                        )\n                        resolved_nested_count += 1\n                jobs.append((nested_destination, child_chain))\n                continue\n'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--builder", required=True, type=Path)
    args = parser.parse_args()
    path = args.builder.resolve()
    text = path.read_text(encoding="utf-8")

    if NEW in text:
        print("Stage 2 recursive nested-path fix already applied")
        return 0
    if OLD not in text:
        raise SystemExit(
            "build_corpus.py does not contain the expected Stage 2 block; refusing to patch an unknown version"
        )

    path.write_text(text.replace(OLD, NEW), encoding="utf-8", newline="\n")
    print("Applied Stage 2 recursive nested-path fix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
