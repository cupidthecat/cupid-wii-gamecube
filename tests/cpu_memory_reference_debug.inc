// SPDX-License-Identifier: GPL-2.0-or-later

// Original fail-fast adapters for unused virtual debugger services.
void PPCPatches::ApplyExistingPatch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCPatches::Patch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCPatches::UnPatch(std::size_t index) {
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

std::size_t PPCDebugInterface::SetWatch(u32 address, std::string name) {
  (void)address;
  (void)name;
  throw std::logic_error("Unused reference debugger operation");
}

const Common::Debug::Watch &PPCDebugInterface::GetWatch(std::size_t index) const {
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

const std::vector<Common::Debug::Watch> &PPCDebugInterface::GetWatches() const {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UnsetWatch(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UpdateWatch(std::size_t index, u32 address, std::string name) {
  (void)index;
  (void)address;
  (void)name;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UpdateWatchAddress(std::size_t index, u32 address) {
  (void)index;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UpdateWatchName(std::size_t index, std::string name) {
  (void)index;
  (void)name;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UpdateWatchLockedState(std::size_t index, bool locked) {
  (void)index;
  (void)locked;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::EnableWatch(std::size_t index) {
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::DisableWatch(std::size_t index) {
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

bool PPCDebugInterface::HasEnabledWatch(u32 address) const {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::RemoveWatch(std::size_t index) {
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::LoadWatchesFromStrings(std::span<const std::string> watches) {
  (void)watches;
  throw std::logic_error("Unused reference debugger operation");
}

std::vector<std::string> PPCDebugInterface::SaveWatchesToStrings() const {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ClearWatches() {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::SetPatch(const Core::CPUThreadGuard &guard, u32 address, u32 value) {
  (void)guard;
  (void)address;
  (void)value;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::SetPatch(const Core::CPUThreadGuard &guard, u32 address,
                                 std::vector<u8> value) {
  (void)guard;
  (void)address;
  (void)value;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::SetFramePatch(const Core::CPUThreadGuard &guard, u32 address, u32 value) {
  (void)guard;
  (void)address;
  (void)value;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::SetFramePatch(const Core::CPUThreadGuard &guard, u32 address,
                                      std::vector<u8> value) {
  (void)guard;
  (void)address;
  (void)value;
  throw std::logic_error("Unused reference debugger operation");
}

const std::vector<Common::Debug::MemoryPatch> &PPCDebugInterface::GetPatches() const {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::UnsetPatch(const Core::CPUThreadGuard &guard, u32 address) {
  (void)guard;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::EnablePatch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::DisablePatch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

bool PPCDebugInterface::HasEnabledPatch(u32 address) const {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::RemovePatch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ClearPatches(const Core::CPUThreadGuard &guard) {
  (void)guard;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ApplyExistingPatch(const Core::CPUThreadGuard &guard, std::size_t index) {
  (void)guard;
  (void)index;
  throw std::logic_error("Unused reference debugger operation");
}

Common::Debug::Threads PPCDebugInterface::GetThreads(const Core::CPUThreadGuard &guard) const {
  (void)guard;
  throw std::logic_error("Unused reference debugger operation");
}

std::string PPCDebugInterface::Disassemble(const Core::CPUThreadGuard *guard, u32 address) const {
  (void)guard;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

std::string PPCDebugInterface::GetRawMemoryString(const Core::CPUThreadGuard &guard, int memory,
                                                  u32 address) const {
  (void)guard;
  (void)memory;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

bool PPCDebugInterface::IsAlive() const {
  throw std::logic_error("Unused reference debugger operation");
}

bool PPCDebugInterface::IsBreakpoint(u32 address) const {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::AddBreakpoint(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::RemoveBreakpoint(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ClearAllBreakpoints() {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ToggleBreakpoint(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ClearAllMemChecks() {
  throw std::logic_error("Unused reference debugger operation");
}

bool PPCDebugInterface::IsMemCheck(u32 address, size_t size) const {
  (void)address;
  (void)size;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::ToggleMemCheck(u32 address, bool read, bool write, bool log) {
  (void)address;
  (void)read;
  (void)write;
  (void)log;
  throw std::logic_error("Unused reference debugger operation");
}

u32 PPCDebugInterface::ReadMemory(const Core::CPUThreadGuard &guard, u32 address) const {
  (void)guard;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

u32 PPCDebugInterface::ReadExtraMemory(const Core::CPUThreadGuard &guard, int memory,
                                       u32 address) const {
  (void)guard;
  (void)memory;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

u32 PPCDebugInterface::ReadInstruction(const Core::CPUThreadGuard &guard, u32 address) const {
  (void)guard;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

std::optional<u32>
PPCDebugInterface::GetMemoryAddressFromInstruction(const std::string &instruction) const {
  (void)instruction;
  throw std::logic_error("Unused reference debugger operation");
}

u32 PPCDebugInterface::GetPC() const {
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::SetPC(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::RunTo(u32 address) {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

u32 PPCDebugInterface::GetColor(const Core::CPUThreadGuard *guard, u32 address) const {
  (void)guard;
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

std::string PPCDebugInterface::GetDescription(u32 address) const {
  (void)address;
  throw std::logic_error("Unused reference debugger operation");
}

void PPCDebugInterface::Clear(const Core::CPUThreadGuard &guard) {
  (void)guard;
  throw std::logic_error("Unused reference debugger operation");
}
