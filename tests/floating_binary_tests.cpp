// SPDX-License-Identifier: GPL-2.0-or-later
#include "floating_binary_test_support.hpp"
#include "test_support.hpp"
#include <iostream>

using namespace floating_binary_test;

namespace {
u64 literal_cases = 0, host_cases = 0, exception_cases = 0, aggregate = 0;
constexpr std::array<std::array<unsigned, 3>, 6> aliases{
    {{1, 2, 3}, {3, 2, 3}, {2, 2, 3}, {2, 2, 2}, {0, 0, 0}, {31, 0, 31}}};

struct Fixture {
  CodeBus bus;
  Interpreter cpu;
  Console console;

  explicit Fixture(Console c, ExecutionBackend backend) : cpu(bus, c), console(c) {
    cpu.set_execution_backend(backend);
  }

  void one(Operation op, unsigned index, const std::array<unsigned, 3> &regs, bool rc,
           unsigned guest, unsigned fault, unsigned x87 = 0, unsigned simd = 0, unsigned flush = 0,
           unsigned status = 0) {
    const auto old = cpu.state();
    auto before = patterned(console);
    before.cycles = old.cycles;
    before.instructions = old.instructions;
    before.timebase = old.timebase;
    const auto [d, a, b] = regs;
    before.fpr[a].bits = {literals[(index + 3) % 20], literals[(index + 11) % 20]};
    before.fpr[b].bits = {literals[index % 20], literals[(index + 7) % 20]};
    before.fpscr = (before.fpscr & ~7u) | guest;
    if (fault == 1 || fault == 3)
      before.msr &= ~msr::fp;
    if (fault == 2 || fault == 3)
      before.spr[spr::hid2] &= ~0x20000000u;
    cpu.state() = before;
    const u32 word = opcode(op, d, a, b, rc);
    bus.write32(0x1000, word);
    const auto original_bytes = bus.bytes;
    bus.fetch_count = 0;
    bus.record = true;
    State expected = before;
    const bool illegal = op.primary == 4 && (fault == 2 || fault == 3);
    const bool unavailable = (before.msr & msr::fp) == 0;
    if (illegal || unavailable) {
      expected.pc = illegal ? 0x700 : 0x800;
      expected.spr[spr::srr0] = 0x1000;
      expected.spr[spr::srr1] = (before.msr & 0x87c0ffffu) | (illegal ? 0x80000u : 0u);
      expected.msr = (before.msr & ~msr::le) | ((before.msr & msr::ile) ? msr::le : 0u);
      expected.msr &= ~0x04ef36u;
    } else {
      expected_binary(expected, before, op, d, a, b, rc);
      expected.pc = 0x1004;
      ++expected.instructions;
    }
    expected.last_pc = 0x1000;
    expected.last_opcode = word;
    ++expected.cycles;
    expected.timebase += (before.cycles % 12) == 11;
#if defined(_M_X64) || defined(__x86_64__)
    host_controls(x87, simd, flush, status);
    const auto host_before = HostSnapshot::capture();
#else
    (void)x87;
    (void)simd;
    (void)flush;
    (void)status;
#endif
    const u32 cycles = cpu.step();
#if defined(_M_X64) || defined(__x86_64__)
    const auto host_after = HostSnapshot::capture();
    require(std::fesetenv(FE_DFL_ENV) == 0, "host default restore failed");
    require(host_before == host_after, "raw host FP control/status changed");
#endif
    bus.record = false;
    require(cycles == 1, "binary opcode cycle cost differs");
    equal(cpu.state(), expected);
    require(bus.bytes == original_bytes, "register opcode changed authored RAM");
    require(bus.fetch_count == 4, "unexpected bus transaction count");
    for (unsigned n = 0; n < 4; ++n)
      require(bus.fetches[n] == 0x1000 + n, "fetch ordering differs");
    aggregate ^= state_hash(cpu.state());
  }
};

void matrix(ExecutionBackend backend) {
  for (Console console : {Console::GameCube, Console::Wii}) {
    Fixture f(console, backend);
    for (Operation op : operations)
      for (unsigned n = 0; n < 20; ++n)
        for (const auto &regs : aliases)
          for (bool rc : {false, true})
            for (unsigned guest = 0; guest < 8; ++guest) {
              f.one(op, n, regs, rc, guest, 0);
              ++literal_cases;
            }
    for (Operation op : operations)
      for (unsigned fault = 1; fault < 4; ++fault)
        for (bool rc : {false, true})
          for (const auto &regs : aliases) {
            f.one(op, 14, regs, rc, 7, fault);
            ++exception_cases;
          }
#if defined(_M_X64) || defined(__x86_64__)
    for (Operation op : operations)
      for (unsigned x87 = 0; x87 < 4; ++x87)
        for (unsigned simd = 0; simd < 4; ++simd)
          for (unsigned flush : {0u, 0x40u, 0x8000u, 0x8040u})
            for (unsigned status = 0; status < 4; ++status)
              for (unsigned guest = 0; guest < 8; ++guest)
                for (bool rc : {false, true})
                  for (unsigned index : {14 + (guest % 4), 2 + (guest % 6)}) {
                    f.one(op, index, aliases[(x87 + simd + status) % aliases.size()], rc, guest, 0,
                          x87, simd, flush, status);
                    ++host_cases;
                  }
    // Invalid paired execution takes priority over unavailable FP without
    // touching any incoming host rounding/flush/exception configuration.
    for (Operation op : operations)
      for (unsigned fault = 1; fault < 4; ++fault)
        for (unsigned x87 = 0; x87 < 4; ++x87)
          for (unsigned simd = 0; simd < 4; ++simd)
            for (unsigned flush : {0u, 0x40u, 0x8000u, 0x8040u})
              for (unsigned status : {0u, 1u, 3u}) {
                f.one(op, 15, aliases[3], true, 7, fault, x87, simd, flush, status);
                ++exception_cases;
              }
#endif
  }
}
} // namespace

int main() {
  return run_tests([] {
#if defined(_M_X64) || defined(__x86_64__)
    HostEnvironment environment;
#endif
    matrix(ExecutionBackend::Interpreter);
    if (NativeInteger::supported())
      matrix(ExecutionBackend::NativeInteger);
    std::cout << "{\"literal_cases\":" << literal_cases << ",\"host_cases\":" << host_cases
              << ",\"exception_cases\":" << exception_cases << ",\"aggregate\":" << aggregate
              << ",\"actual_handlers\":12,\"all_full_state_RAM_fetch_checks\":true}\n";
  });
}
