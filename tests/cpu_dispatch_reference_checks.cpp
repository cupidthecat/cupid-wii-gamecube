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
#include "Common/FPURoundMode.h"
#include "Common/FileUtil.h"
#include "Common/FloatUtils.h"
#include "Common/GekkoDisassembler.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/SymbolDB.h"
#include "Core/CPUThreadConfigCallback.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
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
#include "Core/HW/SystemTimers.h"
#include "Core/HW/VideoInterface.h"
#include "Core/HW/WII_IPC.h"
#include "Core/Host.h"
#include "Core/PowerPC/GDBStub.h"
#include "Core/PowerPC/Interpreter/Interpreter.h"
#include "Core/PowerPC/JitCommon/JitBase.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCTables.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/EFBInterface.h"
#include "VideoCommon/PixelEngine.h"
#include "cupid/core/memory.hpp"
#include "cupid/cpu/cache.hpp"
#include "cupid/cpu/interpreter.hpp"

// The actual interpreter step, metadata, exception checker, monitor, MMU and
// memory manager and full instruction table are linked unchanged. The cases
// dispatch selected authored integer and branch words; unused scheduler,
// debugger and device services fail explicitly.
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
CPUThreadGuard::CPUThreadGuard(Core::System &system) : m_system(system), m_was_cpu_thread(false) {
  unused();
}

CPUThreadGuard::~CPUThreadGuard() {
  if (m_was_cpu_thread || m_was_unpaused)
    std::terminate();
}

bool IsCPUThread() {
  unused();
}

bool IsRunning(Core::System &) {
  unused();
}

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

::Interpreter &System::GetInterpreter() const {
  unused();
}

SystemTimers::SystemTimersManager &System::GetSystemTimers() const {
  unused();
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
namespace FPU {
void SetSIMDMode(RoundMode, bool) {
  unused();
}
} // namespace FPU

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

namespace File {
const std::string &GetUserPath(unsigned int) {
  unused();
}

IOFile::IOFile(const std::string &, const char[], SharedAccess) : m_file(nullptr), m_good(false) {
  unused();
}

IOFile::~IOFile() {
  if (m_file)
    std::terminate();
}

bool IOFile::Open(const std::string &, const char[], SharedAccess) {
  unused();
}
} // namespace File

namespace CPUThreadConfigCallback {
ConfigChangedCallbackID AddConfigChangedCallback(Config::ConfigChangedCallback) {
  unused();
}

void RemoveConfigChangedCallback(ConfigChangedCallbackID) {
  unused();
}
} // namespace CPUThreadConfigCallback

namespace SystemTimers {
void SystemTimersManager::DecrementerSet() {
  unused();
}

void SystemTimersManager::TimeBaseSet() {
  unused();
}
} // namespace SystemTimers

void Host_UpdateDisasmDialog() {
  unused();
}

void Host_JitCacheInvalidation() {
  unused();
}

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

namespace Config {
const Info<bool> MAIN_DISABLE_ICACHE{{}, false};
const Info<bool> MAIN_ACCURATE_CPU_CACHE{{}, false};

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

void JitInterface::DoState(PointerWrap &) {
  unused();
}

CPUCoreBase *JitInterface::InitJitCore(PowerPC::CPUCore) {
  unused();
}

CPUCoreBase *JitInterface::GetCore() const {
  unused();
}

void JitInterface::Shutdown() {
  unused();
}

void require(bool condition, unsigned case_number, const char *field) {
  if (!condition)
    throw std::runtime_error("Step case " + std::to_string(case_number) + ": " + field);
}

namespace {
PowerPC::PowerPCManager *active_manager = nullptr;
unsigned hook_checks = 0;

} // namespace

namespace Core {
PowerPC::PowerPCManager &System::GetPowerPC() const {
  if (!active_manager)
    unused();
  return *active_manager;
}

CoreTiming::CoreTimingManager &System::GetCoreTiming() const {
  unused();
}
} // namespace Core

namespace CoreTiming {
EventType *CoreTimingManager::RegisterEvent(const std::string &, TimedCallback) {
  unused();
}

void CoreTimingManager::ScheduleEvent(s64, EventType *, u64, FromThread) {
  unused();
}

void CoreTimingManager::Advance() {
  unused();
}
} // namespace CoreTiming

namespace Config {
bool IsDebuggingEnabled() {
  unused();
}
} // namespace Config

namespace CPU {
State CPUManager::GetState() const {
  unused();
}
} // namespace CPU

namespace HLE {
TryReplaceFunctionResult TryReplaceFunction(PPCSymbolDB &, u32, PowerPC::CoreMode) {
  ++hook_checks;
  return {};
}
} // namespace HLE

namespace Common {
std::string GekkoDisassembler::Disassemble(u32, u32, bool) {
  unused();
}
} // namespace Common

void JitInterface::UpdateMembase() {
  if (m_jit)
    unused();
}

const TBreakPoint *BreakPoints::GetBreakpoint(u32) const {
  unused();
}

double Expression::Evaluate(Core::System &) const {
  unused();
}

std::string PPCSymbolDB::GetDescription(u32) const {
  unused();
}

namespace HLE {
void Execute(const Core::CPUThreadGuard &, u32, u32) {
  unused();
}
} // namespace HLE

namespace GPFifo {
bool GPFifoManager::IsBNE() const {
  unused();
}

void GPFifoManager::ResetGatherPipe() {
  unused();
}

void GPFifoManager::CheckGatherPipe() {
  unused();
}
} // namespace GPFifo

namespace SystemTimers {
u32 SystemTimersManager::GetFakeDecrementer() const {
  unused();
}

u64 SystemTimersManager::GetFakeTimeBase() const {
  unused();
}
} // namespace SystemTimers

struct ReadEvent {
  u32 physical;
  std::size_t count;
  bool operator==(const ReadEvent &) const = default;
};

std::vector<ReadEvent> source_reads;
std::vector<ReadEvent> target_reads;
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
extern "C" void real_copy(const Memory::MemoryManager *, void *, u32,
                          std::size_t) asm("__real__ZNK6Memory13MemoryManager11CopyFromEmuEPvjm");
extern "C" void
observed_copy(const Memory::MemoryManager *, void *, u32,
              std::size_t) asm("__wrap__ZNK6Memory13MemoryManager11CopyFromEmuEPvjm");

extern "C" void observed_copy(const Memory::MemoryManager *memory, void *output, u32 address,
                              std::size_t count) {
  source_reads.push_back({address, count});
  real_copy(memory, output, address, count);
}

extern "C" u32 real_word(const Memory::MemoryManager *,
                         u32) asm("__real__ZNK6Memory13MemoryManager8Read_U32Ej");
extern "C" u32 observed_word(const Memory::MemoryManager *,
                             u32) asm("__wrap__ZNK6Memory13MemoryManager8Read_U32Ej");

extern "C" u32 observed_word(const Memory::MemoryManager *memory, u32 address) {
  source_reads.push_back({address, 4});
  return real_word(memory, address);
}
#endif

std::vector<ReadEvent> expected_transfers(u32 physical, bool enabled, bool locked,
                                          unsigned warm = 0) {
  if (!enabled)
    return {{physical, 4}};
  std::vector<ReadEvent> result;
  unsigned copied = 0;
  while (copied < 4) {
    const u32 cursor = physical + copied;
    const unsigned count = std::min(4u - copied, 32u - (cursor & 31u));
    const unsigned line = ((physical & 31u) + copied) / 32u;
    if (!(warm & (1u << line)))
      result.push_back(locked ? ReadEvent{cursor, count} : ReadEvent{cursor & ~31u, 32});
    copied += count;
  }
  return result;
}

void require_transfers(const std::vector<ReadEvent> &expected, unsigned case_number,
                       const char *field) {
  require(target_reads == expected, case_number, field);
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
  require(source_reads == target_reads, case_number, "actual ordered backing copies");
#endif
}

class BackingBus final : public cupid::Bus {
public:
  explicit BackingBus(cupid::Memory &memory) : memory_(memory) {
  }

  cupid::u8 read8(cupid::u32 address) override {
    return memory_.read8(address);
  }

  void write8(cupid::u32 address, cupid::u8 value) override {
    memory_.write8(address, value);
  }

  cupid::u32 instruction_cache_key(cupid::u32 physical) const override {
    return memory_.instruction_cache_key(physical);
  }

  bool read_instruction_words(cupid::u32 physical, std::span<cupid::u32> words) override {
    target_reads.push_back({physical, words.size_bytes()});
    return memory_.read_instruction_words(physical, words);
  }

  bool read_instruction_bytes(cupid::u32 physical, std::span<cupid::u8> bytes) override {
    target_reads.push_back({physical, bytes.size()});
    return memory_.read_instruction_bytes(physical, bytes);
  }

private:
  cupid::Memory &memory_;
};

unsigned byte_backing_cases(Core::System &system) {
  unsigned cases = 0;
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
    auto &memory = *memory_owner;
    memory.Init();
    cupid::Memory product(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    unsigned pi = 0, devices = 0;
    product.memory_error = [&] { ++pi; };
    product.mmio_handles = [&](u32) -> bool {
      ++devices;
      unused();
    };
    product.mmio_read = [&](u32, unsigned) -> u32 {
      ++devices;
      unused();
    };
    product.mmio_write = [&](u32, u32, unsigned) {
      ++devices;
      unused();
    };
    product.fifo_write = [&](u8) {
      ++devices;
      unused();
    };
    std::array<u8, 128> pattern{};
    for (unsigned n = 0; n < pattern.size(); ++n)
      pattern[n] = static_cast<u8>(n * 17u + 5u);
    for (u32 base : {0u, 0x017fff80u}) {
      memory.CopyToEmu(base, pattern.data(), pattern.size());
      product.copy_in(base, pattern);
    }
    if (wii) {
      for (u32 base : {0x10000000u, 0x13ffff80u}) {
        memory.CopyToEmu(base, pattern.data(), pattern.size());
        product.copy_in(base, pattern);
      }
    }
    for (u32 address :
         {0u,          1u,          31u,         0x017fffe0u, 0x017ffff0u, 0x017fffffu,
          0x01800000u, 0x01ffffffu, 0x02000000u, 0x07ffffffu, 0x08000000u, 0x0c008000u,
          0x10000000u, 0x13ffffe0u, 0x13ffffffu, 0x14000000u, 0xe0000000u, 0x80000000u,
          0xc000001fu, 0x40000000u, 0xfffffffeu})
      for (unsigned width = 1; width <= 32; ++width) {
        ++cases;
        std::array<u8, 64> expected, obtained;
        expected.fill(0x5a);
        obtained.fill(0x5a);
        const auto before_messages = reference_messages.size();
        const auto before_pi = reference_pi_requests;
        const auto before_errors = product.instruction_backing_read_errors();
        memory.CopyFromEmu(expected.data() + 16, address, width);
        const bool success =
            product.read_instruction_bytes(address, std::span(obtained).subspan(16, width));
        require(expected == obtained, cases, "byte backing output and whole guard retention");
        require(reference_messages.size() == before_messages + (success ? 0u : 2u), cases,
                "actual byte backing diagnostic");
        require(product.instruction_backing_read_errors() == before_errors + (success ? 0u : 1u),
                cases, "product byte backing diagnostic");
        require(reference_pi_requests == before_pi && pi == 0 && devices == 0, cases,
                "byte backing service exclusion");
      }
    auto manager = std::make_unique<PowerPC::PowerPCManager>(system);
    active_manager = manager.get();
    auto &state = manager->GetPPCState();
    state.iCache.Cache::Init(memory);
    BackingBus bus(product);
    cupid::cpu::InstructionCache cache;
    unsigned cache_cases = 0;
    for (bool locked : {false, true})
      for (unsigned warm = 0; warm < 4; ++warm)
        for (unsigned offset = 0; offset < 32; ++offset) {
          ++cache_cases;
          state.iCache.Reset(system.GetJitInterface());
          cache.reset();
          state.spr[SPR_HID0] = 0x8000;
          std::array<u8, 64> old_bytes, new_bytes;
          for (unsigned n = 0; n < old_bytes.size(); ++n) {
            old_bytes[n] = static_cast<u8>(n * 3u + 7u);
            new_bytes[n] = static_cast<u8>(n * 5u + 11u);
          }
          memory.CopyToEmu(0x2000, old_bytes.data(), old_bytes.size());
          product.copy_in(0x2000, old_bytes);
          for (unsigned line = 0; line < 2; ++line)
            if (warm & (1u << line)) {
              state.iCache.ReadInstruction(memory, state, 0x2000u + line * 32u);
              cache.read(bus, 0x2000u + line * 32u, true, false);
            }
          memory.CopyToEmu(0x2000, new_bytes.data(), new_bytes.size());
          product.copy_in(0x2000, new_bytes);
          state.spr[SPR_HID0] = 0x8000u | (locked ? 0x2000u : 0u);
          source_reads.clear();
          target_reads.clear();
          const u32 expected = state.iCache.ReadInstruction(memory, state, 0x2000u + offset);
          const u32 obtained = cache.read(bus, 0x2000u + offset, true, locked);
          u32 authored = 0;
          for (unsigned n = 0; n < 4; ++n) {
            const unsigned index = offset + n;
            authored = (authored << 8) |
                       ((warm & (1u << (index / 32u))) ? old_bytes[index] : new_bytes[index]);
          }
          require(expected == authored && obtained == expected, cache_cases,
                  "warm/cold byte assembly");
          require_transfers(expected_transfers(0x2000u + offset, true, locked, warm), cache_cases,
                            "ordered per-line backing transfers");
          require(pi == 0 && devices == 0, cache_cases, "cache byte service exclusion");
        }
    unsigned retained_cases = 0;
    for (u32 rejected_base : {0x01800000u, 0x08000000u, 0xe0000000u})
      for (unsigned offset = 0; offset < 32; ++offset) {
        ++retained_cases;
        state.iCache.Reset(system.GetJitInterface());
        cache.reset();
        state.spr[SPR_HID0] = 0x8000;
        std::array<u8, 64> retained;
        for (unsigned n = 0; n < retained.size(); ++n)
          retained[n] = static_cast<u8>(n * 13u + 7u);
        memory.CopyToEmu(0x2000, retained.data(), retained.size());
        product.copy_in(0x2000, retained);
        for (u32 address : {0x2000u, 0x2020u}) {
          state.iCache.ReadInstruction(memory, state, address);
          cache.read(bus, address, true, false);
        }
        state.iCache.Reset(system.GetJitInterface());
        cache.reset();
        source_reads.clear();
        target_reads.clear();
        u32 authored = 0;
        for (unsigned n = 0; n < 4; ++n)
          authored = (authored << 8) | retained[offset + n];
        require(state.iCache.ReadInstruction(memory, state, rejected_base + offset) == authored &&
                    cache.read(bus, rejected_base + offset, true, false) == authored,
                retained_cases, "rejected fills preserve bounded initialized line words");
        require_transfers(expected_transfers(rejected_base + offset, true, false), retained_cases,
                          "rejected fill transfer order");
        source_reads.clear();
        target_reads.clear();
        require(state.iCache.ReadInstruction(memory, state, rejected_base + offset) == authored &&
                    cache.read(bus, rejected_base + offset, true, false) == authored,
                retained_cases, "rejected fill installs retained tag");
        require(source_reads.empty() && target_reads.empty(), retained_cases,
                "retained tag hit has no backing copy");
        require(pi == 0 && devices == 0, retained_cases, "rejected fill service exclusion");
      }
    std::cout << retained_cases << " actual/product retained rejected cache fill cases\n";
    // Fallback reads never populate a missing line. Updating backing remains visible.
    state.iCache.Reset(system.GetJitInterface());
    cache.reset();
    state.spr[SPR_HID0] = 0xa000;
    for (u32 value : {0x12345678u, 0xabcdef01u}) {
      memory.Write_U32(value, 0x201f);
      const std::array<u8, 4> encoded{static_cast<u8>(value >> 24), static_cast<u8>(value >> 16),
                                      static_cast<u8>(value >> 8), static_cast<u8>(value)};
      product.copy_in(0x201f, encoded);
      require(state.iCache.ReadInstruction(memory, state, 0x201f) == value &&
                  cache.read(bus, 0x201f, true, true) == value,
              cases, "locked split fallback retains missing tags");
    }
    // Conflicting tags and changed backing distinguish a retained hit from an
    // allocation. Many tags share the host hint slot as well as the guest set.
    state.iCache.Reset(system.GetJitInterface());
    cache.reset();
    u32 sequence = 0x5713a26du;
    constexpr unsigned sequence_cases = 65536;
    for (unsigned n = 0; n < sequence_cases; ++n) {
      sequence ^= sequence << 13;
      sequence ^= sequence >> 17;
      sequence ^= sequence << 5;
      const u32 region = wii && (sequence & 0x80000000u) ? 0x10000000u : 0u;
      const u32 line = 0x2000u + ((sequence >> 7) & 127u) * 0x1000u;
      const u32 physical = region + line + (sequence & 31u);
      const u32 alias = (sequence >> 29) & 3u;
      const u32 address = physical | (alias << 30);
      std::array<u8, 64> bytes{};
      for (unsigned byte = 0; byte < bytes.size(); ++byte)
        bytes[byte] = static_cast<u8>((sequence >> (byte & 15u)) + n + byte * 7u);
      memory.CopyToEmu(region + line, bytes.data(), bytes.size());
      product.copy_in(region + line, bytes);
      if (n % 251u == 0) {
        state.iCache.Invalidate(memory, system.GetJitInterface(), address);
        cache.invalidate(address);
      }
      if (n % 1021u == 0) {
        state.iCache.Reset(system.GetJitInterface());
        cache.reset();
      }
      const bool enabled = n % 17u != 0, locked = n % 5u == 0;
      state.spr[SPR_HID0] = (enabled ? 0x8000u : 0u) | (locked ? 0x2000u : 0u);
      source_reads.clear();
      target_reads.clear();
      const u32 expected = state.iCache.ReadInstruction(memory, state, address);
      require(cache.read(bus, address, enabled, locked) == expected, n,
              "cache conflict sequence value and replacement visibility");
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
      require(source_reads == target_reads, n, "cache conflict sequence ordered backing copies");
#endif
      require(pi == 0 && devices == 0, n, "cache conflict sequence service exclusion");
      for (unsigned repeat = 0; repeat < 2; ++repeat) {
        source_reads.clear();
        target_reads.clear();
        const u32 repeated = address + repeat * 4u;
        require(cache.read(bus, repeated, enabled, locked) ==
                    state.iCache.ReadInstruction(memory, state, repeated),
                n, "cache conflict sequence repeated line hit");
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
        require(source_reads == target_reads, n, "repeated line hit backing copies");
#endif
      }
    }
    std::cout << sequence_cases * 3u << " actual/product cache conflict sequence reads\n";
    std::cout << cache_cases << " actual/product warm/cold cache byte cases\n";
    memory.Shutdown();
    active_manager = nullptr;
  }
  return cases;
}

unsigned integer_dispatch_cases(Core::System &system, bool carry_only, bool rotate_only = false) {
  unsigned cases = 0, divergences = 0;
  std::vector<u32> program;
  const auto ext = [](unsigned xo, unsigned d, unsigned a, unsigned b, bool rc, bool oe = false) {
    return (31u << 26) | (d << 21) | (a << 16) | (b << 11) | (xo << 1) |
           (static_cast<u32>(oe) << 10) | static_cast<u32>(rc);
  };
  for (bool rc : {false, true}) {
    for (bool oe : {false, true})
      for (unsigned xo :
           {8u, 10u, 40u, 104u, 136u, 138u, 200u, 202u, 232u, 234u, 235u, 266u, 459u, 491u})
        program.push_back(ext(xo, 3, 4, 5, rc, oe));
    for (unsigned xo :
         {11u, 24u, 26u, 28u, 60u, 75u, 124u, 284u, 316u, 412u, 444u, 476u, 536u, 792u, 922u, 954u})
      program.push_back(ext(xo, 3, 4, 5, rc));
    for (unsigned shift = 0; shift < 32; ++shift)
      program.push_back(ext(824, 3, 4, shift, rc));
    for (unsigned op : {20u, 21u, 23u})
      for (unsigned n = 0; n < 64; ++n)
        program.push_back((op << 26) | (3u << 21) | (4u << 16) | ((n & 31u) << 11) |
                          (((n * 7u) & 31u) << 6) | (((n * 13u) & 31u) << 1) |
                          static_cast<u32>(rc));
  }
  for (unsigned op : {12u, 13u, 14u, 24u, 25u, 26u, 27u, 28u, 29u})
    for (u32 immediate : {0u, 1u, 0x7fffu, 0x8000u, 0xffffu})
      program.push_back((op << 26) | (3u << 21) | (4u << 16) | immediate);
  for (unsigned field = 0; field < 8; ++field) {
    for (unsigned op : {10u, 11u})
      for (u32 immediate : {0u, 1u, 0x8000u, 0xffffu})
        program.push_back((op << 26) | (field << 23) | (4u << 16) | immediate);
    for (unsigned xo : {0u, 32u})
      program.push_back(ext(xo, field << 2, 4, 5, false));
  }
  for (unsigned bo = 0; bo < 32; ++bo)
    for (unsigned bi : {0u, 2u, 17u, 31u})
      for (bool link : {false, true}) {
        for (bool absolute : {false, true})
          program.push_back((16u << 26) | (bo << 21) | (bi << 16) | 0xfff8u |
                            (static_cast<u32>(absolute) << 1) | static_cast<u32>(link));
        program.push_back((19u << 26) | (bo << 21) | (bi << 16) | (16u << 1) |
                          static_cast<u32>(link));
        if (bo & 4u)
          program.push_back((19u << 26) | (bo << 21) | (bi << 16) | (528u << 1) |
                            static_cast<u32>(link));
      }
  if (carry_only) {
    program.clear();
    for (unsigned xo : {232u, 234u})
      for (bool oe : {false, true})
        for (bool rc : {false, true})
          program.push_back(ext(xo, 3, 4, 0, rc, oe));
  }
  if (rotate_only) {
    program.clear();
    for (unsigned op : {20u, 21u, 23u})
      for (unsigned begin = 0; begin < 32; ++begin)
        for (unsigned end = 0; end < 32; ++end)
          for (unsigned shift = 0; shift < 32; ++shift)
            for (bool rc : {false, true})
              program.push_back((op << 26) | (3u << 21) | (4u << 16) | (shift << 11) |
                                (begin << 6) | (end << 1) | static_cast<u32>(rc));
  }
  constexpr std::array<u32, 4> rotate_values{0u, 0xffffffffu, 0x12345663u, 0x87654324u};
  constexpr std::array<u32, 8> carry_values{0u,          1u,          2u,          0x7fffffffu,
                                            0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu};
  constexpr std::array<u32, 16> values{0u,          1u,          2u,          31u,
                                       32u,         33u,         63u,         64u,
                                       0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu,
                                       0xfffffffeu, 0x01020304u, 0xaaaaaaaau, 0x55555555u};
  for (bool wii : {false, true}) {
    system.SetIsWii(wii);
    auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
    auto &memory = *memory_owner;
    memory.Init();
    auto manager = std::make_unique<PowerPC::PowerPCManager>(system);
    active_manager = manager.get();
    auto mmu = std::make_unique<PowerPC::MMU>(system, memory, *manager);
    mmu->IBATUpdated();
    mmu->DBATUpdated();
    auto &state = manager->GetPPCState();
    state.iCache.Cache::Init(memory);
    ::Interpreter cpu(system, state, *mmu, manager->GetBranchWatch(), manager->GetSymbolDB());
    cpu.Init();
    cupid::Memory product_memory(wii ? cupid::Console::Wii : cupid::Console::GameCube);
    BackingBus product_bus(product_memory);
    cupid::cpu::Interpreter product_cpu(product_bus,
                                        wii ? cupid::Console::Wii : cupid::Console::GameCube);
    std::vector<u32> bases{0x2000u, 0x201cu, 0x017fffc0u, 0x80002000u, 0xc0002000u, 0x40002000u};
    if (wii) {
      bases.push_back(0x10002000u);
      bases.push_back(0x13ffffc0u);
    }
    for (bool locked : {false})
      for (bool enabled : {false})
        for (u32 base : {0x2000u})
          for (unsigned offset : {0u})
            for (unsigned input = 0; input < (rotate_only ? 4u : 64u); ++input)
              for (u32 opcode : program) {
                ++cases;
                const u32 address = base + offset;
                const std::array<u8, 64> empty{};
                memory.CopyToEmu(address & ~31u, empty.data(), empty.size());
                memory.Write_U32(opcode, address);
                // Instruction caching is disabled throughout these private arithmetic controls.
                state.msr.Hex = 0;
                state.spr[SPR_HID0] = (enabled ? 0x8000u : 0u) | (locked ? 0x2000u : 0u);
                state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
                state.pc = address;
                state.npc = 0x12345678;
                state.Exceptions = 0;
                for (unsigned n = 0; n < 32; ++n)
                  state.gpr[n] = values[(n + input) & 15u];
                state.gpr[4] = carry_only ? carry_values[input & 7u] : values[input & 15u];
                state.gpr[5] = values[(input * 7u + (input >> 4)) & 15u];
                if (rotate_only) {
                  // The register-indexed opcode reads all 32 count registers,
                  // including source/count and destination/count aliases.
                  for (unsigned n = 0; n < 32; ++n)
                    state.gpr[n] = (rotate_values[(n + input) & 3u] & ~31u) | n;
                  state.gpr[3] = rotate_values[input];
                  state.gpr[4] = rotate_values[(input + 1u) & 3u];
                }
                state.cr.Set(0xa1b2c3d4u ^ (input * 0x11111111u));
                const u32 flags = rotate_only ? ((input & 3u) << 30) | ((input & 1u) << 29)
                                  : carry_only
                                      ? (((input >> 3) & 1u) << 29) | (((input >> 4) & 3u) << 30)
                                      : (input & 7u) << 29;
                state.SetXER(UReg_XER{flags | 0x0000ff7fu});
                state.spr[SPR_LR] = 0x12345678;
                state.spr[SPR_CTR] = values[(input + 3u) & 15u];
                state.spr[SPR_SRR0] = 0x12345678;
                state.spr[SPR_SRR1] = 0x56789abc;
                std::ranges::fill(product_memory.range(address & ~31u, empty.size()), u8{0});
                product_memory.write32(address, opcode);
                product_cpu.reset();
                auto &product = product_cpu.state();
                product.pc = address;
                product.spr[cupid::cpu::spr::hid0] = state.spr[SPR_HID0];
                for (unsigned n = 0; n < 32; ++n)
                  product.gpr[n] = state.gpr[n];
                product.cr = state.cr.Get();
                product.spr[cupid::cpu::spr::xer] = state.GetXER().Hex;
                product.spr[cupid::cpu::spr::lr] = state.spr[SPR_LR];
                product.spr[cupid::cpu::spr::ctr] = state.spr[SPR_CTR];
                product.spr[cupid::cpu::spr::srr0] = state.spr[SPR_SRR0];
                product.spr[cupid::cpu::spr::srr1] = state.spr[SPR_SRR1];
                const unsigned before_hooks = hook_checks, before_pi = reference_pi_requests;
                source_reads.clear();
                target_reads.clear();
                const int cycles = cpu.SingleStepInner();
                const u32 product_cycles = product_cpu.step();
                require_transfers(expected_transfers(address, enabled, locked), cases,
                                  "CPU ordered backing transfers");
                bool equal = product_cycles == static_cast<u32>(cycles) && product.pc == state.pc &&
                             product.msr == state.msr.Hex && product.cr == state.cr.Get() &&
                             product.spr[cupid::cpu::spr::xer] == state.GetXER().Hex &&
                             product.spr[cupid::cpu::spr::lr] == state.spr[SPR_LR] &&
                             product.spr[cupid::cpu::spr::ctr] == state.spr[SPR_CTR] &&
                             product.spr[cupid::cpu::spr::srr0] == state.spr[SPR_SRR0] &&
                             product.spr[cupid::cpu::spr::srr1] == state.spr[SPR_SRR1];
                for (unsigned n = 0; n < 32; ++n)
                  equal = equal && product.gpr[n] == state.gpr[n];
                if (!equal) {
                  ++divergences;
                  std::cerr << "First integer divergence case " << cases << " input " << input
                            << " opcode " << std::hex << opcode << " CR " << state.cr.Get() << "/"
                            << product.cr << " XER " << state.GetXER().Hex << "/"
                            << product.spr[cupid::cpu::spr::xer] << " PC " << state.pc << "/"
                            << product.pc << " cycles " << std::dec << cycles << "/"
                            << product_cycles << "\n";
                  throw std::runtime_error("Integer/branch guest-state divergence");
                }
                require(state.Exceptions == 0 && state.pc == state.npc, cases,
                        "actual selected table dispatch guest state");
                require(hook_checks == before_hooks + 1 && reference_pi_requests == before_pi,
                        cases, "actual metadata and excluded services");
              }
    memory.Shutdown();
    active_manager = nullptr;
  }
  std::cout << cases
            << (rotate_only  ? " actual-table exhaustive rotate cases; "
                : carry_only ? " actual-table minus-one carry cases; "
                             : " actual-table integer/branch cases; ")
            << divergences << " divergences; scheduler, device and other dispatch excluded\n";
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
  std::cout << "Unchanged source backing transfers observed through forwarding link wrappers\n";
#else
  std::cout
      << "Product transfer order checked; source call interception unavailable on this target\n";
#endif
  require(cases == (rotate_only  ? 1572864u
                    : carry_only ? 1024u
                                 : 199296u),
          cases, "integer comparison case count");
  return cases;
}

int main() {
  try {
    auto &system = Core::System::GetInstance();
    const unsigned byte_cases = byte_backing_cases(system);
    std::cout << byte_cases << " actual/product strict byte backing cases\n";
    unsigned cases = 0, divergences = 0;
    constexpr std::array<u32, 8> program{0u,
                                         0x60000000u,
                                         (14u << 26) | (3u << 21) | 7u,
                                         (14u << 26) | (3u << 21) | (3u << 16) | 0xfffbu,
                                         (15u << 26) | (3u << 21) | (3u << 16) | 1u,
                                         (27u << 26) | (3u << 21) | (3u << 16) | 0xa5a5u,
                                         0x48000008u,
                                         0x48000009u};
    for (bool wii : {false, true}) {
      system.SetIsWii(wii);
      auto memory_owner = std::make_unique<Memory::MemoryManager>(system);
      auto &memory = *memory_owner;
      memory.Init();
      auto manager = std::make_unique<PowerPC::PowerPCManager>(system);
      active_manager = manager.get();
      auto mmu = std::make_unique<PowerPC::MMU>(system, memory, *manager);
      mmu->IBATUpdated();
      mmu->DBATUpdated();
      auto &state = manager->GetPPCState();
      state.iCache.Cache::Init(memory);
      ::Interpreter cpu(system, state, *mmu, manager->GetBranchWatch(), manager->GetSymbolDB());
      cpu.Init();
      cupid::Memory product_memory(wii ? cupid::Console::Wii : cupid::Console::GameCube);
      BackingBus product_bus(product_memory);
      cupid::cpu::Interpreter product_cpu(product_bus,
                                          wii ? cupid::Console::Wii : cupid::Console::GameCube);
      std::vector<u32> bases{0x2000u, 0x201cu, 0x017fffc0u, 0x80002000u, 0xc0002000u, 0x40002000u};
      if (wii) {
        bases.push_back(0x10002000u);
        bases.push_back(0x13ffffc0u);
      }
      for (bool locked : {false, true})
        for (bool enabled : {false, true})
          for (u32 base : bases)
            for (unsigned offset : {0u, 1u, 2u, 3u, 28u, 29u, 30u, 31u})
              for (u32 opcode : program) {
                ++cases;
                const u32 address = base + offset;
                const std::array<u8, 64> empty{};
                memory.CopyToEmu(address & ~31u, empty.data(), empty.size());
                memory.Write_U32(opcode, address);
                state.iCache.Reset(system.GetJitInterface());
                state.msr.Hex = 0;
                state.spr[SPR_HID0] = (enabled ? 0x8000u : 0u) | (locked ? 0x2000u : 0u);
                state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
                state.pc = address;
                state.npc = 0x12345678;
                state.Exceptions = 0;
                for (unsigned n = 0; n < 32; ++n)
                  state.gpr[n] = 0x12340000u + n * 0x101u;
                state.cr.Set(0xa1b2c3d4u);
                state.SetXER(UReg_XER{0xe0000003u});
                state.spr[SPR_LR] = 0x12345678;
                state.spr[SPR_CTR] = 0x23456789;
                state.spr[SPR_SRR0] = 0x12345678;
                state.spr[SPR_SRR1] = 0x56789abc;
                std::ranges::fill(product_memory.range(address & ~31u, empty.size()), u8{0});
                product_memory.write32(address, opcode);
                product_cpu.reset();
                auto &product = product_cpu.state();
                product.pc = address;
                product.spr[cupid::cpu::spr::hid0] = state.spr[SPR_HID0];
                for (unsigned n = 0; n < 32; ++n)
                  product.gpr[n] = state.gpr[n];
                product.cr = state.cr.Get();
                product.spr[cupid::cpu::spr::xer] = state.GetXER().Hex;
                product.spr[cupid::cpu::spr::lr] = state.spr[SPR_LR];
                product.spr[cupid::cpu::spr::ctr] = state.spr[SPR_CTR];
                product.spr[cupid::cpu::spr::srr0] = state.spr[SPR_SRR0];
                product.spr[cupid::cpu::spr::srr1] = state.spr[SPR_SRR1];
                const unsigned before_hooks = hook_checks, before_pi = reference_pi_requests;
                source_reads.clear();
                target_reads.clear();
                const int cycles = cpu.SingleStepInner();
                const u32 product_cycles = product_cpu.step();
                require_transfers(expected_transfers(address, enabled, locked), cases,
                                  "CPU ordered backing transfers");
                bool equal = product_cycles == static_cast<u32>(cycles) && product.pc == state.pc &&
                             product.msr == state.msr.Hex && product.cr == state.cr.Get() &&
                             product.spr[cupid::cpu::spr::xer] == state.GetXER().Hex &&
                             product.spr[cupid::cpu::spr::lr] == state.spr[SPR_LR] &&
                             product.spr[cupid::cpu::spr::ctr] == state.spr[SPR_CTR] &&
                             product.spr[cupid::cpu::spr::srr0] == state.spr[SPR_SRR0] &&
                             product.spr[cupid::cpu::spr::srr1] == state.spr[SPR_SRR1];
                for (unsigned n = 0; n < 32; ++n)
                  equal = equal && product.gpr[n] == state.gpr[n];
                if (!equal)
                  ++divergences;
                require(state.Exceptions == 0 && state.pc == state.npc, cases,
                        "actual selected table dispatch guest state");
                require(cycles == (opcode == 0 ? 0 : 1) && hook_checks == before_hooks + 1 &&
                            reference_pi_requests == before_pi,
                        cases, "actual metadata and excluded services");
              }
      memory.Shutdown();
      active_manager = nullptr;
    }
    std::cout << cases << " actual-table selected aligned/odd-PC cases; " << divergences
              << " divergences; scheduler, device and other dispatch excluded\n";
#if defined(CUPID_OBSERVE_REFERENCE_COPY)
    std::cout << "Unchanged source backing transfers observed through forwarding link wrappers\n";
#else
    std::cout
        << "Product transfer order checked; source call interception unavailable on this target\n";
#endif
    integer_dispatch_cases(system, true);
    integer_dispatch_cases(system, false);
    integer_dispatch_cases(system, false, true);
    return divergences != 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
