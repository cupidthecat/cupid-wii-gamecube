# SPDX-License-Identifier: GPL-2.0-or-later
"""Reproduce the symmetric host-audio window from its defining eigenproblem."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct


def window_bits():
    length, bandwidth = 129, 4
    diagonal = [((length - 1) / 2 - k) ** 2
                * math.cos(2 * math.pi * bandwidth / length) for k in range(length)]
    adjacent = [k * (length - k) / 2 for k in range(1, length)]
    sequence = [1.0] * length
    for iteration in range(20000):
        product = [diagonal[k] * sequence[k]
                   + (adjacent[k - 1] * sequence[k - 1] if k else 0)
                   + (adjacent[k] * sequence[k + 1] if k + 1 < length else 0)
                   for k in range(length)]
        scale = max(product)
        product = [value / scale for value in product]
        change = max(abs(a - b) for a, b in zip(sequence, product))
        sequence = product
        if change < 1e-15:
            break
    else:
        raise RuntimeError("Slepian sequence did not converge")
    convolution = [sum(sequence[k] for k in range(max(0, n - 127), min(length - 1, n) + 1))
                   for n in range(256)]
    scale = max(convolution[k] + convolution[k + 128] for k in range(128))
    weights = [float(f"{value / scale:.10f}") for value in convolution]
    bits = [struct.unpack("<I", struct.pack("<f", value))[0] for value in weights]
    if bits != list(reversed(bits)):
        raise RuntimeError("Window symmetry changed")
    return bits, iteration + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check the compiled source array")
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parent.parent / "src/audio/host_mixer.cpp")
    parser.add_argument("--emit", type=Path, help="Write a generated half-window array")
    args = parser.parse_args()
    bits, iterations = window_bits()
    if args.check or not args.emit:
        source = args.source.read_text(encoding="utf-8")
        array = re.search(r"window_half_bits\s*\{(.*?)\};", source, re.S)
        if not array:
            raise RuntimeError("Source does not contain the window array")
        actual = [int(word, 16) for word in re.findall(r"0x([0-9a-fA-F]{8})u", array.group(1))]
        if actual != bits[:128]:
            raise RuntimeError("Compiled window coefficients differ from the generated bits")
    if args.emit:
        lines = ["// SPDX-License-Identifier: GPL-2.0-or-later",
                 "constexpr std::array<u32, 128> window_half_bits{"]
        for start in range(0, 128, 8):
            lines.append("  " + ", ".join(f"0x{word:08x}u" for word in bits[start:start + 8]) + ",")
        lines.append("};")
        args.emit.write_text("\n".join(lines) + "\n", encoding="utf-8")
    packed = b"".join(struct.pack("<I", word) for word in bits)
    print(json.dumps({"coefficients": len(bits), "iterations": iterations,
                      "sha256_le_float_bytes": hashlib.sha256(packed).hexdigest()}))


if __name__ == "__main__":
    main()
