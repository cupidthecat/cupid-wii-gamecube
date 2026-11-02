# SPDX-License-Identifier: GPL-2.0-or-later
"""Expose unchanged gather-buffer bookkeeping to the CPU comparison host."""

import hashlib
import pathlib
import sys

from reference_source_extract import declaration


def extract(root, output):
    source = root / "Source/Core/Core/HW/GPFifo.cpp"
    text = source.read_text(encoding="utf-8")
    methods = [r"GPFifoManager::GPFifoManager\(",
               r"size_t GPFifoManager::GetGatherPipeCount\(",
               r"void GPFifoManager::SetGatherPipeCount\(",
               r"void GPFifoManager::Init\(",
               r"bool GPFifoManager::IsBNE\(\) const",
               r"void GPFifoManager::ResetGatherPipe\(",
               r"void GPFifoManager::FastWrite8\("]
    header = "\n".join(text.splitlines()[:2]) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header + "// Source SHA256: " + hashlib.sha256(source.read_bytes()).hexdigest()
                      + "\n" + "\n".join(declaration(text, method) for method in methods),
                      encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: gather_pipe_reference_extract.py <checkout> <output>")
    extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
