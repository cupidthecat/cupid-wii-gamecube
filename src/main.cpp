// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/build_info.hpp"
#include "cupid/disc/filesystem.hpp"
#include "cupid/runtime/machine.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
using namespace cupid;

struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Options {
  std::filesystem::path game, bios, user = "userdata", trace, state, log, output, input, frame_dump;
  std::filesystem::path ipl_fonts;
  std::filesystem::path input_replay;
  std::string console = "auto", graphics = "software", audio = "none";
  std::string display_aspect = "standard", remote_speakers = "off";
  std::string extract_file;
  runtime::RunLimits limits;
  u64 trace_count = 1000, trace_start = 0, offset = 0, length = 0;
  bool help = false, version = false, self_test = false, headless = false, inspect = false,
       validate_bios = false, validate_fonts = false;
  bool read_disc = false, read_game = false, trace_after_boot = false;
  bool list_files = false;
};

u64 integer(std::string_view text) {
  u64 value = 0;
  int base = 10;
  if (text.starts_with("0x")) {
    base = 16;
    text.remove_prefix(2);
  }
  auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
    throw UsageError("Expected an unsigned integer, received: " + std::string(text));
  return value;
}

double number(std::string_view text) {
  double value = 0;
  auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      !std::isfinite(value) || value < 0)
    throw UsageError("Expected a finite nonnegative number");
  return value;
}

Options parse(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto value = [&]() -> std::string {
      if (++i >= argc)
        throw UsageError("Missing value for " + arg);
      return argv[i];
    };
    if (arg == "--help" || arg == "-h")
      o.help = true;
    else if (arg == "--version")
      o.version = true;
    else if (arg == "--self-test")
      o.self_test = true;
    else if (arg == "--headless")
      o.headless = true;
    else if (arg == "--inspect")
      o.inspect = true;
    else if (arg == "--validate-bios")
      o.validate_bios = true;
    else if (arg == "--validate-fonts")
      o.validate_fonts = true;
    else if (arg == "--read-disc")
      o.read_disc = true;
    else if (arg == "--read-game")
      o.read_game = true;
    else if (arg == "--list-files")
      o.list_files = true;
    else if (arg == "--extract-file")
      o.extract_file = value();
    else if (arg == "--trace-after-boot")
      o.trace_after_boot = true;
    else if (arg == "--game")
      o.game = value();
    else if (arg == "--bios")
      o.bios = value();
    else if (arg == "--ipl-fonts")
      o.ipl_fonts = value();
    else if (arg == "--user-dir")
      o.user = value();
    else if (arg == "--trace")
      o.trace = value();
    else if (arg == "--trace-count")
      o.trace_count = integer(value());
    else if (arg == "--trace-start")
      o.trace_start = integer(value());
    else if (arg == "--dump-state")
      o.state = value();
    else if (arg == "--frame-dump")
      o.frame_dump = value();
    else if (arg == "--input-config")
      o.input = value();
    else if (arg == "--input-replay")
      o.input_replay = value();
    else if (arg == "--log")
      o.log = value();
    else if (arg == "--output")
      o.output = value();
    else if (arg == "--console")
      o.console = value();
    else if (arg == "--graphics")
      o.graphics = value();
    else if (arg == "--display-aspect")
      o.display_aspect = value();
    else if (arg == "--audio")
      o.audio = value();
    else if (arg == "--remote-speakers")
      o.remote_speakers = value();
    else if (arg == "--max-instructions")
      o.limits.instructions = integer(value());
    else if (arg == "--max-frames")
      o.limits.frames = integer(value());
    else if (arg == "--run-seconds")
      o.limits.seconds = number(value());
    else if (arg == "--offset")
      o.offset = integer(value());
    else if (arg == "--length")
      o.length = integer(value());
    else
      throw UsageError("Unknown option: " + arg);
  }
  if (o.console != "auto" && o.console != "gamecube" && o.console != "wii")
    throw UsageError("Console must be auto, gamecube or wii");
  if (o.graphics != "software")
    throw UsageError("Graphics backend must be software");
  if (o.display_aspect != "standard" && o.display_aspect != "widescreen" &&
      o.display_aspect != "pixels")
    throw UsageError("Display aspect must be standard, widescreen or pixels");
  if (o.audio != "none" && o.audio != "sdl")
    throw UsageError("Audio backend must be none or sdl");
  if (o.remote_speakers != "on" && o.remote_speakers != "off")
    throw UsageError("Remote speakers must be on or off");
  if (o.remote_speakers == "on" && o.audio != "sdl")
    throw UsageError("Remote speakers require --audio sdl");
  if (o.read_disc && o.read_game)
    throw UsageError("Select one disc-read address space");
  const unsigned commands = unsigned(o.inspect) + unsigned(o.validate_bios) +
                            unsigned(o.validate_fonts) + unsigned(o.read_disc) +
                            unsigned(o.read_game) + unsigned(o.list_files) +
                            unsigned(!o.extract_file.empty());
  if (commands > 1)
    throw UsageError("Select one inspection or extraction command");
  if (!o.bios.empty() && !o.ipl_fonts.empty())
    throw UsageError("Select --bios or --ipl-fonts");
  if (!o.extract_file.empty() && o.output.empty())
    throw UsageError("File extraction requires --output PATH");
  if ((o.read_disc || o.read_game) &&
      (o.output.empty() || !o.length || o.length > 32 * 1024 * 1024))
    throw UsageError("Disc reads need --output and --length between 1 and 33554432");
  return o;
}

const char *help = R"(cupid-wii-gamecube - GameCube and Wii CLI runtime

Usage: cupid-wii-gamecube --game PATH [options]
       cupid-wii-gamecube --self-test | --help | --version

  --game PATH                 RVZ, ISO/GCM disc, DOL or ELF executable
  --console auto|gamecube|wii  Select the guest console (auto from disc)
  --headless                  Disable the game-output window
  --bios PATH                 Validate and load GameCube IPL for EXI reads
  --validate-bios             Check --bios without running software
  --ipl-fonts PATH            Load separately validated IPL font blocks
  --validate-fonts            Check --ipl-fonts without running software
  --graphics software         CPU rendering and SDL presentation
  --display-aspect MODE       standard (VI), widescreen or pixels; default standard
  --audio none|sdl             Select guest PCM output (default none)
  --remote-speakers on|off     Mix decoded Remote speakers (default off; requires SDL audio)
  --input-config PATH         Keyboard action=key mappings
  --input-replay PATH         Deterministic guest input events, including headless
  --user-dir PATH             Explicit persistent-data directory (userdata)
  --max-instructions N        Bound CPU steps, including faults (default 1000000)
  --max-frames N              Stop after N presented frames (0 disables)
  --run-seconds N             Wall-clock bound (default 10; 0 disables)
  --trace PATH                Write execution state as JSON lines
  --trace-count N             Maximum traced instructions (default 1000)
  --trace-start N             Begin tracing at instruction N (default 0)
  --trace-after-boot          Start the trace at the game's entry point
  --dump-state PATH           Write final machine state as JSON
  --frame-dump PATH           Save the last guest frame as PPM
  --log PATH                  Save progress and diagnostic messages
  --inspect                   Print disc metadata without booting
  --list-files                Print the decoded disc filesystem as JSON lines
  --extract-file PATH         Read a file from the disc filesystem
  --read-disc | --read-game   Read raw disc or plaintext game-partition bytes
  --offset N --length N       Disc read range; decimal or 0x hexadecimal
  --output PATH               Destination of an explicitly requested read

Exit codes: 0 command completed or run limit; 2 usage; 3 input;
            4 unsupported guest behavior; 5 host I/O; 6 self-test failure.
Commercial gameplay is under development. See docs/compatibility.md.
)";

void json_string(std::ostream &out, std::string_view text) {
  out << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      out << '\\' << c;
    else if (c < 32)
      out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
    else
      out << c;
  }
  out << '"';
}

void state_output(runtime::Machine &machine, const Options &options, const std::string &outcome) {
  machine.write_state(std::cout, outcome);
  if (!options.state.empty()) {
    if (!options.state.parent_path().empty())
      std::filesystem::create_directories(options.state.parent_path());
    std::ofstream file(options.state);
    machine.write_state(file, outcome);
    if (!file)
      throw std::runtime_error("Cannot write final state file");
  }
}

void protect_inputs(const Options &options) {
  for (const auto &input :
       {options.game, options.bios, options.ipl_fonts, options.input, options.input_replay}) {
    if (input.empty())
      continue;
    for (const auto &output :
         {options.trace, options.state, options.log, options.output, options.frame_dump,
          options.user / "gc-sram.bin", options.user / "memory-card-a.raw",
          options.user / "memory-card-b.raw"}) {
      if (output.empty())
        continue;
      std::error_code error;
      if ((std::filesystem::exists(input) && std::filesystem::exists(output) &&
           std::filesystem::equivalent(input, output, error)) ||
          std::filesystem::absolute(input).lexically_normal() ==
              std::filesystem::absolute(output).lexically_normal())
        throw UsageError("An output path resolves to an input asset: " + output.string());
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = parse(argc, argv);
    protect_inputs(options);
    if (options.help) {
      std::cout << help;
      return 0;
    }
    if (options.version) {
      std::cout << "cupid-wii-gamecube " << build::version << " (" << build::commit << ")\n";
      return 0;
    }
    if (options.self_test) {
      runtime::Machine machine(Console::GameCube);
      machine.load_executable(runtime::self_test_dol());
      for (unsigned i = 0; i < 7; ++i)
        machine.step();
      if (machine.memory().read32(0x5000) != 16 || machine.cpu().state().gpr[7] != 16 ||
          machine.cpu().state().instructions != 7) {
        std::cerr << "Guest execution self-test failed\n";
        return 6;
      }
      std::cout << "Guest DOL execution self-test passed\n";
      return 0;
    }
    if (options.validate_bios) {
      if (options.bios.empty())
        throw UsageError("--validate-bios requires --bios");
      auto bios = runtime::load_bios(options.bios);
      std::cout << "Validated GameCube firmware: " << bios.revision << ", 2097152 bytes, CRC32 "
                << std::hex << bios.crc32 << '\n';
      return 0;
    }
    if (options.validate_fonts) {
      if (options.ipl_fonts.empty())
        throw UsageError("--validate-fonts requires --ipl-fonts");
      const auto fonts = runtime::load_ipl_fonts(options.ipl_fonts);
      std::cout << "Validated IPL font blocks: Shift JIS " << fonts.shift_jis.encoded_size
                << " encoded / " << fonts.shift_jis.decoded_size << " decoded bytes; Western "
                << fonts.western.encoded_size << " encoded / " << fonts.western.decoded_size
                << " decoded bytes; container CRC32 " << std::hex << fonts.container_crc32 << '\n';
      return 0;
    }
    if (options.game.empty())
      throw UsageError("Supply --game PATH or --self-test");
    if (!std::filesystem::is_regular_file(options.game))
      throw std::runtime_error("Game file does not exist: " + options.game.string());
    std::unique_ptr<disc::Image> image;
    const auto extension = options.game.extension().string();
    const bool dol = extension == ".dol" || extension == ".DOL";
    const bool elf = extension == ".elf" || extension == ".ELF";
    if (!dol && !elf)
      image = std::make_unique<disc::Image>(options.game);
    if (options.inspect) {
      if (!image)
        throw UsageError("Disc inspection requires an RVZ or raw disc image");
      const auto &header = image->header();
      std::cout << "{\"id\":";
      json_string(std::cout, header.id);
      std::cout << ",\"title\":";
      json_string(std::cout, header.title);
      std::cout << ",\"console\":\"" << (header.console == Console::Wii ? "wii" : "gamecube")
                << "\",\"format\":\"" << image->format() << "\",\"compression\":\""
                << image->compression() << "\",\"disc_bytes\":" << image->size()
                << ",\"game_bytes\":" << image->game_size()
                << ",\"dol_offset\":" << image->main_dol_offset()
                << ",\"fst_offset\":" << image->fst_offset()
                << ",\"fst_bytes\":" << image->fst_size() << "}\n";
      return 0;
    }
    if (options.read_disc || options.read_game) {
      if (!image)
        throw UsageError("Disc reads require an RVZ or raw disc image");
      if (std::filesystem::absolute(options.output).lexically_normal() ==
          std::filesystem::absolute(options.game).lexically_normal())
        throw UsageError("Disc-read output must be a different file from the input game");
      std::vector<u8> bytes(static_cast<std::size_t>(options.length));
      if (options.read_game)
        image->read_game(options.offset, bytes);
      else
        image->read(options.offset, bytes);
      if (!options.output.parent_path().empty())
        std::filesystem::create_directories(options.output.parent_path());
      std::ofstream file(options.output, std::ios::binary | std::ios::trunc);
      file.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
      if (!file)
        throw std::runtime_error("Cannot write decoded disc range");
      std::cout << "Read " << bytes.size() << " bytes\n";
      return 0;
    }
    if (options.list_files || !options.extract_file.empty()) {
      if (!image)
        throw UsageError("Filesystem commands require a disc image");
      disc::Filesystem filesystem(*image);
      if (options.list_files) {
        for (const auto &entry : filesystem.entries()) {
          std::cout << "{\"path\":";
          json_string(std::cout, entry.path);
          std::cout << ",\"directory\":" << (entry.directory ? "true" : "false")
                    << ",\"offset\":" << entry.offset << ",\"bytes\":" << entry.size << "}\n";
        }
        return 0;
      }
      const auto *entry = filesystem.find(options.extract_file);
      if (!entry || entry->directory)
        throw std::runtime_error("Requested disc file does not exist");
      if (options.offset > entry->size)
        throw UsageError("File read offset exceeds the file length");
      const u64 size = options.length ? options.length : entry->size - options.offset;
      if (size > entry->size - options.offset)
        throw UsageError("File read range exceeds the file length");
      if (!options.output.parent_path().empty())
        std::filesystem::create_directories(options.output.parent_path());
      std::ofstream file(options.output, std::ios::binary | std::ios::trunc);
      std::vector<u8> buffer(512 * 1024);
      for (u64 cursor = 0; cursor < size;) {
        const auto count = static_cast<std::size_t>(std::min<u64>(buffer.size(), size - cursor));
        filesystem.read_file(entry->path, options.offset + cursor,
                             std::span<u8>(buffer).first(count));
        file.write(reinterpret_cast<const char *>(buffer.data()),
                   static_cast<std::streamsize>(count));
        if (!file)
          throw std::runtime_error("Cannot write extracted disc file");
        cursor += count;
      }
      file.flush();
      if (!file)
        throw std::runtime_error("Cannot write extracted disc file");
      std::cout << "Read " << size << " bytes from " << entry->path << '\n';
      return 0;
    }
    const Console console = options.console == "wii"
                                ? Console::Wii
                                : (options.console == "gamecube"
                                       ? Console::GameCube
                                       : (image ? image->header().console : Console::GameCube));
    if (console == Console::Wii && !options.bios.empty())
      throw UsageError("GameCube IPL does not provide Wii system data");
    std::ofstream log;
    if (!options.log.empty()) {
      if (!options.log.parent_path().empty())
        std::filesystem::create_directories(options.log.parent_path());
      log.open(options.log);
      if (!log)
        throw std::runtime_error("Cannot open log file");
    }
    auto message = [&](const std::string &text) {
      std::cerr << text << '\n';
      if (log)
        log << text << '\n';
    };
    const auto display_aspect =
        options.display_aspect == "widescreen"
            ? platform::DisplayAspect::Widescreen
            : (options.display_aspect == "pixels" ? platform::DisplayAspect::Pixels
                                                  : platform::DisplayAspect::Standard);
    platform::Output output({options.headless, options.audio == "sdl", options.frame_dump,
                             options.input, display_aspect, options.remote_speakers == "on"});
    runtime::Machine machine(console);
    machine.set_output(&output);
    machine.set_limits(options.limits);
    machine.set_user_directory(options.user);
    if (!options.input_replay.empty())
      machine.set_input_replay(options.input_replay);
    if (!options.bios.empty()) {
      auto bios = runtime::load_bios(options.bios);
      machine.install_bios(bios);
      message("Loaded validated " + bios.revision + " firmware for EXI");
    }
    if (!options.ipl_fonts.empty()) {
      const auto fonts = runtime::load_ipl_fonts(options.ipl_fonts);
      machine.install_fonts(fonts);
      message("Loaded structurally validated IPL fonts for EXI");
    }
    if (!options.trace.empty())
      machine.set_trace(options.trace, options.trace_count, options.trace_after_boot,
                        options.trace_start);
    int status = 0;
    std::string outcome;
    try {
      if (dol || elf)
        machine.load_executable(runtime::read_file(options.game, 128 * 1024 * 1024), elf);
      else
        machine.load_disc(options.game);
      message("Boot stage: " + machine.boot_info().stage);
      machine.run();
    } catch (const runtime::LimitReached &error) {
      outcome = error.what();
      message("Stopped at " + outcome);
    } catch (const cpu::UnsupportedInstruction &error) {
      outcome = "unsupported-instruction";
      message(error.what());
      status = 4;
    } catch (const BusFault &error) {
      outcome = "unsupported-device";
      message(error.what());
      status = 4;
    } catch (const std::exception &error) {
      outcome = "runtime-error";
      message(error.what());
      status = 4;
    }
    machine.persist();
    if (!options.frame_dump.empty() && output.presented_frames())
      output.save_frame();
    state_output(machine, options, outcome);
    return status;
  } catch (const UsageError &error) {
    std::cerr << error.what() << "\nUse --help for options.\n";
    return 2;
  } catch (const cpu::UnsupportedInstruction &error) {
    std::cerr << error.what() << '\n';
    return 4;
  } catch (const std::filesystem::filesystem_error &error) {
    std::cerr << error.what() << '\n';
    return 5;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 3;
  }
}
