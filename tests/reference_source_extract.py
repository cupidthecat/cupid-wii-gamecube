# SPDX-License-Identifier: GPL-2.0-or-later
"""Locate complete declarations without treating comments or strings as code."""

import re


def masked(text):
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(tokens, lambda match: " " * len(match.group()), text)


def declaration(text, pattern):
    clean = masked(text)
    match = re.search(pattern + r"[^;{}]*\{", clean)
    if not match:
        raise ValueError(f"Required handler declaration is absent: {pattern}")
    first = clean.index("{", match.start())
    depth = 1
    last = first + 1
    while depth:
        if last >= len(clean):
            raise ValueError("Unclosed handler declaration")
        depth += (clean[last] == "{") - (clean[last] == "}")
        last += 1
    if clean[last : last + 1] == ";":
        last += 1
    return text[match.start() : last] + "\n"


