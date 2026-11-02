# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the supplied texture layout constructor and mip iterator unchanged."""

import hashlib
import pathlib
import re
import sys


def declaration(text, pattern):
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    clean = re.sub(tokens, lambda match: " " * len(match.group()), text)
    match = re.search(pattern + r"[^;{}]*\{", clean)
    if not match:
        raise ValueError(f"Required texture layout method is absent: {pattern}")
    first = clean.index("{", match.start())
    depth, last = 1, first + 1
    while depth:
        if last >= len(clean):
            raise ValueError("Unclosed texture layout method")
        depth += (clean[last] == "{") - (clean[last] == "}")
        last += 1
    return text[match.start():last] + "\n"


def extract(root, output):
    source = root / "Source/Core/VideoCommon/TextureInfo.cpp"
    text = source.read_text(encoding="utf-8")
    methods = [
        r"TextureInfo::TextureInfo\(",
        r"TextureInfo::MipLevels TextureInfo::GetMipMapLevels\(\) const",
        r"TextureInfo::MipLevel::MipLevel\(",
        r"TextureInfo::MipLevelIterator& TextureInfo::MipLevelIterator::operator\+\+\(\)",
        r"void TextureInfo::MipLevelIterator::CreateMipLevel\(",
    ]
    # Preserve the supplied source's legal header in the generated local file.
    header = "\n".join(text.splitlines()[:2]) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header + "// Source SHA256: " + hashlib.sha256(source.read_bytes()).hexdigest()
                      + "\n" + "\n".join(declaration(text, method) for method in methods),
                      encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: texture_mip_reference_extract.py <checkout> <output>")
    extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
