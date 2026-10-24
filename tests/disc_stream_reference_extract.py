# SPDX-License-Identifier: GPL-2.0-or-later
"""Expose selected external drive handlers to a comparison-only host harness."""

import pathlib
import re
import sys


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


def extract(root, output):
    directory = root / "Source/Core/Core/HW/DVD"
    source = (directory / "DVDInterface.cpp").read_text(encoding="utf-8")
    header = (directory / "DVDInterface.h").read_text(encoding="utf-8")
    enums = [
        declaration(header, rf"enum class {name}\b")
        for name in ("DICommand", "DriveState", "DriveError", "DIInterruptType", "ReplyType")
    ]
    functions = [
        declaration(source, rf"(?:size_t|void|u32|bool) DVDInterface::{name}\(")
        for name in (
            "ProcessDTKSamples",
            "AdvanceDTK",
            "DTKStreamingCallback",
            "ResetDrive",
            "CheckReadPreconditions",
            "AudioBufferConfig",
        )
    ]
    pack = declaration(source, r"static u64 PackFinishExecutingCommandUserdata\(")
    execute = declaration(source, r"void DVDInterface::ExecuteCommand\(")
    case_matches = list(re.finditer(r"^  case DICommand::(\w+):", masked(execute), re.M))
    selected = {"RequestError", "AudioStream", "RequestAudioStatus", "AudioBufferConfig", "Seek"}
    cases = []
    for index, match in enumerate(case_matches):
        if match.group(1) in selected:
            cases.append(execute[match.start() : case_matches[index + 1].start()])
            selected.remove(match.group(1))
    if selected:
        raise ValueError(f"Required command cases are absent: {selected}")
    wrapper = """
void DVDInterface::ExecuteCommand(u32 command, u32 argument1, u32 argument2)
{
  m_DICMDBUF = {command, argument1, argument2};
  m_last_interrupt = DIInterruptType::TCINT;
  auto& interrupt_type = m_last_interrupt;
  if (static_cast<DICommand>(command >> 24) != DICommand::RequestError)
    SetDriveError(DriveError::None);
  switch (static_cast<DICommand>(command >> 24))
  {
""" + "".join(cases) + """
  default:
    throw std::invalid_argument("Command outside selected drive comparison scope");
  }
}
"""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "// Generated from the external pinned checkout for local comparisons.\n"
        "// SPDX-License-Identifier: GPL-2.0-or-later\n"
        "#ifdef CUPID_DISC_REFERENCE_ENUMS\n"
        + "".join(enums)
        + "#else\n"
        + pack
        + "".join(functions)
        + wrapper
        + "#endif\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: disc_stream_reference_extract.py <checkout> <output>")
    extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
