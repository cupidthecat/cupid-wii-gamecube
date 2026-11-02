// SPDX-License-Identifier: GPL-2.0-or-later
// Uses the same real step, instruction tables, SPR handler and monitor as the
// surrounding comparison. No replacement thermal or counter logic is supplied.
void system_register_reference_cases(Core::System &system) {
  unsigned cases = 0, steps = 0;
  constexpr std::array<unsigned, 14> observed{SPR_MMCR0, SPR_MMCR1, SPR_PMC1,  SPR_PMC2,  SPR_PMC3,
                                              SPR_PMC4,  SPR_THRM1, SPR_THRM2, SPR_THRM3, SPR_SRR0,
                                              SPR_SRR1,  SPR_LR,    SPR_CTR,   SPR_WPAR};
  const auto special = [](unsigned operation, unsigned reg, unsigned index) {
    return (31u << 26) | (reg << 21) | ((index & 31) << 16) | ((index >> 5) << 11) |
           (operation << 1);
  };
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
    cupid::cpu::Interpreter product_cpu(product_memory,
                                        wii ? cupid::Console::Wii : cupid::Console::GameCube);
    auto &product = product_cpu.state();
    memory.Write_U32(0x3fc00000, 0x2000);
    product_memory.write32(0x2000, 0x3fc00000);
    const auto prepare = [&] {
      ++cases;
      state.pc = 0x1000;
      state.npc = 0x12345678;
      state.msr.Hex = cupid::cpu::msr::fp;
      state.Exceptions = 0;
      state.spr[SPR_HID0] = 0;
      state.spr[SPR_DEC] = 0xffffffff;
      state.spr[SPR_SRR0] = 0x12345678;
      state.spr[SPR_SRR1] = 0x56789abc;
      state.cr.Set(0xa1b2c3d4);
      state.SetXER(UReg_XER{0});
      state.fpscr.Hex = 0;
      state.gpr[4] = 0x2000;
      for (unsigned n = 0; n < 32; ++n)
        state.ps[n].SetBoth(u64{0}, u64{0});
      product_cpu.reset();
      product.pc = state.pc;
      product.msr = state.msr.Hex;
      product.cr = state.cr.Get();
      product.fpscr = state.fpscr.Hex;
      std::copy(std::begin(state.gpr), std::end(state.gpr), product.gpr.begin());
      std::copy(std::begin(state.spr), std::end(state.spr), product.spr.begin());
      product.spr[cupid::cpu::spr::xer] = state.GetXER().Hex;
    };
    const auto compare = [&] {
      require(product.pc == state.pc && product.msr == state.msr.Hex &&
                  product.cr == state.cr.Get() && product.fpscr == state.fpscr.Hex &&
                  product.spr[cupid::cpu::spr::xer] == state.GetXER().Hex,
              cases, "system-register architectural control state");
      for (unsigned n = 0; n < 32; ++n)
        require(product.gpr[n] == state.gpr[n] &&
                    product.fpr[n].bits[0] == state.ps[n].PS0AsU64() &&
                    product.fpr[n].bits[1] == state.ps[n].PS1AsU64(),
                cases, "system-register integer and floating registers");
      for (unsigned reg : observed)
        require(product.spr[reg] == state.spr[reg], cases, "system-register observed SPRs");
    };
    const auto step = [&](u32 opcode) {
      memory.Write_U32(opcode, state.pc);
      product_memory.write32(product.pc, opcode);
      const unsigned hooks = hook_checks, pi = reference_pi_requests;
      const int cycles = cpu.SingleStepInner();
      require(product_cpu.step() == static_cast<u32>(cycles), cases, "system-register step cycles");
      compare();
      require(hook_checks == hooks + 1 && reference_pi_requests == pi, cases,
              "system-register step services");
      ++steps;
    };
    const auto finish = [&] {
      const bool pending = (state.Exceptions & EXCEPTION_PERFORMANCE_MONITOR) != 0;
      require((state.Exceptions & ~EXCEPTION_PERFORMANCE_MONITOR) == 0, cases,
              "system-register exception scope");
      // Keep an already requested interrupt observable, but do not request a
      // fresh one during the no-interrupt NOP control below.
      state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
      product.spr[SPR_MMCR0] = product.spr[SPR_MMCR1] = 0;
      state.msr.Hex |= cupid::cpu::msr::ee;
      product.msr |= cupid::cpu::msr::ee;
      manager->CheckExternalExceptions();
      if (pending) {
        require(product_cpu.step() == 1, cases, "performance interrupt delivery cycle");
        compare();
        require(product.pc == 0xf00, cases, "performance interrupt vector");
      } else
        step(0x60000000);
    };
    for (unsigned target : {SPR_THRM1, SPR_THRM2, SPR_THRM3})
      for (unsigned threshold = 0; threshold < 128; ++threshold)
        for (u32 flags = 0; flags < 8; ++flags)
          for (u32 latched = 0; latched < 4; ++latched)
            for (u32 enabled : {0u, 1u}) {
              state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
              state.spr[SPR_THRM1] = (threshold << 23) | 0x345678 | flags | (latched << 30);
              state.spr[SPR_THRM2] =
                  ((127 - threshold) << 23) | 0x654320 | (flags ^ 5) | ((latched ^ 3) << 30);
              state.spr[SPR_THRM3] = 0xa5a50000 | enabled;
              state.gpr[3] = target == SPR_THRM3 ? 0x5a5a0000 | enabled : state.spr[target];
              prepare();
              step(special(467, 3, target));
              for (unsigned reg : {SPR_THRM1, SPR_THRM2, SPR_THRM3})
                step(special(339, 6, reg));
              finish();
            }
    constexpr std::array<unsigned, 4> widths{128, 64, 32, 32}, shifts{6, 0, 27, 22};
    constexpr std::array<unsigned, 4> counters{SPR_PMC1, SPR_PMC2, SPR_PMC3, SPR_PMC4};
    for (unsigned counter = 0; counter < 4; ++counter)
      for (unsigned selector = 0; selector < widths[counter]; ++selector)
        for (u32 initial : {0u, 0x7ffffffdu, 0x80000000u, 0xffffffffu})
          for (u32 interrupts : {0u, 1u << 14, 1u << 15, 3u << 14}) {
            state.spr[SPR_MMCR0] = interrupts;
            state.spr[SPR_MMCR1] = 0;
            for (unsigned reg : counters)
              state.spr[reg] = initial;
            const unsigned target = counter < 2 ? SPR_MMCR0 : SPR_MMCR1;
            state.gpr[3] = (selector << shifts[counter]) | (target == SPR_MMCR0 ? interrupts : 0);
            prepare();
            step(special(467, 3, target));
            step((32u << 26) | (5u << 21) | (4u << 16));
            step((48u << 26) | (3u << 21) | (4u << 16));
            step((63u << 26) | (3u << 21) | (4u << 16) | (5u << 11) | (21u << 1));
            finish();
          }
    unsigned pending_bytes = 0, resets = 0;
    product_memory.fifo_gather_reset = [&] {
      pending_bytes = 0;
      ++resets;
    };
    for (unsigned partial = 0; partial < GPFifo::GATHER_PIPE_SIZE; ++partial) {
      state.spr[SPR_MMCR0] = state.spr[SPR_MMCR1] = 0;
      state.spr[SPR_WPAR] = GPFifo::GATHER_PIPE_PHYSICAL_ADDRESS | 1;
      state.gpr[3] = GPFifo::GATHER_PIPE_PHYSICAL_ADDRESS;
      prepare();
      auto &gather = system.GetGPFifo();
      gather.Init();
      pending_bytes = partial;
      resets = 0;
      for (unsigned n = 0; n < partial; ++n)
        gather.FastWrite8(static_cast<u8>(n));
      step(special(339, 6, SPR_WPAR));
      require(pending_bytes == partial && resets == 0 &&
                  state.gather_pipe_ptr - state.gather_pipe_base_ptr == partial,
              cases, "WPAR read preserves gather prefix");
      step(special(467, 3, SPR_WPAR));
      require(pending_bytes == 0 && resets == 1 &&
                  state.gather_pipe_ptr == state.gather_pipe_base_ptr,
              cases, "WPAR write resets gather prefix");
      pending_bytes = 31 - partial;
      for (unsigned n = 0; n < pending_bytes; ++n)
        gather.FastWrite8(static_cast<u8>(n + 17));
      step(special(467, 3, SPR_WPAR));
      require(pending_bytes == 0 && resets == 2 &&
                  state.gather_pipe_ptr == state.gather_pipe_base_ptr,
              cases, "unchanged WPAR value still resets gather prefix");
      finish();
    }
    memory.Shutdown();
    active_manager = nullptr;
  }
  require(cases == 57408 && steps == 283890, cases, "system-register comparison coverage");
  std::cout << cases << " actual SPR/monitor/gather sequences and " << steps
            << " dispatched steps passed; scheduler timing and thermal interrupts excluded\n";
}
