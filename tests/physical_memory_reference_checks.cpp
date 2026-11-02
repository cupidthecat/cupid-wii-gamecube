// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
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
#include "Core/Core.h"
#include "Core/HW/AudioInterface.h"
#include "Core/HW/CPU.h"
#include "Core/HW/DSP.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/GPFifo.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/MemoryInterface.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/VideoInterface.h"
#include "Core/HW/WII_IPC.h"
#include "Core/PowerPC/GDBStub.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/EFBInterface.h"
#include "VideoCommon/PixelEngine.h"
#include "cupid/core/memory.hpp"
#include "cupid/cpu/cache.hpp"
#include "cupid/cpu/data_cache.hpp"
#include "cupid/cpu/interpreter.hpp"

// The actual memory manager, MMU, and instruction cache are linked unchanged.
// Original host-service adapters provide byte storage and record diagnostics.
// All service objects have ordinary lifetimes; unused execution and device
// services fail explicitly. No expected transfer or cache data is synthesized.
namespace {
[[noreturn]] void unused(std::source_location call = std::source_location::current()) {
  throw std::logic_error(call.function_name());
}

std::vector<u8> arena_storage;
std::vector<std::string> reference_messages;
unsigned reference_pi_requests = 0;

} // namespace

#include "unused_jit_adapters.inc"

namespace Core {
struct System::Impl {
  explicit Impl(System &system) : jit(system), pi(system) {
  }

  JitInterface jit;
  ProcessorInterface::ProcessorInterfaceManager pi;
  PowerPC::PowerPCState state{};
};

System::System() : m_impl(std::make_unique<Impl>(*this)), m_mmu_enabled(true) {
}

System::~System() = default;

PowerPC::PowerPCState &System::GetPPCState() const {
  return m_impl->state;
}

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

AudioInterface::AudioInterfaceManager &System::GetAudioInterface() const {
  unused();
}

CommandProcessor::CommandProcessorManager &System::GetCommandProcessor() const {
  unused();
}

DSP::DSPManager &System::GetDSP() const {
  unused();
}

DVD::DVDInterface &System::GetDVDInterface() const {
  unused();
}

ExpansionInterface::ExpansionInterfaceManager &System::GetExpansionInterface() const {
  unused();
}

IOS::WiiIPC &System::GetWiiIPC() const {
  unused();
}

MemoryInterface::MemoryInterfaceManager &System::GetMemoryInterface() const {
  unused();
}

PixelEngine::PixelEngineManager &System::GetPixelEngine() const {
  unused();
}

SerialInterface::SerialInterfaceManager &System::GetSerialInterface() const {
  unused();
}

VideoInterface::VideoInterfaceManager &System::GetVideoInterface() const {
  unused();
}

void DisplayMessage(std::string, int) {
  unused();
}
} // namespace Core

namespace AudioInterface {
void AudioInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace AudioInterface

namespace CommandProcessor {
void CommandProcessorManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace CommandProcessor

namespace DSP {
void DSPManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace DSP

namespace DVD {
void DVDInterface::RegisterMMIO(MMIO::Mapping *, u32, bool) {
  unused();
}
} // namespace DVD

namespace ExpansionInterface {
void ExpansionInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace ExpansionInterface

namespace IOS {
void WiiIPC::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace IOS

namespace MemoryInterface {
void MemoryInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace MemoryInterface

namespace PixelEngine {
void PixelEngineManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace PixelEngine

namespace ProcessorInterface {
void ProcessorInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace ProcessorInterface

namespace SerialInterface {
void SerialInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace SerialInterface

namespace VideoInterface {
void VideoInterfaceManager::RegisterMMIO(MMIO::Mapping *, u32) {
  unused();
}
} // namespace VideoInterface

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

size_t MemArena::GetPageSize() const {
  return 4096;
}

void MemArena::GrabSHMSegment(size_t byte_count, std::string_view) {
  if (!arena_storage.empty())
    throw std::logic_error("Fixture arena already allocated");
  arena_storage.resize(byte_count);
}

void *MemArena::CreateView(s64 offset, size_t byte_count) {
  if (offset < 0 || static_cast<u64>(offset) > arena_storage.size() ||
      byte_count > arena_storage.size() - static_cast<size_t>(offset))
    throw std::out_of_range("Fixture arena view");
  return arena_storage.data() + static_cast<size_t>(offset);
}

void MemArena::ReleaseView(void *view, size_t byte_count) {
  const auto first = reinterpret_cast<std::uintptr_t>(arena_storage.data());
  const auto observed = reinterpret_cast<std::uintptr_t>(view);
  if (observed < first || observed - first > arena_storage.size() ||
      byte_count > arena_storage.size() - (observed - first))
    throw std::out_of_range("Fixture arena release");
}

void MemArena::ReleaseSHMSegment() {
  arena_storage.clear();
}

u8 *MemArena::ReserveMemoryRegion(size_t) {
  unused();
}

void MemArena::ReleaseMemoryRegion() {
  unused();
}

void *MemArena::MapInMemoryRegion(s64, size_t, void *, bool) {
  unused();
}

bool MemArena::ChangeMappingProtection(void *, size_t, bool) {
  unused();
}

void MemArena::UnmapFromMemoryRegion(void *, size_t) {
  unused();
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

std::string GetStringT(const char *message) {
  return message;
}

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view format,
                     const fmt::format_args &arguments) {
  reference_messages.push_back(fmt::vformat(format, arguments));
  return false;
}

namespace Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view format,
                       const fmt::format_args &arguments) {
  reference_messages.push_back(fmt::vformat(format, arguments));
}
} // namespace Log

namespace Debug {
MemoryPatches::MemoryPatches() = default;
MemoryPatches::~MemoryPatches() = default;
} // namespace Debug
} // namespace Common

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
const Info<bool> MAIN_DISABLE_ICACHE{{}, false};

ConfigChangedCallbackID AddConfigChangedCallback(ConfigChangedCallback) {
  unused();
}

void RemoveConfigChangedCallback(ConfigChangedCallbackID) {
  unused();
}

const Info<bool> MAIN_LOW_DCBZ_HACK{{}, false};
const Info<bool> MAIN_RAM_OVERRIDE_ENABLE{{}, false};
const Info<u32> MAIN_MEM1_SIZE{{}, 0x01800000};
const Info<u32> MAIN_MEM2_SIZE{{}, 0x04000000};

u64 GetConfigVersion() {
  return 1;
}

std::optional<std::string> GetAsString(const Location &) {
  return std::nullopt;
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
}

JitInterface::JitInterface(Core::System &system) : m_system(system) {
}

JitInterface::~JitInterface() {
  if (m_jit)
    std::terminate();
  // This driver never creates a compiled execution backend.
  (void)m_jit.release();
}

void require(bool condition, unsigned case_number, const char *field) {
  if (!condition)
    throw std::runtime_error("MMU case " + std::to_string(case_number) + ": " + field);
}

void memory_checks() {
  auto &system = Core::System::GetInstance();
  unsigned data_cases = 0, span_cases = 0, dma_cases = 0, error_cases = 0;
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    Memory::MemoryManager memory(system);
    memory.Init();
    PowerPC::PowerPCManager manager(system);
    PowerPC::MMU mmu(system, memory, manager);
    auto &state = manager.GetPPCState();
    state.pc = 0x1000;
    state.m_enable_dcache = false;
    state.msr.Hex = 0;
    system.GetPPCState().pc = 0x1000;
    cupid::Memory guest(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    unsigned pi = 0;
    guest.memory_error = [&] { ++pi; };
    const auto read = [&](u32 address, unsigned width) -> u64 {
      switch (width) {
      case 1:
        return mmu.Read<u8>(address);
      case 2:
        return mmu.Read<u16>(address);
      case 4:
        return mmu.Read<u32>(address);
      case 8:
        return mmu.Read<u64>(address);
      default:
        throw std::logic_error("Fixture width");
      }
    };
    const auto write = [&](u32 address, u64 value, unsigned width) {
      switch (width) {
      case 1:
        mmu.Write<u8>(static_cast<u32>(value), address);
        break;
      case 2:
        mmu.Write<u16>(static_cast<u32>(value), address);
        break;
      case 4:
        mmu.Write<u32>(static_cast<u32>(value), address);
        break;
      case 8:
        mmu.Write<u64>(value, address);
        break;
      default:
        throw std::logic_error("Fixture width");
      }
    };
    const auto guest_write = [&](u32 address, u64 value, unsigned width) {
      if (width == 8) {
        guest.write_cpu_data(address, static_cast<u32>(value >> 32), 4);
        guest.write_cpu_data(address + 4, static_cast<u32>(value), 4);
      } else {
        guest.write_cpu_data(address, static_cast<u32>(value), width);
      }
    };
    constexpr std::array<u32, 6> boundaries{0x01800000, 0x02000000, 0x04000000,
                                            0x06000000, 0x07800000, 0x08000000};
    for (u32 boundary : boundaries)
      for (int displacement = -8; displacement <= 8; ++displacement)
        for (unsigned width : {1u, 2u, 4u, 8u}) {
          const u32 address = boundary + static_cast<u32>(displacement);
          if (u64(address) + width > 0x08000000)
            continue;
          ++data_cases;
          for (int offset = -4; offset < 16; ++offset) {
            const u32 backing = (address + static_cast<u32>(offset)) & 0x01ffffff;
            memory.GetRAM()[backing] = static_cast<u8>(0xa0 + offset);
            guest.write_cpu_data(backing, static_cast<u8>(0xa0 + offset), 1);
          }
          u64 expected = 0;
          for (unsigned n = 0; n < width; ++n)
            expected = (expected << 8) | (0xa0 + n);
          require(read(address, width) == expected &&
                      guest.read_cpu_data(address, width) == expected,
                  data_cases, "actual/product CPU RAM read");
          constexpr u64 payload = 0x123456789abcdef0ull;
          write(address, payload, width);
          guest_write(address, payload, width);
          for (int offset = -4; offset < 16; ++offset) {
            const u32 backing = (address + static_cast<u32>(offset)) & 0x01ffffff;
            const u8 expected_byte =
                offset >= 0 && static_cast<unsigned>(offset) < width
                    ? static_cast<u8>(payload >> ((width - 1 - static_cast<unsigned>(offset)) * 8))
                    : static_cast<u8>(0xa0 + offset);
            require(memory.GetRAM()[backing] == expected_byte &&
                        guest.read_cpu_data(backing, 1) == expected_byte,
                    data_cases, "actual/product RAM write guards");
          }
        }
    constexpr std::array<u32, 8> starts{0,          0x20,       0x017ffff8, 0x017fffff,
                                        0x01800000, 0x01fffff8, 0x02000000, 0x07fffff8};
    for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u})
      for (u32 start : starts)
        for (size_t length : {size_t{0}, size_t{1}, size_t{4}, size_t{8}, size_t{16}}) {
          ++span_cases;
          const u32 address = start | alias;
          const bool expected = memory.GetPointerForRange(address, length) != nullptr;
          bool observed = true;
          try {
            (void)guest.range(address, length);
          } catch (const cupid::BusFault &fault) {
            require(!fault.unsupported_reason, span_cases, "ordinary host range fault");
            observed = false;
          }
          require(expected == observed, span_cases, "actual/product strict host range");
        }
    for (u32 alias : {0u, 0x40000000u, 0x80000000u, 0xc0000000u})
      for (u32 start : {0x20u, 0x017fffe0u, 0x01800000u, 0x02000000u}) {
        ++dma_cases;
        const bool valid = start < 0x01800000;
        std::fill_n(memory.GetL1Cache(), 32, 0x5a);
        std::fill(guest.range(0xe0000000, 32).begin(), guest.range(0xe0000000, 32).end(), 0x5a);
        for (unsigned n = 0; n < 32; ++n) {
          memory.GetRAM()[(start + n) & 0x01ffffff] = 0xa3;
          guest.write_cpu_data((start + n) & 0x01ffffff, 0xa3, 1);
        }
        mmu.DMA_MemoryToLC(0, start | alias, 1);
        bool observed = true;
        try {
          guest.locked_cache_dma(start | alias, 0, 32, true);
        } catch (const cupid::BusFault &fault) {
          require(!fault.unsupported_reason, dma_cases, "ordinary DMA host range fault");
          observed = false;
        }
        require(observed == valid, dma_cases, "actual/product DMA range");
        for (unsigned n = 0; n < 32; ++n)
          require(memory.GetL1Cache()[n] == guest.read_cpu_data(0xe0000000 + n, 1), dma_cases,
                  "actual/product DMA bytes");
      }
    for (u32 address : {0x10000000u, 0x14000000u, 0x40000020u, 0x80000020u, 0xc0000020u})
      for (unsigned width : {1u, 2u, 4u, 8u}) {
        if (wii && address == 0x10000000)
          continue;
        ++error_cases;
        const unsigned source_before = reference_pi_requests, guest_before = pi;
        require(read(address, width) == 0 && guest.read_cpu_data(address, width) == 0, error_cases,
                "actual/product unresolved read zero");
        require(reference_pi_requests - source_before == 1 && pi - guest_before == 1, error_cases,
                "actual/product unresolved read PI");
        write(address, 0x123456789abcdef0ull, width);
        guest_write(address, 0x123456789abcdef0ull, width);
        require(reference_pi_requests - source_before == pi - guest_before &&
                    pi - guest_before == 1 + (width == 8 ? 2u : 1u),
                error_cases, "actual/product unresolved store PI");
        require(state.Exceptions == 0, error_cases, "unresolved physical is not DSI");
      }
    const u32 table = 0x01800000 | (((0x321u ^ 7u) << 6) & 0xffc0);
    mmu.Write<u32>(0x80000000 | (0x321u << 7), table);
    mmu.Write<u32>(0x4003, table + 4);
    guest.write_cpu_data(table, 0x80000000 | (0x321u << 7), 4);
    guest.write_cpu_data(table + 4, 0x4003, 4);
    state.sr[4] = 0x321;
    state.spr[SPR_SDR] = 0x01800000;
    mmu.SDRUpdated();
    state.msr.Hex = 0x10;
    (void)mmu.Read<u32>(0x40007000);
    require(!guest.write_page_table32(table + 4, 0x4103), 0, "strict product R update");
    mmu.Write<u32>(0xabcdef01, 0x40007000);
    require(!guest.write_page_table32(table + 4, 0x4183), 0, "strict product C update");
    require(memory.GetRAM()[table + 6] == 0x40 && memory.GetRAM()[table + 7] == 3 &&
                guest.read_cpu_data(table + 4, 4) == 0x4003 && guest.page_table_write_errors() == 2,
            0, "actual/product rejected R/C backing and diagnostic count");
    memory.Shutdown();
  }
  std::cout << data_cases << " actual/product CPU RAM cases, " << span_cases << " host ranges, "
            << dma_cases << " DMA cases, " << error_cases
            << " unresolved PI cases, and padding PTE R/C cases passed\n";
}

void cache_checks() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0x5a48b279;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    Memory::MemoryManager memory(system);
    memory.Init();
    PowerPC::PowerPCManager manager(system);
    PowerPC::MMU mmu(system, memory, manager);
    auto &state = manager.GetPPCState();
    state.iCache.Cache::Init(memory);
    state.pc = 0x1000;
    system.GetPPCState().pc = 0x1000;
    state.m_enable_dcache = false;
    state.msr.Hex = 0;
    cupid::Memory bus(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    cupid::cpu::InstructionCache guest;
    constexpr std::array<unsigned, 48> history{
        0, 1,  2, 3, 4, 5,  6,  7,  0,  3,  7,  1, 8, 9, 2, 10, 6, 11, 4, 12, 5, 13, 7,  14,
        1, 15, 0, 8, 9, 10, 11, 12, 13, 14, 15, 3, 0, 4, 1, 5,  2, 6,  7, 8,  3, 9,  10, 0};
    for (unsigned operation = 0; operation < 300000; ++operation) {
      const bool warming = operation < 128 * history.size();
      const unsigned choice = warming ? 20u : next() % 32;
      const unsigned set =
          (warming ? static_cast<unsigned>(operation / history.size()) : next()) & 127;
      const unsigned tag = warming ? history[operation % history.size()] : next() & 15;
      const unsigned word = next() & 7;
      const u32 bank = wii && (next() & 1) ? 0x10000000u : 0u;
      const u32 physical = bank | 0x100000u | (tag << 12) | (set << 5) | (word << 2);
      const u32 alias = (next() & 3) << 30;
      const u32 address = physical | alias;
      if (choice == 0) {
        guest.reset();
        state.iCache.Reset(system.GetJitInterface());
      } else if (choice < 4) {
        guest.invalidate(address);
        state.iCache.Invalidate(memory, system.GetJitInterface(), address);
      } else {
        const bool enabled = choice >= 7;
        const bool locked = choice >= 28;
        state.spr[SPR_HID0] = (enabled ? 0x8000u : 0) | (locked ? 0x2000u : 0);
        const u32 value = next();
        memory.Write_U32(value, physical);
        bus.write32(physical, value);
        const auto reference = mmu.TryReadInstruction(address);
        const u32 observed = guest.read(bus, address, enabled, locked);
        ++comparisons;
        if (!reference.valid || observed != reference.hex) {
          std::cerr << "case=" << comparisons << " operation=" << operation << " wii=" << wii
                    << " enabled=" << enabled << " locked=" << locked << " address=" << std::hex
                    << address << " expected=" << reference.hex << " observed=" << observed
                    << std::dec << '\n';
          throw std::runtime_error("Mixed instruction cache divergence");
        }
      }
    }
    memory.Shutdown();
  }
  std::cout << comparisons << " mixed cache reads across 600000 operations passed\n";
}

void fetch_backing_checks() {
  try {
    auto &system = Core::System::GetInstance();
    unsigned cases = 0, divergences = 0;
    for (bool wii : {false, true}) {
      system.SetIsWii(wii);
      Memory::MemoryManager memory(system);
      memory.Init();
      PowerPC::PowerPCManager manager(system);
      PowerPC::MMU mmu(system, memory, manager);
      auto &state = manager.GetPPCState();
      state.iCache.Cache::Init(memory);
      state.msr.Hex = 0;
      state.m_enable_dcache = false;
      cupid::Memory bus(wii ? cupid::Console::Wii : cupid::Console::GameCube);
      cupid::cpu::InstructionCache cache;
      unsigned product_pi = 0, device_reads = 0;
      bus.memory_error = [&] { ++product_pi; };
      bus.mmio_handles = [](u32 address) { return (address & 0xf8000000) == 0x08000000; };
      bus.mmio_read = [&](u32, unsigned) {
        ++device_reads;
        return 0x3860002au;
      };
      const auto compare = [&](u32 address, bool enabled, bool locked) {
        ++cases;
        state.spr[SPR_HID0] = (enabled ? 0x8000u : 0) | (locked ? 0x2000u : 0);
        state.Exceptions = 0;
        const unsigned pi_before = reference_pi_requests;
        const auto diagnostic_before = reference_messages.size();
        const auto product_diagnostic_before = bus.instruction_backing_read_errors();
        const auto reference = mmu.TryReadInstruction(address);
        require(reference.valid && state.Exceptions == 0 && reference_pi_requests == pi_before,
                cases, "actual cold fetch is valid without PI/ISI");
        try {
          const u32 observed = cache.read(bus, address, enabled, locked);
          require(bus.instruction_backing_read_errors() - product_diagnostic_before ==
                      static_cast<u64>(reference_messages.size() != diagnostic_before),
                  cases, "one product diagnostic per rejected backing copy");
          if (observed != reference.hex) {
            ++divergences;
            if (divergences < 12)
              std::cerr << "value case=" << cases << " wii=" << wii << " addr=" << std::hex
                        << address << " expected=" << reference.hex << " got=" << observed
                        << std::dec << '\n';
          }
        } catch (const cupid::BusFault &) {
          ++divergences;
          if (divergences < 12)
            std::cerr << "fault case=" << cases << " wii=" << wii << " addr=" << std::hex << address
                      << " expected=" << reference.hex << std::dec << '\n';
        }
      };
      const auto reset = [&] {
        state.iCache.Reset(system.GetJitInterface());
        cache.reset();
      };
      constexpr std::array<u32, 11> invalid{0x01802000, 0x02002000, 0x04002000, 0x06002000,
                                            0x08002000, 0x0c002000, 0x14002000, 0xe0002000,
                                            0x20002000, 0x38002000, 0x7e002000};
      for (unsigned set_number = 0; set_number < 128; ++set_number) {
        const u32 set_bits = set_number << 5;
        for (bool enabled : {false, true})
          for (u32 address : invalid) {
            reset();
            for (unsigned word = 0; word < 8; ++word)
              compare(address + set_bits + word * 4, enabled, false);
          }
        // Invalidation and Reset discard validity while retaining initialized words.
        for (unsigned operation = 0; operation < 2; ++operation) {
          reset();
          for (unsigned word = 0; word < 8; ++word) {
            const u32 value = 0x38600011u + word;
            memory.Write_U32(value, 0x2000 + set_bits + word * 4);
            bus.write32(0x2000 + set_bits + word * 4, value);
          }
          compare(0x2000 + set_bits, true, false);
          if (operation == 0) {
            state.iCache.Invalidate(memory, system.GetJitInterface(), 0x2000 + set_bits);
            cache.invalidate(0x2000 + set_bits);
          } else {
            reset();
          }
          for (unsigned word = 0; word < 8; ++word)
            compare(0x02002000 + set_bits + word * 4, true, false);
          // A newly installed failed-fill line is a defined hit even while locked.
          for (unsigned word = 0; word < 8; ++word)
            compare(0x02002000 + set_bits + word * 4, true, true);
        }
        // Full-set replacement must retain the selected line after failed CopyFromEmu.
        reset();
        for (unsigned tag = 0; tag < 8; ++tag) {
          const u32 address = 0x00102000 + set_bits + tag * 4096;
          for (unsigned word = 0; word < 8; ++word) {
            const u32 value = 0x38600020u + tag * 8 + word;
            memory.Write_U32(value, address + word * 4);
            bus.write32(address + word * 4, value);
          }
          compare(address, true, false);
        }
        for (unsigned word = 0; word < 8; ++word)
          compare(0x01802000 + set_bits + word * 4, true, false);
      }
      require(product_pi == 0, cases, "fetch path excludes PI");
      require(device_reads == 0, cases, "fetch backing excludes device callbacks");
      std::cout << "wii=" << wii << " device callbacks=" << device_reads << '\n';
      memory.Shutdown();
    }
    std::cout << cases << " defined fetch/cache cases, " << divergences << " divergences\n";
    require(divergences == 0, cases, "strict instruction backing");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    throw;
  }
}

void data_cache_checks() {
  auto &system = Core::System::GetInstance();
  unsigned operations = 0;
  u32 random = 0x31ca8b75;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    Memory::MemoryManager memory(system);
    memory.Init();
    auto reference = std::make_unique<PowerPC::Cache>();
    reference->Init(memory);
    cupid::Memory bus(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    auto product = std::make_unique<cupid::cpu::DataCache>();
    std::array<u8, 32> input{}, expected{}, actual{};
    const auto compare_backing = [&] {
      for (u32 bank : {0u, 0x10000000u}) {
        if (bank && !wii)
          continue;
        const auto original = memory.GetSpanForAddress(bank | 0x100000).first(0x11000);
        const auto candidate = bus.range(bank | 0x100000, 0x11000);
        require(std::equal(original.begin(), original.end(), candidate.begin()), operations,
                "data-cache dirty writeback and external backing");
      }
    };
    for (unsigned step = 0; step < 50000; ++step) {
      ++operations;
      const bool warming = step < 128 * 16;
      const unsigned operation = warming ? 1 : next() % 12;
      const unsigned set = warming ? (step / 16) : next() & 127;
      const unsigned tag = warming ? (step % 16) : next() & 15;
      const u32 bank = wii && (next() & 1) ? 0x10000000 : 0;
      const u32 physical = bank | 0x100000 | (tag << 12) | (set << 5) | (next() & 31);
      const u32 address = physical | ((next() & 3) << 30);
      const unsigned length = 1 + next() % 32;
      const bool locked = (next() & 3) == 0;
      for (u8 &byte : input)
        byte = u8(next());
      switch (operation) {
      case 0:
      case 1:
      case 2:
        reference->Write(memory, address, input.data(), length, locked);
        product->write(bus, address, std::span<const u8>(input).first(length), locked);
        break;
      case 3:
        reference->Store(memory, address);
        product->store(bus, address);
        break;
      case 4:
        reference->Invalidate(memory, address);
        product->invalidate(bus, address);
        break;
      case 5:
        reference->Flush(memory, address);
        product->flush(bus, address);
        break;
      case 6:
        reference->FlushAll(memory);
        product->flush_all(bus);
        break;
      case 7:
        reference->Reset();
        product->reset();
        break;
      case 8:
        memory.CopyToEmu(physical, input.data(), length);
        bus.copy_in(physical, std::span<const u8>(input).first(length));
        break;
      default:
        expected.fill(0xac);
        actual.fill(0xac);
        reference->Read(memory, address, expected.data(), length, locked);
        product->read(bus, address, std::span<u8>(actual).first(length), locked);
        require(expected == actual, operations, "data-cache read bytes and untouched tail");
        break;
      }
      if ((step & 63) == 0)
        compare_backing();
    }
    reference->FlushAll(memory);
    product->flush_all(bus);
    compare_backing();
    // Reset retains line bytes. Failed unlocked copies install the requested
    // tag over those bytes; a subsequent locked hit has a defined result.
    for (unsigned set = 0; set < 128; ++set) {
      reference->Reset();
      product->reset();
      const u32 address = 0x100000 | (set << 5);
      for (u8 &byte : input)
        byte = u8(next());
      reference->Write(memory, address, input.data(), 32, false);
      product->write(bus, address, input, false);
      reference->Reset();
      product->reset();
      for (bool locked : {false, true}) {
        reference->Read(memory, 0x01800000 | (set << 5), expected.data(), 32, locked);
        product->read(bus, 0x01800000 | (set << 5), actual, locked);
        ++operations;
        require(expected == actual, operations, "data-cache retained rejected fill");
      }
    }
    memory.Shutdown();
  }
  require(operations == 100512, operations, "data-cache comparison coverage");
  std::cout << operations << " actual data-cache operations and backing comparisons passed\n";
}

void data_cache_mmu_checks() {
  auto &system = Core::System::GetInstance();
  unsigned comparisons = 0;
  u32 random = 0x741c5ab3;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    Memory::MemoryManager memory(system);
    memory.Init();
    PowerPC::PowerPCManager manager(system);
    PowerPC::MMU mmu(system, memory, manager);
    auto &reference = manager.GetPPCState();
    reference.dCache.Init(memory);
    reference.m_enable_dcache = true;
    cupid::Memory bus(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    cupid::cpu::Interpreter product(bus, wii ? cupid::Console::Wii : cupid::Console::GameCube);
    product.set_data_cache_enabled(true);
    unsigned product_pi = 0;
    bus.memory_error = [&] { ++product_pi; };
    auto &state = product.state();
    const auto compare_backing = [&] {
      for (u32 bank : {0u, 0x10000000u}) {
        if (bank && !wii)
          continue;
        const auto expected = memory.GetSpanForAddress(bank | 0x100000).first(0x11000);
        const auto actual = bus.range(bank | 0x100000, 0x11000);
        require(std::equal(expected.begin(), expected.end(), actual.begin()), comparisons,
                "data-cache MMU backing visibility");
      }
    };
    for (unsigned operation = 0; operation < 12000; ++operation) {
      ++comparisons;
      reference_pi_requests = product_pi = 0;
      const unsigned kind = next() % 9, width = 1u << (next() % 3), mode = next() % 4;
      const u32 bank = wii && (next() & 1) ? 0x10000000 : 0;
      const u32 offset = next() & 0xffff;
      const u32 physical = bank | 0x100000 | offset;
      const u32 address =
          mode >= 2 ? 0x40000000 | offset : physical | (mode == 1 && !bank ? 0x04000000 : 0);
      reference.spr[SPR_HID0] = state.spr[cupid::cpu::spr::hid0] =
          0x4000 | ((next() & 3) == 0 ? 0x1000 : 0);
      reference.msr.Hex = state.msr = mode >= 2 ? cupid::cpu::msr::dr : 0;
      reference.spr[SPR_DBAT0U] = state.spr[536] = 0x40000002;
      reference.spr[SPR_DBAT0L] = state.spr[537] = bank | 0x100002 | (mode == 3 ? 0x40 : 0);
      mmu.DBATUpdated();
      state.gpr[3] = address;
      state.gpr[4] = next();
      state.pc = 0x1000;
      u32 opcode = 0;
      u32 expected_value = state.gpr[5];
      const auto indexed = [&](unsigned code, unsigned reg) {
        return (31u << 26) | (reg << 21) | (3u << 11) | (code << 1);
      };
      if (kind < 3) {
        expected_value = width == 1   ? mmu.Read<u8>(address)
                         : width == 2 ? mmu.Read<u16>(address)
                                      : mmu.Read<u32>(address);
        opcode = indexed(width == 1 ? 87 : width == 2 ? 279 : 23, 5);
      } else if (kind < 6) {
        if (width == 1)
          mmu.Write<u8>(state.gpr[4], address);
        else if (width == 2)
          mmu.Write<u16>(state.gpr[4], address);
        else
          mmu.Write<u32>(state.gpr[4], address);
        opcode = indexed(width == 1 ? 215 : width == 2 ? 407 : 151, 4);
      } else if (kind == 6) {
        mmu.StoreDCacheLine(address);
        opcode = indexed(54, 0);
      } else if (kind == 7) {
        mmu.FlushDCacheLine(address);
        opcode = indexed(86, 0);
      } else {
        mmu.InvalidateDCacheLine(address);
        opcode = indexed(470, 0);
      }
      bus.write32(0x1000, opcode);
      product.step();
      require(state.pc == 0x1004 && reference.Exceptions == 0, comparisons,
              "data-cache MMU instruction completion");
      require(state.gpr[5] == expected_value, comparisons, "data-cache MMU load result");
      require(product_pi == reference_pi_requests, comparisons, "data-cache MMU PI requests");
      if ((operation & 31) == 0)
        compare_backing();
    }
    reference.dCache.FlushAll(memory);
    product.set_data_cache_enabled(false);
    compare_backing();
    memory.Shutdown();
  }
  require(comparisons == 24000, comparisons, "data-cache MMU coverage");
  std::cout << comparisons << " actual MMU/data-cache and guest instruction comparisons passed\n";
}

int main() {
  try {
    data_cache_checks();
    data_cache_mmu_checks();
    memory_checks();
    fetch_backing_checks();
    cache_checks();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
