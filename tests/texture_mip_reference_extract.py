# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the supplied texture layout constructor and mip iterator unchanged."""

import hashlib
import pathlib
import sys

from reference_source_extract import declaration


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
