// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/CPUDetect.h"
#include "Common/Hash.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Core/CoreTiming.h"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPHost.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"
#include "Core/DSP/Jit/DSPEmitterBase.h"
#include "Core/HW/SystemTimers.h"
#include "Core/System.h"
#include "cupid/dsp/instruction_engine.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/mman.h>
#include <vector>

// Unchanged IFX, mailbox, DMA and accelerator methods run on initialized
// source objects. Host memory and page protection are bounded fixture services.
// This comparison does not replace the register or transfer implementation.
namespace {
std::vector<u8> host(24 * 1024 * 1024), aram(16 * 1024 * 1024), product_aram(16 * 1024 * 1024);
unsigned interrupts = 0;
u64 guest_ticks = 0;
std::vector<s64> forced_checks;
} // namespace

// Texture hash dispatch is retained by sanitizer metadata but is not used here.
CPUInfo::CPUInfo() = default;
CPUInfo cpu_info;

// Control-register methods use an ordinary system object with a controlled
// timebase and recorded rescheduling requests. No host waiting is performed.
namespace Core {
struct System::Impl {
  explicit Impl(System &system) : timers(system), timing(system) {
  }

  SystemTimers::SystemTimersManager timers;
  CoreTiming::CoreTimingManager timing;
};

System::System() : m_impl(std::make_unique<Impl>(*this)) {
}

System::~System() = default;

SystemTimers::SystemTimersManager &System::GetSystemTimers() const {
  return m_impl->timers;
}

CoreTiming::CoreTimingManager &System::GetCoreTiming() const {
  return m_impl->timing;
}
} // namespace Core

namespace SystemTimers {
SystemTimersManager::SystemTimersManager(Core::System &system) : m_system(system) {
}

SystemTimersManager::~SystemTimersManager() = default;

u64 SystemTimersManager::GetFakeTimeBase() const {
  return guest_ticks;
}
} // namespace SystemTimers

namespace CoreTiming {
CoreTimingManager::CoreTimingManager(Core::System &system) : m_system(system) {
}

void CoreTimingManager::ForceExceptionCheck(s64 cycles) {
  forced_checks.push_back(cycles);
}
} // namespace CoreTiming

namespace Common {
PrecisionTimer::PrecisionTimer() = default;
PrecisionTimer::~PrecisionTimer() = default;

void *AllocateMemoryPages(size_t size) {
  void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    throw std::runtime_error("mmap failed");
  return p;
}

bool FreeMemoryPages(void *p, size_t size) {
  return munmap(p, size) == 0;
}

bool WriteProtectMemory(void *p, size_t size, bool executable) {
  return mprotect(p, size, PROT_READ | (executable ? PROT_EXEC : 0)) == 0;
}

bool UnWriteProtectMemory(void *p, size_t size, bool executable) {
  return mprotect(p, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0)) == 0;
}

std::string GetStringT(const char *s) {
  return s;
}

bool MsgAlertFmtImpl(bool, MsgType type, Log::LogType, const char *, int, fmt::string_view f,
                     const fmt::format_args &args) {
  if (type == MsgType::Question)
    return false; // Continue with authored synthetic ROMs.
  throw std::runtime_error(fmt::vformat(f, args));
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
}
} // namespace Log
} // namespace Common

namespace DSP::JIT {
DSPEmitter::~DSPEmitter() = default;

std::unique_ptr<DSPEmitter> CreateDSPEmitter(DSPCore &) {
  throw std::runtime_error("JIT requested outside interpreter fixture");
}
} // namespace DSP::JIT

namespace DSP::Host {
u8 ReadHostMemory(u32 addr) {
  return aram[addr & 0xffffff];
}

void WriteHostMemory(u8 value, u32 addr) {
  aram[addr & 0xffffff] = value;
}

void DMAToDSP(u16 *dst, u32 addr, u32 size) {
  addr &= 0x3fffffff;
  if (addr > host.size() || size > host.size() - addr)
    return;
  for (u32 i = 0; i < size / 2; ++i)
    dst[i] = u16((u16(host[addr + i * 2]) << 8) | host[addr + i * 2 + 1]);
}

void DMAFromDSP(const u16 *src, u32 addr, u32 size) {
  addr &= 0x3fffffff;
  if (addr > host.size() || size > host.size() - addr)
    return;
  for (u32 i = 0; i < size / 2; ++i) {
    host[addr + i * 2] = u8(src[i] >> 8);
    host[addr + i * 2 + 1] = u8(src[i]);
  }
}

void InterruptRequest() {
  ++interrupts;
}

void CodeLoaded(DSPCore &core, u32 addr, size_t size) {
  addr &= 0x3fffffff;
  if (addr > host.size() || size > host.size() - addr)
    throw std::runtime_error("Invalid instruction source");
  core.DSPState().GetAnalyzer().Analyze(core.DSPState());
  core.DSPState().SetIRAMCRC(Common::HashEctor(host.data() + addr, size));
}
} // namespace DSP::Host

int main() {
  DSP::DSPInitOptions options;
  options.core_type = DSP::DSPInitOptions::CoreType::Interpreter;
  options.irom_contents.fill(0x0021);
  options.coef_contents.fill(0);
  auto reference = std::make_unique<DSP::DSPCore>();
  if (!reference->Initialize(options))
    throw std::runtime_error("Reference init failed");
  auto &state = reference->DSPState();
  cupid::Memory memory(cupid::Console::GameCube);
  std::array<u8, 8192> program{};
  std::array<u8, 4096> coefficients{};
  for (unsigned i = 1; i < program.size(); i += 2)
    program[i] = 0x21;
  cupid::dsp::InstructionEngine product(memory, cupid::Console::GameCube, program, coefficients);
  unsigned actual_interrupts = 0;
  product.interrupt = [&] { ++actual_interrupts; };
  product.aram_read = [&](u32 a, std::span<u8> out) {
    std::copy_n(product_aram.begin() + a, out.size(), out.begin());
  };
  product.aram_write = [&](u32 a, std::span<const u8> in) {
    std::copy(in.begin(), in.end(), product_aram.begin() + a);
  };
  auto write = [&](u16 reg, u16 value) {
    state.WriteIFX(reg, value);
    product.core().write_data(reg, value);
  };
  auto read = [&](u16 reg) {
    const auto expected = state.ReadIFX(reg), actual = product.core().read_data(reg);
    if (expected != actual)
      throw std::runtime_error("IFX read mismatch " + std::to_string(reg));
  };
  unsigned checked = 0;
  for (unsigned sample = 0; sample < 256; ++sample) {
    for (u16 reg : {u16(0xfffc), u16(0xfffd), u16(0xfffe), u16(0xffff), u16(0xfffb)}) {
      write(reg, u16(sample * 257));
      for (u16 mailbox = 0xfffc; mailbox != 0; ++mailbox)
        read(mailbox);
      if (interrupts != actual_interrupts)
        throw std::runtime_error("IRQ mismatch");
      ++checked;
    }
  }
  for (unsigned dir = 0; dir < 4; ++dir)
    for (unsigned masked = 0; masked < 2; ++masked)
      for (unsigned length : {0u, 1u, 2u, 3u, 31u, 32u, 511u, 512u, 4096u})
        for (unsigned offset : {0u, 1u, 255u, 0x8000u}) {
          const u32 address = 0x80001001;
          for (unsigned i = 0; i < 8192; ++i)
            host[0x1000 + i] = u8(i * 37 + checked);
          std::copy_n(host.begin() + 0x1000, 8192, memory.range(0x1000, 8192).begin());
          write(0xffc9, u16(dir));
          write(0xffef, u16(masked));
          write(0xffcd, u16(offset));
          write(0xffce, u16(address >> 16));
          write(0xffcf, u16(address));
          write(0xffcb, u16(length));
          for (u16 reg : {u16(0xffc9), u16(0xffcb), u16(0xffcd), u16(0xffce), u16(0xffcf)})
            read(reg);
          if (!std::equal(state.dram, state.dram + 4096, product.core().data_ram().begin()) ||
              !std::equal(state.iram, state.iram + 4096,
                          product.core().instruction_ram().begin()) ||
              !std::equal(host.begin() + 0x1000, host.begin() + 0x3000,
                          memory.range(0x1000, 8192).begin()))
            throw std::runtime_error("DMA mismatch " + std::to_string(checked));
          ++checked;
        }
  // Register routing, accelerator side effects and untouched register storage.
  // Independent ARAM stores preserve read/write order rather than sharing results.
  for (unsigned i = 0; i < aram.size(); ++i)
    aram[i] = product_aram[i] = u8(i * 43 + 7);
  for (unsigned sample = 0; sample < 128; ++sample) {
    for (unsigned reg = 0; reg < 256; ++reg) {
      if (reg == 0xcb)
        continue; // DMA lengths are bounded in the matrix above.
      write(u16(0xff00 | reg), u16(sample * 739 + reg * 991));
      read(u16(0xff00 | reg));
      if (state.exceptions != product.core().state().exceptions)
        throw std::runtime_error("Accelerator exception routing mismatch");
      ++checked;
    }
    if (aram != product_aram)
      throw std::runtime_error("Accelerator backing mismatch");
  }
  DSP::Interpreter::Interpreter interpreter(*reference);
  state.pc = product.core().state().pc = 0x8000;
  state.exceptions = product.core().state().exceptions = 0;
  state.r.sr = product.core().state().registers[19] = 0xa00;
  product.guest_timebase = [] { return guest_ticks; };
  for (unsigned i = 0; i < 4096; ++i)
    host[0x1000000 + i] = i & 1 ? 0x21 : 0;
  std::copy_n(host.begin() + 0x1000000, 4096, memory.range(0x1000000, 4096).begin());
  unsigned controls = 0;
  for (unsigned round = 0; round < 32; ++round)
    for (u16 control : {u16(0x804), u16(0x800), u16(0), u16(4), u16(0x801), u16(0x802), u16(0xc04),
                        u16(0xc00), u16(0xc03), u16(0xffff)}) {
      if (control == 0x804 && (round & 3) == 3)
        guest_ticks = std::numeric_limits<u64>::max() - 120;
      guest_ticks += 17;
      state.r.sr = product.core().state().registers[19] = round & 1 ? 0xa00 : 0;
      interpreter.WriteControlRegister(control);
      if (control & 2) {
        reference->CheckExternalInterrupt();
        reference->CheckExceptions();
      }
      product.write16(0x0a, control);
      for (unsigned delay : {0u, 1u, 128u, 1u, 1u}) {
        guest_ticks += delay;
        if (interpreter.ReadControlRegister() != product.read16(0x0a))
          throw std::runtime_error("Control read mismatch");
        ++controls;
      }
      if (state.pc != product.core().state().pc ||
          state.control_reg != product.core().state().control ||
          state.exceptions != product.core().state().exceptions)
        throw std::runtime_error("Control state mismatch");
      for (unsigned r = 0; r < 32; ++r)
        if (state.ReadRegister(r) != product.core().state().registers[r])
          throw std::runtime_error("Control register mismatch");
      for (unsigned st = 0; st < 4; ++st)
        if (state.reg_stack_ptrs[st] != product.core().state().stack_positions[st] ||
            !std::equal(std::begin(state.reg_stacks[st]), std::end(state.reg_stacks[st]),
                        product.core().state().stacks[st].begin()))
          throw std::runtime_error("Control stack mismatch");
      if (!std::equal(state.iram, state.iram + 4096, product.core().instruction_ram().begin()))
        throw std::runtime_error("Control IRAM mismatch");
    }
  if (forced_checks.empty() ||
      std::any_of(forced_checks.begin(), forced_checks.end(), [](s64 n) { return n != 50; }))
    throw std::runtime_error("Unexpected control timing requests");
  std::cout << controls << " actual control/timebase observations passed\n";
  reference->Shutdown();
  std::cout << checked << " actual IFX mailbox/IRQ/DMA/accelerator cases passed\n";
}
