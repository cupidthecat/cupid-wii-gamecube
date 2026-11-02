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
// memory manager are linked unchanged. No host hooks or nonzero dispatch are
// enabled; unused scheduler, debugger and device services fail explicitly.
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
u32 ClassifyDouble(double) {
  unused();
}

u32 ClassifyFloat(float) {
  unused();
}

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

namespace PowerPC {
const std::array<u64, 16> ConditionRegister::s_crTable = [] {
  std::array<u64, 16> values{};
  for (u8 field = 0; field < values.size(); ++field)
    values[field] = ConditionRegister::PPCToInternal(field);
  return values;
}();

} // namespace PowerPC

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

void Interpreter::HLEFunction(Interpreter &, UGeckoInstruction) {
  unused();
}

void Interpreter::RunInterpreterOp(Interpreter &, UGeckoInstruction) {
  unused();
}

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

unsigned translated_fault_cases(Core::System &system) {
  unsigned cases = 0, cycle_divergences = 0;
  // Large cache and BAT arrays use normal heap lifetimes rather than nested
  // native stack frames. The actual service constructors remain unchanged.
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
    state.spr[SPR_SDR] = 0;
    mmu->SDRUpdated();
    for (unsigned monitor_mode : {0u, 1u, 11u})
      for (bool enabled : {false, true})
        for (u32 mode : {0u, cupid::cpu::msr::pr, cupid::cpu::msr::fp, cupid::cpu::msr::ee,
                         cupid::cpu::msr::dr, cupid::cpu::msr::ile})
          for (bool no_execute : {false, true})
            for (u32 address : {0x80342000u, 0x9efff000u}) {
              ++cases;
              state.iCache.Reset(system.GetJitInterface());
              state.msr.Hex = cupid::cpu::msr::ir | cupid::cpu::msr::ri | mode;
              state.sr[address >> 28] = no_execute ? 0x10000000u : 0u;
              state.spr[SPR_HID0] = enabled ? 0x8000u : 0u;
              state.pc = address;
              state.npc = 0x12345678;
              state.Exceptions = 0;
              state.gpr[3] = 0xabcdef01;
              state.spr[SPR_SRR0] = 0x12345678;
              state.spr[SPR_SRR1] = 0x56789abc;
              state.spr[SPR_DAR] = 0xabcdef01;
              state.spr[SPR_DSISR] = 0x12345678;
              state.spr[SPR_MMCR0] = monitor_mode == 0 ? 0u : (1u << 6) | monitor_mode;
              state.spr[SPR_MMCR1] = monitor_mode == 0 ? 0u : (monitor_mode << 27) | (1u << 22);
              const std::array<u32, 4> counters{SPR_PMC1, SPR_PMC2, SPR_PMC3, SPR_PMC4};
              for (unsigned n = 0; n < counters.size(); ++n)
                state.spr[counters[n]] = 0x12345678u + n;
              cupid::Memory product_memory(wii ? cupid::Console::Wii : cupid::Console::GameCube);
              cupid::cpu::Interpreter product_cpu(product_memory, wii ? cupid::Console::Wii
                                                                      : cupid::Console::GameCube);
              auto &product = product_cpu.state();
              unsigned product_pi = 0;
              product_memory.memory_error = [&] { ++product_pi; };
              product.pc = address;
              product.msr = state.msr.Hex;
              product.sr[address >> 28] = state.sr[address >> 28];
              product.gpr[3] = state.gpr[3];
              product.spr[cupid::cpu::spr::hid0] = state.spr[SPR_HID0];
              product.spr[cupid::cpu::spr::dar] = state.spr[SPR_DAR];
              product.spr[cupid::cpu::spr::dsisr] = state.spr[SPR_DSISR];
              product.spr[cupid::cpu::spr::srr0] = state.spr[SPR_SRR0];
              product.spr[cupid::cpu::spr::srr1] = state.spr[SPR_SRR1];
              product.spr[cupid::cpu::spr::mmcr0] = state.spr[SPR_MMCR0];
              product.spr[cupid::cpu::spr::mmcr1] = state.spr[SPR_MMCR1];
              product.cycles = 11;
              product.timebase = 0x12345678;
              for (unsigned n = 0; n < counters.size(); ++n)
                product.spr[counters[n]] = state.spr[counters[n]];
              const unsigned before_hooks = hook_checks, before_pi = reference_pi_requests;
              const int cycles = cpu.SingleStepInner();
              const u32 product_cycles = product_cpu.step();
              require(product.pc == state.pc && product.msr == state.msr.Hex &&
                          product.gpr[3] == state.gpr[3] &&
                          product.spr[cupid::cpu::spr::srr0] == state.spr[SPR_SRR0] &&
                          product.spr[cupid::cpu::spr::srr1] == state.spr[SPR_SRR1] &&
                          product.spr[cupid::cpu::spr::dar] == state.spr[SPR_DAR] &&
                          product.spr[cupid::cpu::spr::dsisr] == state.spr[SPR_DSISR],
                      cases, "actual/product ISI guest state");
              require(state.pc == 0x400 && state.npc == 0x400 && state.spr[SPR_SRR0] == address,
                      cases, "actual ISI vector and saved instruction address");
              require(cycles == 0 && state.Exceptions == 0, cases,
                      "actual ISI zero cycle metadata");
              require(product.instructions == 0 && product_pi == 0 &&
                          product_memory.instruction_backing_read_errors() == 0,
                      cases, "product excluded execution and backing services");
              require(hook_checks == before_hooks + 1 && reference_pi_requests == before_pi, cases,
                      "actual ISI hook and physical service counts");
              for (unsigned n = 0; n < counters.size(); ++n)
                require(state.spr[counters[n]] == 0x12345678u + n &&
                            product.spr[counters[n]] == state.spr[counters[n]],
                        cases, "actual/product ISI counter retention");
              if (product_cycles != static_cast<u32>(cycles) || product.cycles != 11 ||
                  product.timebase != 0x12345678)
                ++cycle_divergences;
            }
    memory.Shutdown();
    active_manager = nullptr;
  }
  require(cycle_divergences == 0, cases, "actual/product ISI cycle and clock retention");
  return cases;
}

int main() {
  try {
    auto &system = Core::System::GetInstance();
    unsigned cases = 0;
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
      for (unsigned monitor_mode : {0u, 1u, 11u})
        for (bool enabled : {false, true})
          for (u32 address : {0x2000u, 0x01802000u, 0x02002000u, 0x0c002000u}) {
            ++cases;
            state.iCache.Reset(system.GetJitInterface());
            state.msr.Hex = 0;
            state.spr[SPR_HID0] = enabled ? 0x8000u : 0;
            state.pc = address;
            state.npc = 0x12345678;
            state.Exceptions = 0;
            state.gpr[3] = 0xabcdef01;
            state.spr[SPR_SRR0] = 0x12345678;
            state.spr[SPR_SRR1] = 0x56789abc;
            state.spr[SPR_MMCR0] = monitor_mode == 0 ? 0u : (1u << 6) | monitor_mode;
            state.spr[SPR_MMCR1] = monitor_mode == 0 ? 0u : (monitor_mode << 27) | (1u << 22);
            const std::array<u32, 4> counters{SPR_PMC1, SPR_PMC2, SPR_PMC3, SPR_PMC4};
            for (unsigned n = 0; n < counters.size(); ++n)
              state.spr[counters[n]] = 0x12345678 + n;
            cupid::Memory product_memory(wii ? cupid::Console::Wii : cupid::Console::GameCube);
            cupid::cpu::Interpreter product_cpu(product_memory, wii ? cupid::Console::Wii
                                                                    : cupid::Console::GameCube);
            auto &product = product_cpu.state();
            unsigned product_pi = 0, device_reads = 0;
            product_memory.memory_error = [&] { ++product_pi; };
            product_memory.mmio_handles = [](u32 physical) {
              return (physical & 0xf8000000) == 0x08000000;
            };
            product_memory.mmio_read = [&](u32, unsigned) {
              ++device_reads;
              return 0x3860002au;
            };
            product.pc = address;
            product.gpr[3] = state.gpr[3];
            product.spr[cupid::cpu::spr::hid0] = state.spr[SPR_HID0];
            product.spr[cupid::cpu::spr::srr0] = state.spr[SPR_SRR0];
            product.spr[cupid::cpu::spr::srr1] = state.spr[SPR_SRR1];
            product.spr[cupid::cpu::spr::mmcr0] = state.spr[SPR_MMCR0];
            product.spr[cupid::cpu::spr::mmcr1] = state.spr[SPR_MMCR1];
            for (unsigned n = 0; n < counters.size(); ++n)
              product.spr[counters[n]] = state.spr[counters[n]];
            const unsigned before_hooks = hook_checks, before_pi = reference_pi_requests;
            const int cycles = cpu.SingleStepInner();
            const u32 product_cycles = product_cpu.step();
            require(product_cycles == static_cast<u32>(cycles) && product.pc == state.pc &&
                        product.msr == state.msr.Hex && product.gpr[3] == state.gpr[3] &&
                        product.spr[cupid::cpu::spr::srr0] == state.spr[SPR_SRR0] &&
                        product.spr[cupid::cpu::spr::srr1] == state.spr[SPR_SRR1],
                    cases, "actual/product zero step cycles and guest state");
            require(product.instructions == 0 && product.cycles == 0 && product_pi == 0 &&
                        device_reads == 0,
                    cases, "product execution count and excluded device paths");
            require(state.pc == address + 4 && state.npc == address + 4, cases,
                    "actual zero dispatch PC/NPC");
            require(cycles == 0, cases, "actual zero cycle metadata and monitor arguments");
            require(hook_checks == before_hooks + 1 && reference_pi_requests == before_pi, cases,
                    "actual zero caller service counts");
            require(state.Exceptions == 0 && state.gpr[3] == 0xabcdef01 &&
                        state.spr[SPR_SRR0] == 0x12345678 && state.spr[SPR_SRR1] == 0x56789abc,
                    cases, "actual zero exception/register preservation");
            for (unsigned n = 0; n < counters.size(); ++n)
              require(state.spr[counters[n]] == 0x12345678u + n &&
                          product.spr[counters[n]] == state.spr[counters[n]],
                      cases, "actual/product zero monitor counter retention");
          }
      memory.Shutdown();
      active_manager = nullptr;
    }
    const unsigned isi_cases = translated_fault_cases(system);
    std::cout
        << cases
        << " actual/product zero-dispatch cases passed; scheduler and nonzero dispatch excluded\n";
    std::cout
        << isi_cases
        << " actual/product translated-ISI cases passed; scheduler and nonzero dispatch excluded\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
