#!/usr/bin/env python3
"""Build the RA2/YR 1.001 compatibility corpus from an extracted game directory.

Repository-maintenance tool only; this is not an engine runtime dependency.

Stage 1 preserves canonical top-level MIX files, audited nested MIX files, high-value
INI/CSF files, and official YRO maps. Stage 2 resolves MIX filename hashes using a
pinned OpenRA/XCC global filename database plus each archive's local MIX database,
then materializes all confidently named INI/MAP/TMP/PAL/SHP/VXL/HVA/CSF leaf assets.
Unknown hashes are recorded instead of being assigned guessed filenames.

Dependency: cryptography (needed for encrypted Westwood MIX header decryption).
"""
from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
import tempfile
import time
from typing import Iterable
import urllib.request
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

OPENRA_MIX_DB_COMMIT = "a520984d91eda9de48a62b1d15c1e3bad0d4fb1a"
OPENRA_MIX_DB_BLOB_SHA1 = "273db510a3284d2dc533954e5ee3a909d6153a0e"
OPENRA_MIX_DB_URL = (
    "https://raw.githubusercontent.com/OpenRA/OpenRA/"
    f"{OPENRA_MIX_DB_COMMIT}/global%20mix%20database.dat"
)

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

LEAF_EXTENSIONS = {".ini", ".map", ".tmp", ".pal", ".shp", ".vxl", ".hva", ".csf"}
INI_EXPLICIT_FILENAME_RE = re.compile(
    r"(?i)([A-Za-z0-9_~!@#$%^&()+\-.'\[\]]+\.(?:mix|ini|map|tmp|pal|shp|vxl|hva|csf))"
)


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
    return zlib.crc32("".join(chars).encode("ascii", errors="replace")) & 0xFFFFFFFF


def hash_classic(name: str) -> int:
    data = bytearray(name.upper().encode("ascii", errors="replace"))
    data.extend(b"\0" * ((-len(data)) % 4))
    result = 0
    for offset in range(0, len(data), 4):
        value = struct.unpack_from("<I", data, offset)[0]
        result = (((result << 1) | (result >> 31)) + value) & 0xFFFFFFFF
    return result


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def read_c_string(data: bytes, offset: int) -> tuple[str, int]:
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated string in XCC MIX database")
    return data[offset:end].decode("latin-1"), end + 1


def parse_xcc_global_database(data: bytes) -> list[str]:
    names: list[str] = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise ValueError("truncated XCC global MIX database group header")
        count = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        if count < 0 or count > 1_000_000:
            raise ValueError(f"implausible XCC global MIX database group count: {count}")
        for _ in range(count):
            filename, offset = read_c_string(data, offset)
            _, offset = read_c_string(data, offset)
            if filename:
                names.append(filename)
    return names


def parse_xcc_local_database(data: bytes) -> list[str]:
    if len(data) < 52:
        raise ValueError("XCC local MIX database is shorter than 52 bytes")
    count = struct.unpack_from("<i", data, 48)[0]
    if count < 0 or count > 1_000_000:
        raise ValueError(f"implausible XCC local MIX database entry count: {count}")
    names: list[str] = []
    offset = 52
    for _ in range(count):
        filename, offset = read_c_string(data, offset)
        if filename:
            names.append(filename)
    return names


def obtain_global_mix_database(explicit: Path | None) -> tuple[bytes, dict[str, object]]:
    if explicit is not None:
        data = explicit.read_bytes()
        source = str(explicit.resolve())
    else:
        cache_dir = Path(tempfile.gettempdir()) / "ra2yr-corpus"
        cache_dir.mkdir(parents=True, exist_ok=True)
        cached = cache_dir / f"openra-global-mix-database-{OPENRA_MIX_DB_COMMIT}.dat"
        if cached.is_file():
            data = cached.read_bytes()
            if git_blob_sha1(data) != OPENRA_MIX_DB_BLOB_SHA1:
                cached.unlink()
                data = b""
        else:
            data = b""

        if not data:
            last_error: Exception | None = None
            request = urllib.request.Request(
                OPENRA_MIX_DB_URL,
                headers={"User-Agent": "RA2YR-bycpp-corpus-builder/2"},
            )
            for attempt in range(1, 6):
                try:
                    with urllib.request.urlopen(request, timeout=45) as response:
                        data = response.read()
                    break
                except Exception as exc:
                    last_error = exc
                    if attempt < 5:
                        time.sleep(attempt * 2)
            if not data:
                raise RuntimeError(
                    "Could not download the pinned OpenRA global MIX filename database after 5 attempts. "
                    "Retry later or pass --mix-database <path>."
                ) from last_error
            cached.write_bytes(data)
        source = OPENRA_MIX_DB_URL

    actual_blob = git_blob_sha1(data)
    if actual_blob != OPENRA_MIX_DB_BLOB_SHA1:
        raise ValueError(
            "OpenRA global MIX database identity mismatch: "
            f"expected git blob {OPENRA_MIX_DB_BLOB_SHA1}, got {actual_blob}"
        )
    names = parse_xcc_global_database(data)
    if not names:
        raise ValueError("OpenRA global MIX database parsed successfully but contained no filenames")
    metadata = {
        "source": source,
        "openraCommit": OPENRA_MIX_DB_COMMIT,
        "gitBlobSha1": actual_blob,
        "byteLength": len(data),
        "filenameCount": len(names),
    }
    return data, metadata


@dataclass(frozen=True)
class MixEntry:
    file_hash: int
    offset: int
    length: int


@dataclass(frozen=True)
class ResolvedName:
    filename: str
    source: str


class MixArchive:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.entries: dict[int, MixEntry] = {}
        self.flags = 0
        self.encrypted = False
        self.checksum = False
        self.data_start = 0
        self.file_count = 0
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

        if len(index) != 12 * count:
            raise ValueError(f"{self.path}: truncated MIX index")

        self.file_count = count
        for index_number in range(count):
            file_hash, offset, length = struct.unpack_from("<III", index, index_number * 12)
            entry = MixEntry(file_hash, offset, length)
            start = self.data_start + offset
            end = start + length
            if start < self.data_start or end > len(self.data):
                raise ValueError(
                    f"{self.path}: entry 0x{file_hash:08X} points outside archive bounds"
                )
            if file_hash in self.entries:
                raise ValueError(f"{self.path}: duplicate MIX hash 0x{file_hash:08X}")
            self.entries[file_hash] = entry

    def get_by_hash(self, file_hash: int) -> bytes | None:
        entry = self.entries.get(file_hash)
        if entry is None:
            return None
        start = self.data_start + entry.offset
        return self.data[start:start + entry.length]

    def get(self, name: str) -> bytes | None:
        data = self.get_by_hash(hash_crc(name))
        if data is not None:
            return data
        return self.get_by_hash(hash_classic(name))

    def local_database_names(self) -> list[str]:
        for candidate_hash in (
            hash_crc("local mix database.dat"),
            hash_classic("local mix database.dat"),
        ):
            raw = self.get_by_hash(candidate_hash)
            if raw is not None:
                return parse_xcc_local_database(raw)
        return []

    def resolve_names(
        self,
        global_names: Iterable[str],
        additional_names: Iterable[str] = (),
    ) -> tuple[dict[int, ResolvedName], str, int]:
        local_names = self.local_database_names()
        candidates: dict[str, set[str]] = {}

        def add(names: Iterable[str], source: str) -> None:
            for name in names:
                if name:
                    candidates.setdefault(name, set()).add(source)

        add(global_names, "openra-global-db")
        add(local_names, "local-mix-db")
        add(additional_names, "ini-explicit-reference")

        indexes: dict[str, dict[int, ResolvedName]] = {"classic": {}, "crc32": {}}
        collisions: dict[str, set[int]] = {"classic": set(), "crc32": set()}
        for filename, sources in candidates.items():
            source_label = "+".join(sorted(sources))
            for hash_type, value in (
                ("classic", hash_classic(filename)),
                ("crc32", hash_crc(filename)),
            ):
                if value not in self.entries:
                    continue
                existing = indexes[hash_type].get(value)
                if existing is not None and existing.filename.lower() != filename.lower():
                    collisions[hash_type].add(value)
                    continue
                indexes[hash_type][value] = ResolvedName(filename, source_label)

        for hash_type in indexes:
            for value in collisions[hash_type]:
                indexes[hash_type].pop(value, None)

        hash_type = "crc32" if len(indexes["crc32"]) > len(indexes["classic"]) else "classic"
        return indexes[hash_type], hash_type, len(local_names)


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


def safe_name_path(name: str) -> Path | None:
    normalized = name.replace("\\", "/")
    pure = PurePosixPath(normalized)
    if pure.is_absolute() or not pure.parts:
        return None
    invalid_chars = set('<>:"|?*')
    parts: list[str] = []
    for part in pure.parts:
        if part in ("", ".", ".."):
            return None
        if any(char in invalid_chars or ord(char) < 32 for char in part):
            return None
        parts.append(part)
    return Path(*parts)


def collect_explicit_ini_filenames(corpus: Path) -> set[str]:
    names: set[str] = set()
    ini_root = corpus / "extracted" / "ini"
    if not ini_root.is_dir():
        return names
    for path in ini_root.rglob("*.ini"):
        text = path.read_bytes().decode("latin-1", errors="ignore")
        names.update(match.group(1) for match in INI_EXPLICIT_FILENAME_RE.finditer(text))
    return names


def build(source: Path, repo_root: Path, mix_database: Path | None) -> None:
    corpus = repo_root / "CNCRA2YR1.001" / "corpus"
    if corpus.exists():
        shutil.rmtree(corpus)

    records: list[dict[str, object]] = []
    recorded_paths: set[str] = set()

    def record(
        path: Path,
        source_name: str,
        role: str,
        container: str | None = None,
        **extra: object,
    ) -> None:
        relative = path.relative_to(repo_root).as_posix()
        if relative in recorded_paths:
            return
        item: dict[str, object] = {
            "path": relative,
            "size": path.stat().st_size,
            "sha256": sha256_file(path),
            "source": source_name,
            "role": role,
            "container": container,
        }
        item.update(extra)
        records.append(item)
        recorded_paths.add(relative)

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

    database_data, database_metadata = obtain_global_mix_database(mix_database)
    global_names = parse_xcc_global_database(database_data)
    ini_names = collect_explicit_ini_filenames(corpus)

    jobs: list[tuple[Path, tuple[str, ...]]] = []
    for path in sorted((corpus / "top-level").iterdir(), key=lambda item: item.name.lower()):
        if path.suffix.lower() == ".mix":
            jobs.append((path, (path.name,)))
    for path in sorted((corpus / "nested").rglob("*"), key=lambda item: item.as_posix().lower()):
        if path.is_file() and path.suffix.lower() == ".mix":
            rel = path.relative_to(corpus / "nested")
            jobs.append((path, tuple(rel.parts)))

    seen_jobs: set[tuple[str, ...]] = set()
    archive_reports: list[dict[str, object]] = []
    unresolved_records: list[dict[str, object]] = []
    extracted_leaf_count = 0
    resolved_nested_count = 0

    while jobs:
        archive_path, chain = jobs.pop(0)
        if chain in seen_jobs:
            continue
        seen_jobs.add(chain)
        archive = MixArchive(archive_path)
        resolved, hash_type, local_name_count = archive.resolve_names(global_names, ini_names)

        unresolved_hashes = sorted(set(archive.entries) - set(resolved))
        archive_reports.append({
            "containerChain": list(chain),
            "fileCount": archive.file_count,
            "hashType": hash_type,
            "resolvedCount": len(resolved),
            "unresolvedCount": len(unresolved_hashes),
            "localDatabaseFilenameCount": local_name_count,
        })
        for file_hash in unresolved_hashes:
            entry = archive.entries[file_hash]
            unresolved_records.append({
                "containerChain": list(chain),
                "hash": f"0x{file_hash:08X}",
                "offset": entry.offset,
                "length": entry.length,
            })

        for file_hash, resolution in sorted(
            resolved.items(), key=lambda item: item[1].filename.lower()
        ):
            relative_name = safe_name_path(resolution.filename)
            if relative_name is None:
                continue
            extension = relative_name.suffix.lower()
            raw = archive.get_by_hash(file_hash)
            if raw is None:
                raise AssertionError("resolved MIX entry disappeared")

            if extension == ".mix":
                child_chain = chain + tuple(relative_name.parts)
                audited_destination = corpus / "nested" / Path(*child_chain)
                if audited_destination.is_file():
                    if audited_destination.read_bytes() != raw:
                        raise ValueError(
                            f"Audited nested MIX bytes disagree with resolved parent entry: "
                            f"{' -> '.join(child_chain)}"
                        )
                    nested_destination = audited_destination
                else:
                    nested_base = corpus / "nested-recursive"
                    for part in chain:
                        nested_base /= f"{part}.contents"
                    nested_destination = nested_base / relative_name
                    if nested_destination.exists():
                        if nested_destination.read_bytes() != raw:
                            raise ValueError(
                                f"Recursive nested MIX changed while resolving: "
                                f"{' -> '.join(child_chain)}"
                            )
                    else:
                        write_bytes(raw, nested_destination)
                        record(
                            nested_destination,
                            resolution.filename,
                            "nested-mix-resolved",
                            " -> ".join(chain),
                            mixHash=f"0x{file_hash:08X}",
                            hashType=hash_type,
                            resolutionSource=resolution.source,
                            containerChain=list(chain),
                        )
                        resolved_nested_count += 1
                jobs.append((nested_destination, child_chain))
                continue

            if extension not in LEAF_EXTENSIONS:
                continue

            leaf_destination = corpus / "extracted" / "leaf" / Path(*chain) / relative_name
            if not leaf_destination.exists():
                write_bytes(raw, leaf_destination)
                extracted_leaf_count += 1
            record(
                leaf_destination,
                resolution.filename,
                "resolved-leaf-asset",
                " -> ".join(chain),
                mixHash=f"0x{file_hash:08X}",
                hashType=hash_type,
                resolutionSource=resolution.source,
                containerChain=list(chain),
            )

    archive_reports.sort(key=lambda item: [part.lower() for part in item["containerChain"]])
    unresolved_records.sort(
        key=lambda item: ([part.lower() for part in item["containerChain"]], item["hash"])
    )

    resolution_report = {
        "schema": 1,
        "filenameDatabase": database_metadata,
        "explicitIniFilenameCandidateCount": len(ini_names),
        "archiveCount": len(archive_reports),
        "resolvedNestedMixAdded": resolved_nested_count,
        "resolvedLeafAssetsAdded": extracted_leaf_count,
        "totalUnresolvedHashes": len(unresolved_records),
        "archives": archive_reports,
    }
    (corpus / "MIX-RESOLUTION-REPORT.json").write_text(
        json.dumps(resolution_report, ensure_ascii=False, indent=2),
        encoding="utf-8",
        newline="\n",
    )
    (corpus / "UNKNOWN-MIX-HASHES.json").write_text(
        json.dumps(
            {"schema": 1, "count": len(unresolved_records), "entries": unresolved_records},
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
        newline="\n",
    )

    records.sort(key=lambda item: str(item["path"]).lower())
    total_bytes = sum(int(item["size"]) for item in records)
    manifest = {
        "schema": 2,
        "sourceProfile": "audited-ra2-1.006-yr-1.001-no-movies-repack",
        "entryCount": len(records),
        "totalBytes": total_bytes,
        "leafExtraction": {
            "enabled": True,
            "extensions": sorted(LEAF_EXTENSIONS),
            "filenameDatabase": database_metadata,
            "archiveCount": len(archive_reports),
            "resolvedLeafAssetsAdded": extracted_leaf_count,
            "resolvedNestedMixAdded": resolved_nested_count,
            "unresolvedHashCount": len(unresolved_records),
        },
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
    print(f"Scanned MIX archives: {len(archive_reports)}")
    print(f"Resolved leaf assets added: {extracted_leaf_count}")
    print(f"Resolved nested MIX added: {resolved_nested_count}")
    print(f"Unresolved MIX hashes recorded: {len(unresolved_records)}")
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
    parser.add_argument(
        "--mix-database",
        type=Path,
        default=None,
        help=(
            "Optional local copy of OpenRA global mix database.dat. When omitted, the "
            "builder downloads the pinned database and verifies its Git blob identity."
        ),
    )
    args = parser.parse_args()
    source = args.source.resolve()
    repo_root = args.repo_root.resolve()
    mix_database = args.mix_database.resolve() if args.mix_database else None
    if not source.is_dir():
        parser.error(f"--source is not a directory: {source}")
    if not (repo_root / "README.md").is_file():
        parser.error(f"--repo-root does not look like RA2YR-bycpp: {repo_root}")
    if mix_database is not None and not mix_database.is_file():
        parser.error(f"--mix-database is not a file: {mix_database}")
    build(source, repo_root, mix_database)
    return 0


if __name__ == "__main__":
    sys.exit(main())
