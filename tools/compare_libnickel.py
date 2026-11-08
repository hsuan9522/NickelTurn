#!/usr/bin/env python3
"""Compare NickelTurn's relevant ELF symbols in two Kobo libnickel files."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


SYMBOLS = (
    "_ZN15KepubBookReader12addCssToHtmlE7QString",
    "_ZN19KepubBookReaderBase13updateHtmlCssEv",
    "_ZN19KepubBookReaderBase13reloadChapterEv",
    "_ZNK10WebkitView7webViewEv",
    "_ZN13CustomWebView27setPageProgressionDirectionERK10QByteArray",
    "_ZN19KepubBookReaderBase26bookmarkForCurrentLocationEv",
    "_ZN18QSharedDataPointerI15BookmarkPrivateED1Ev",
    "_ZN8ScObjectD1Ev",
    "_ZN19KepubBookReaderBase18goToBookmarkAnchorEv",
    "_ZN18QSharedDataPointerI15BookmarkPrivateEaSERKS1_",
    "_ZNK7Content9getBookIdEv",
    "_ZN15KepubBookReader16renderShortcoverERK10Shortcover21ReadingNavigationType",
    "_ZN15KepubBookReader19setWritingDirectionE16WritingDirection",
    "_ZN10WebkitView19setWritingDirectionE16WritingDirection",
    "_ZNK11ReadingView32leftToRightPageProgressDirectionEv",
)


def cstring(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        return ""
    return data[offset:end].decode("utf-8", "replace")


def inspect(path: Path) -> tuple[str, dict[str, tuple[int, int]]]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x01\x01":
        raise ValueError(f"{path}: expected a 32-bit little-endian ELF")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    section_offset, section_entry_size, section_count, string_index = (
        header[6], header[11], header[12], header[13]
    )
    sections = [
        struct.unpack_from("<IIIIIIIIII", data, section_offset + i * section_entry_size)
        for i in range(section_count)
    ]
    section_names_section = sections[string_index]
    section_names = data[
        section_names_section[4] : section_names_section[4] + section_names_section[5]
    ]
    section_names_by_index = [cstring(section_names, section[0]) for section in sections]

    build_id = "missing"
    for index, section in enumerate(sections):
        if section_names_by_index[index] != ".note.gnu.build-id":
            continue
        pos, end = section[4], section[4] + section[5]
        while pos + 12 <= end:
            name_size, desc_size, note_type = struct.unpack_from("<III", data, pos)
            pos += 12
            name = data[pos : pos + name_size]
            pos = (pos + name_size + 3) & ~3
            desc = data[pos : pos + desc_size]
            pos = (pos + desc_size + 3) & ~3
            if name.startswith(b"GNU") and note_type == 3:
                build_id = desc.hex()
                break

    found: dict[str, tuple[int, int]] = {}
    for index, section in enumerate(sections):
        if section_names_by_index[index] not in (".dynsym", ".symtab"):
            continue
        strings_section = sections[section[6]]
        strings = data[strings_section[4] : strings_section[4] + strings_section[5]]
        entry_size = section[9] or 16
        for pos in range(section[4], section[4] + section[5], entry_size):
            name_offset, value, size, _info, _other, _section_index = struct.unpack_from(
                "<IIIBBH", data, pos
            )
            name = cstring(strings, name_offset)
            if name in SYMBOLS:
                found[name] = (value & ~1, size)
    return build_id, found


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: python tools/compare_libnickel.py OLD.so NEW.so")
        return 2
    old_path, new_path = map(Path, sys.argv[1:])
    old_id, old_symbols = inspect(old_path)
    new_id, new_symbols = inspect(new_path)
    print(f"OLD {old_path}: Build ID {old_id}")
    print(f"NEW {new_path}: Build ID {new_id}")
    print(f"{'SYMBOL':78} {'OLD OFFSET':>12} {'NEW OFFSET':>12} {'OLD SIZE':>10} {'NEW SIZE':>10}")
    for symbol in SYMBOLS:
        old = old_symbols.get(symbol)
        new = new_symbols.get(symbol)
        old_offset = f"0x{old[0]:08x}" if old else "MISSING"
        new_offset = f"0x{new[0]:08x}" if new else "MISSING"
        old_size = f"0x{old[1]:x}" if old else "-"
        new_size = f"0x{new[1]:x}" if new else "-"
        print(f"{symbol:78} {old_offset:>12} {new_offset:>12} {old_size:>10} {new_size:>10}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
