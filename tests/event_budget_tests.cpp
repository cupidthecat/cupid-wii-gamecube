// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/interpreter.hpp"
#include "cupid/dsp/engine.hpp"
#include "cupid/gfx/command_processor.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/ios/ipc.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

using namespace cupid;

namespace {
constexpr u32 pi = 0x0c003000, cp = 0x0c000000, pe = 0x0c001000;
constexpr u32 dsp_base = 0x0c005000, ipc_base = 0x0d000000, request = 0x10000;
constexpr u64 maximum = std::numeric_limits<u64>::max();

struct Fixture {
  Console console;
  Memory memory;
  hw::Devices devices;
  gfx::CommandProcessor graphics;
  dsp::Engine engine;
  ios::Ipc ipc;
  cpu::Interpreter cpu;
  u64 elapsed = 0;
  std::vector<u64> dsp_interrupts;

  explicit Fixture(Console selected)
      : console(selected), memory(selected), devices(selected), graphics(memory, devices, selected),
        engine(memory, selected), ipc(memory, devices, selected), cpu(memory, selected) {
    memory.mmio_handles = [this](u32 address) { return devices.handles(address); };
    memory.mmio_read = [this](u32 address, unsigned width) { return devices.read(address, width); };
    memory.mmio_write = [this](u32 address, u32 value, unsigned width) {
      devices.write(address, value, width);
    };
    devices.graphics_read = [this](u32 address, unsigned width) {
      return graphics.read(address, width);
    };
    devices.graphics_write = [this](u32 address, u32 value, unsigned width) {
      graphics.write(address, value, width);
    };
    devices.dsp_read = [this](u32 offset) { return engine.read16(offset); };
    devices.dsp_write = [this](u32 offset, u16 value) { engine.write16(offset, value); };
    engine.interrupt = [this] {
      dsp_interrupts.push_back(elapsed);
      devices.dsp_interrupt();
    };
    devices.ipc_read = [this](u32 address) { return ipc.read(address); };
    devices.ipc_write = [this](u32 address, u32 value) { ipc.write(address, value); };
  }

  u64 dsp_period() const {
    return console == Console::Wii ? 729000 : 486000;
  }

  void advance(u64 cycles) {
    elapsed += cycles;
    graphics.advance(cycles);
    engine.advance(cycles);
    ipc.advance(cycles);
    devices.advance(cycles);
  }

  u64 budget() {
    return std::min({graphics.event_free_cycles(), engine.event_free_cycles(),
                     ipc.event_free_cycles(), devices.event_free_cycles()});
  }

  void guest_write(u32 address, u32 value, unsigned width = 4) {
    const u32 instruction = ((width == 2 ? 44u : 36u) << 26) | (3u << 21) | (4u << 16);
    const u32 code = width == 2 ? 0x1004 : 0x1000;
    memory.write32(code, instruction);
    cpu.state().pc = code;
    cpu.state().gpr[3] = value;
    cpu.state().gpr[4] = address;
    const u32 cycles = cpu.step();
    CHECK(cpu.state().pc == code + 4);
    advance(cycles);
  }

  void fifo(unsigned offset, u32 value) {
    devices.write(cp + offset, value & 0xffff, 2);
    devices.write(cp + offset + 2, value >> 16, 2);
  }

  void bp(u8 index, u32 value) {
    const u32 command = (u32(index) << 24) | (value & 0xffffff);
    const std::array<u8, 5> bytes{0x61, u8(command >> 24), u8(command >> 16), u8(command >> 8),
                                  u8(command)};
    graphics.submit(bytes);
  }

  void send(u32 value) {
    devices.write(dsp_base, value >> 16, 2);
    devices.write(dsp_base + 2, value & 0xffff, 2);
  }

  u32 receive() {
    return (devices.read(dsp_base + 4, 2) << 16) | devices.read(dsp_base + 6, 2);
  }

  void load_card_program() {
    devices.write(dsp_base + 0x0a, 0x900, 2);
    CHECK(receive() == 0x8071feed);
    constexpr u32 hash = 0x65d6cc6f;
    std::array<u8, 32> bytes{};
    for (unsigned bit = 0; bit < 32; ++bit)
      if (hash & (1u << bit)) {
        const unsigned distance = (bit * 11) % 32;
        bytes[distance ? 32 - distance : 0] ^= 1;
      }
    CHECK(dsp::firmware_hash(bytes) == hash);
    memory.copy_in(0x2000, bytes);
    send(0x80f3a001);
    send(0x2000);
    send(0x80f3a002);
    send(32);
    send(0x80f3d001);
    send(0);
    CHECK(engine.program_hash() == hash);
  }

  void submit_ipc() {
    memory.write32(request, 2);
    memory.write32(request + 8, 99);
    devices.write(ipc_base, request, 4);
    devices.write(ipc_base + 4, 0x31, 4);
  }

  void acknowledge_ipc(u32 value) {
    devices.write(ipc_base + 4, value, 4);
    devices.write(ipc_base + 0x30, 0x40000000, 4);
  }
};

void graphics_budgets(Console console) {
  auto fixture = std::make_unique<Fixture>(console);
  auto &f = *fixture;
  CHECK(f.graphics.event_free_cycles() == maximum);
  f.devices.write(pi + 4, 0xe00, 4);
  f.devices.write(pe + 0x0a, 3, 2);
  f.bp(0x48, 0x1234);
  f.bp(0x47, 0xabcd);
  f.bp(0x45, 2);
  CHECK(f.graphics.event_free_cycles() == 499);
  f.graphics.advance(499);
  CHECK(f.graphics.event_free_cycles() == 0);
  CHECK(f.devices.interrupt_causes(0x600) == 0);
  CHECK(f.graphics.read(pe + 0x0e, 2) == 0);
  f.graphics.advance(0);
  CHECK(f.graphics.event_free_cycles() == 0);
  f.graphics.advance(1);
  CHECK(f.graphics.read(pe + 0x0e, 2) == 0xabcd);
  CHECK(f.devices.interrupt_causes(0x600) == 0x600);
  CHECK(f.graphics.event_free_cycles() == maximum);
  f.devices.write(pi, 0x600, 4);
  CHECK(f.graphics.event_free_cycles() == 0);
  f.graphics.advance(0);
  CHECK(f.devices.interrupt_causes(0x600) == 0x600);
  CHECK(f.graphics.event_free_cycles() == maximum);
  f.guest_write(pe + 0x0a, 15, 2);
  CHECK(f.devices.interrupt_causes(0x600) == 0);
  CHECK(f.graphics.event_free_cycles() == maximum);
  f.devices.set_interrupt(0xe00, true);
  CHECK(f.graphics.event_free_cycles() == 0);
  f.graphics.advance(0);
  CHECK(f.graphics.event_free_cycles() == maximum);

  f.bp(0x48, 0x4321);
  CHECK(f.graphics.event_free_cycles() == 499);
  f.graphics.advance(maximum);
  CHECK(f.graphics.read(pe + 0x0e, 2) == 0x4321);
  CHECK(f.graphics.event_free_cycles() == maximum);

  f.devices.write(pe + 0x0a, 15, 2);
  f.fifo(0x20, 0x3000);
  f.fifo(0x24, 0x3040);
  f.fifo(0x38, 0x3000);
  f.fifo(0x30, 32);
  const u64 commands = f.graphics.command_count();
  CHECK(f.graphics.event_free_cycles() == maximum);
  f.guest_write(cp + 2, 1, 2);
  CHECK(f.graphics.command_count() == commands + 32);
  CHECK(f.graphics.read(cp + 0x30, 2) == 0);
  CHECK(f.graphics.event_free_cycles() == maximum);

  // A breakpoint holds a full FIFO for the next advance, so no positive budget is safe.
  f.fifo(0x3c, 0x3000);
  f.fifo(0x38, 0x3000);
  f.devices.write(cp + 2, 3, 2);
  f.fifo(0x30, 32);
  CHECK(f.graphics.event_free_cycles() == 0);
  f.graphics.advance(1);
  CHECK(f.graphics.command_count() == commands + 32);
  f.guest_write(cp + 2, 1, 2);
  CHECK(f.graphics.command_count() == commands + 64);
  CHECK(f.graphics.event_free_cycles() == maximum);

  f.fifo(0x2c, 32);
  f.devices.write(cp + 2, 9, 2);
  CHECK(f.devices.interrupt_causes(0x800) == 0x800);
  f.devices.write(pi, 0x800, 4);
  CHECK(f.graphics.event_free_cycles() == 0);
  f.graphics.advance(1);
  CHECK(f.devices.interrupt_causes(0x800) == 0x800);
}

void dsp_budgets(Console console) {
  auto fixture = std::make_unique<Fixture>(console);
  auto &f = *fixture;
  const u64 period = f.dsp_period();
  CHECK(f.engine.event_free_cycles() == period - 1);
  f.advance(period - 1);
  CHECK(f.engine.event_free_cycles() == 0);
  f.load_card_program();
  CHECK(f.engine.event_free_cycles() == 0);
  CHECK(f.dsp_interrupts.empty());
  f.advance(0);
  CHECK(f.engine.event_free_cycles() == 0 && f.dsp_interrupts.empty());
  f.advance(1);
  CHECK(f.engine.event_free_cycles() == period - 1);
  CHECK(f.dsp_interrupts == std::vector<u64>{period});
  f.advance(period - 1);
  CHECK(f.engine.event_free_cycles() == 0);
  f.advance(1);
  CHECK(f.dsp_interrupts == (std::vector<u64>{period, 2 * period}));
  CHECK(f.receive() == 0xdcd10000);
  f.advance(period);
  CHECK(f.dsp_interrupts.size() == 2);
  CHECK(f.engine.event_free_cycles() == period - 1);
  // MMIO selects a new program without resetting the periodic update phase.
  const u64 before = f.elapsed;
  f.guest_write(dsp_base + 0x0a, 0, 2);
  CHECK(f.engine.program_hash() == 1);
  CHECK(f.engine.event_free_cycles() == period - 1 - (f.elapsed - before));
  f.advance(1560);
  CHECK((f.engine.read16(0x0a) & 0x400) == 0);

  Memory memory(console);
  dsp::Engine overflow(memory, console);
  overflow.advance(maximum - 1);
  CHECK(overflow.event_free_cycles() == 1);
  overflow.advance(1);
  CHECK(overflow.event_free_cycles() == 0);
  CHECK_THROWS(overflow.advance(1));
  CHECK(overflow.event_free_cycles() == 0);
  overflow.advance(0);
  CHECK(overflow.event_free_cycles() == 0);
}

void ipc_budgets() {
  auto fixture = std::make_unique<Fixture>(Console::Wii);
  auto &f = *fixture;
  CHECK(f.ipc.event_free_cycles() == maximum);
  f.devices.write(pi + 4, 0x4000, 4);
  f.submit_ipc();
  CHECK(f.ipc.event_free_cycles() == 5999);
  f.advance(5999);
  CHECK(f.ipc.event_free_cycles() == 0);
  CHECK(f.memory.read32(request) == 2);
  f.advance(0);
  CHECK(f.ipc.event_free_cycles() == 0);
  f.advance(1);
  CHECK(f.ipc.event_free_cycles() == 1199);
  CHECK(f.memory.read32(request) == 8);
  CHECK(f.memory.read32(request + 4) == 0xfffffffc);
  CHECK(f.devices.interrupt_causes(0x4000) == 0);
  f.advance(1199);
  CHECK(f.ipc.event_free_cycles() == 0);
  f.advance(1);
  CHECK(f.devices.interrupt_causes(0x4000) == 0x4000);
  CHECK(f.ipc.event_free_cycles() == 5399);
  f.acknowledge_ipc(0x32);
  CHECK(f.devices.interrupt_causes(0x4000) == 0);
  CHECK(f.ipc.event_free_cycles() == 5399);
  f.advance(5399);
  CHECK(f.ipc.event_free_cycles() == 0);
  f.advance(1);
  CHECK(f.devices.read(ipc_base + 8, 4) == request);
  CHECK(f.devices.read(ipc_base + 4, 4) & 4);
  CHECK(f.ipc.event_free_cycles() == 1199);
  f.advance(1200);
  CHECK(f.ipc.event_free_cycles() == maximum - f.elapsed);
  f.acknowledge_ipc(0x34);
  CHECK(f.devices.interrupt_causes(0x4000) == 0);
  f.memory.write32(request, 2);
  f.guest_write(ipc_base, request);
  CHECK(f.ipc.event_free_cycles() == maximum - f.elapsed);
  const u64 before = f.elapsed;
  f.guest_write(ipc_base + 4, 0x31);
  CHECK(f.ipc.event_free_cycles() == 5999 - (f.elapsed - before));

  for (Console console : {Console::GameCube, Console::Wii}) {
    Memory memory(console);
    hw::Devices devices(console);
    ios::Ipc overflow(memory, devices, console);
    overflow.advance(maximum - 1);
    CHECK(overflow.event_free_cycles() == 1);
    CHECK_THROWS(overflow.advance(2));
    CHECK(overflow.event_free_cycles() == 1);
    overflow.advance(1);
    CHECK(overflow.event_free_cycles() == 0);
    CHECK_THROWS(overflow.advance(1));
  }
  Memory memory(Console::Wii);
  hw::Devices devices(Console::Wii);
  ios::Ipc overdue(memory, devices, Console::Wii);
  overdue.advance(maximum - 6000);
  memory.write32(request, 2);
  memory.write32(request + 8, 99);
  overdue.write(ipc_base, request);
  overdue.write(ipc_base + 4, 0x31);
  CHECK(overdue.event_free_cycles() == 5999);
  overdue.advance(6000);
  // Subsequent reply/interrupt timestamps have wrapped beneath the current clock.
  // An overdue queue never advertises a positive interval.
  CHECK(overdue.cycles() == maximum);
  CHECK(overdue.event_free_cycles() == 0);
  overdue.advance(0);
  CHECK(overdue.event_free_cycles() == 0);

  ios::Ipc wrapped(memory, devices, Console::Wii);
  wrapped.advance(maximum - 2000);
  memory.write32(request, 2);
  wrapped.write(ipc_base, request);
  wrapped.write(ipc_base + 4, 0x31);
  // This already overdue event excludes grouping even with 2,000 valid clock cycles left.
  CHECK(wrapped.event_free_cycles() == 0);
  CHECK(wrapped.cycles() == maximum - 2000);
  wrapped.advance(0);
  CHECK(memory.read32(request) == 8);
  CHECK(wrapped.cycles() == maximum - 2000);
  CHECK(wrapped.event_free_cycles() == 2000);
}

void partitioned_core(Console console) {
  auto stepped = std::make_unique<Fixture>(console);
  auto grouped = std::make_unique<Fixture>(console);
  for (Fixture *f : {stepped.get(), grouped.get()}) {
    f->devices.write(pi + 4, 0x4640, 4);
    f->devices.write(pe + 0x0a, 3, 2);
    f->bp(0x48, 0x1357);
    f->bp(0x45, 2);
    f->load_card_program();
    if (console == Console::Wii)
      f->submit_ipc();
  }
  const u64 period = stepped->dsp_period();
  for (u64 target : {u64(499), u64(500), u64(5999), u64(6000), u64(7199), u64(7200), u64(12600),
                     period - 1, period, period + 1, 2 * period - 1, 2 * period}) {
    while (grouped->elapsed < target) {
      const u64 safe = grouped->budget();
      const u64 piece = std::min(target - grouped->elapsed, safe ? safe : 1);
      grouped->advance(piece);
      for (u64 tick = 0; tick < piece; ++tick)
        stepped->advance(1);
      CHECK(stepped->devices.interrupt_causes(0xffffffff) ==
            grouped->devices.interrupt_causes(0xffffffff));
      CHECK(stepped->dsp_interrupts == grouped->dsp_interrupts);
      CHECK(stepped->graphics.event_free_cycles() == grouped->graphics.event_free_cycles());
      CHECK(stepped->engine.event_free_cycles() == grouped->engine.event_free_cycles());
      CHECK(stepped->ipc.event_free_cycles() == grouped->ipc.event_free_cycles());
    }
    CHECK(stepped->engine.program_hash() == grouped->engine.program_hash());
    CHECK(stepped->graphics.command_count() == grouped->graphics.command_count());
    for (u32 address : {pe + 0x0a, pe + 0x0e, cp, cp + 2, cp + 0x30, dsp_base + 0x0a})
      CHECK(stepped->devices.read(address, 2) == grouped->devices.read(address, 2));
    if (console == Console::Wii) {
      CHECK(stepped->devices.read(ipc_base + 4, 4) == grouped->devices.read(ipc_base + 4, 4));
      CHECK(stepped->devices.read(ipc_base + 8, 4) == grouped->devices.read(ipc_base + 8, 4));
      const auto a = stepped->memory.range(request, 64), b = grouped->memory.range(request, 64);
      CHECK(std::equal(a.begin(), a.end(), b.begin()));
      if (target == 7200)
        for (Fixture *f : {stepped.get(), grouped.get()})
          f->acknowledge_ipc(0x32);
    }
    if (target == 500)
      for (Fixture *f : {stepped.get(), grouped.get()}) {
        (void)f->graphics.event_free_cycles();
        f->devices.write(pi, 0x600, 4);
        CHECK(f->graphics.event_free_cycles() == 0);
      }
  }
  CHECK(grouped->dsp_interrupts == (std::vector<u64>{period, 2 * period}));
  CHECK(grouped->graphics.read(pe + 0x0e, 2) == 0x1357);
}

void same_cpu(const cpu::State &a, const cpu::State &b) {
  CHECK(a.gpr == b.gpr && a.spr == b.spr && a.sr == b.sr);
  for (unsigned index = 0; index < a.fpr.size(); ++index)
    CHECK(a.fpr[index].bits == b.fpr[index].bits);
  CHECK(a.pc == b.pc && a.msr == b.msr && a.cr == b.cr && a.fpscr == b.fpscr);
  CHECK(a.last_pc == b.last_pc && a.last_opcode == b.last_opcode);
  CHECK(a.cycles == b.cycles && a.instructions == b.instructions && a.timebase == b.timebase);
}

void polling_cpu_at_device_boundaries(Console console, cpu::ExecutionBackend backend) {
  struct Event {
    std::array<u64, 5> properties{};
    cpu::State cpu;
    std::vector<u8> bytes;
  };

  auto stepped = std::make_unique<Fixture>(console);
  auto grouped = std::make_unique<Fixture>(console);
  std::vector<Event> stepped_events, grouped_events;
  for (Fixture *f : {stepped.get(), grouped.get()}) {
    auto &events = f == stepped.get() ? stepped_events : grouped_events;
    f->cpu.configure_boot();
    f->cpu.set_execution_backend(backend);
    f->cpu.state().pc = 0x8000401c;
    f->cpu.state().gpr[3] = 0x80009000;
    const std::array<u32, 5> code{0x80a30000, 0x2c050000, 0x4182fff8, 0x38e70001, 0x4bfffff0};
    for (unsigned index = 0; index < code.size(); ++index)
      f->memory.write32(0x401c + index * 4, code[index]);
    f->devices.dma_write = [f, &events](u32 address, std::span<const u8> bytes) {
      events.push_back(
          {{1, f->elapsed, address, bytes.size(), f->devices.interrupt_causes(0xffffffff)},
           f->cpu.state(),
           {bytes.begin(), bytes.end()}});
      f->memory.copy_in(address, bytes);
    };
    f->devices.dma_read = [f](u32 address, std::span<u8> bytes) {
      f->memory.copy_out(address, bytes);
    };
    f->devices.audio_samples = [f, &events](std::span<const s16> samples, u32 rate) {
      Event event{
          {2, f->elapsed, rate, f->devices.interrupt_causes(0xffffffff)}, f->cpu.state(), {}};
      for (s16 sample : samples) {
        event.bytes.push_back(u8(u16(sample) >> 8));
        event.bytes.push_back(u8(sample));
      }
      events.push_back(std::move(event));
    };
    f->devices.xfb_present = [f, &events](const hw::Xfb &xfb) {
      events.push_back(
          {{3, f->elapsed, xfb.field, xfb.address, f->devices.field_count()}, f->cpu.state(), {}});
    };
    const auto dsp_interrupt = f->engine.interrupt;
    f->engine.interrupt = [f, &events, dsp_interrupt] {
      events.push_back(
          {{4, f->elapsed, f->devices.interrupt_causes(0xffffffff)}, f->cpu.state(), {}});
      dsp_interrupt();
    };
    f->devices.write(pi + 4, 0x4640, 4);
    f->devices.write(pe + 0x0a, 3, 2);
    f->bp(0x48, 0x1357);
    f->load_card_program();
    f->devices.write(0x0c002000, 16, 2);
    f->devices.write(0x0c00200c, 0x10001, 4);
    f->devices.write(0x0c002010, 0x10001, 4);
    f->devices.write(0x0c00201c, 0x10000100, 4);
    for (u32 address = 0xa000; address < 0xa060; address += 4)
      f->memory.write32(address, 0x01fefc03);
    f->devices.write(dsp_base + 0x30, 0xa000, 4);
    f->devices.write(dsp_base + 0x36, 0x8003, 2);
    f->devices.write(0x0c006008, 0x12000000, 4);
    f->devices.write(0x0c006014, 0x9000, 4);
    f->devices.write(0x0c006018, 32, 4);
    f->devices.write(0x0c00601c, 3, 4);
    if (console == Console::Wii)
      f->submit_ipc();
    events.clear();
  }
  u32 largest = 0;
  const u64 dma_delay = console == Console::Wii ? 218700 : 145800;
  const u64 dsp_period = grouped->dsp_period();
  for (u64 target : {u64{1}, u64{2}, u64{3}, u64{499}, u64{500}, u64{4095}, u64{4096}, u64{4097},
                     u64{5999}, u64{6000}, u64{7199}, u64{7200}, dma_delay - 1, dma_delay,
                     dma_delay + 1, dsp_period - 1, dsp_period, dsp_period + 1}) {
    while (grouped->elapsed < target) {
      const u64 safe = std::min({target - grouped->elapsed, u64{4096}, grouped->budget()});
      grouped->cpu.set_external_interrupt(grouped->devices.external_interrupt_pending());
      const auto run =
          safe >= 2 ? grouped->cpu.try_integer_run(u32(safe), u32(safe)) : cpu::IntegerRun{};
      const u32 cycles = run.instructions ? run.cycles : grouped->cpu.step();
      largest = std::max(largest, run.instructions);
      grouped->advance(cycles);
      for (u32 tick = 0; tick < cycles; ++tick) {
        stepped->cpu.set_external_interrupt(stepped->devices.external_interrupt_pending());
        CHECK(stepped->cpu.step() == 1);
        stepped->advance(1);
      }
    }
    same_cpu(stepped->cpu.state(), grouped->cpu.state());
    CHECK(stepped->memory.state_hash() == grouped->memory.state_hash());
    CHECK(stepped_events.size() == grouped_events.size());
    for (std::size_t index = 0; index < stepped_events.size(); ++index) {
      CHECK(stepped_events[index].properties == grouped_events[index].properties);
      CHECK(stepped_events[index].bytes == grouped_events[index].bytes);
      same_cpu(stepped_events[index].cpu, grouped_events[index].cpu);
    }
    // Observations flush quiet device counters, then execution resumes at the
    // same remaining deadline with the same beam, IRQ, mailbox and IPC state.
    for (u32 address : {pi, 0x0c00202cu, 0x0c006000u})
      CHECK(stepped->devices.read(address, 4) == grouped->devices.read(address, 4));
    CHECK(stepped->budget() == grouped->budget());
    if (console == Console::Wii)
      CHECK(stepped->devices.read(ipc_base + 4, 4) == grouped->devices.read(ipc_base + 4, 4));
  }
  CHECK(largest > 1024 && largest <= 4096);
  CHECK(grouped->memory.read32(0x9000) == 2 && grouped->cpu.state().gpr[7] > 0);
  for (u64 kind : {1u, 2u, 3u, 4u})
    CHECK(std::any_of(grouped_events.begin(), grouped_events.end(),
                      [kind](const Event &event) { return event.properties[0] == kind; }));
  CHECK(grouped->dsp_interrupts == std::vector<u64>{dsp_period});
}
} // namespace

int main() {
  return run_tests([] {
    for (Console console : {Console::GameCube, Console::Wii}) {
      graphics_budgets(console);
      dsp_budgets(console);
      partitioned_core(console);
      for (cpu::ExecutionBackend backend :
           {cpu::ExecutionBackend::Interpreter, cpu::ExecutionBackend::NativeInteger})
        if (backend != cpu::ExecutionBackend::NativeInteger || cpu::NativeInteger::supported())
          polling_cpu_at_device_boundaries(console, backend);
    }
    ipc_budgets();
  });
}
