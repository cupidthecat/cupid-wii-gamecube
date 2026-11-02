// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse the ordinary-lifetime service adapters; the selected implementation
// translation units remain external and unchanged.
#define main existing_dispatch_fixture_main
#include "cpu_dispatch_reference_checks.cpp"
#undef main

namespace {
unsigned native_control_source_cases(Core::System &system) {
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
    cupid::cpu::NativeInteger native;
    for (unsigned bo = 0; bo < 32; ++bo)
      for (unsigned bi = 0; bi < 32; ++bi)
        for (bool link : {false, true})
          for (bool absolute : {false, true})
            for (s32 displacement : {-32768, 4, 32764})
              for (u32 counter : {0u, 1u, 2u, 0xffffffffu})
                for (bool condition : {false, true}) {
                  ++cases;
                  const u32 entry = (cases & 1) ? 0x2000 : 0x4000;
                  const std::array<u32, 4> words{0x38630001,
                                                 (16u << 26) | (bo << 21) | (bi << 16) |
                                                     (static_cast<u32>(displacement) & 0xfffc) |
                                                     (absolute ? 2u : 0) | static_cast<u32>(link),
                                                 0x38630002, 0x68641234};
                  for (unsigned i = 0; i < words.size(); ++i)
                    memory.Write_U32(words[i], entry + i * 4);
                  state.msr.Hex = 0;
                  state.spr[SPR_HID0] = state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
                  state.pc = entry;
                  state.npc = 0x12345678;
                  state.Exceptions = 0;
                  for (unsigned i = 0; i < 32; ++i)
                    state.gpr[i] = 0x87654321u + i * 0x9e3779b9u;
                  state.cr.Set(condition ? 1u << (31 - bi) : 0);
                  state.SetXER(UReg_XER{((cases & 7u) << 29) | 0x0000ff7fu});
                  state.spr[SPR_LR] = 0x12345678;
                  state.spr[SPR_CTR] = counter;
                  cupid::cpu::State actual;
                  actual.pc = entry;
                  for (unsigned i = 0; i < 32; ++i)
                    actual.gpr[i] = state.gpr[i];
                  actual.cr = state.cr.Get();
                  actual.spr[cupid::cpu::spr::xer] = state.GetXER().Hex;
                  actual.spr[cupid::cpu::spr::lr] = state.spr[SPR_LR];
                  actual.spr[cupid::cpu::spr::ctr] = counter;
                  unsigned retired = 0;
                  bool taken = false;
                  const unsigned hooks_before = hook_checks, pi_before = reference_pi_requests;
                  for (u32 word : words) {
                    taken = false;
                    if ((word >> 26) == 16) {
                      const u32 next_counter = state.spr[SPR_CTR] - ((bo & 4) ? 0 : 1);
                      taken = ((bo & 4) || ((next_counter != 0) != ((bo & 2) != 0))) &&
                              ((bo & 16) || (state.cr.GetBit(bi) == ((bo >> 3) & 1)));
                    }
                    require(cpu.SingleStepInner() == 1, cases, "selected source cycle count");
                    ++retired;
                    require(state.Exceptions == 0 && state.pc == state.npc, cases,
                            "selected source clean retirement");
                    if (taken)
                      break;
                  }
                  cupid::cpu::NativeControlRun result;
                  require(native.execute_control_sequence(actual, words, result), cases,
                          "compiled control admission");
                  require(result.instructions == retired && result.taken == taken, cases,
                          "compiled taken and retirement result");
                  require(actual.pc == state.pc && actual.cr == state.cr.Get() &&
                              actual.spr[cupid::cpu::spr::xer] == state.GetXER().Hex &&
                              actual.spr[cupid::cpu::spr::lr] == state.spr[SPR_LR] &&
                              actual.spr[cupid::cpu::spr::ctr] == state.spr[SPR_CTR],
                          cases, "compiled control source register state");
                  for (unsigned i = 0; i < 32; ++i)
                    require(actual.gpr[i] == state.gpr[i], cases,
                            "compiled control source GPR state");
                  require(hook_checks == hooks_before + retired &&
                              reference_pi_requests == pi_before,
                          cases, "selected source service exclusion");
                }
    memory.Shutdown();
    active_manager = nullptr;
  }
  std::cout << cases
            << " compiled control/source dispatch cases passed; "
               "GPR/CR/XER/LR/CTR/PC and taken/count only; clocks and devices excluded\n";
  return cases;
}
} // namespace

int main() {
  try {
    require(native_control_source_cases(Core::System::GetInstance()) == 196608u, 0,
            "source control comparison case count");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
