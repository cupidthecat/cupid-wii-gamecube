// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Common/Config/Config.h"
#include "Common/Debug/MemoryPatches.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/SymbolDB.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/CPU.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/PowerPC/GDBStub.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/EFBInterface.h"
#include "cupid/cpu/interpreter.hpp"

// The actual MMU and instruction callers are linked unchanged into this test.
// These adapters construct ordinary service objects around an authored RAM
// fixture. They never cast raw storage into objects or synthesize expected data.
// Any service outside the selected RAM-load paths fails explicitly.
namespace {
[[noreturn]] void unused() {
  throw std::logic_error("Unexercised reference service");
}

std::vector<u8> physical_memory(1024 * 1024);
std::unique_ptr<MMIO::Mapping> unused_mmio;

std::unique_ptr<MMIO::Mapping> acquire_mmio() {
  if (unused_mmio)
    return std::move(unused_mmio);
  return std::make_unique<MMIO::Mapping>();
}
} // namespace

namespace Core {
struct System::Impl {
  explicit Impl(System &system) : jit(system) {
  }

  JitInterface jit;
};

System::System() : m_impl(std::make_unique<Impl>(*this)), m_mmu_enabled(true) {
}

System::~System() = default;

CPU::CPUManager &System::GetCPU() const {
  unused();
}

ProcessorInterface::ProcessorInterfaceManager &System::GetProcessorInterface() const {
  unused();
}

GPFifo::GPFifoManager &System::GetGPFifo() const {
  unused();
}

Memory::MemoryManager &System::GetMemory() const {
  unused();
}

PowerPC::MMU &System::GetMMU() const {
  unused();
}

JitInterface &System::GetJitInterface() const {
  return m_impl->jit;
}
} // namespace Core

namespace Common {
#ifdef _WIN32
// The fixture never reserves host mappings. Complete the native arena's unused
// vector element without permitting a region object to be constructed.
struct WindowsMemoryRegion {
  WindowsMemoryRegion() = delete;
};
#endif
MemArena::MemArena() = default;
MemArena::~MemArena() = default;
#ifdef _WIN32
DynamicLibrary::DynamicLibrary() = default;

DynamicLibrary::~DynamicLibrary() {
  if (IsOpen())
    std::terminate();
}
#endif
SymbolDB::SymbolDB() = default;
SymbolDB::~SymbolDB() = default;

std::string GetStringT(const char *) {
  unused();
}

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  unused();
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  unused();
}
} // namespace Log

namespace Debug {
MemoryPatches::MemoryPatches() = default;
MemoryPatches::~MemoryPatches() = default;
} // namespace Debug
} // namespace Common

namespace Memory {
MemoryManager::MemoryManager(Core::System &system)
    : m_ram(physical_memory.data()), m_ram_size_real(static_cast<u32>(physical_memory.size())),
      m_ram_size(static_cast<u32>(physical_memory.size())), m_ram_mask(m_ram_size - 1),
      m_mmio_mapping(acquire_mmio()), m_page_size(4096), m_guest_pages_per_host_page(1),
      m_host_page_type(HostPageType::SmallPages), m_system(system) {
}

MemoryManager::~MemoryManager() {
  unused_mmio = std::move(m_mmio_mapping);
}

u8 *MemoryManager::GetPointerForRange(u32, size_t) const {
  unused();
}

void MemoryManager::CopyFromEmu(void *, u32, size_t) const {
  unused();
}

void MemoryManager::CopyToEmu(u32, const void *, size_t) {
  unused();
}

void MemoryManager::Write_U32(u32 value, u32 address) {
  if (address > physical_memory.size() - 4)
    throw std::out_of_range("Reference PTE write range");
  for (unsigned n = 0; n < 4; ++n)
    physical_memory[address + n] = static_cast<u8>(value >> ((3 - n) * 8));
}

void MemoryManager::AddPageTableMapping(u32, u32, bool) {
  unused();
}

void MemoryManager::RemovePageTableMappings(const std::set<u32> &) {
  unused();
}

// This fixture creates no host fast-memory mappings or compiled backend.
void MemoryManager::RemoveAllPageTableMappings() {
}

void MemoryManager::UpdateDBATMappings(const PowerPC::BatTable &) {
}
} // namespace Memory

PPCSymbolDB::PPCSymbolDB() = default;
PPCSymbolDB::~PPCSymbolDB() = default;

const Common::Symbol *PPCSymbolDB::AddFunction(const Core::CPUThreadGuard &, u32) {
  unused();
}

const Common::Symbol *PPCSymbolDB::GetSymbolFromAddr(u32) const {
  unused();
}

BreakPoints::BreakPoints(Core::System &system) : m_system(system) {
}

BreakPoints::~BreakPoints() = default;

MemChecks::MemChecks(Core::System &system) : m_system(system) {
}

MemChecks::~MemChecks() = default;

TMemCheck *MemChecks::GetMemCheck(u32, size_t) {
  unused();
}

bool MemChecks::OverlapsMemcheck(u32, u32) const {
  unused();
}

bool TMemCheck::Action(Core::System &, u64, u32, bool, size_t, u32) {
  unused();
}

void ExprDeleter::operator()(expr *) const {
  unused();
}

void ExprVarListDeleter::operator()(expr_var_list *) const {
  unused();
}

PPCDebugInterface::PPCDebugInterface(Core::System &system, PPCSymbolDB &symbols)
    : m_system(system), m_ppc_symbol_db(symbols) {
}

PPCDebugInterface::~PPCDebugInterface() = default;
#include "cpu_memory_reference_debug.inc"

namespace PowerPC {
const std::array<u64, 16> ConditionRegister::s_crTable = [] {
  std::array<u64, 16> values{};
  for (u8 field = 0; field < values.size(); ++field)
    values[field] = ConditionRegister::PPCToInternal(field);
  return values;
}();

PowerPCManager::PowerPCManager(Core::System &system)
    : m_ppc_state{}, m_breakpoints(system), m_memchecks(system),
      m_debug_interface(system, m_symbol_db), m_registered_config_callback_id{}, m_system(system) {
}

PowerPCManager::~PowerPCManager() = default;
InstructionCache::~InstructionCache() = default;

void Cache::Read(Memory::MemoryManager &, u32, void *, u32, bool) {
  unused();
}

void Cache::Write(Memory::MemoryManager &, u32, const void *, u32, bool) {
  unused();
}

void Cache::Store(Memory::MemoryManager &, u32) {
  unused();
}

void Cache::Invalidate(Memory::MemoryManager &, u32) {
  unused();
}

void Cache::Flush(Memory::MemoryManager &, u32) {
  unused();
}

void Cache::Touch(Memory::MemoryManager &, u32, bool) {
  unused();
}

u32 InstructionCache::ReadInstruction(Memory::MemoryManager &, PowerPCState &, u32) {
  unused();
}

void InstructionCache::Invalidate(Memory::MemoryManager &, JitInterface &, u32) {
  unused();
}

double PairedSingle::PS0AsDouble() const {
  return std::bit_cast<double>(ps0);
}

double PairedSingle::PS1AsDouble() const {
  return std::bit_cast<double>(ps1);
}

void PairedSingle::SetPS0(double value) {
  ps0 = std::bit_cast<u64>(value);
}

void PairedSingle::SetPS1(double value) {
  ps1 = std::bit_cast<u64>(value);
}
} // namespace PowerPC

Interpreter::Interpreter(Core::System &system, PowerPC::PowerPCState &state, PowerPC::MMU &mmu,
                         Core::BranchWatch &branches, PPCSymbolDB &symbols)
    : m_system(system), m_ppc_state(state), m_mmu(mmu), m_branch_watch(branches),
      m_ppc_symbol_db(symbols) {
}

Interpreter::~Interpreter() = default;

void Interpreter::Init() {
  unused();
}

void Interpreter::Shutdown() {
  unused();
}

void Interpreter::SingleStep() {
  unused();
}

void Interpreter::Run() {
  unused();
}

void Interpreter::ClearCache() {
  unused();
}

const char *Interpreter::GetName() const {
  unused();
}

namespace CPU {
void CPUManager::Break() {
  unused();
}

bool CPUManager::IsStepping() const {
  unused();
}
} // namespace CPU

namespace GPFifo {
void GPFifoManager::Write8(u8) {
  unused();
}

void GPFifoManager::Write16(u16) {
  unused();
}

void GPFifoManager::Write32(u32) {
  unused();
}

void GPFifoManager::Write64(u64) {
  unused();
}
} // namespace GPFifo

bool TryParse(const std::string &, bool *) {
  unused();
}

namespace Config {
const Info<bool> MAIN_LOW_DCBZ_HACK{{}, false};

u64 GetConfigVersion() {
  unused();
}

std::optional<std::string> GetAsString(const Location &) {
  unused();
}
} // namespace Config

namespace ProcessorInterface {
void ProcessorInterfaceManager::SetInterrupt(u32, bool) {
  unused();
}
} // namespace ProcessorInterface

namespace GDBStub {
bool IsActive() {
  unused();
}

void TakeControl() {
  unused();
}
} // namespace GDBStub

class UnusedEfb final : public EFBInterfaceBase {
public:
  void ReinterpretPixelData(EFBReinterpretType) override {
    unused();
  }

  void PokeColor(u16, u16, u32) override {
    unused();
  }

  void PokeDepth(u16, u16, u32) override {
    unused();
  }

  u32 PeekColorInternal(u16, u16) override {
    unused();
  }

  u32 PeekDepthInternal(u16, u16) override {
    unused();
  }
};

EFBInterfaceBase::~EFBInterfaceBase() = default;
std::unique_ptr<EFBInterfaceBase> g_efb_interface = std::make_unique<UnusedEfb>();

u32 EFBInterfaceBase::PeekColor(u16, u16) {
  unused();
}

u32 EFBInterfaceBase::PeekDepth(u16, u16) {
  unused();
}

bool JitInterface::WantsPageTableMappings() const {
  return false;
}

void JitInterface::ClearSafe() {
}

void JitInterface::InvalidateICacheLine(u32) {
  unused();
}

JitInterface::JitInterface(Core::System &system) : m_system(system) {
}

JitInterface::~JitInterface() {
  if (m_jit)
    std::terminate();
  // This driver never creates a compiled execution backend.
  (void)m_jit.release();
}

class GuestBus : public cupid::Bus {
public:
  cupid::u8 read8(cupid::u32 address) override {
    if (address >= physical_memory.size())
      throw std::out_of_range("Guest read range");
    return physical_memory[address];
  }

  void write8(cupid::u32 address, cupid::u8 value) override {
    if (address >= physical_memory.size())
      throw std::out_of_range("Guest write range");
    physical_memory[address] = value;
  }
};

u32 read_physical_word(u32 address) {
  u32 value = 0;
  for (unsigned n = 0; n < 4; ++n)
    value = (value << 8) | physical_memory.at(address + n);
  return value;
}

u32 hash_group(unsigned page) {
  return 0x20000 | (((0x123u ^ page) << 6) & 0xffff);
}

struct ComparisonInstruction {
  unsigned direct, indexed, width;
  std::array<Interpreter::Instruction, 4> handlers;
  bool floating;
};

void require(bool condition, unsigned case_number, const char *field) {
  if (!condition)
    throw std::runtime_error("MMU case " + std::to_string(case_number) + ": " + field);
}

unsigned compare_loads() {
  constexpr std::array<ComparisonInstruction, 4> instructions{{
      {40,
       279,
       2,
       {Interpreter::lhz, Interpreter::lhzu, Interpreter::lhzx, Interpreter::lhzux},
       false},
      {42,
       343,
       2,
       {Interpreter::lha, Interpreter::lhau, Interpreter::lhax, Interpreter::lhaux},
       false},
      {32,
       23,
       4,
       {Interpreter::lwz, Interpreter::lwzu, Interpreter::lwzx, Interpreter::lwzux},
       false},
      {50,
       599,
       8,
       {Interpreter::lfd, Interpreter::lfdu, Interpreter::lfdx, Interpreter::lfdux},
       true},
  }};
  constexpr std::array<Interpreter::Instruction, 4> quantized{
      Interpreter::psq_l, Interpreter::psq_lu, Interpreter::psq_lx, Interpreter::psq_lux};
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0x927e65b1;
  const auto compare = [&](unsigned width, unsigned distance, unsigned mappings,
                           unsigned attributes, u32 opcode, Interpreter::Instruction handler,
                           std::optional<unsigned> quantization, unsigned scale) {
    ++comparisons;
    std::fill(physical_memory.begin(), physical_memory.end(), 0);
    Memory::MemoryManager memory(system);
    PowerPC::PowerPCManager manager(system);
    PowerPC::MMU mmu(system, memory, manager);
    auto &reference = manager.GetPPCState();
    Interpreter executor(system, reference, mmu, manager.GetBranchWatch(), manager.GetSymbolDB());
    const u32 original_msr =
        cupid::cpu::msr::dr | cupid::cpu::msr::fp | cupid::cpu::msr::pr | cupid::cpu::msr::ri;
    reference.msr.Hex = original_msr;
    reference.pc = 0x1000;
    reference.sr[4] = 0x123;
    reference.pagetable_base = 0x20000;
    reference.pagetable_mask = 0xffff;
    reference.m_enable_dcache = false;
    reference.spr[SPR_HID2] = 0xa0000000;
    reference.spr[SPR_DSISR] = 0x13579bdf;
    reference.spr[SPR_DAR] = 0x2468ace0;
    if (quantization)
      reference.spr[SPR_GQR0 + 1] = (*quantization << 16) | (scale << 24);
    mmu.DBATUpdated();
    const u32 address = 0x40008000 - distance;
    reference.gpr[7] = address - 5;
    reference.gpr[8] = 5;
    reference.gpr[9] = 0x8ac12467;
    reference.ps[9].SetBoth(u64{0x1122334455667788ull}, u64{0x9abcdef012345678ull});
    for (unsigned page = 7; page < 9; ++page) {
      const bool mapped = (mappings & (page == 7 ? 1u : 2u)) != 0;
      const u32 attr = (page == 7 ? attributes : (attributes * 5 + 3) & 15) << 3;
      memory.Write_U32(mapped ? 0x80000000 | (0x123 << 7) : 0, hash_group(page));
      memory.Write_U32((page == 7 ? 0x4000u : 0x8000u) | 3 | attr, hash_group(page) + 4);
    }
    for (unsigned n = 0; n < width; ++n) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      physical_memory[n < distance ? 0x5000 - distance + n : 0x8000 + n - distance] =
          static_cast<u8>(random);
    }
    const auto initial_memory = physical_memory;
    UGeckoInstruction instruction;
    instruction.hex = opcode;
    handler(executor, instruction);
    const std::array<u32, 2> reference_ptes{read_physical_word(hash_group(7) + 4),
                                            read_physical_word(hash_group(8) + 4)};
    std::copy(initial_memory.begin(), initial_memory.end(), physical_memory.begin());
    GuestBus bus;
    cupid::cpu::Interpreter guest(bus);
    auto &state = guest.state();
    state.pc = 0x1000;
    state.msr = original_msr;
    state.sr[4] = 0x123;
    state.spr[cupid::cpu::spr::sdr1] = 0x20000;
    state.spr[cupid::cpu::spr::hid2] = 0xa0000000;
    state.spr[cupid::cpu::spr::dar] = 0x2468ace0;
    state.spr[cupid::cpu::spr::dsisr] = 0x13579bdf;
    if (quantization)
      state.spr[913] = (*quantization << 16) | (scale << 24);
    state.gpr[7] = address - 5;
    state.gpr[8] = 5;
    state.gpr[9] = 0x8ac12467;
    state.fpr[9].bits = {0x1122334455667788ull, 0x9abcdef012345678ull};
    bus.write32(0x1000, opcode);
    guest.step();
    const bool faulted = reference.Exceptions != 0;
    require(state.pc == (faulted ? 0x300u : 0x1004u), comparisons, "exception result");
    require(state.gpr[7] == reference.gpr[7], comparisons, "update register");
    require(state.gpr[9] == reference.gpr[9], comparisons, "integer destination");
    require(state.fpr[9].bits[0] == reference.ps[9].PS0AsU64() &&
                state.fpr[9].bits[1] == reference.ps[9].PS1AsU64(),
            comparisons, "floating destination lanes");
    require(state.spr[cupid::cpu::spr::dar] == reference.spr[SPR_DAR], comparisons, "DAR");
    require(state.spr[cupid::cpu::spr::dsisr] == reference.spr[SPR_DSISR], comparisons, "DSISR");
    require(read_physical_word(hash_group(7) + 4) == reference_ptes[0] &&
                read_physical_word(hash_group(8) + 4) == reference_ptes[1],
            comparisons, "PTE reference/change bits");
  };
  for (const auto &instruction : instructions)
    for (unsigned distance = 1; distance < instruction.width; ++distance) {
      if (instruction.floating && distance != 4)
        continue;
      for (unsigned form = 0; form < 4; ++form)
        for (unsigned mappings = 0; mappings < 4; ++mappings)
          for (unsigned attributes = 0; attributes < 16; ++attributes) {
            const u32 opcode =
                form < 2 ? ((instruction.direct + (form & 1)) << 26) | (9u << 21) | (7u << 16) | 5
                         : (31u << 26) | (9u << 21) | (7u << 16) | (8u << 11) |
                               ((instruction.indexed + ((form & 1) ? 32 : 0)) << 1);
            compare(instruction.width, distance, mappings, attributes, opcode,
                    instruction.handlers[form], std::nullopt, 0);
          }
    }
  for (unsigned type : {0u, 4u, 5u, 6u, 7u})
    for (const bool scalar : {false, true}) {
      const unsigned width = (type == 0 ? 4 : type == 4 || type == 6 ? 1 : 2) * (scalar ? 1 : 2);
      for (unsigned distance = 1; distance < width; ++distance)
        for (unsigned form = 0; form < 4; ++form)
          for (unsigned mappings = 0; mappings < 4; ++mappings)
            for (unsigned attributes = 0; attributes < 16; ++attributes)
              for (unsigned scale : {0u, 31u, 32u, 63u}) {
                if (type == 0 && scale != 0)
                  continue;
                const u32 opcode = form < 2 ? ((56u + (form & 1)) << 26) | (9u << 21) | (7u << 16) |
                                                  (1u << 12) | 5 | (scalar ? 0x8000u : 0)
                                            : (4u << 26) | (9u << 21) | (7u << 16) | (8u << 11) |
                                                  (1u << 7) | (((form & 1) ? 38u : 6u) << 1) |
                                                  (scalar ? 0x400u : 0);
                compare(width, distance, mappings, attributes, opcode, quantized[form], type,
                        scale);
              }
    }
  return comparisons;
}

int main() {
  try {
    const auto count = compare_loads();
    std::cout << count << " direct source load comparisons passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
