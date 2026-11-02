// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/accelerator.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace cupid::dsp {
bool SampleAccelerator::readable(u8 reg) noexcept {
  return reg == 0xd1 || (reg >= 0xd3 && reg <= 0xdf);
}

bool SampleAccelerator::writable(u8 reg) noexcept {
  return readable(reg) && reg != 0xdd;
}

u8 SampleAccelerator::byte(u32 address) {
  if (!memory_read)
    throw std::runtime_error("DSP sample memory read is not connected");
  return memory_read(address);
}

void SampleAccelerator::raise(unsigned code) {
  if (exception)
    exception(code);
}

u16 SampleAccelerator::current_sample() {
  const u32 address = state_.current;
  switch (state_.format & 3) {
  case 0: {
    const u8 packed = byte(address >> 1);
    return (address & 1) ? packed & 15 : packed >> 4;
  }
  case 1:
    return byte(address);
  case 2: {
    const u8 high = byte(address * 2);
    return u16((u16(high) << 8) | byte(address * 2 + 1));
  }
  default:
    return 0;
  }
}

u16 SampleAccelerator::read_raw() {
  const u16 value = current_sample();
  if ((state_.format & 3) == 3)
    state_.current = (state_.current & ~3u) | ((state_.current + 1) & 3);
  else
    ++state_.current;
  if (state_.current - 1 == state_.end) {
    state_.current = state_.start;
    raise(3);
  }
  state_.current &= 0xbfffffff;
  return value;
}

void SampleAccelerator::write_raw(u16 value) {
  if (!(state_.current & 0x80000000))
    return;
  if (!memory_write)
    throw std::runtime_error("DSP sample memory write is not connected");
  memory_write(state_.current * 2, u8(value >> 8));
  memory_write(state_.current * 2 + 1, u8(value));
  ++state_.current;
  // The caller requests this exception on every accepted raw write. Unlike
  // reads, this path does not remask the incremented current-address register.
  raise(4);
}

u16 SampleAccelerator::read_sample(std::span<const s16, 16> coefficients) {
  if (state_.stopped)
    return 0;
  const unsigned decoding = (state_.format >> 2) & 3;
  const s16 raw = s16((decoding & 1) ? state_.input : current_sample());
  const unsigned predictor = (state_.predictor >> 4) & 7;
  const s32 first = s32(coefficients[predictor * 2]) * state_.history1;
  const s32 second = s32(coefficients[predictor * 2 + 1]) * state_.history2;
  unsigned step = 2;
  u16 result;
  if (decoding == 0) {
    const s32 nibble = (raw & 15) - ((raw & 8) ? 16 : 0);
    const s32 history = std::bit_cast<s32>(u32(0x400) + u32(first) + u32(second));
    const s32 decoded = nibble * (1 << (state_.predictor & 15)) + (history >> 11);
    result = u16(std::clamp(decoded, s32(-32768), s32(32767)));
    state_.history2 = state_.history1;
    state_.history1 = s16(result);
    ++state_.current;
    if ((state_.end & 15) == 0 && state_.current == state_.end) {
      state_.current = state_.start + 1;
    } else if ((state_.end & 15) == 1 && state_.current == state_.end - 1) {
      state_.current = state_.start;
    } else if ((state_.current & 15) == 0) {
      state_.predictor = byte((state_.current & ~15u) >> 1);
      state_.current += 2;
      step = 4;
    }
  } else {
    const unsigned scale = (state_.format >> 4) & 3;
    const unsigned shift = scale == 0 ? 11 : scale == 2 ? 16 : 0;
    const s64 decoded =
        s64((s32(state_.gain) * raw) >> shift) + (first >> shift) + (second >> shift);
    result = u16(decoded);
    state_.history2 = state_.history1;
    state_.history1 = s16(result);
    if (decoding != 1)
      ++state_.current;
  }
  if (state_.current == state_.end + step - 1) {
    state_.current = state_.start;
    state_.stopped = true;
    raise(5);
  }
  state_.current &= 0xbfffffff;
  return result;
}

u16 SampleAccelerator::read(u8 reg, std::span<const s16, 16> coefficients) {
  switch (reg) {
  case 0xd1:
    return state_.format;
  case 0xd3:
    return read_raw();
  case 0xd4:
    return u16(state_.start >> 16);
  case 0xd5:
    return u16(state_.start);
  case 0xd6:
    return u16(state_.end >> 16);
  case 0xd7:
    return u16(state_.end);
  case 0xd8:
    return u16(state_.current >> 16);
  case 0xd9:
    return u16(state_.current);
  case 0xda:
    return state_.predictor;
  case 0xdb:
    return u16(state_.history1);
  case 0xdc:
    return u16(state_.history2);
  case 0xdd:
    return read_sample(coefficients);
  case 0xde:
    return u16(state_.gain);
  case 0xdf:
    return state_.input;
  default:
    throw std::invalid_argument("Unknown DSP sample register read");
  }
}

void SampleAccelerator::write(u8 reg, u16 value) {
  switch (reg) {
  case 0xd1:
    state_.format = value;
    return;
  case 0xd3:
    write_raw(value);
    return;
  case 0xd4:
  case 0xd5:
  case 0xd6:
  case 0xd7:
  case 0xd8:
  case 0xd9: {
    u32 &address = reg < 0xd6 ? state_.start : reg < 0xd8 ? state_.end : state_.current;
    address = (reg & 1) ? (address & 0xffff0000) | value : (address & 0xffff) | (u32(value) << 16);
    address &= reg < 0xd8 ? 0x3fffffff : 0xbfffffff;
    return;
  }
  case 0xda:
    state_.predictor = value & 127;
    return;
  case 0xdb:
    state_.history1 = s16(value);
    return;
  case 0xdc:
    state_.history2 = s16(value);
    state_.stopped = false;
    return;
  case 0xde:
    state_.gain = s16(value);
    return;
  case 0xdf:
    state_.input = value;
    return;
  default:
    throw std::invalid_argument("Unknown DSP sample register write");
  }
}
} // namespace cupid::dsp
