// SPDX-License-Identifier: GPL-2.0-or-later
#include "Common/Logging/Log.h"
#include "Core/DSP/DSPAccelerator.h"
#include "cupid/dsp/accelerator.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Common::Log {
void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
}
} // namespace Common::Log

// The unchanged accelerator runs against an independent, bounded byte store.
// Results, history, stop latches, addresses and ordered callbacks are compared.
struct Event {
  unsigned kind;
  u32 address;
  u16 value;
  bool operator==(const Event &) const = default;
};

struct Bus {
  std::array<u8, 4096> bytes{};
  std::vector<Event> events;

  u8 read(u32 address) {
    u8 value = bytes[address & 4095];
    events.push_back({0, address, value});
    return value;
  }

  void write(u32 address, u8 value) {
    bytes[address & 4095] = value;
    events.push_back({1, address, value});
  }

  void exception(unsigned code) {
    events.push_back({2, 0, u16(code)});
  }
};

class Reference final : public DSP::Accelerator {
public:
  Bus bus;

  bool stopped() const {
    return m_reads_stopped;
  }

protected:
  void OnRawReadEndException() override {
    bus.exception(3);
  }

  void OnRawWriteEndException() override {
    bus.exception(4);
  }

  void OnSampleReadEndException() override {
    bus.exception(5);
  }

  u8 ReadMemory(u32 a) override {
    return bus.read(a);
  }

  void WriteMemory(u32 a, u8 v) override {
    bus.write(a, v);
  }
};

int main() {
  unsigned comparisons = 0;
  u32 random = 0x9c314a75;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return random;
  };
  for (unsigned format = 0; format < 65536; ++format) {
    Reference ref;
    cupid::dsp::SampleAccelerator product;
    Bus actual;
    for (unsigned i = 0; i < actual.bytes.size(); ++i)
      ref.bus.bytes[i] = actual.bytes[i] = u8(i * 37 + format);
    product.memory_read = [&](u32 a) { return actual.read(a); };
    product.memory_write = [&](u32 a, u8 v) { actual.write(a, v); };
    product.exception = [&](unsigned c) { actual.exception(c); };
    std::array<s16, 16> coefficients{};
    for (auto &v : coefficients)
      v = s16(next());
    const auto put = [&](u8 reg, u16 value) {
      product.write(reg, value);
      switch (reg) {
      case 0xd1:
        ref.SetSampleFormat(value);
        break;
      case 0xda:
        ref.SetPredScale(value);
        break;
      case 0xdb:
        ref.SetYn1(s16(value));
        break;
      case 0xdc:
        ref.SetYn2(s16(value));
        break;
      case 0xde:
        ref.SetGain(s16(value));
        break;
      case 0xdf:
        ref.SetInput(value);
        break;
      default:
        throw std::runtime_error("setter");
      }
    };
    const auto address = [&](u8 reg, u32 value) {
      product.write(reg, u16(value >> 16));
      product.write(u8(reg + 1), u16(value));
      if (reg == 0xd4)
        ref.SetStartAddress(value);
      else if (reg == 0xd6)
        ref.SetEndAddress(value);
      else
        ref.SetCurrentAddress(value);
    };
    put(0xd1, u16(format));
    put(0xda, u16(next()));
    put(0xdb, u16(next()));
    put(0xdc, u16(next()));
    put(0xde, u16(next()));
    put(0xdf, u16(next()));
    const u32 current = (format & 63) | ((format & 0x100) ? 0x80000000u : 0u);
    address(0xd4, (format >> 6) & 63);
    address(0xd6, (current & 63) + ((format >> 2) & 15));
    address(0xd8, current);
    for (unsigned step = 0; step < 12; ++step) {
      unsigned choice = next() % 6;
      u16 expected = 0, observed = 0;
      if (choice == 0) {
        expected = ref.ReadRaw();
        observed = product.read_raw();
      } else if (choice < 3) {
        expected = ref.ReadSample(coefficients.data());
        observed = product.read_sample(coefficients);
      } else if (choice == 3) {
        const u16 value = u16(next());
        ref.WriteRaw(value);
        product.write_raw(value);
      } else if (choice == 4)
        put(0xdc, u16(next()));
      else
        put(0xdf, u16(next()));
      ++comparisons;
      const auto &s = product.state();
      if (expected != observed || s.start != ref.GetStartAddress() ||
          s.end != ref.GetEndAddress() || s.current != ref.GetCurrentAddress() ||
          s.format != ref.GetSampleFormat() || s.gain != ref.GetGain() ||
          s.history1 != ref.GetYn1() || s.history2 != ref.GetYn2() ||
          s.predictor != ref.GetPredScale() || s.input != ref.GetInput() ||
          s.stopped != ref.stopped() || actual.bytes != ref.bus.bytes ||
          actual.events != ref.bus.events) {
        std::cerr << "format=" << std::hex << format << " step=" << std::dec << step
                  << " choice=" << choice << " value=" << std::hex << expected << "/" << observed
                  << " current=" << ref.GetCurrentAddress() << "/" << s.current << '\n';
        return 1;
      }
    }
  }
  std::cout << comparisons << " actual sample accelerator operations passed\n";
}
