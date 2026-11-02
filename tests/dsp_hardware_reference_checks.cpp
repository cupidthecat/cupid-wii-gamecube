// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/CPUDetect.h"
#include "Common/Hash.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPHost.h"
#include "Core/DSP/Jit/DSPEmitterBase.h"
#include "cupid/dsp/instruction_engine.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <vector>

// Unchanged IFX, mailbox, DMA and accelerator methods run on initialized
// source objects. Host memory and page protection are bounded fixture services.
// This comparison does not replace the register or transfer implementation.
namespace {
std::vector<u8> host(24 * 1024 * 1024), aram(16 * 1024 * 1024), product_aram(16 * 1024 * 1024);
unsigned interrupts = 0;
} // namespace

// Texture hash dispatch is retained by sanitizer metadata but is not used here.
CPUInfo::CPUInfo() = default;
CPUInfo cpu_info;

namespace Common {
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
  reference->Shutdown();
  std::cout << checked << " actual IFX mailbox/IRQ/DMA/accelerator cases passed\n";
}
