#!/usr/bin/env python3
"""Build the RA2/YR 1.001 compatibility corpus from an extracted game directory.

Repository-maintenance tool only; this is not an engine runtime dependency.

Dependency: cryptography (needed for encrypted Westwood MIX header decryption).
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

try:
    from cryptography.hazmat.decrepit.ciphers.algorithms import Blowfish
    from cryptography.hazmat.primitives.ciphers import Cipher, modes
except ImportError as exc:
    raise SystemExit(
        "Missing dependency 'cryptography'. Install with: py -m pip install cryptography"
    ) from exc

PUBLIC_KEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"
PUBLIC_MODULUS = int.from_bytes(base64.b64decode(PUBLIC_KEY_B64)[2:], "big")

CANONICAL_TOP_LEVEL = [
    "ra2.mix",
    "ra2md.mix",
    "expandmd01.mix",
    "language.mix",
    "langmd.mix",
    "MULTIMD.MIX",
    "MAPSMD03.MIX",
]

RA2_NESTED = [
    "cache.mix", "local.mix", "conquer.mix", "generic.mix", "isogen.mix",
    "isotemp.mix", "isosnow.mix", "isourb.mix", "temperat.mix", "snow.mix",
    "urban.mix", "tem.mix", "sno.mix", "urb.mix", "load.mix", "neutral.mix",
    "sidec01.mix", "sidec02.mix", "sidenc01.mix", "sidenc02.mix",
]

YR_NESTED = [
    "cachemd.mix", "localmd.mix", "conqmd.mix", "genermd.mix", "isogenmd.mix",
    "isotemmd.mix", "isosnomd.mix", "isourbmd.mix", "des.mix", "isodes.mix",
    "isodesmd.mix", "ubn.mix", "isoubn.mix", "isoubnmd.mix", "lun.mix",
    "isolun.mix", "isolunmd.mix", "snowmd.mix", "loadmd.mix", "sidec02md.mix",
    "desert.mix", "urbann.mix", "lunar.mix", "ntrlmd.mix",
]

RA2_INIS = [
    "art.ini", "ai.ini", "battle.ini", "coopcamp.ini", "eva.ini",
    "keyboard.ini", "mapsel.ini", "mpbattle.ini", "mpcoop.ini", "mpduel.ini",
    "mpmeat.ini", "mpmodes.ini", "mpmw.ini", "mpnaval.ini", "mpsiege.ini",
    "mpunholy.ini", "mission.ini", "rmg.ini", "rules.ini", "snow.ini",
    "sound.ini", "temperat.ini", "theme.ini", "tutorial.ini", "ui.ini",
    "urban.ini",
]

YR_INIS = [
    "artmd.ini", "aimd.ini", "battlemd.ini", "coopcampmd.ini", "desertmd.ini",
    "evamd.ini", "lunarmd.ini", "mapselmd.ini", "mpbattlemd.ini", "mpcoopmd.ini",
    "mpduelmd.ini", "mpmeatmd.ini", "mpmodesmd.ini", "mpmwmd.ini",
    "mpnavalmd.ini", "mpsiegemd.ini", "mpunholymd.ini", "missionmd.ini",
    "rmgmd.ini", "rulesmd.ini", "snowmd.ini", "soundmd.ini", "temperatmd.ini",
    "thememd.ini", "uimd.ini", "urbanmd.ini", "urbannmd.ini",
]


def derive_key(src: bytes) -> bytes:
    if len(src) < 80:
        raise ValueError("Westwood key block is shorter than 80 bytes")
    out = b""
    for offset in (0, 40):
        cipher_value = int.from_bytes(src[offset:offset + 40], "little")
        message = pow(cipher_value, 0x10001, PUBLIC_MODULUS)
        out += message.to_bytes(40, "little")[:39]
    return out[:56]


def hash_crc(name: str) -> int:
    upper = name.upper()
    length = len(upper)
    padding = (-length) % 4
    chars = list(upper) + ["\0"] * padding
    if padding:
        remainder = length % 4
        chars[length] = chr(remainder)
        for index in range(1, padding):
            chars[length + index] = chars[length - remainder]
    return zlib.crc32("".join(chars).encode("ascii")) & 0xFFFFFFFF


class MixArchive:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.entries: dict[int, tuple[int, int]] = {}
        self.flags = 0
        self.encrypted = False
        self.checksum = False
        self._parse()

    def _parse(self) -> None:
        data = self.data
        if len(data) < 6:
            raise ValueError(f"{self.path}: too small to be a MIX")

        is_cnc = struct.unpack_from("<H", data, 0)[0] != 0
        if is_cnc:
            count, _ = struct.unpack_from("<HI", data, 0)
            index = data[6:6 + 12 * count]
            self.data_start = 6 + 12 * count
        else:
            self.flags = struct.unpack_from("<I", data, 0)[0]
            self.encrypted = bool(self.flags & 0x20000)
            self.checksum = bool(self.flags & 0x10000)
            if self.encrypted:
                key = derive_key(data[4:84])
                first = Cipher(Blowfish(key), modes.ECB()).decryptor().update(data[84:92])
                count = struct.unpack_from("<H", first, 0)[0]
                blocks = (13 + count * 12) // 8
                encrypted_header = data[84:84 + blocks * 8]
                header = Cipher(Blowfish(key), modes.ECB()).decryptor().update(encrypted_header)
                count_check, _ = struct.unpack_from("<HI", header, 0)
                if count_check != count:
                    raise ValueError(f"{self.path}: decrypted MIX header count mismatch")
                index = header[6:6 + 12 * count]
                self.data_start = 84 + blocks * 8
            else:
                count, _ = struct.unpack_from("<HI", data, 4)
                index = data[10:10 + 12 * count]
                self.data_start = 10 + 12 * count

        self.file_count = count
        for index_number in range(count):
            file_hash, offset, length = struct.unpack_from("<III", index, index_number * 12)
            self.entries[file_hash] = (offset, length)

    def get(self, name: str) -> bytes | None:
        entry = self.entries.get(hash_crc(name))
        if entry is None:
            return None
        offset, length = entry
        start = self.data_start + offset
        return self.data[start:start + length]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_exact(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def write_bytes(data: bytes, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)


def build(source: Path, repo_root: Path) -> None:
    corpus = repo_root / "尤里的复仇1.001" / "corpus"
    if corpus.exists():
        shutil.rmtree(corpus)

    records: list[dict[str, object]] = []

    def record(path: Path, source_name: str, role: str, container: str | None = None) -> None:
        records.append({
            "path": path.relative_to(repo_root).as_posix(),
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
            "source": source_name,
            "role": role,
            "container": container,
        })

    for name in CANONICAL_TOP_LEVEL:
        source_file = source / name
        if not source_file.is_file():
            raise FileNotFoundError(f"Required source file is missing: {source_file}")
        destination = corpus / "top-level" / name
        copy_exact(source_file, destination)
        record(destination, name, "canonical-top-level")

    for parent_name, names, subdir in (
        ("ra2.mix", RA2_NESTED, "ra2.mix"),
        ("ra2md.mix", YR_NESTED, "ra2md.mix"),
    ):
        archive = MixArchive(source / parent_name)
        expected_count = 21 if parent_name == "ra2.mix" else 25
        if archive.file_count != expected_count:
            raise ValueError(
                f"{parent_name}: expected {expected_count} index entries for this audited corpus, "
                f"found {archive.file_count}. Refusing to silently treat another repack as identical."
            )
        for name in names:
            data = archive.get(name)
            if data is None:
                raise KeyError(f"{parent_name}: required nested MIX not found: {name}")
            destination = corpus / "nested" / subdir / name
            write_bytes(data, destination)
            record(destination, parent_name, "nested-mix", parent_name)

    key_sources = [
        ("ra2.mix", MixArchive(source / "ra2.mix").get("key.ini")),
        ("ra2md.mix", MixArchive(source / "ra2md.mix").get("key.ini")),
        ("local.mix", MixArchive(corpus / "nested" / "ra2.mix" / "local.mix").get("key.ini")),
        ("localmd.mix", MixArchive(corpus / "nested" / "ra2md.mix" / "localmd.mix").get("key.ini")),
    ]
    if any(data is None for _, data in key_sources):
        raise KeyError("key.ini was not found in all four audited locations")
    key_hashes = {
        hashlib.sha256(data).hexdigest()
        for _, data in key_sources
        if data is not None
    }
    if len(key_hashes) != 1:
        raise ValueError("key.ini copies differ across audited containers")
    key_data = key_sources[0][1]
    assert key_data is not None
    key_destination = corpus / "extracted" / "ini" / "common" / "key.ini"
    write_bytes(key_data, key_destination)
    record(
        key_destination,
        "ra2.mix/ra2md.mix/local.mix/localmd.mix",
        "shared-ini",
    )

    for parent_name, names in (
        ("language.mix", ("audio.mix", "cameo.mix")),
        ("langmd.mix", ("audiomd.mix", "cameomd.mix")),
    ):
        archive = MixArchive(source / parent_name)
        for name in names:
            data = archive.get(name)
            if data is None:
                raise KeyError(f"{parent_name}: required nested MIX not found: {name}")
            destination = corpus / "nested" / parent_name / name
            write_bytes(data, destination)
            record(destination, parent_name, "nested-mix", parent_name)

    for parent_name, name in (("language.mix", "ra2.csf"), ("langmd.mix", "ra2md.csf")):
        data = MixArchive(source / parent_name).get(name)
        if data is None:
            raise KeyError(f"{parent_name}: {name} not found")
        destination = corpus / "extracted" / "strings" / name
        write_bytes(data, destination)
        record(destination, parent_name, "string-table", parent_name)

    for nested_rel, names, target in (
        (("ra2.mix", "local.mix"), RA2_INIS, "ra2"),
        (("ra2md.mix", "localmd.mix"), YR_INIS, "yr-base"),
    ):
        archive = MixArchive(corpus / "nested" / nested_rel[0] / nested_rel[1])
        for name in names:
            data = archive.get(name)
            if data is None:
                continue
            destination = corpus / "extracted" / "ini" / target / name
            write_bytes(data, destination)
            record(destination, nested_rel[1], "extracted-ini", nested_rel[1])

    patch = MixArchive(source / "expandmd01.mix")
    for name in ("rulesmd.ini", "soundmd.ini"):
        data = patch.get(name)
        if data is None:
            raise KeyError(f"expandmd01.mix: effective 1.001 {name} not found")
        destination = corpus / "extracted" / "ini" / "yr-1.001-patch" / name
        write_bytes(data, destination)
        record(destination, "expandmd01.mix", "effective-patch-ini", "expandmd01.mix")

    yro_files = sorted(source.glob("*.yro"))
    if len(yro_files) != 13:
        raise ValueError(f"Expected 13 audited official .yro maps, found {len(yro_files)}")
    for source_file in yro_files:
        destination = corpus / "official-maps" / source_file.name
        copy_exact(source_file, destination)
        record(destination, source_file.name, "official-map-addon")

    records.sort(key=lambda item: str(item["path"]).lower())
    total_bytes = sum(int(item["size"]) for item in records)
    manifest = {
        "schema": 1,
        "sourceProfile": "audited-ra2-1.006-yr-1.001-no-movies-repack",
        "entryCount": len(records),
        "totalBytes": total_bytes,
        "entries": records,
    }
    (corpus / "corpus-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2),
        encoding="utf-8",
        newline="\n",
    )
    with (corpus / "CORPUS-SHA256SUMS.txt").open(
        "w",
        encoding="utf-8",
        newline="\n",
    ) as handle:
        for item in records:
            relative = str(item["path"]).split("corpus/", 1)[1]
            handle.write(f"{item['sha256']}  {relative}\n")

    print(f"Built {len(records)} corpus resources")
    print(f"Total payload: {total_bytes} bytes ({total_bytes / 1024**3:.3f} GiB)")
    print(f"Output: {corpus}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        required=True,
        type=Path,
        help="Extracted RA2/YR game directory",
    )
    parser.add_argument(
        "--repo-root",
        default=Path.cwd(),
        type=Path,
        help="RA2YR-bycpp repository root",
    )
    args = parser.parse_args()
    source = args.source.resolve()
    repo_root = args.repo_root.resolve()
    if not source.is_dir():
        parser.error(f"--source is not a directory: {source}")
    if not (repo_root / "README.md").is_file():
        parser.error(f"--repo-root does not look like RA2YR-bycpp: {repo_root}")
    build(source, repo_root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
