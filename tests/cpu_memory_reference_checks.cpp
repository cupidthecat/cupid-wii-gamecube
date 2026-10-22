// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <source_location>
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
// Any service outside the selected memory comparison paths fails explicitly.
namespace {
[[noreturn]] void unused(std::source_location call = std::source_location::current()) {
  throw std::logic_error(call.function_name());
}

std::vector<u8> physical_memory(1024 * 1024);
std::unique_ptr<MMIO::Mapping> unused_mmio;
unsigned reference_pi_requests = 0;
unsigned reference_warning_calls = 0;
std::vector<std::string> reference_warning_samples;
unsigned guest_pi_requests = 0;
std::vector<std::array<u8, 32>> reference_pi_snapshots;
std::vector<std::array<u8, 32>> guest_pi_snapshots;

std::array<u8, 32> pi_snapshot() {
  std::array<u8, 32> snapshot{};
  std::copy_n(physical_memory.begin() + 0x4000, 32, snapshot.begin());
  return snapshot;
}

std::unique_ptr<MMIO::Mapping> acquire_mmio() {
  if (unused_mmio)
    return std::move(unused_mmio);
  return std::make_unique<MMIO::Mapping>();
}
} // namespace

#include "unused_jit_adapters.inc"

namespace Core {
struct System::Impl {
  explicit Impl(System &system) : jit(system), pi(system) {
  }

  JitInterface jit;
  ProcessorInterface::ProcessorInterfaceManager pi;
};

System::System() : m_impl(std::make_unique<Impl>(*this)), m_mmu_enabled(true) {
}

System::~System() = default;

CPU::CPUManager &System::GetCPU() const {
  unused();
}

ProcessorInterface::ProcessorInterfaceManager &System::GetProcessorInterface() const {
  return m_impl->pi;
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
// Compiled cores remain unused, so their lazy region may only have empty state.
LazyMemoryRegion::LazyMemoryRegion() = default;

LazyMemoryRegion::~LazyMemoryRegion() {
  if (m_memory || m_size)
    std::terminate();
#ifdef _WIN32
  if (m_zero_block || !m_writable_block_handles.empty())
    std::terminate();
#endif
}
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
void GenericLogFmtImpl(LogLevel level, LogType type, const char *, int, fmt::string_view format,
                       const fmt::format_args &arguments) {
  if (level != LogLevel::LWARNING || type != LogType::POWERPC)
    unused();
  ++reference_warning_calls;
  if (reference_warning_samples.size() < 8)
    reference_warning_samples.push_back(fmt::vformat(format, arguments));
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
  if (HasAny())
    unused();
  return false;
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

bool Interpreter::Init() {
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
ProcessorInterfaceManager::ProcessorInterfaceManager(Core::System &system) : m_system(system) {
}

ProcessorInterfaceManager::~ProcessorInterfaceManager() = default;

void ProcessorInterfaceManager::SetInterrupt(u32 cause, bool set) {
  if (cause != 1 || !set)
    unused();
  ++reference_pi_requests;
  reference_pi_snapshots.push_back(pi_snapshot());
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
  void ReinterpretPixelData(FramebufferManager *, EFBReinterpretType) override {
    unused();
  }

  void PokeColor(FramebufferManager *, u16, u16, u32) override {
    unused();
  }

  void PokeDepth(FramebufferManager *, u16, u16, u32) override {
    unused();
  }

  u32 PeekColorInternal(FramebufferManager *, u16, u16) override {
    unused();
  }

  u32 PeekDepthInternal(FramebufferManager *, u16, u16) override {
    unused();
  }
};

// No framebuffer service is constructed by these memory fixtures.
// Any unexpected ownership remains a hard failure, including during cleanup.
constexpr bool framebuffer_delete_is_noexcept =
    noexcept(std::default_delete<FramebufferManager>{}(nullptr));

template <>
#if defined(__cpp_lib_constexpr_memory) && __cpp_lib_constexpr_memory >= 202202L
constexpr
#endif
    void
    std::default_delete<FramebufferManager>::operator()(FramebufferManager *manager) const
    noexcept(framebuffer_delete_is_noexcept) {
  if (manager)
    std::terminate();
}

std::unique_ptr<FramebufferManager> g_framebuffer_manager;
EFBInterfaceBase::~EFBInterfaceBase() = default;
std::unique_ptr<EFBInterfaceBase> g_efb_interface = std::make_unique<UnusedEfb>();

u32 EFBInterfaceBase::PeekColor(FramebufferManager *, u16, u16) {
  unused();
}

u32 EFBInterfaceBase::PeekDepth(FramebufferManager *, u16, u16) {
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
  void memory_access_error() override {
    ++guest_pi_requests;
    guest_pi_snapshots.push_back(pi_snapshot());
  }

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
    auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
    auto &memory = *memory_owner;
    auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
    auto &manager = *manager_owner;
    auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
    auto &mmu = *mmu_owner;
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
    auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(bus);
    auto &guest = *guest_owner;
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

unsigned compare_stores() {
  struct StoreInstruction {
    unsigned direct, indexed, width;
    std::array<Interpreter::Instruction, 4> handlers;
  };

  constexpr std::array<StoreInstruction, 4> instructions{
      {{38, 215, 1, {Interpreter::stb, Interpreter::stbu, Interpreter::stbx, Interpreter::stbux}},
       {44, 407, 2, {Interpreter::sth, Interpreter::sthu, Interpreter::sthx, Interpreter::sthux}},
       {36, 151, 4, {Interpreter::stw, Interpreter::stwu, Interpreter::stwx, Interpreter::stwux}},
       {54,
        727,
        8,
        {Interpreter::stfd, Interpreter::stfdu, Interpreter::stfdx, Interpreter::stfdux}}}};
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0xaecc3098;
  for (const auto &instruction : instructions)
    for (unsigned position = 0; position < 16; ++position) {
      if (instruction.width == 8 && (position & 3))
        continue;
      const u32 address = 0x40008000 + position - 8;
      for (unsigned form = 0; form < 4; ++form)
        for (unsigned mappings = 0; mappings < 4; ++mappings)
          for (unsigned attributes = 0; attributes < 16; ++attributes) {
            ++comparisons;
            std::fill(physical_memory.begin(), physical_memory.end(), 0);
            std::fill(physical_memory.begin() + 0x4000, physical_memory.begin() + 0xa000, 0x5a);
            auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
            auto &memory = *memory_owner;
            auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
            auto &manager = *manager_owner;
            auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
            auto &mmu = *mmu_owner;
            auto &reference = manager.GetPPCState();
            Interpreter executor(system, reference, mmu, manager.GetBranchWatch(),
                                 manager.GetSymbolDB());
            const u32 original_msr = cupid::cpu::msr::dr | cupid::cpu::msr::fp |
                                     cupid::cpu::msr::pr | cupid::cpu::msr::ri;
            reference.msr.Hex = original_msr;
            reference.pc = 0x1000;
            reference.sr[4] = 0x123;
            reference.pagetable_base = 0x20000;
            reference.pagetable_mask = 0xffff;
            reference.m_enable_dcache = false;
            reference.spr[SPR_DSISR] = 0x13579bdf;
            reference.spr[SPR_DAR] = 0x2468ace0;
            mmu.DBATUpdated();
            reference.gpr[7] = address - 5;
            reference.gpr[8] = 5;
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            reference.gpr[9] = random;
            reference.ps[9].SetBoth(u64{0x82765acf30149abcull}, u64{0x9abcdef012345678ull});
            for (unsigned page = 7; page < 9; ++page) {
              const bool mapped = (mappings & (page == 7 ? 1u : 2u)) != 0;
              const u32 attr = (page == 7 ? attributes : (attributes * 5 + 3) & 15) << 3;
              memory.Write_U32(mapped ? 0x80000000 | (0x123 << 7) : 0, hash_group(page));
              memory.Write_U32((page == 7 ? 0x4000u : 0x8000u) | 3 | attr, hash_group(page) + 4);
            }
            const auto initial = physical_memory;
            const u32 opcode =
                form < 2 ? ((instruction.direct + (form & 1)) << 26) | (9u << 21) | (7u << 16) | 5
                         : (31u << 26) | (9u << 21) | (7u << 16) | (8u << 11) |
                               ((instruction.indexed + ((form & 1) ? 32 : 0)) << 1);
            UGeckoInstruction decoded;
            decoded.hex = opcode;
            reference_pi_requests = 0;
            reference_pi_snapshots.clear();
            instruction.handlers[form](executor, decoded);
            const auto expected = physical_memory;
            const unsigned expected_pi = reference_pi_requests;
            const auto expected_pi_snapshots = reference_pi_snapshots;
            std::copy(initial.begin(), initial.end(), physical_memory.begin());
            GuestBus bus;
            auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(bus);
            auto &guest = *guest_owner;
            auto &state = guest.state();
            state.pc = 0x1000;
            state.msr = original_msr;
            state.sr[4] = 0x123;
            state.spr[cupid::cpu::spr::sdr1] = 0x20000;
            state.spr[cupid::cpu::spr::dsisr] = 0x13579bdf;
            state.spr[cupid::cpu::spr::dar] = 0x2468ace0;
            state.gpr[7] = address - 5;
            state.gpr[8] = 5;
            state.gpr[9] = random;
            state.fpr[9].bits = {0x82765acf30149abcull, 0x9abcdef012345678ull};
            bus.write32(0x1000, opcode);
            guest_pi_requests = 0;
            guest_pi_snapshots.clear();
            guest.step();
            require(state.pc == (reference.Exceptions ? 0x300u : 0x1004u), comparisons,
                    "store exception");
            require(state.gpr[7] == reference.gpr[7], comparisons, "store update");
            require(state.spr[cupid::cpu::spr::dar] == reference.spr[SPR_DAR], comparisons,
                    "store DAR");
            require(state.spr[cupid::cpu::spr::dsisr] == reference.spr[SPR_DSISR], comparisons,
                    "store DSISR");
            if (guest_pi_requests != expected_pi)
              std::cerr << "opcode=" << std::hex << opcode << " EA=" << address
                        << " attrs=" << attributes << " mappings=" << mappings
                        << " expectedPI=" << expected_pi << " actualPI=" << guest_pi_requests
                        << " firstPTE=" << read_physical_word(hash_group(7) + 4) << std::dec
                        << '\n';
            require(guest_pi_requests == expected_pi, comparisons, "store PI request count");
            require(guest_pi_snapshots == expected_pi_snapshots, comparisons,
                    "store callback data order");
            require(std::equal(expected.begin() + 0x4000, expected.begin() + 0xa000,
                               physical_memory.begin() + 0x4000),
                    comparisons, "store data/guards");
            require(read_physical_word(hash_group(7) + 4) ==
                        ((u32(expected[hash_group(7) + 4]) << 24) |
                         (u32(expected[hash_group(7) + 5]) << 16) |
                         (u32(expected[hash_group(7) + 6]) << 8) | expected[hash_group(7) + 7]),
                    comparisons, "store first PTE");
            require(read_physical_word(hash_group(8) + 4) ==
                        ((u32(expected[hash_group(8) + 4]) << 24) |
                         (u32(expected[hash_group(8) + 5]) << 16) |
                         (u32(expected[hash_group(8) + 6]) << 8) | expected[hash_group(8) + 7]),
                    comparisons, "store second PTE");
          }
    }
  return comparisons;
}

unsigned compare_quantized_stores() {
  constexpr std::array<Interpreter::Instruction, 4> handlers{
      Interpreter::psq_st, Interpreter::psq_stu, Interpreter::psq_stx, Interpreter::psq_stux};
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0xaecc3098;
  for (unsigned type : {0u, 4u, 5u, 6u, 7u})
    for (const bool scalar : {false, true})
      for (unsigned scale : {0u, 31u, 32u, 63u}) {
        if (type == 0 && scale != 0)
          continue;
        const unsigned width = (type == 0 ? 4 : type == 4 || type == 6 ? 1 : 2) * (scalar ? 1 : 2);
        for (unsigned distance = 1; distance < width; ++distance) {
          const u32 address = 0x40008000 - distance;
          for (unsigned form = 0; form < 4; ++form)
            for (unsigned mappings = 0; mappings < 4; ++mappings)
              for (unsigned attributes = 0; attributes < 16; ++attributes) {
                ++comparisons;
                std::fill(physical_memory.begin(), physical_memory.end(), 0);
                std::fill(physical_memory.begin() + 0x4000, physical_memory.begin() + 0xa000, 0x5a);
                auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
                auto &memory = *memory_owner;
                auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
                auto &manager = *manager_owner;
                auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
                auto &mmu = *mmu_owner;
                auto &reference = manager.GetPPCState();
                Interpreter executor(system, reference, mmu, manager.GetBranchWatch(),
                                     manager.GetSymbolDB());
                const u32 original_msr = cupid::cpu::msr::dr | cupid::cpu::msr::fp |
                                         cupid::cpu::msr::pr | cupid::cpu::msr::ri;
                reference.msr.Hex = original_msr;
                reference.pc = 0x1000;
                reference.sr[4] = 0x123;
                reference.pagetable_base = 0x20000;
                reference.pagetable_mask = 0xffff;
                reference.m_enable_dcache = false;
                reference.spr[SPR_DSISR] = 0x13579bdf;
                reference.spr[SPR_DAR] = 0x2468ace0;
                reference.spr[SPR_HID2] = 0xa0000000;
                reference.spr[SPR_GQR0 + 1] = type | (scale << 8);
                mmu.DBATUpdated();
                reference.gpr[7] = address - 5;
                reference.gpr[8] = 5;
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                reference.gpr[9] = random;
                reference.ps[9].SetPS0(3.25);
                reference.ps[9].SetPS1(-9.5);
                for (unsigned page = 7; page < 9; ++page) {
                  const bool mapped = (mappings & (page == 7 ? 1u : 2u)) != 0;
                  const u32 attr = (page == 7 ? attributes : (attributes * 5 + 3) & 15) << 3;
                  memory.Write_U32(mapped ? 0x80000000 | (0x123 << 7) : 0, hash_group(page));
                  memory.Write_U32((page == 7 ? 0x4000u : 0x8000u) | 3 | attr,
                                   hash_group(page) + 4);
                }
                const auto initial = physical_memory;
                const u32 opcode = form < 2 ? ((60u + (form & 1)) << 26) | (9u << 21) | (7u << 16) |
                                                  (1u << 12) | 5 | (scalar ? 0x8000u : 0)
                                            : (4u << 26) | (9u << 21) | (7u << 16) | (8u << 11) |
                                                  (1u << 7) | (((form & 1) ? 39u : 7u) << 1) |
                                                  (scalar ? 0x400u : 0);
                UGeckoInstruction decoded;
                decoded.hex = opcode;
                reference_pi_requests = 0;
                reference_pi_snapshots.clear();
                handlers[form](executor, decoded);
                const auto expected = physical_memory;
                const unsigned expected_pi = reference_pi_requests;
                const auto expected_pi_snapshots = reference_pi_snapshots;
                std::copy(initial.begin(), initial.end(), physical_memory.begin());
                GuestBus bus;
                auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(bus);
                auto &guest = *guest_owner;
                auto &state = guest.state();
                state.pc = 0x1000;
                state.msr = original_msr;
                state.sr[4] = 0x123;
                state.spr[cupid::cpu::spr::sdr1] = 0x20000;
                state.spr[cupid::cpu::spr::dsisr] = 0x13579bdf;
                state.spr[cupid::cpu::spr::dar] = 0x2468ace0;
                state.spr[cupid::cpu::spr::hid2] = 0xa0000000;
                state.spr[913] = type | (scale << 8);
                state.gpr[7] = address - 5;
                state.gpr[8] = 5;
                state.gpr[9] = random;
                state.fpr[9].set(3.25);
                state.fpr[9].set(-9.5, 1);
                bus.write32(0x1000, opcode);
                guest_pi_requests = 0;
                guest_pi_snapshots.clear();
                guest.step();
                require(state.pc == (reference.Exceptions ? 0x300u : 0x1004u), comparisons,
                        "store exception");
                require(state.gpr[7] == reference.gpr[7], comparisons, "store update");
                require(state.spr[cupid::cpu::spr::dar] == reference.spr[SPR_DAR], comparisons,
                        "store DAR");
                require(state.spr[cupid::cpu::spr::dsisr] == reference.spr[SPR_DSISR], comparisons,
                        "store DSISR");
                if (guest_pi_requests != expected_pi)
                  std::cerr << "opcode=" << std::hex << opcode << " EA=" << address
                            << " attrs=" << attributes << " mappings=" << mappings
                            << " expectedPI=" << expected_pi << " actualPI=" << guest_pi_requests
                            << " firstPTE=" << read_physical_word(hash_group(7) + 4) << std::dec
                            << '\n';
                require(guest_pi_requests == expected_pi, comparisons, "store PI request count");
                require(guest_pi_snapshots == expected_pi_snapshots, comparisons,
                        "store callback data order");
                require(std::equal(expected.begin() + 0x4000, expected.begin() + 0xa000,
                                   physical_memory.begin() + 0x4000),
                        comparisons, "store data/guards");
                require(read_physical_word(hash_group(7) + 4) ==
                            ((u32(expected[hash_group(7) + 4]) << 24) |
                             (u32(expected[hash_group(7) + 5]) << 16) |
                             (u32(expected[hash_group(7) + 6]) << 8) | expected[hash_group(7) + 7]),
                        comparisons, "store first PTE");
                require(read_physical_word(hash_group(8) + 4) ==
                            ((u32(expected[hash_group(8) + 4]) << 24) |
                             (u32(expected[hash_group(8) + 5]) << 16) |
                             (u32(expected[hash_group(8) + 6]) << 8) | expected[hash_group(8) + 7]),
                        comparisons, "store second PTE");
              }
        }
      }

  return comparisons;
}

unsigned compare_bat_stores() {
  struct StoreInstruction {
    unsigned direct, indexed, width;
    std::array<Interpreter::Instruction, 4> handlers;
  };

  constexpr std::array<StoreInstruction, 4> instructions{
      {{38, 215, 1, {Interpreter::stb, Interpreter::stbu, Interpreter::stbx, Interpreter::stbux}},
       {44, 407, 2, {Interpreter::sth, Interpreter::sthu, Interpreter::sthx, Interpreter::sthux}},
       {36, 151, 4, {Interpreter::stw, Interpreter::stwu, Interpreter::stwx, Interpreter::stwux}},
       {54,
        727,
        8,
        {Interpreter::stfd, Interpreter::stfdu, Interpreter::stfdx, Interpreter::stfdux}}}};
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0xaecc3098;
  for (const auto &instruction : instructions)
    for (unsigned position = 0; position < 8; ++position) {
      if (instruction.width == 8 && (position & 3))
        continue;
      const u32 address = 0x40004000 + position;
      for (unsigned form = 0; form < 4; ++form)
        for (unsigned mappings = 3; mappings < 4; ++mappings)
          for (unsigned attributes = 0; attributes < 16; ++attributes) {
            ++comparisons;
            std::fill(physical_memory.begin(), physical_memory.end(), 0);
            std::fill(physical_memory.begin() + 0x4000, physical_memory.begin() + 0xa000, 0x5a);
            auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
            auto &memory = *memory_owner;
            auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
            auto &manager = *manager_owner;
            auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
            auto &mmu = *mmu_owner;
            auto &reference = manager.GetPPCState();
            Interpreter executor(system, reference, mmu, manager.GetBranchWatch(),
                                 manager.GetSymbolDB());
            const u32 original_msr = cupid::cpu::msr::dr | cupid::cpu::msr::fp |
                                     cupid::cpu::msr::pr | cupid::cpu::msr::ri;
            reference.msr.Hex = original_msr;
            reference.pc = 0x1000;
            reference.sr[4] = 0x123;
            reference.pagetable_base = 0x20000;
            reference.pagetable_mask = 0xffff;
            reference.m_enable_dcache = false;
            reference.spr[SPR_DSISR] = 0x13579bdf;
            reference.spr[SPR_DAR] = 0x2468ace0;
            reference.spr[SPR_DBAT0U] = 0x40000003;
            reference.spr[SPR_DBAT0L] = (attributes << 3) | 2;
            mmu.DBATUpdated();
            reference.gpr[7] = address - 5;
            reference.gpr[8] = 5;
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            reference.gpr[9] = random;
            reference.ps[9].SetBoth(u64{0x82765acf30149abcull}, u64{0x9abcdef012345678ull});
            for (unsigned page = 7; page < 9; ++page) {
              const bool mapped = (mappings & (page == 7 ? 1u : 2u)) != 0;
              const u32 attr = (page == 7 ? attributes : (attributes * 5 + 3) & 15) << 3;
              memory.Write_U32(mapped ? 0x80000000 | (0x123 << 7) : 0, hash_group(page));
              memory.Write_U32((page == 7 ? 0x4000u : 0x8000u) | 3 | attr, hash_group(page) + 4);
            }
            const auto initial = physical_memory;
            const u32 opcode =
                form < 2 ? ((instruction.direct + (form & 1)) << 26) | (9u << 21) | (7u << 16) | 5
                         : (31u << 26) | (9u << 21) | (7u << 16) | (8u << 11) |
                               ((instruction.indexed + ((form & 1) ? 32 : 0)) << 1);
            UGeckoInstruction decoded;
            decoded.hex = opcode;
            reference_pi_requests = 0;
            reference_pi_snapshots.clear();
            instruction.handlers[form](executor, decoded);
            const auto expected = physical_memory;
            const unsigned expected_pi = reference_pi_requests;
            const auto expected_pi_snapshots = reference_pi_snapshots;
            std::copy(initial.begin(), initial.end(), physical_memory.begin());
            GuestBus bus;
            auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(bus);
            auto &guest = *guest_owner;
            auto &state = guest.state();
            state.pc = 0x1000;
            state.msr = original_msr;
            state.sr[4] = 0x123;
            state.spr[cupid::cpu::spr::sdr1] = 0x20000;
            state.spr[536] = 0x40000003;
            state.spr[537] = (attributes << 3) | 2;
            state.spr[cupid::cpu::spr::dsisr] = 0x13579bdf;
            state.spr[cupid::cpu::spr::dar] = 0x2468ace0;
            state.gpr[7] = address - 5;
            state.gpr[8] = 5;
            state.gpr[9] = random;
            state.fpr[9].bits = {0x82765acf30149abcull, 0x9abcdef012345678ull};
            bus.write32(0x1000, opcode);
            guest_pi_requests = 0;
            guest_pi_snapshots.clear();
            guest.step();
            require(state.pc == (reference.Exceptions ? 0x300u : 0x1004u), comparisons,
                    "store exception");
            require(state.gpr[7] == reference.gpr[7], comparisons, "store update");
            require(state.spr[cupid::cpu::spr::dar] == reference.spr[SPR_DAR], comparisons,
                    "store DAR");
            require(state.spr[cupid::cpu::spr::dsisr] == reference.spr[SPR_DSISR], comparisons,
                    "store DSISR");
            if (guest_pi_requests != expected_pi)
              std::cerr << "opcode=" << std::hex << opcode << " EA=" << address
                        << " attrs=" << attributes << " mappings=" << mappings
                        << " expectedPI=" << expected_pi << " actualPI=" << guest_pi_requests
                        << " firstPTE=" << read_physical_word(hash_group(7) + 4) << std::dec
                        << '\n';
            require(guest_pi_requests == expected_pi, comparisons, "store PI request count");
            require(guest_pi_snapshots == expected_pi_snapshots, comparisons,
                    "store callback data order");
            require(std::equal(expected.begin() + 0x4000, expected.begin() + 0xa000,
                               physical_memory.begin() + 0x4000),
                    comparisons, "store data/guards");
            require(read_physical_word(hash_group(7) + 4) ==
                        ((u32(expected[hash_group(7) + 4]) << 24) |
                         (u32(expected[hash_group(7) + 5]) << 16) |
                         (u32(expected[hash_group(7) + 6]) << 8) | expected[hash_group(7) + 7]),
                    comparisons, "store first PTE");
            require(read_physical_word(hash_group(8) + 4) ==
                        ((u32(expected[hash_group(8) + 4]) << 24) |
                         (u32(expected[hash_group(8) + 5]) << 16) |
                         (u32(expected[hash_group(8) + 6]) << 8) | expected[hash_group(8) + 7]),
                    comparisons, "store second PTE");
          }
    }
  return comparisons;
}

unsigned compare_tlb_stores() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  for (unsigned attributes = 0; attributes < 16; ++attributes)
    for (unsigned offset = 0; offset < 8; ++offset)
      for (unsigned operation : {38u, 44u, 36u}) {
        std::fill(physical_memory.begin(), physical_memory.end(), 0);
        std::fill(physical_memory.begin() + 0x4000, physical_memory.begin() + 0xa000, 0x5a);
        auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
        auto &memory = *memory_owner;
        auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
        auto &manager = *manager_owner;
        auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
        auto &mmu = *mmu_owner;
        auto &reference = manager.GetPPCState();
        Interpreter executor(system, reference, mmu, manager.GetBranchWatch(),
                             manager.GetSymbolDB());
        reference.msr.Hex = cupid::cpu::msr::dr | cupid::cpu::msr::ri;
        reference.pc = 0x1000;
        reference.sr[4] = 0x123;
        reference.pagetable_base = 0x20000;
        reference.pagetable_mask = 0xffff;
        reference.m_enable_dcache = false;
        reference.spr[SPR_DAR] = 0x2468ace0;
        reference.spr[SPR_DSISR] = 0x13579bdf;
        const u32 pteg = hash_group(3), address = 0x40003000 + offset;
        reference.gpr[7] = address - 5;
        reference.gpr[9] = 0xa1b2c3d4;
        memory.Write_U32(0x80000000 | (0x123 << 7), pteg);
        memory.Write_U32(0x4003 | (attributes << 3), pteg + 4);
        mmu.DBATUpdated();
        auto reference_memory = physical_memory;
        auto guest_memory = physical_memory;
        GuestBus bus;
        auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(bus);
        auto &guest = *guest_owner;
        auto &state = guest.state();
        state.msr = reference.msr.Hex;
        state.sr[4] = 0x123;
        state.spr[cupid::cpu::spr::sdr1] = 0x20000;
        state.spr[cupid::cpu::spr::dar] = 0x2468ace0;
        state.spr[cupid::cpu::spr::dsisr] = 0x13579bdf;
        state.gpr[7] = address - 5;
        state.gpr[9] = 0xa1b2c3d4;
        const auto compare_step = [&](u32 opcode, Interpreter::Instruction handler) {
          ++comparisons;
          std::copy(reference_memory.begin(), reference_memory.end(), physical_memory.begin());
          reference_pi_requests = 0;
          UGeckoInstruction decoded;
          decoded.hex = opcode;
          handler(executor, decoded);
          reference_memory = physical_memory;
          const auto expected_pi = reference_pi_requests;
          std::copy(guest_memory.begin(), guest_memory.end(), physical_memory.begin());
          state.pc = 0x1000;
          bus.write32(0x1000, opcode);
          guest_pi_requests = 0;
          guest.step();
          guest_memory = physical_memory;
          require(state.pc == (reference.Exceptions ? 0x300u : 0x1004u), comparisons,
                  "TLB exception");
          require(state.gpr[7] == reference.gpr[7] && state.gpr[9] == reference.gpr[9] &&
                      state.gpr[10] == reference.gpr[10],
                  comparisons, "TLB registers");
          require(state.spr[cupid::cpu::spr::dar] == reference.spr[SPR_DAR] &&
                      state.spr[cupid::cpu::spr::dsisr] == reference.spr[SPR_DSISR],
                  comparisons, "TLB fault fields");
          require(expected_pi == guest_pi_requests, comparisons, "TLB PI requests");
          require(std::equal(reference_memory.begin() + 0x4000, reference_memory.begin() + 0xa000,
                             guest_memory.begin() + 0x4000),
                  comparisons, "TLB data");
          require(std::equal(reference_memory.begin() + pteg, reference_memory.begin() + pteg + 8,
                             guest_memory.begin() + pteg),
                  comparisons, "TLB PTE fields");
        };
        const auto change_pte = [&](u32 high, u32 low) {
          std::copy(reference_memory.begin(), reference_memory.end(), physical_memory.begin());
          memory.Write_U32(high, pteg);
          memory.Write_U32(low, pteg + 4);
          reference_memory = physical_memory;
          std::copy(guest_memory.begin(), guest_memory.end(), physical_memory.begin());
          bus.write32(pteg, high);
          bus.write32(pteg + 4, low);
          guest_memory = physical_memory;
        };
        const auto read_opcode = (34u << 26) | (10u << 21) | (7u << 16) | 5;
        const auto write_opcode = (operation << 26) | (9u << 21) | (7u << 16) | 5;
        const auto store_handler = operation == 38   ? Interpreter::stb
                                   : operation == 44 ? Interpreter::sth
                                                     : Interpreter::stw;
        const auto invalidate_opcode = (31u << 26) | (7u << 11) | (306u << 1);
        compare_step(read_opcode, Interpreter::lbz);
        const u32 revised = 0x8103 | ((attributes ^ 12) << 3);
        change_pte(0x80000000 | (0x123 << 7), revised);
        compare_step(write_opcode, store_handler);
        compare_step(write_opcode, store_handler);
        compare_step(invalidate_opcode, Interpreter::tlbie);
        compare_step(write_opcode, store_handler);
        change_pte(0x80000000 | (0x123 << 7), 0x8183 | (attributes << 3));
        compare_step(write_opcode, store_handler);
        compare_step(invalidate_opcode, Interpreter::tlbie);
        change_pte(0, 0x8183 | (attributes << 3));
        compare_step(write_opcode, store_handler);
      }
  return comparisons;
}

unsigned compare_bat_load_boundaries() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0, divergences = 0;
  for (unsigned console = 0; console < 2; ++console)
    for (unsigned extended = 0; extended < 2; ++extended)
      for (unsigned bank = 0; bank < 8; ++bank)
        for (unsigned mask = 0; mask < 8; ++mask)
          for (unsigned overlap = 0; overlap < 8; ++overlap)
            for (unsigned index = 0; index < 8; ++index) {
              ++comparisons;
              std::fill(physical_memory.begin(), physical_memory.end(), 0);
              for (unsigned page = 0; page < 8; ++page)
                physical_memory[page * 0x20000 + 0x20] = static_cast<u8>(page + 0x80);
              system.SetIsWii(console != 0);
              auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
              auto &memory = *memory_owner;
              auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
              auto &manager = *manager_owner;
              auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
              auto &mmu = *mmu_owner;
              auto &reference = manager.GetPPCState();
              Interpreter executor(system, reference, mmu, manager.GetBranchWatch(),
                                   manager.GetSymbolDB());
              const unsigned bat = bank < 4 ? SPR_DBAT0U + bank * 2 : SPR_DBAT4U + (bank - 4) * 2;
              const u32 upper = 0x40000003 | (overlap << 17) | (mask << 2);
              const u32 lower = 2;
              reference.spr[bat] = upper;
              reference.spr[bat + 1] = lower;
              reference.spr[SPR_HID4] = extended ? 0x02000000 : 0;
              reference.pc = 0x1000;
              reference.msr.Hex = cupid::cpu::msr::dr;
              reference.m_enable_dcache = false;
              reference.pagetable_base = 0;
              reference.pagetable_mask = 0xffff;
              reference.sr[4] = 0x321;
              reference.gpr[7] = 0x40000020 | (index << 17);
              reference.gpr[9] = 0x13579bdf;
              mmu.DBATUpdated();
              const u32 opcode = (34u << 26) | (9u << 21) | (7u << 16);
              UGeckoInstruction decoded;
              decoded.hex = opcode;
              Interpreter::lbz(executor, decoded);
              GuestBus bus;
              auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(
                  bus, console ? cupid::Console::Wii : cupid::Console::GameCube);
              auto &guest = *guest_owner;
              auto &state = guest.state();
              state.pc = 0x1000;
              state.msr = cupid::cpu::msr::dr;
              state.sr[4] = 0x321;
              state.spr[bat] = upper;
              state.spr[bat + 1] = lower;
              state.spr[cupid::cpu::spr::hid4] = extended ? 0x02000000 : 0;
              state.gpr[7] = 0x40000020 | (index << 17);
              state.gpr[9] = 0x13579bdf;
              bus.write32(0x1000, opcode);
              guest.step();
              const bool expected_fault = (reference.Exceptions & EXCEPTION_DSI) != 0;
              const bool actual_fault = state.pc == 0x300;
              if (expected_fault != actual_fault || state.gpr[9] != reference.gpr[9]) {
                ++divergences;
                if (divergences < 17)
                  std::cout << "case=" << comparisons << " console=" << console
                            << " extended=" << extended << " bank=" << bank << " mask=" << mask
                            << " overlap=" << overlap << " index=" << index
                            << " expectedFault=" << expected_fault
                            << " actualFault=" << actual_fault << " expected=" << std::hex
                            << reference.gpr[9] << " actual=" << state.gpr[9] << std::dec << '\n';
              }
            }
  require(divergences == 0, comparisons, "BAT load boundary divergence");
  return comparisons;
}

unsigned compare_bat_translations() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0, divergences = 0;
  u32 random = 0x136798df;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (unsigned console = 0; console < 2; ++console)
    for (unsigned extended = 0; extended < 2; ++extended)
      for (unsigned bank = 0; bank < 8; ++bank)
        for (unsigned configuration = 0; configuration < 2304; ++configuration) {
          const u32 mask = configuration < 2048 ? configuration : next() & 2047;
          const u32 virtual_base = next() & 0xfffe0000;
          const u32 physical_base = next() & 0xfffe0000;
          const u32 upper = virtual_base | (mask << 2) | (configuration < 2048 ? 3u : next() & 3);
          const u32 lower = physical_base | (next() & 0x7f);
          system.SetIsWii(console != 0);
          auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
          auto &memory = *memory_owner;
          auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
          auto &manager = *manager_owner;
          auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
          auto &mmu = *mmu_owner;
          auto &reference = manager.GetPPCState();
          const unsigned bat = bank < 4 ? SPR_DBAT0U + bank * 2 : SPR_DBAT4U + (bank - 4) * 2;
          reference.spr[bat] = upper;
          reference.spr[bat + 1] = lower;
          reference.spr[SPR_HID4] = extended ? 0x02000000 : 0;
          for (auto &segment : reference.sr)
            segment = 0x321;
          reference.m_enable_dcache = false;
          reference.pagetable_base = 0;
          reference.pagetable_mask = 0xffff;
          mmu.DBATUpdated();
          GuestBus bus;
          auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(
              bus, console ? cupid::Console::Wii : cupid::Console::GameCube);
          auto &guest = *guest_owner;
          auto &state = guest.state();
          state.pc = 0x1000;
          state.spr[bat] = upper;
          state.spr[bat + 1] = lower;
          state.spr[cupid::cpu::spr::hid4] = extended ? 0x02000000 : 0;
          for (auto &segment : state.sr)
            segment = 0x321;
          for (unsigned query = 0; query < 8; ++query) {
            ++comparisons;
            u32 address = virtual_base | ((next() & mask) << 17) | (next() & 0x1ffff);
            if (query & 1)
              address ^= u32{1} << (17 + (next() % 15));
            const auto expected = mmu.GetTranslatedAddress(address);
            std::optional<u32> observed;
            state.msr = cupid::cpu::msr::dr;
            try {
              observed = guest.translate(address);
            } catch (const std::exception &) {
              throw;
            } catch (...) {
              if (state.spr[cupid::cpu::spr::dar] != address ||
                  state.spr[cupid::cpu::spr::dsisr] != 0x40000000)
                throw std::runtime_error("Unexpected translation failure");
            }
            if (expected != observed) {
              ++divergences;
              if (divergences < 17)
                std::cout << "case=" << comparisons << " console=" << console
                          << " extended=" << extended << " bank=" << bank << " upper=" << std::hex
                          << upper << " lower=" << lower << " address=" << address
                          << " expected=" << expected.value_or(0xffffffff)
                          << " observed=" << observed.value_or(0xffffffff) << std::dec << '\n';
            }
          }
        }
  require(divergences == 0, comparisons, "BAT address divergence");
  return comparisons;
}

unsigned compare_mutable_bat_banks() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0x835713df;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  std::fill(physical_memory.begin(), physical_memory.end(), 0);
  for (bool wii : {false, true})
    for (bool instruction : {false, true})
      for (bool user : {false, true}) {
        system.SetIsWii(wii);
        auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
        auto &memory = *memory_owner;
        auto manager_owner = std::make_unique<PowerPC::PowerPCManager>(system);
        auto &manager = *manager_owner;
        auto mmu_owner = std::make_unique<PowerPC::MMU>(system, memory, manager);
        auto &mmu = *mmu_owner;
        auto &reference = manager.GetPPCState();
        GuestBus bus;
        auto guest_owner = std::make_unique<cupid::cpu::Interpreter>(
            bus, wii ? cupid::Console::Wii : cupid::Console::GameCube);
        auto &guest = *guest_owner;
        auto &state = guest.state();
        for (auto &segment : state.sr)
          segment = 0x321;
        const auto base_for = [&](unsigned bank) {
          return bank < 4 ? (instruction ? SPR_IBAT0U : SPR_DBAT0U) + bank * 2
                          : (instruction ? SPR_IBAT4U : SPR_DBAT4U) + (bank - 4) * 2;
        };
        for (unsigned iteration = 0; iteration < 2048; ++iteration) {
          for (unsigned bank = 0; bank < 8; ++bank) {
            const unsigned base = base_for(bank);
            // Both valid and malformed bases/masks, all WIMG and PP bits.
            const u32 upper = next(), lower = next();
            reference.spr[base] = state.spr[base] = upper;
            reference.spr[base + 1] = state.spr[base + 1] = lower;
          }
          for (unsigned query = 0; query < 8; ++query) {
            const unsigned bank = next() & 7, base = base_for(bank);
            if (query == 2 || query == 5) {
              // Mutations occur on an already-used CPU instance, including
              // enabling/disabling a higher overlapping entry.
              reference.spr[base] = state.spr[base] = next();
              reference.spr[base + 1] = state.spr[base + 1] = next();
            }
            if (query >= 4) {
              const unsigned other = base_for((bank + 3) & 7);
              reference.spr[other] = state.spr[other] = state.spr[base];
              reference.spr[other + 1] = state.spr[other + 1] = next();
            }
            reference.spr[SPR_HID4] = state.spr[cupid::cpu::spr::hid4] =
                query & 1 ? 0x02000000u : 0;
            if (instruction)
              mmu.IBATUpdated();
            else
              mmu.DBATUpdated();
            const u32 mask = (state.spr[base] & 0x1ffc) << 15;
            u32 address = (state.spr[base] & 0xfffe0000) | (next() & (mask | 0x1ffff));
            if (query == 0 || query == 3)
              address = next();
            const auto &table = instruction ? mmu.GetIBATTable() : mmu.GetDBATTable();
            const u32 entry = table[address >> PowerPC::BAT_INDEX_SHIFT];
            const std::optional<u32> expected =
                entry & PowerPC::BAT_MAPPED_BIT
                    ? std::optional<u32>((entry & PowerPC::BAT_RESULT_MASK) |
                                         (address & (PowerPC::BAT_PAGE_SIZE - 1)))
                    : std::nullopt;
            state.pc = 0x13579000;
            state.msr = (instruction ? cupid::cpu::msr::ir : cupid::cpu::msr::dr) |
                        (user ? cupid::cpu::msr::pr : 0);
            const auto before = state;
            std::optional<u32> observed;
            try {
              observed = guest.translate(address, instruction, query & 1);
            } catch (const std::exception &) {
              throw;
            } catch (...) {
              require(!expected, comparisons, "unexpected mutable BAT fault");
              require(state.spr[cupid::cpu::spr::srr0] == (instruction ? address : before.pc),
                      comparisons, "mutable BAT saved PC");
              require(state.spr[cupid::cpu::spr::srr1] ==
                          ((before.msr & 0x87c0ffff) | (instruction ? 0x40000000u : 0)),
                      comparisons, "mutable BAT saved MSR");
              if (!instruction)
                require(state.spr[cupid::cpu::spr::dar] == address &&
                            state.spr[cupid::cpu::spr::dsisr] ==
                                (0x40000000u | (query & 1 ? 0x02000000u : 0)),
                        comparisons, "mutable BAT data fault");
            }
            ++comparisons;
            require(expected == observed, comparisons, "mutable IBAT/DBAT table output");
            require(state.gpr == before.gpr && state.sr == before.sr && state.cr == before.cr &&
                        state.fpscr == before.fpscr && state.pc == before.pc &&
                        state.last_pc == before.last_pc &&
                        state.last_opcode == before.last_opcode &&
                        state.instructions == before.instructions &&
                        state.cycles == before.cycles && state.timebase == before.timebase,
                    comparisons, "mutable BAT retained CPU state");
            for (unsigned n = 0; n < state.fpr.size(); ++n)
              require(state.fpr[n].bits == before.fpr[n].bits, comparisons,
                      "mutable BAT FPR state");
            if (expected)
              require(state.spr == before.spr && state.msr == before.msr, comparisons,
                      "mutable BAT successful register retention");
          }
        }
      }
  require(comparisons == 131072, comparisons, "mutable BAT case count");
  return comparisons;
}

int main() {
  try {
    const auto loads = compare_loads();
    const auto stores =
        compare_stores() + compare_quantized_stores() + compare_bat_stores() + compare_tlb_stores();
    const auto bat_loads = compare_bat_load_boundaries();
    const auto bat_addresses = compare_bat_translations();
    const auto mutable_bats = compare_mutable_bat_banks();
    require(reference_warning_calls != 0 && !reference_warning_samples.empty(), bat_addresses,
            "BAT warning adapter exercised");
    std::cout << loads << " direct source load comparisons and " << stores
              << " store/BAT/TLB comparisons, plus " << bat_loads << " BAT load cases and "
              << bat_addresses << " BAT address cases and " << mutable_bats
              << " mutable IBAT/DBAT table cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
