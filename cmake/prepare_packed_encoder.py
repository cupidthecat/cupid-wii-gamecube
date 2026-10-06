# SPDX-License-Identifier: GPL-2.0-or-later
"""Define packed word loads in the optional private encoder comparison."""

import argparse
import hashlib
import json
from pathlib import Path


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def prepare(source, destination):
    original = source.read_bytes()
    canonical = original.replace(b"\r\n", b"\n")
    expected = "0f43cae2bc67f55e829b6363970c8aaf3d57d970411d75fb1b689b5e5f063b3e"
    if sha256(canonical) != expected:
        raise ValueError("The packed encoder comparison requires its pinned source checksum")
    raw = b"*(u32*)src"
    replacement = b"LoadPackedWord(src)"
    if canonical.count(raw) != 18 or replacement in canonical:
        raise ValueError("The packed encoder comparison requires exactly eighteen raw word loads")
    header = b'#include "VideoBackends/Software/TextureEncoder.h"\n'
    include = b"\n#include <cstring>\n"
    scope = b"namespace TextureEncoder\n{\n"
    helper = (
        b"// Read the same four native-order bytes without requiring pixel alignment.\n"
        b"static inline u32 LoadPackedWord(const u8* bytes)\n"
        b"{\n"
        b"  u32 value;\n"
        b"  std::memcpy(&value, bytes, sizeof(value));\n"
        b"  return value;\n"
        b"}\n\n"
    )
    if canonical.count(header) != 1 or canonical.count(scope) != 1:
        raise ValueError("The packed encoder comparison has an unexpected declaration layout")
    generated = canonical.replace(header, header + include).replace(scope, scope + helper)
    generated = generated.replace(raw, replacement)
    restored = generated.replace(replacement, raw).replace(scope + helper, scope)
    restored = restored.replace(header + include, header)
    if restored != canonical:
        raise ValueError("The packed encoder load transform is not reversible")
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    generated = generated.replace(b"\n", newline)
    restored = restored.replace(b"\n", newline)
    if restored != original:
        raise ValueError("The packed encoder load transform changed original source bytes")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != generated:
        destination.write_bytes(generated)
    receipt = dict(
        source=str(source), source_sha256=sha256(original),
        canonical_source_sha256=sha256(canonical), generated=str(destination),
        generated_sha256=sha256(generated), defined_word_loads=18,
        reverse_transform_byte_identical=True,
        scope="Only packed source word loads change; algorithms, comparison cases and sanitizer flags are retained.",
    )
    destination.with_suffix(".receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    prepare(arguments.source, arguments.destination)
