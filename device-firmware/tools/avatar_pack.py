#!/usr/bin/env python3
"""Build the read-only AVA1 avatar partition image."""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import tempfile
import zlib
from pathlib import Path

from PIL import Image, UnidentifiedImageError


MAGIC = b"AVA1"
VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 28
NAME_RE = re.compile(r"^[a-z0-9_-]{1,15}$")


class AvatarPackError(ValueError):
    pass


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _validate_png(path: Path) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise AvatarPackError(f"cannot read PNG {path.name}: {exc}") from exc
    if len(data) < 29 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AvatarPackError(f"invalid PNG: {path.name}")
    try:
        with Image.open(path) as image:
            image.verify()
        with Image.open(path) as image:
            if image.size != (96, 156):
                raise AvatarPackError(f"{path.name} must be 96x156")
            if image.mode != "RGBA":
                raise AvatarPackError(f"{path.name} must be RGBA")
    except (OSError, SyntaxError, UnidentifiedImageError) as exc:
        raise AvatarPackError(f"invalid PNG: {path.name}") from exc
    if data[28] != 0:
        raise AvatarPackError(f"{path.name} must be non-interlaced PNG")
    return data


def build_pack(input_dir: Path, partition_size: int) -> bytes:
    root = Path(input_dir)
    paths = list(root.glob("*.png"))
    folded = [path.stem.casefold() for path in paths]
    if len(folded) != len(set(folded)):
        raise AvatarPackError("duplicate avatar name (case-insensitive)")
    for path in paths:
        if not NAME_RE.fullmatch(path.stem):
            raise AvatarPackError(f"invalid name: {path.stem}")
    paths.sort(key=lambda path: path.stem)
    names = [path.stem for path in paths]
    if not paths:
        raise AvatarPackError("avatar catalog must contain at least one PNG file")
    if len(paths) > 255:
        raise AvatarPackError("avatar catalog exceeds 255 entries")

    pngs = [_validate_png(path) for path in paths]
    header_size = HEADER_SIZE + len(paths) * ENTRY_SIZE
    cursor = _align4(header_size)
    entries = bytearray()
    locations: list[tuple[int, bytes]] = []
    for path, png in zip(paths, pngs):
        cursor = _align4(cursor)
        name = path.stem.encode("ascii") + b"\0"
        entries.extend(struct.pack(
            "<16sIII", name.ljust(16, b"\0"), cursor, len(png),
            zlib.crc32(png) & 0xFFFFFFFF,
        ))
        locations.append((cursor, png))
        cursor += len(png)
    total_size = cursor
    if partition_size <= 0 or total_size > partition_size:
        raise AvatarPackError(
            f"avatar pack overflow: needs {total_size} bytes, partition has {partition_size}"
        )

    output = bytearray(b"\xff" * partition_size)
    output[:HEADER_SIZE] = struct.pack(
        "<4sBBHII", MAGIC, VERSION, len(paths), header_size, total_size,
        zlib.crc32(entries) & 0xFFFFFFFF,
    )
    output[HEADER_SIZE:header_size] = entries
    for offset, png in locations:
        output[offset:offset + len(png)] = png
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--catalog-output", type=Path)
    parser.add_argument("--size", required=True, type=lambda value: int(value, 0))
    args = parser.parse_args()
    packed = build_pack(args.input, args.size)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{args.output.name}.", dir=args.output.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(packed)
        os.replace(temporary, args.output)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise
    if args.catalog_output:
        names = sorted(path.stem for path in args.input.glob("*.png"))
        args.catalog_output.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(
            prefix=f".{args.catalog_output.name}.", dir=args.catalog_output.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                json.dump({"version": 1, "avatars": names}, stream,
                          ensure_ascii=True, separators=(",", ":"))
                stream.write("\n")
            os.replace(temporary, args.catalog_output)
        except BaseException:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            raise
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
