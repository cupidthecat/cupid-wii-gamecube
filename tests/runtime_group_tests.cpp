// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/runtime/machine.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <random>
#include <span>
#include <sstream>

using namespace cupid;

namespace {
struct TemporaryFile {
  std::filesystem::path path;

  TemporaryFile() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() /
           ("cupid-group-" + std::to_string(random()) + "-" + std::to_string(random()) + ".txt");
    CHECK(!std::filesystem::exists(path));
  }

  ~TemporaryFile() {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
  }
};

u32 immediate(unsigned primary, unsigned destination, unsigned base, u16 value) {
  return (primary << 26) | (destination << 21) | (base << 16) | value;
}

u32 extended(unsigned xo, unsigned destination, unsigned a, unsigned b, bool record = false) {
  return (31u << 26) | (destination << 21) | (a << 16) | (b << 11) | (xo << 1) |
         static_cast<u32>(record);
}

std::vector<u8> program() {
  const std::array<u32, 16> code{immediate(14, 3, 3, 1),
                                 immediate(14, 4, 4, 7),
                                 immediate(24, 3, 5, 0x1234),
                                 extended(316, 4, 6, 5),
                                 (21u << 26) | (6u << 21) | (7u << 16) | (3u << 11) | (1u << 6) |
                                     (31u << 1),
                                 extended(266, 8, 7, 5, true),
                                 immediate(12, 11, 11, 0xffff),
                                 immediate(36, 20, 13, 0x80),
                                 immediate(36, 21, 13, 0x34),
                                 immediate(32, 9, 13, 0x80),
                                 immediate(36, 9, 12, 0x6000),
                                 immediate(36, 3, 12, 0x6004),
                                 immediate(26, 9, 10, 0x4321),
                                 extended(40, 15, 4, 8, true),
                                 immediate(28, 15, 16, 0x7fff),
                                 0x4bffffc4};
  std::vector<u8> dol(0x100 + code.size() * 4);
  const auto word = [&](std::size_t offset, u32 value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      dol[offset + byte] = static_cast<u8>(value >> (24 - byte * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, static_cast<u32>(code.size() * 4));
  word(0xe0, 0x80004000);
  for (std::size_t index = 0; index < code.size(); ++index)
    word(0x100 + index * 4, code[index]);
  return dol;
}

std::vector<u8> polling_program() {
  // The load is the last word of one line; its comparison and taken branch
  // are in the next line. DMA changes the polled word without CPU involvement.
  const std::array<u32, 12> code{immediate(32, 5, 3, 0),
                                 immediate(11, 0, 5, 0),
                                 0x4182fff8,
                                 immediate(36, 20, 13, 0x80),
                                 immediate(36, 21, 13, 0x34),
                                 immediate(32, 9, 13, 0x80),
                                 immediate(36, 9, 12, 0x6000),
                                 immediate(40, 10, 14, 0x2c),
                                 immediate(36, 10, 12, 0x6004),
                                 immediate(14, 11, 11, 1),
                                 0x4bffffd8,
                                 0x60000000};
  std::vector<u8> dol(0x160);
  const auto word = [&](std::size_t offset, u32 value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      dol[offset + byte] = static_cast<u8>(value >> (24 - byte * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, 0x60);
  word(0xe0, 0x8000401c);
  for (std::size_t index = 0; index < code.size(); ++index)
    word(0x11c + index * 4, code[index]);
  return dol;
}

void same_cpu(const cpu::State &a, const cpu::State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned index = 0; index < a.fpr.size(); ++index)
    CHECK(a.fpr[index].bits == b.fpr[index].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}

std::string reported_state(runtime::Machine &machine) {
  std::ostringstream output;
  machine.write_state(output, "test");
  auto text = output.str();
  const auto first = text.find(",\"native_executed\":");
  const auto last = text.find(",\"cpu_state_hash\":");
  CHECK(first != std::string::npos && last > first);
  text.erase(first, last - first);
  return text;
}

enum class Mode { Devices, Decrementer, Monitor, External, FrameLimit, Trace };

struct OrderedEvent {
  std::array<u64, 10> properties{};
  cpu::State cpu;
  std::vector<s16> samples;
};

void record_events(runtime::Machine &machine, std::vector<OrderedEvent> &events) {
  auto &devices = machine.devices();
  const auto frame = devices.xfb_present;
  devices.xfb_present = [&machine, &events, frame](const hw::Xfb &xfb) {
    events.push_back(
        {{1, xfb.cycles, machine.devices().field_count(), xfb.address, xfb.width, xfb.height,
          xfb.stride_bytes, xfb.field, machine.devices().interrupt_causes(0xffffffff),
          std::bit_cast<u32>(xfb.aspect_ratio)},
         machine.cpu().state(),
         {}});
    frame(xfb);
  };
  const auto audio = devices.audio_samples;
  devices.audio_samples = [&machine, &events, audio](std::span<const s16> samples, u32 rate) {
    events.push_back(
        {{2, machine.devices().cycles(), rate, machine.devices().interrupt_causes(0xffffffff)},
         machine.cpu().state(),
         {samples.begin(), samples.end()}});
    audio(samples, rate);
  };
  const auto disc = devices.disc_audio_samples;
  devices.disc_audio_samples = [&machine, &events, disc](std::span<const s16> samples, u32 divisor,
                                                         u8 left, u8 right) {
    events.push_back({{3, machine.devices().cycles(), divisor, left, right,
                       machine.devices().interrupt_causes(0xffffffff)},
                      machine.cpu().state(),
                      {samples.begin(), samples.end()}});
    disc(samples, divisor, left, right);
  };
  const auto dma = devices.dma_write;
  devices.dma_write = [&machine, &events, dma](u32 address, std::span<const u8> bytes) {
    events.push_back({{4, machine.devices().cycles(), address, bytes.size(),
                       machine.devices().interrupt_causes(0xffffffff)},
                      machine.cpu().state(),
                      {bytes.begin(), bytes.end()}});
    dma(address, bytes);
  };
}

void prepare(runtime::Machine &machine, platform::Output &output, Console console,
             cpu::ExecutionBackend backend, const std::filesystem::path &replay, Mode mode,
             bool polling = false, u32 first_field_delay = 2, std::span<const u8> executable = {}) {
  machine.set_output(&output);
  machine.cpu().set_execution_backend(backend);
  machine.set_input_replay(replay);
  if (executable.empty())
    machine.load_executable(polling ? polling_program() : program());
  else
    machine.load_executable(executable);
  auto &memory = machine.memory();
  auto &state = machine.cpu().state();
  state.gpr[12] = 0x80000000;
  state.gpr[13] = 0xcc006400;
  state.gpr[20] = 0x40000000;
  state.gpr[21] = 0x701;
  state.spr[cpu::spr::dec] = 0xffffffff;
  memory.write32(0x900, 0x4c000064); // rfi
  memory.write32(0xf00, 0x4c000064);
  // A real memory-access PI interrupt acknowledges itself before returning.
  // Exception entry clears relocation, so the handler uses the physical PI
  // address rather than the normal mapped guest alias.
  memory.write32(0x500, immediate(15, 29, 0, 0x0c00));
  memory.write32(0x504, immediate(14, 30, 0, 1));
  memory.write32(0x508, immediate(36, 30, 29, 0x3000));
  memory.write32(0x50c, 0x4c000064);

  const u32 halfline = console == Console::GameCube ? 7722 : 11583;
  auto &devices = machine.devices();
  devices.write(0x0c002000, 16, 2);
  devices.write(0x0c00200c, 0x10001, 4);
  devices.write(0x0c002010, 0x10001, 4);
  devices.write(0x0c00201c, 0x10000100, 4);
  devices.write(0x0c002024, 0x10000100, 4);
  for (u32 address = 0x2000; address < 0x2500; address += 4)
    memory.write32(address, 0x51805180);
  devices.advance(3u * halfline - first_field_delay);

  for (u32 address = 0x8000; address < 0x8060; address += 4)
    memory.write32(address, 0x01fefc03);
  devices.write(0x0c005030, 0x8000, 4);
  devices.write(0x0c005036, 0x8003, 2);
  machine.graphics().write(0x0c00100a, 3, 2);
  const std::array<u8, 15> commands{0x61, 0x48, 0,    0x12, 0x34, 0x61, 0x47, 0,
                                    0xab, 0xcd, 0x61, 0x45, 0,    0,    2};
  machine.graphics().submit(commands);

  if (console == Console::Wii) {
    const std::array<u8, 8> path{'/', 'd', 'e', 'v', '/', 'f', 's', 0};
    memory.copy_in(0x11000, path);
    memory.write32(0x10000, 1);
    memory.write32(0x1000c, 0x11000);
    memory.write32(0x10010, 3);
    devices.write(0x0d000000, 0x10000, 4);
    devices.write(0x0d000004, 0x31, 4);
  }
  if (mode == Mode::Decrementer) {
    state.msr |= cpu::msr::ee;
    state.spr[cpu::spr::dec] = 1;
  } else if (mode == Mode::Monitor) {
    state.spr[cpu::spr::mmcr0] = (1u << 6) | 1;
    state.spr[cpu::spr::mmcr1] = (11u << 27) | (1u << 22);
  } else if (mode == Mode::External) {
    state.msr |= cpu::msr::ee;
    devices.write(0x0c003004, 1, 4);
    memory.memory_access_error();
  }
  // The Wii DMA packet period exceeds 100,000 cycles. Retain enough execution
  // for nonzero packets on both consoles after programming the DMA descriptor.
  machine.set_limits({200003, mode == Mode::FrameLimit ? u64{1} : 0, 0});
}

enum class PollBoundary {
  Quota,
  Frame,
  Dma,
  Dsp,
  Ios,
  HostPoll,
  ReplayReplacement,
  Decrementer,
  External,
  Overflow,
  ZeroCycles,
  Fault
};

void compare_ram_poll(Console console, cpu::ExecutionBackend backend, PollBoundary boundary) {
  TemporaryFile replay, replacement;
  {
    std::ofstream input(replay.path);
    input << "instruction 0 pad0.a=1\n"
             "instruction 4095 pad0.a=0 pad0.b=1\n"
             "instruction 4096 pad0.b=0 pad0.x=1\n"
             "instruction 4097 pad0.x=0 pad0.y=1\n"
             "field 1 pad0.stick-y=70\nfield 2 pad0.stick-y=90\n";
    std::ofstream next(replacement.path);
    next << "instruction 0 pad0.a=0 pad0.x=1\n";
    CHECK(input && next);
  }
  platform::Output stepped_output({true, false, {}, {}}), grouped_output({true, false, {}, {}});
  auto stepped = std::make_unique<runtime::Machine>(console);
  auto grouped = std::make_unique<runtime::Machine>(console);
  std::vector<OrderedEvent> stepped_events, grouped_events;
  for (const auto &pair :
       {std::pair{stepped.get(), &stepped_output}, std::pair{grouped.get(), &grouped_output}}) {
    auto &machine = *pair.first;
    prepare(machine, *pair.second, console, backend, replay.path,
            boundary == PollBoundary::Decrementer ? Mode::Decrementer : Mode::Devices, true,
            boundary == PollBoundary::HostPoll || boundary == PollBoundary::ReplayReplacement ? 4095
                                                                                              : 2);
    auto &state = machine.cpu().state();
    state.gpr[3] = 0x80009000;
    state.gpr[14] = 0xcc002000;
    if (boundary == PollBoundary::External) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 0x600, 4);
    }
    if (boundary == PollBoundary::Dsp) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 0x40, 4);
    }
    if (boundary == PollBoundary::Ios) {
      state.gpr[3] = 0x80010000;
      machine.memory().write32(0x4020, immediate(11, 0, 5, 1));
    }
    machine.set_limits({boundary == PollBoundary::Dsp
                            ? (console == Console::Wii ? 729000u : 486000u) + 37
                        : boundary == PollBoundary::Dma ? 1000003u
                                                        : 12007u,
                        boundary == PollBoundary::Frame ? u64{1} : 0, 0});
    if (boundary == PollBoundary::Overflow) {
      state.instructions = std::numeric_limits<u64>::max() - 4;
      state.cycles = std::numeric_limits<u64>::max() - 4;
      state.timebase = std::numeric_limits<u64>::max() - 1;
      machine.set_limits({31, 0, 0});
    } else if (boundary == PollBoundary::ZeroCycles) {
      machine.memory().write32(0x401c, 0);
      machine.memory().write32(0x4020, 0);
      machine.memory().write32(0x4024, 0);
      machine.set_limits({3, 0, 0});
    } else if (boundary == PollBoundary::Fault) {
      state.pc = 0x9000401c;
      machine.set_limits({17, 0, 0});
    } else if (boundary == PollBoundary::Dma || boundary == PollBoundary::Dsp) {
      auto &devices = machine.devices();
      if (boundary == PollBoundary::Dma) {
        devices.write(0x0c006008, 0x12000000, 4);
        devices.write(0x0c006014, 0x9000, 4);
        devices.write(0x0c006018, 32, 4);
        devices.write(0x0c00601c, 3, 4);
      }
      // The card program raises periodic DSP interrupts while its mail is queued.
      devices.write(0x0c00500a, 0x900, 2);
      (void)devices.read(0x0c005004, 2);
      (void)devices.read(0x0c005006, 2);
      std::array<u8, 32> firmware{};
      for (unsigned bit = 0; bit < 32; ++bit)
        if (0x65d6cc6fu & (1u << bit)) {
          const unsigned distance = (bit * 11) % 32;
          firmware[distance ? 32 - distance : 0] ^= 1;
        }
      machine.memory().copy_in(0x18000, firmware);
      const auto send = [&](u32 value) {
        devices.write(0x0c005000, value >> 16, 2);
        devices.write(0x0c005002, value & 65535, 2);
      };
      for (u32 value : {0x80f3a001u, 0x18000u, 0x80f3a002u, 32u, 0x80f3d001u, 0u})
        send(value);
    }
    record_events(machine, pair.first == stepped.get() ? stepped_events : grouped_events);
    if (boundary == PollBoundary::HostPoll || boundary == PollBoundary::ReplayReplacement) {
      // Schedule the callback at attempt4095, one attempt before the host poll.
      const auto frame = machine.devices().xfb_present;
      machine.devices().xfb_present = [&machine, frame, boundary,
                                       &replacement](const hw::Xfb &xfb) {
        frame(xfb);
        if (boundary == PollBoundary::HostPoll)
          machine.set_limits({0, 0, std::numeric_limits<double>::min()});
        else {
          machine.set_input_replay(replacement.path);
          machine.memory().write32(0x9000, 1);
          machine.set_limits({machine.cpu().state().instructions + 13, 0, 0});
        }
      };
    }
  }
  const auto execute = [](runtime::Machine &machine, bool run) {
    try {
      if (run)
        machine.run();
      for (;;)
        machine.step();
    } catch (const runtime::LimitReached &limit) {
      return std::string(limit.what());
    }
  };
  const auto outcome = execute(*stepped, false);
  CHECK(outcome == execute(*grouped, true));
  CHECK(outcome == (boundary == PollBoundary::Frame      ? "frame-limit"
                    : boundary == PollBoundary::HostPoll ? "time-limit"
                                                         : "instruction-limit"));
  same_cpu(stepped->cpu().state(), grouped->cpu().state());
  CHECK(reported_state(*stepped) == reported_state(*grouped));
  for (const auto &[address, size] :
       {std::pair{0u, Memory::mem1_size}, std::pair{0xe0000000u, Memory::cache_size}}) {
    const auto a = stepped->memory().range(address, size),
               b = grouped->memory().range(address, size);
    CHECK(std::equal(a.begin(), a.end(), b.begin()));
  }
  if (console == Console::Wii) {
    const auto a = stepped->memory().range(0x10000000, Memory::mem2_size);
    const auto b = grouped->memory().range(0x10000000, Memory::mem2_size);
    CHECK(std::equal(a.begin(), a.end(), b.begin()));
  }
  CHECK(stepped_events.size() == grouped_events.size());
  for (std::size_t i = 0; i < stepped_events.size(); ++i) {
    CHECK(stepped_events[i].properties == grouped_events[i].properties);
    CHECK(stepped_events[i].samples == grouped_events[i].samples);
    same_cpu(stepped_events[i].cpu, grouped_events[i].cpu);
  }
  if (boundary == PollBoundary::Quota && backend == cpu::ExecutionBackend::NativeInteger) {
    // A long taken RAM loop must actually use the bulk path in this comparison.
    CHECK(grouped->cpu().native_statistics().executed * 16 <
          stepped->cpu().native_statistics().executed);
  }
  if (boundary == PollBoundary::Dma) {
    CHECK(grouped->memory().read32(0x9000) == 2);
    CHECK(grouped->cpu().state().gpr[11] > 0);
    CHECK(std::any_of(grouped_events.begin(), grouped_events.end(), [](const OrderedEvent &event) {
      return event.properties[0] == 4 && event.properties[2] == 0x9000;
    }));
  }
  if (boundary == PollBoundary::ZeroCycles)
    CHECK(grouped->cpu().state().instructions == 0 && grouped->cpu().state().cycles == 0);
  if (boundary == PollBoundary::Overflow)
    CHECK(grouped->cpu().state().instructions == 26 && grouped->cpu().state().cycles == 26 &&
          grouped->cpu().state().timebase == 0);
  if (boundary == PollBoundary::External)
    CHECK(grouped->cpu().state().spr[cpu::spr::srr0] == 0x80004024);
  if (boundary == PollBoundary::Dsp)
    CHECK(grouped->cpu().state().spr[cpu::spr::srr0] == 0x8000401c);
  if (boundary == PollBoundary::Ios)
    CHECK(grouped->memory().read32(0x10000) == 8 && grouped->cpu().state().gpr[11] > 0);
  if (boundary == PollBoundary::HostPoll) {
    CHECK(grouped->cpu().state().instructions == 4096);
    CHECK(!grouped_events.empty() && grouped_events.front().cpu.instructions == 4095);
  }
  if (boundary == PollBoundary::ReplayReplacement) {
    CHECK(grouped->cpu().state().instructions == 4108);
    CHECK((grouped->memory().read32(0x6000) >> 16 & 0x400) != 0);
  }
}

void compare_runtime(Console console, cpu::ExecutionBackend backend, Mode mode) {
  TemporaryFile replay, stepped_trace, grouped_trace;
  {
    std::ofstream input(replay.path);
    input << "instruction 0 pad0.a=1 pad0.stick-x=200\n"
             "instruction 7 pad0.a=0 pad0.b=1\n"
             "instruction 8 pad0.stick-x=20\n"
             "instruction 9 pad0.b=0\n"
             "instruction 15 pad0.a=1\n"
             "instruction 16 pad0.a=0\n"
             "instruction 17 pad0.stick-x=128\n"
             "instruction 4095 pad0.b=1\n"
             "instruction 4096 pad0.stick-x=55\n"
             "instruction 4097 pad0.b=0\n"
             "field 1 pad0.stick-y=70\n"
             "field 2 pad0.stick-y=90\n";
    CHECK(input);
  }
  platform::Output stepped_output({true, false, {}, {}}), grouped_output({true, false, {}, {}});
  auto stepped = std::make_unique<runtime::Machine>(console);
  auto grouped = std::make_unique<runtime::Machine>(console);
  prepare(*stepped, stepped_output, console, backend, replay.path, mode);
  prepare(*grouped, grouped_output, console, backend, replay.path, mode);
  std::vector<OrderedEvent> stepped_events, grouped_events;
  record_events(*stepped, stepped_events);
  record_events(*grouped, grouped_events);
  if (mode == Mode::Trace) {
    stepped->set_trace(stepped_trace.path, 40, false, 4090);
    grouped->set_trace(grouped_trace.path, 40, false, 4090);
  }
  const auto execute = [&](runtime::Machine &machine, bool run) {
    std::string outcome;
    try {
      if (run)
        machine.run();
      for (;;)
        machine.step();
    } catch (const runtime::LimitReached &limit) {
      outcome = limit.what();
    }
    CHECK(outcome == (mode == Mode::FrameLimit ? "frame-limit" : "instruction-limit"));
  };
  execute(*stepped, false);
  execute(*grouped, true);
  same_cpu(stepped->cpu().state(), grouped->cpu().state());
  CHECK(reported_state(*stepped) == reported_state(*grouped));
  CHECK(stepped_events.size() == grouped_events.size());
  for (std::size_t index = 0; index < stepped_events.size(); ++index) {
    CHECK(stepped_events[index].properties == grouped_events[index].properties);
    CHECK(stepped_events[index].samples == grouped_events[index].samples);
    same_cpu(stepped_events[index].cpu, grouped_events[index].cpu);
  }
  const auto same_memory = [&](u32 address, u32 size) {
    const auto a = stepped->memory().range(address, size);
    const auto b = grouped->memory().range(address, size);
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  };
  same_memory(0, Memory::mem1_size);
  same_memory(0xe0000000, Memory::cache_size);
  if (console == Console::Wii)
    same_memory(0x10000000, Memory::mem2_size);
  CHECK(grouped_output.presented_frames() > 0);
  if (mode != Mode::FrameLimit) {
    CHECK(grouped->memory().read32(0x6004) > 0);
    CHECK(grouped_output.audio_nonzero_samples() > 0);
    CHECK(grouped->graphics().read(0x0c00100e, 2) == 0xabcd);
    if (console == Console::Wii)
      CHECK(grouped->memory().read32(0x10000) == 8);
  }
  if (backend == cpu::ExecutionBackend::NativeInteger &&
      (mode == Mode::Devices || mode == Mode::Decrementer || mode == Mode::External))
    CHECK(grouped->cpu().native_statistics().block_executed > 0);

  // Equal final snapshots also continue through subsequent MMIO observations.
  for (u32 address : {0x0c003000u, 0x0c002030u, 0x0c006430u, 0x0c006434u, 0x0c006438u})
    CHECK(stepped->memory().read32(address) == grouped->memory().read32(address));
  stepped.reset();
  grouped.reset();
  if (mode == Mode::Trace)
    CHECK(runtime::read_file(stepped_trace.path, 65536) ==
          runtime::read_file(grouped_trace.path, 65536));
}

std::vector<u8> fallthrough_copy_program() {
  // Eight executed words begin at the last two words of one resident line.
  // A not-taken interior branch precedes the RAM store and terminal back edge.
  const std::array<u32, 12> code{immediate(32, 0, 5, 0),
                                 immediate(11, 0, 0, 0),
                                 0x41820008,
                                 immediate(36, 0, 3, 0),
                                 immediate(14, 3, 3, 4),
                                 immediate(14, 5, 5, 4),
                                 (31u << 26) | (3u << 16) | (4u << 11),
                                 0x4082ffe4,
                                 immediate(14, 11, 11, 1),
                                 immediate(14, 3, 3, 0xffc0),
                                 immediate(14, 5, 5, 0xffc0),
                                 0x4bffffd4};
  std::vector<u8> dol(0x180);
  const auto word = [&](std::size_t offset, u32 value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      dol[offset + byte] = u8(value >> (24 - byte * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, 0x70);
  word(0xe0, 0x80004018);
  for (std::size_t i = 0; i < code.size(); ++i)
    word(0x118 + i * 4, code[i]);
  return dol;
}

enum class CopyBoundary {
  Quota,
  FieldCut,
  DecCut,
  Dma,
  Dsp,
  Ios,
  Replay,
  HostPoll,
  Frame,
  DeclinedMmio,
  DeclinedFault,
  TakenSequential,
  Trace,
  External
};

void compare_copy(Console console, cpu::ExecutionBackend backend, CopyBoundary boundary,
                  unsigned cut = 0, bool quiet = false, std::span<const u8> executable = {}) {
  const auto copy_executable = fallthrough_copy_program();
  if (executable.empty())
    executable = copy_executable;
  TemporaryFile replay, replacement, stepped_trace, grouped_trace;
  {
    std::ofstream input(replay.path), next(replacement.path);
    if (quiet)
      input << "instruction 0 pad0.a=1\n";
    else
      input << "instruction 0 pad0.a=1\ninstruction 3 pad0.a=0\ninstruction 4 pad0.b=1\n"
               "instruction 7 pad0.b=0\ninstruction 8 pad0.x=1\ninstruction 9 pad0.x=0\n"
               "instruction 4095 pad0.y=1\ninstruction 4096 pad0.y=0\ninstruction 4097 pad0.a=1\n"
               "field 1 pad0.stick-y=70\nfield 2 pad0.stick-y=90\n";
    next << "instruction 0 pad0.a=0 pad0.x=1\n";
    CHECK(input && next);
  }
  platform::Output stepped_output({true, false, {}, {}}), grouped_output({true, false, {}, {}});
  auto stepped = std::make_unique<runtime::Machine>(console),
       grouped = std::make_unique<runtime::Machine>(console);
  std::vector<OrderedEvent> stepped_events, grouped_events;
  for (const auto &pair :
       {std::pair{stepped.get(), &stepped_output}, std::pair{grouped.get(), &grouped_output}}) {
    auto &machine = *pair.first;
    prepare(machine, *pair.second, console, backend, replay.path, Mode::Devices, false,
            boundary == CopyBoundary::HostPoll || boundary == CopyBoundary::Replay  ? 4095
            : boundary == CopyBoundary::FieldCut || boundary == CopyBoundary::Frame ? cut
                                                                                    : 20000,
            executable);
    auto &state = machine.cpu().state();
    state.gpr[3] = 0x8000b000;
    const unsigned bytes = quiet ? 4096 : 64;
    state.gpr[4] = state.gpr[3] + bytes;
    state.gpr[5] = 0x80009000;
    for (unsigned offset = 0; offset < bytes; offset += 4)
      machine.memory().write32(0x9000 + offset, 0x12340001 + offset);
    machine.graphics().advance(4096);
    // Warm both instruction lines using only public one-instruction CPU calls.
    // Twenty-four cycles leaves both hidden timebase/DEC remainders at zero.
    const auto saved = state;
    for (unsigned i = 0; i < 24; ++i)
      CHECK(machine.cpu().step() == 1);
    state = saved;
    for (unsigned offset = 0; offset < bytes; offset += 4)
      machine.memory().write32(0xb000 + offset, 0);
    machine.set_limits({boundary == CopyBoundary::Quota || boundary == CopyBoundary::DecCut ? cut
                        : boundary == CopyBoundary::Dma ? 1000003u
                        : boundary == CopyBoundary::Dsp
                            ? (console == Console::Wii ? 729000u : 486000u) + 37
                            : 12007u,
                        boundary == CopyBoundary::Frame ? u64{1} : 0, 0});
    if (boundary == CopyBoundary::DecCut) {
      state.msr |= cpu::msr::ee;
      state.spr[cpu::spr::dec] = 1;
    } else if (boundary == CopyBoundary::External) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 1, 4);
      machine.memory().memory_access_error();
    } else if (boundary == CopyBoundary::DeclinedMmio) {
      state.gpr[3] = 0xcc006480;
      state.gpr[4] = state.gpr[3] + 64;
    } else if (boundary == CopyBoundary::DeclinedFault) {
      state.gpr[3] = 0x50000000;
      state.gpr[4] = state.gpr[3] + 64;
      machine.set_limits({17, 0, 0});
    } else if (boundary == CopyBoundary::TakenSequential) {
      // Invalidate the cached line after changing the instruction through RAM.
      machine.memory().write32(0x4020, 0x41820004);
      machine.memory().write32(0x4100, extended(982, 0, 0, 6));
      for (unsigned i = 0; i < 8; ++i)
        machine.memory().write32(0x4104 + i * 4, 0x60000000);
      state.gpr[6] = 0x80004020;
      const auto changed = state;
      state.pc = 0x80004100;
      CHECK(machine.cpu().step() == 4);
      for (unsigned i = 0; i < 8; ++i)
        CHECK(machine.cpu().step() == 1);
      state = changed;
      for (unsigned offset = 0; offset < 64; offset += 4)
        machine.memory().write32(0x9000 + offset, 0);
    } else if (boundary == CopyBoundary::Dma) {
      machine.devices().write(0x0c006008, 0x12000000, 4);
      machine.devices().write(0x0c006014, 0x9000, 4);
      machine.devices().write(0x0c006018, 32, 4);
      machine.devices().write(0x0c00601c, 3, 4);
    } else if (boundary == CopyBoundary::Dsp) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 0x40, 4);
      auto &devices = machine.devices();
      devices.write(0x0c00500a, 0x900, 2);
      (void)devices.read(0x0c005004, 2);
      (void)devices.read(0x0c005006, 2);
      std::array<u8, 32> firmware{};
      for (unsigned bit = 0; bit < 32; ++bit)
        if (0x65d6cc6fu & (1u << bit)) {
          const unsigned distance = (bit * 11) % 32;
          firmware[distance ? 32 - distance : 0] ^= 1;
        }
      machine.memory().copy_in(0x18000, firmware);
      const auto send = [&](u32 value) {
        devices.write(0x0c005000, value >> 16, 2);
        devices.write(0x0c005002, value & 65535, 2);
      };
      for (u32 value : {0x80f3a001u, 0x18000u, 0x80f3a002u, 32u, 0x80f3d001u, 0u})
        send(value);
    } else if (boundary == CopyBoundary::Ios)
      state.gpr[5] = 0x80010000;
    if (boundary == CopyBoundary::Trace)
      machine.set_trace(pair.first == stepped.get() ? stepped_trace.path : grouped_trace.path, 40,
                        false, 4090);
    record_events(machine, pair.first == stepped.get() ? stepped_events : grouped_events);
    if (boundary == CopyBoundary::HostPoll || boundary == CopyBoundary::Replay) {
      const auto frame = machine.devices().xfb_present;
      machine.devices().xfb_present = [&machine, frame, boundary,
                                       &replacement](const hw::Xfb &xfb) {
        frame(xfb);
        if (boundary == CopyBoundary::HostPoll)
          machine.set_limits({0, 0, std::numeric_limits<double>::min()});
        else {
          machine.set_input_replay(replacement.path);
          machine.memory().write32(0x9000, 0);
          machine.set_limits({machine.cpu().state().instructions + 13, 0, 0});
        }
      };
    }
  }
  const auto execute = [](runtime::Machine &machine, bool run) {
    try {
      if (run)
        machine.run();
      for (;;)
        machine.step();
    } catch (const runtime::LimitReached &limit) {
      return std::string(limit.what());
    }
  };
  const auto outcome = execute(*stepped, false);
  CHECK(outcome == execute(*grouped, true));
  CHECK(outcome == (boundary == CopyBoundary::HostPoll ? "time-limit"
                    : boundary == CopyBoundary::Frame  ? "frame-limit"
                                                       : "instruction-limit"));
  same_cpu(stepped->cpu().state(), grouped->cpu().state());
  CHECK(reported_state(*stepped) == reported_state(*grouped));
  for (const auto &[address, size] :
       {std::pair{0u, Memory::mem1_size}, std::pair{0xe0000000u, Memory::cache_size}}) {
    const auto a = stepped->memory().range(address, size),
               b = grouped->memory().range(address, size);
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }
  if (console == Console::Wii) {
    const auto a = stepped->memory().range(0x10000000, Memory::mem2_size),
               b = grouped->memory().range(0x10000000, Memory::mem2_size);
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }
  CHECK(stepped_events.size() == grouped_events.size());
  for (std::size_t i = 0; i < stepped_events.size(); ++i) {
    CHECK(stepped_events[i].properties == grouped_events[i].properties);
    CHECK(stepped_events[i].samples == grouped_events[i].samples);
    same_cpu(stepped_events[i].cpu, grouped_events[i].cpu);
  }
  if (boundary == CopyBoundary::FieldCut || boundary == CopyBoundary::Frame)
    CHECK(!grouped_events.empty() && grouped_events.front().cpu.instructions == cut);
  if (boundary == CopyBoundary::HostPoll)
    CHECK(grouped->cpu().state().instructions == 4096);
  if (boundary == CopyBoundary::Replay)
    CHECK(grouped->cpu().state().instructions == 4108);
  if (boundary == CopyBoundary::Dma) {
    CHECK(grouped->memory().read32(0x9000) == 2);
    CHECK(std::any_of(grouped_events.begin(), grouped_events.end(), [](const OrderedEvent &event) {
      return event.properties[0] == 4 && event.properties[2] == 0x9000;
    }));
  }
  if (boundary == CopyBoundary::Dsp)
    CHECK(grouped->cpu().state().spr[cpu::spr::srr0] != 0);
  if (boundary == CopyBoundary::External)
    CHECK(grouped->cpu().state().spr[cpu::spr::srr0] == 0x80004018);
  if (boundary == CopyBoundary::Ios)
    CHECK(grouped->memory().read32(0x10000) == 8);
  if (boundary == CopyBoundary::DeclinedFault)
    CHECK(grouped->cpu().state().spr[cpu::spr::dar] == 0x50000000);
  stepped.reset();
  grouped.reset();
  if (boundary == CopyBoundary::Trace)
    CHECK(runtime::read_file(stepped_trace.path, 65536) ==
          runtime::read_file(grouped_trace.path, 65536));
}

std::vector<u8> register_branch_program() {
  // The compare and conditional branch straddle resident instruction lines.
  // A taken branch repeats register work; a fallthrough stores the completed
  // count before resetting it. This exercises repeated groups without RAM
  // instructions in the prefix, followed by a normal RAM observation.
  const std::array<u32, 7> code{
      immediate(14, 7, 7, 1), immediate(11, 0, 7, 13),  0x4082fff9, immediate(36, 7, 12, 0x6008),
      immediate(14, 7, 0, 0), immediate(14, 11, 11, 1), 0x4bffffe8};
  std::vector<u8> dol(0x180);
  const auto word = [&](std::size_t offset, u32 value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      dol[offset + byte] = u8(value >> (24 - byte * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, 0x70);
  word(0xe0, 0x80004018);
  for (std::size_t index = 0; index < code.size(); ++index)
    word(0x118 + index * 4, code[index]);
  return dol;
}

std::vector<u8> flush_program(unsigned offset = 3, bool link = false) {
  const std::array<u32, 6> code{extended(86, 0, 3, 4),        immediate(14, 3, 3, 32),
                                0x4200fff8u | u32(link),      immediate(14, 11, 11, 1),
                                immediate(36, 3, 12, 0x6008), 0x4bffffecu};
  std::vector<u8> dol(0x180);
  const auto word = [&](std::size_t at, u32 value) {
    for (unsigned byte = 0; byte < 4; ++byte)
      dol[at + byte] = u8(value >> (24 - byte * 8));
  };
  word(0, 0x100);
  word(0x48, 0x80004000);
  word(0x90, 0x80);
  word(0xe0, 0x80004000 + offset * 4);
  for (std::size_t i = 0; i < code.size(); ++i)
    word(0x100 + offset * 4 + i * 4, code[i]);
  return dol;
}

enum class FlushBoundary {
  Quota,
  FieldCut,
  DecPhase,
  Frame,
  Dma,
  Dsp,
  Ios,
  Pe,
  External,
  Replay,
  HostPoll,
  CtrExit,
  Trace,
  Overflow,
  AddressWrap,
  MmioAddress
};

void compare_flush(Console console, cpu::ExecutionBackend backend, FlushBoundary boundary,
                   unsigned cut = 0, unsigned offset = 3, bool link = false) {
  const auto executable = flush_program(offset, link);
  TemporaryFile replay, replacement, stepped_trace, grouped_trace;
  {
    std::ofstream input(replay.path), next(replacement.path);
    input << "instruction 0 pad0.a=1\ninstruction 3 pad0.a=0\ninstruction 4 pad0.b=1\n"
             "instruction 7 pad0.b=0\ninstruction 8 pad0.x=1\ninstruction 9 pad0.x=0\n"
             "instruction 4095 pad0.y=1\ninstruction 4096 pad0.y=0\ninstruction 4097 pad0.a=1\n"
             "field 1 pad0.stick-y=70\nfield 2 pad0.stick-y=90\n";
    next << "instruction 0 pad0.a=0 pad0.x=1\n";
    CHECK(input && next);
  }
  platform::Output stepped_output({true, false, {}, {}}), grouped_output({true, false, {}, {}});
  auto stepped = std::make_unique<runtime::Machine>(console),
       grouped = std::make_unique<runtime::Machine>(console);
  std::vector<OrderedEvent> stepped_events, grouped_events;
  for (const auto &pair :
       {std::pair{stepped.get(), &stepped_output}, std::pair{grouped.get(), &grouped_output}}) {
    auto &machine = *pair.first;
    const u32 field_delay =
        boundary == FlushBoundary::HostPoll || boundary == FlushBoundary::Replay  ? 9555
        : boundary == FlushBoundary::FieldCut || boundary == FlushBoundary::Frame ? cut
                                                                                  : 20000;
    prepare(machine, *pair.second, console, backend, replay.path, Mode::Devices, false, field_delay,
            executable);
    auto &state = machine.cpu().state();
    state.gpr[3] = 0x80009000;
    state.gpr[4] = 0;
    state.spr[cpu::spr::ctr] = 0;
    machine.graphics().advance(4096);
    // Public one-instruction calls warm the resident line. Ordinary cases
    // use24 cycles. DEC cases warm with a positive counter and finish on the
    // loop line at the selected hidden timebase and decrementer phase.
    for (unsigned i = 0; i < 15; ++i)
      machine.memory().write32(0x4100 + i * 4, 0x60000000);
    const auto saved = state;
    if (boundary == FlushBoundary::DecPhase) {
      CHECK(cut < 12);
      state.spr[cpu::spr::dec] = 10;
    }
    for (unsigned i = 0; i < 9; ++i)
      CHECK(machine.cpu().step() == (i % 3 == 0 ? 5u : 1u));
    state.pc = 0x80004100;
    const unsigned nops = boundary == FlushBoundary::DecPhase ? (cut + 10) % 12 : 3;
    for (unsigned i = 0; i < nops; ++i)
      CHECK(machine.cpu().step() == 1);
    if (boundary == FlushBoundary::DecPhase) {
      state.pc = saved.pc;
      CHECK(machine.cpu().step() == 5);
    }
    state = saved;
    const u64 attempts = boundary == FlushBoundary::Quota     ? cut
                         : boundary == FlushBoundary::CtrExit ? cut * 3u + 1
                         : boundary == FlushBoundary::Dma     ? 1000003u
                         : boundary == FlushBoundary::Dsp
                             ? (console == Console::Wii ? 729000u : 486000u) + 37
                             : 12007u;
    machine.set_limits({attempts, boundary == FlushBoundary::Frame ? u64{1} : 0, 0});
    if (boundary == FlushBoundary::DecPhase) {
      state.msr |= cpu::msr::ee;
      state.spr[cpu::spr::dec] = 4;
      machine.set_limits({31, 0, 0});
    } else if (boundary == FlushBoundary::CtrExit) {
      state.spr[cpu::spr::ctr] = cut;
    } else if (boundary == FlushBoundary::AddressWrap) {
      state.gpr[3] = 0xfffffff0;
    } else if (boundary == FlushBoundary::MmioAddress) {
      state.gpr[3] = 0xcc006480;
      state.gpr[4] = 0x40000000;
    } else if (boundary == FlushBoundary::Overflow) {
      state.instructions = std::numeric_limits<u64>::max() - 7;
      state.cycles = std::numeric_limits<u64>::max() - 9;
      state.timebase = std::numeric_limits<u64>::max() - 1;
      machine.set_limits({31, 0, 0});
    } else if (boundary == FlushBoundary::External) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 1, 4);
      machine.memory().memory_access_error();
    } else if (boundary == FlushBoundary::Pe) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 0x600, 4);
      machine.graphics().write(0x0c00100a, 3, 2);
      const std::array<u8, 15> commands{0x61, 0x48, 0,    0x12, 0x34, 0x61, 0x47, 0,
                                        0xab, 0xcd, 0x61, 0x45, 0,    0,    2};
      machine.graphics().submit(commands);
      machine.set_limits({31, 0, 0});
    } else if (boundary == FlushBoundary::Dma) {
      machine.devices().write(0x0c006008, 0x12000000, 4);
      machine.devices().write(0x0c006014, 0x9000, 4);
      machine.devices().write(0x0c006018, 32, 4);
      machine.devices().write(0x0c00601c, 3, 4);
    } else if (boundary == FlushBoundary::Dsp) {
      state.msr |= cpu::msr::ee;
      machine.devices().write(0x0c003004, 0x40, 4);
      auto &devices = machine.devices();
      devices.write(0x0c00500a, 0x900, 2);
      (void)devices.read(0x0c005004, 2);
      (void)devices.read(0x0c005006, 2);
      std::array<u8, 32> firmware{};
      for (unsigned bit = 0; bit < 32; ++bit)
        if (0x65d6cc6fu & (1u << bit)) {
          const unsigned distance = (bit * 11) % 32;
          firmware[distance ? 32 - distance : 0] ^= 1;
        }
      machine.memory().copy_in(0x18000, firmware);
      const auto send = [&](u32 value) {
        devices.write(0x0c005000, value >> 16, 2);
        devices.write(0x0c005002, value & 65535, 2);
      };
      for (u32 value : {0x80f3a001u, 0x18000u, 0x80f3a002u, 32u, 0x80f3d001u, 0u})
        send(value);
    }
    if (boundary == FlushBoundary::Trace)
      machine.set_trace(pair.first == stepped.get() ? stepped_trace.path : grouped_trace.path, 40,
                        false, 4090);
    record_events(machine, pair.first == stepped.get() ? stepped_events : grouped_events);
    if (boundary == FlushBoundary::HostPoll || boundary == FlushBoundary::Replay) {
      const auto frame = machine.devices().xfb_present;
      machine.devices().xfb_present = [&machine, frame, boundary,
                                       &replacement](const hw::Xfb &xfb) {
        frame(xfb);
        if (boundary == FlushBoundary::HostPoll)
          machine.set_limits({0, 0, std::numeric_limits<double>::min()});
        else {
          machine.set_input_replay(replacement.path);
          machine.cpu().state().gpr[3] = 0xfffffff0;
          machine.cpu().state().spr[cpu::spr::ctr] = 2;
          machine.memory().write32(0x9000, 0x12345678);
          machine.set_limits({machine.cpu().state().instructions + 13, 0, 0});
        }
      };
    }
  }
  const auto execute = [](runtime::Machine &machine, bool run) {
    try {
      if (run)
        machine.run();
      for (;;)
        machine.step();
    } catch (const runtime::LimitReached &limit) {
      return std::string(limit.what());
    }
  };
  const auto outcome = execute(*stepped, false);
  CHECK(outcome == execute(*grouped, true));
  CHECK(outcome == (boundary == FlushBoundary::HostPoll ? "time-limit"
                    : boundary == FlushBoundary::Frame  ? "frame-limit"
                                                        : "instruction-limit"));
  same_cpu(stepped->cpu().state(), grouped->cpu().state());
  CHECK(reported_state(*stepped) == reported_state(*grouped));
  for (const auto &[address, size] :
       {std::pair{0u, Memory::mem1_size}, std::pair{0xe0000000u, Memory::cache_size}}) {
    const auto a = stepped->memory().range(address, size),
               b = grouped->memory().range(address, size);
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }
  if (console == Console::Wii) {
    const auto a = stepped->memory().range(0x10000000, Memory::mem2_size),
               b = grouped->memory().range(0x10000000, Memory::mem2_size);
    CHECK(std::equal(a.begin(), a.end(), b.begin(), b.end()));
  }
  CHECK(stepped_events.size() == grouped_events.size());
  for (std::size_t i = 0; i < stepped_events.size(); ++i) {
    CHECK(stepped_events[i].properties == grouped_events[i].properties);
    CHECK(stepped_events[i].samples == grouped_events[i].samples);
    same_cpu(stepped_events[i].cpu, grouped_events[i].cpu);
  }
  if (boundary == FlushBoundary::HostPoll) {
    CHECK(grouped->cpu().state().instructions == 4096);
    CHECK(!grouped_events.empty() && grouped_events.front().cpu.instructions == 4095);
  }
  if (boundary == FlushBoundary::Replay) {
    CHECK(grouped->cpu().state().instructions == 4108);
    CHECK(grouped->memory().read32(0x9000) == 0x12345678);
  }
  if (boundary == FlushBoundary::Dma) {
    CHECK(grouped->memory().read32(0x9000) == 2);
    CHECK(std::any_of(grouped_events.begin(), grouped_events.end(), [](const OrderedEvent &event) {
      return event.properties[0] == 4 && event.properties[2] == 0x9000;
    }));
  }
  if (boundary == FlushBoundary::Dsp || boundary == FlushBoundary::DecPhase ||
      boundary == FlushBoundary::External || boundary == FlushBoundary::Pe)
    CHECK(grouped->cpu().state().spr[cpu::spr::srr0] != 0);
  if (boundary == FlushBoundary::Ios)
    CHECK(grouped->memory().read32(0x10000) == 8);
  if (boundary == FlushBoundary::CtrExit)
    CHECK(grouped->cpu().state().gpr[11] == 1 && grouped->cpu().state().spr[cpu::spr::ctr] == 0);
  for (u32 address : {0x0c003000u, 0x0c002030u, 0x0c006430u, 0x0c006434u, 0x0c006438u})
    CHECK(stepped->memory().read32(address) == grouped->memory().read32(address));
  stepped.reset();
  grouped.reset();
  if (boundary == FlushBoundary::Trace)
    CHECK(runtime::read_file(stepped_trace.path, 65536) ==
          runtime::read_file(grouped_trace.path, 65536));
}
} // namespace

int main() {
  return run_tests([] {
    for (Console console : {Console::GameCube, Console::Wii}) {
      for (cpu::ExecutionBackend backend :
           {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger}) {
        if (backend == cpu::ExecutionBackend::NativeInteger && !cpu::NativeInteger::supported())
          continue;
        for (Mode mode : {Mode::Devices, Mode::Decrementer, Mode::Monitor, Mode::External,
                          Mode::FrameLimit, Mode::Trace})
          compare_runtime(console, backend, mode);
        for (PollBoundary boundary :
             {PollBoundary::Quota, PollBoundary::Frame, PollBoundary::Dma, PollBoundary::Dsp,
              PollBoundary::HostPoll, PollBoundary::ReplayReplacement, PollBoundary::Decrementer,
              PollBoundary::External, PollBoundary::Overflow, PollBoundary::ZeroCycles,
              PollBoundary::Fault})
          compare_ram_poll(console, backend, boundary);
        if (console == Console::Wii)
          compare_ram_poll(console, backend, PollBoundary::Ios);
      }
    }
    for (Console console : {Console::GameCube, Console::Wii})
      for (auto backend :
           {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger}) {
        if (backend == cpu::ExecutionBackend::NativeInteger && !cpu::NativeInteger::supported())
          continue;
        for (unsigned cut = 1; cut <= 13; ++cut) {
          compare_copy(console, backend, CopyBoundary::Quota, cut);
          compare_copy(console, backend, CopyBoundary::DecCut, cut);
        }
        compare_copy(console, backend, CopyBoundary::Quota, 12007);
        for (unsigned cut = 1; cut <= 9; ++cut)
          compare_copy(console, backend, CopyBoundary::FieldCut, cut);
        compare_copy(console, backend, CopyBoundary::Frame, 3);
        for (auto boundary :
             {CopyBoundary::Dma, CopyBoundary::Dsp, CopyBoundary::Replay, CopyBoundary::HostPoll,
              CopyBoundary::DeclinedMmio, CopyBoundary::DeclinedFault,
              CopyBoundary::TakenSequential, CopyBoundary::Trace})
          compare_copy(console, backend, boundary);
        if (console == Console::Wii)
          compare_copy(console, backend, CopyBoundary::Ios);
        // Varying RAM addresses keep this loop out of the stable polling
        // shortcut. A full host-poll window spans hundreds of resident groups.
        for (unsigned quota : {17u, 63u, 64u, 65u, 512u, 4095u, 4096u, 4097u, 8193u})
          compare_copy(console, backend, CopyBoundary::Quota, quota, true);
        for (unsigned deadline : {1u, 7u, 8u, 9u, 511u, 512u, 513u, 4095u, 4096u, 4097u})
          compare_copy(console, backend, CopyBoundary::FieldCut, deadline, true);
        compare_copy(console, backend, CopyBoundary::Frame, 513, true);
        compare_copy(console, backend, CopyBoundary::DecCut, 4097, true);

        // Reuse the same peripheral setup and ordered observations for a
        // register-only conditional loop, including its RAM fallthrough.
        const auto registers = register_branch_program();
        for (unsigned quota : {1u, 2u, 3u, 7u, 8u, 9u, 4095u, 4096u, 4097u})
          compare_copy(console, backend, CopyBoundary::Quota, quota, true, registers);
        for (unsigned deadline : {1u, 2u, 3u, 7u, 8u, 9u, 511u, 512u, 513u, 4095u, 4096u, 4097u})
          compare_copy(console, backend, CopyBoundary::FieldCut, deadline, true, registers);
        for (unsigned quota : {13u, 4097u})
          compare_copy(console, backend, CopyBoundary::DecCut, quota, true, registers);
        compare_copy(console, backend, CopyBoundary::Frame, 513, true, registers);
        for (auto boundary : {CopyBoundary::Dma, CopyBoundary::Dsp, CopyBoundary::Replay,
                              CopyBoundary::HostPoll, CopyBoundary::Trace, CopyBoundary::External})
          compare_copy(console, backend, boundary, 0, true, registers);
        if (console == Console::Wii)
          compare_copy(console, backend, CopyBoundary::Ios, 0, true, registers);
      }
    for (Console console : {Console::GameCube, Console::Wii})
      for (auto backend :
           {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger}) {
        if (backend == cpu::ExecutionBackend::NativeInteger && !cpu::NativeInteger::supported())
          continue;
        for (unsigned cut = 1; cut <= 13; ++cut)
          compare_flush(console, backend, FlushBoundary::Quota, cut);
        for (unsigned cut : {4095u, 4096u, 4097u, 12007u})
          compare_flush(console, backend, FlushBoundary::Quota, cut);
        for (unsigned cut = 1; cut <= 21; ++cut)
          compare_flush(console, backend, FlushBoundary::FieldCut, cut);
        for (unsigned phase = 0; phase < 12; ++phase)
          compare_flush(console, backend, FlushBoundary::DecPhase, phase);
        for (unsigned cut = 1; cut <= 7; ++cut)
          compare_flush(console, backend, FlushBoundary::Frame, cut);
        for (unsigned count = 1; count <= 4; ++count)
          compare_flush(console, backend, FlushBoundary::CtrExit, count, 3, true);
        for (unsigned offset : {0u, 5u, 6u})
          compare_flush(console, backend, FlushBoundary::Quota, 12007, offset, true);
        for (auto boundary :
             {FlushBoundary::Dma, FlushBoundary::Dsp, FlushBoundary::Pe, FlushBoundary::External,
              FlushBoundary::Replay, FlushBoundary::HostPoll, FlushBoundary::Trace,
              FlushBoundary::Overflow, FlushBoundary::AddressWrap, FlushBoundary::MmioAddress})
          compare_flush(console, backend, boundary);
        if (console == Console::Wii)
          compare_flush(console, backend, FlushBoundary::Ios);
      }
  });
}
