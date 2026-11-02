// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPTables.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"
#include "Core/DSP/Jit/DSPEmitterBase.h"
#include "cupid/dsp/instruction.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// Unchanged instruction handlers, dispatch, stacks and memory-map functions
// run on ordinary reference objects. IFX is a controlled host service here;
// register hardware is outside this instruction comparison. No expected arithmetic is
// calculated by this adapter.
struct Access {
  bool write;
  u16 address, value;
  bool operator==(const Access &) const = default;
};

struct Io {
  std::array<u16, 256> words{};
  std::vector<Access> accesses;

  u16 read(u16 address) {
    auto &v = words[address & 255];
    const u16 result = v;
    v ^= 1;
    accesses.push_back({false, address, result});
    return result;
  }

  void write(u16 address, u16 value) {
    words[address & 255] = value;
    accesses.push_back({true, address, value});
  }
};

Io *active_io = nullptr;

// The fixture never creates a JIT; retain its base RTTI for sanitizer metadata.
namespace DSP::JIT {
DSPEmitter::~DSPEmitter() = default;
}

namespace DSP {
u16 SDSP::ReadIFX(u16 address) {
  return active_io->read(address);
}

void SDSP::WriteIFX(u32 address, u16 value) {
  active_io->write(u16(address), value);
}
} // namespace DSP

namespace Common {
std::string GetStringT(const char *s) {
  return s;
}

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view f,
                     const fmt::format_args &a) {
  throw std::runtime_error(fmt::vformat(f, a));
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
}
} // namespace Log
} // namespace Common

int main() {
  DSP::InitInstructionTable();
  auto core = std::make_unique<DSP::DSPCore>();
  DSP::Interpreter::Interpreter interpreter(*core);
  auto &ref = core->DSPState();
  std::array<u16, 4096> iram{}, irom{}, dram{}, initial{};
  std::array<u16, 2048> coef{};
  iram.fill(0x21);
  irom.fill(0x21);
  for (unsigned i = 0; i < initial.size(); ++i)
    initial[i] = u16(i * 479 + 37);
  for (unsigned i = 0; i < coef.size(); ++i)
    coef[i] = u16(i * 331 + 17);
  ref.iram = iram.data();
  ref.irom = irom.data();
  ref.dram = dram.data();
  ref.coef = coef.data();
  auto product = std::make_unique<cupid::dsp::InstructionCore>();
  product->install_roms(irom, coef);
  unsigned checked = 0;
  for (unsigned sample = 0; sample < 12; ++sample)
    for (unsigned word = 0; word < 65536; ++word) {
      u32 random = 0x6d73a91b ^ (word * 0x71c52b3u) ^ (sample * 0x9834553u);
      const auto next = [&] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
      };
      ref.r = {};
      ref.pc = 0;
      ref.control_reg = 0;
      ref.exceptions = 0;
      auto &state = product->state();
      state = {};
      state.control = 0;
      for (unsigned reg = 0; reg < 32; ++reg) {
        u16 value = sample < 4     ? u16(next())
                    : sample == 4  ? 0
                    : sample == 5  ? 1
                    : sample == 6  ? 0xffff
                    : sample == 7  ? 0x8000
                    : sample == 8  ? 0x7fff
                    : sample == 9  ? 0x00ff
                    : sample == 10 ? 0x5555
                                   : 0xaaaa;
        if (reg == 18 || reg == 22)
          value &= 255;
        if (reg == 19)
          value &= u16(0xfeff);
        ref.WriteRegister(reg, value);
        state.registers[reg] = value;
      }
      for (unsigned n = 0; n < 2; ++n) {
        ref.r.ac[n].h = u32(s32(s8(state.registers[16 + n])));
        state.registers[16 + n] = u16(ref.r.ac[n].h);
      }
      if (sample >= 8)
        for (unsigned n = 0; n < 2; ++n) {
          const u64 value = sample == 8    ? 0x7fffffffffull
                            : sample == 9  ? 0xffffff8000000000ull
                            : sample == 10 ? 0x7fffff8000ull
                                           : 0xffffff8000007fffull;
          ref.r.ac[n].val = value;
          state.registers[28 + n] = u16(value);
          state.registers[30 + n] = u16(value >> 16);
          state.registers[16 + n] = u16(s16(s8(value >> 32)));
        }
      for (unsigned stack = 0; stack < 4; ++stack) {
        ref.reg_stack_ptrs[stack] = state.stack_positions[stack] = u8(next() & 31);
        for (unsigned n = 0; n < 32; ++n)
          ref.reg_stacks[stack][n] = state.stacks[stack][n] = u16(next());
      }
      Io expected_io, actual_io;
      for (unsigned i = 0; i < 256; ++i)
        expected_io.words[i] = actual_io.words[i] = u16(next());
      active_io = &expected_io;
      product->hardware_read = [&](u16 a) { return actual_io.read(a); };
      product->hardware_write = [&](u16 a, u16 v) { actual_io.write(a, v); };
      std::copy(initial.begin(), initial.end(), dram.begin());
      std::copy(initial.begin(), initial.end(), product->data_ram().begin());
      iram[0] = product->instruction_ram()[0] = u16(word);
      iram[1] = product->instruction_ram()[1] = u16(next());
      interpreter.Step();
      product->step();
      ++checked;
      for (unsigned reg = 0; reg < 32; ++reg)
        if (ref.ReadRegister(reg) != state.registers[reg]) {
          std::cerr << "sample=" << sample << " word=" << std::hex << word
                    << " op=" << DSP::GetOpTemplate(u16(word))->name << " reg=" << std::dec << reg
                    << " expected=" << std::hex << ref.ReadRegister(reg)
                    << " actual=" << state.registers[reg] << '\n';
          return 1;
        }
      if (ref.pc != state.pc || ref.control_reg != state.control ||
          ref.exceptions != state.exceptions) {
        std::cerr << "Control mismatch word=" << std::hex << word << " pc=" << ref.pc << "/"
                  << state.pc << '\n';
        return 1;
      }
      for (unsigned st = 0; st < 4; ++st)
        if (ref.reg_stack_ptrs[st] != state.stack_positions[st] ||
            !std::equal(std::begin(ref.reg_stacks[st]), std::end(ref.reg_stacks[st]),
                        state.stacks[st].begin())) {
          std::cerr << "Stack mismatch " << std::hex << word << '\n';
          return 1;
        }
      if (!std::equal(dram.begin(), dram.end(), product->data_ram().begin()) ||
          expected_io.words != actual_io.words || expected_io.accesses != actual_io.accesses) {
        std::cerr << "Memory mismatch " << std::hex << word << '\n';
        return 1;
      }
    }
  std::cout << checked << " actual instruction dispatch comparisons passed\n";
  const std::vector<std::vector<u16>> programs{
      {0x0080, 5, 0x1003, 0x0008, 0x0021},
      {0x1102, 5, 0x0008, 0x0082, 7, 0x0009, 0x0021},
      {0x1102, 7, 0x1103, 5, 0x0008, 0x0009, 0x000a, 0x000b, 0x0021},
      {0x0275, 0x0080, 0x1234, 0x0008, 0x0021},
      {0x02bf, 4, 0x0008, 0x0021, 0x0009, 0x02df},
      {0x1100, 5, 0x0008, 0x0081, 0x1234, 0x0080, 0x6789, 0x0021}};
  unsigned chained = 0;
  for (const auto &program : programs) {
    iram.fill(0x21);
    std::copy(program.begin(), program.end(), iram.begin());
    std::copy(iram.begin(), iram.end(), product->instruction_ram().begin());
    ref.r = {};
    ref.pc = 0;
    ref.control_reg = 0;
    ref.exceptions = 0;
    product->state() = {};
    product->state().control = 0;
    for (unsigned st = 0; st < 4; ++st) {
      ref.reg_stack_ptrs[st] = 0;
      std::fill(std::begin(ref.reg_stacks[st]), std::end(ref.reg_stacks[st]), 0);
      ref.r.wr[st] = 0xffff;
      product->state().registers[8 + st] = 0xffff;
    }
    ref.GetAnalyzer().Analyze(ref);
    product->code_changed();
    for (unsigned step = 0; step < 100 && !(ref.control_reg & 4); ++step) {
      interpreter.Step();
      product->step();
      ++chained;
      for (unsigned r = 0; r < 32; ++r)
        if (ref.ReadRegister(r) != product->state().registers[r])
          throw std::runtime_error("Chained register mismatch");
      if (ref.pc != product->state().pc || ref.control_reg != product->state().control)
        throw std::runtime_error("Chained control mismatch");
    }
    if (!(ref.control_reg & 4))
      throw std::runtime_error("Chained program did not halt");
  }
  std::cout << chained << " analyzed branch/loop sequence steps passed\n";
  // Exercise the actual non-threaded run loop at warmup, probe and batch edges.
  // Include idle signatures at several entry offsets and halt inside a batch.
  std::vector<std::vector<u16>> slices = programs;
  for (unsigned halt : {0u, 7u, 8u, 15u, 16u, 17u, 215u, 216u, 217u}) {
    std::vector<u16> code(halt + 1, 0x0008);
    code.back() = 0x0021;
    slices.push_back(code);
  }
  const std::vector<std::vector<u16>> idle{
      {0x26fc, 0x02c0, 0x8000, 0x029d, 0},         {0x27fc, 0x03c0, 0x8000, 0x029d, 0},
      {0x26fe, 0x02c0, 0x8000, 0x029c, 0},         {0x27fe, 0x03c0, 0x8000, 0x029c, 0},
      {0x26fc, 0x02a0, 0x8000, 0x029c, 0},         {0x27fc, 0x03a0, 0x8000, 0x029c, 0},
      {0x00de, 0xfffe, 0x02c0, 0x8000, 0x029c, 0}, {0x00da, 0x0352, 0x8600, 0x0295, 0}};
  for (const auto &loop : idle)
    for (unsigned offset : {0u, 7u, 8u, 16u, 216u}) {
      std::vector<u16> code(offset, 0x0008);
      code.insert(code.end(), loop.begin(), loop.end());
      code.back() = u16(offset);
      slices.push_back(code);
    }
  unsigned slice_cases = 0;
  for (const auto &program : slices)
    for (unsigned budget : {1u, 7u, 8u, 9u, 15u, 16u, 17u, 215u, 216u, 217u, 300u, 2100u})
      for (u16 mailbox : {u16(0), u16(0x8000)}) {
        iram.fill(0x0021);
        std::copy(program.begin(), program.end(), iram.begin());
        std::copy(iram.begin(), iram.end(), product->instruction_ram().begin());
        dram.fill(0);
        std::fill(product->data_ram().begin(), product->data_ram().end(), 0);
        ref.r = {};
        ref.pc = 0;
        ref.control_reg = 0;
        ref.exceptions = 0;
        product->state() = {};
        product->state().control = 0;
        for (unsigned st = 0; st < 4; ++st) {
          ref.reg_stack_ptrs[st] = 0;
          std::fill(std::begin(ref.reg_stacks[st]), std::end(ref.reg_stacks[st]), 0);
          ref.r.wr[st] = product->state().registers[8 + st] = 0xffff;
        }
        Io expected_io, actual_io;
        expected_io.words.fill(mailbox);
        actual_io.words.fill(mailbox);
        active_io = &expected_io;
        product->hardware_read = [&](u16 a) { return actual_io.read(a); };
        product->hardware_write = [&](u16 a, u16 v) { actual_io.write(a, v); };
        ref.GetAnalyzer().Analyze(ref);
        product->code_changed();
        interpreter.RunCycles(int(budget));
        product->run(budget);
        for (unsigned r = 0; r < 32; ++r)
          if (ref.ReadRegister(r) != product->state().registers[r])
            throw std::runtime_error("Run-loop register mismatch at case " +
                                     std::to_string(slice_cases));
        if (ref.pc != product->state().pc || ref.control_reg != product->state().control ||
            expected_io.words != actual_io.words || expected_io.accesses != actual_io.accesses)
          throw std::runtime_error("Run-loop control/access mismatch at case " +
                                   std::to_string(slice_cases));
        ++slice_cases;
      }
  std::cout << slice_cases << " actual run-loop comparisons passed\n";
}
