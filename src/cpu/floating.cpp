// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/floating.hpp"
#include "cupid/cpu/interpreter.hpp"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace cupid::cpu {
namespace {
class HostRounding {
public:
  explicit HostRounding(u32 fpscr) : saved_(std::fegetround()) {
    constexpr int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    bool matching = saved_ == modes[fpscr & 3];
#if defined(_M_X64) || defined(__x86_64__)
    constexpr unsigned simd_modes[] = {0u, 0x6000u, 0x4000u, 0x2000u};
    simd_saved_ = _mm_getcsr();
    matching = matching && (simd_saved_ & 0x6000u) == simd_modes[fpscr & 3];
    // C libraries can report an unrecognized mode for split x87/SIMD controls.
    // Preserve that complete environment before asking C to change rounding.
    if (std::find(std::begin(modes), std::end(modes), saved_) == std::end(modes)) {
      if (std::fegetenv(&split_saved_) != 0)
        throw std::runtime_error("Host floating environment is unavailable");
      split_saved_valid_ = true;
    }
#endif
    changed_ = !matching;
    if (changed_ && std::fesetround(modes[fpscr & 3]) != 0)
      throw std::runtime_error("Host floating rounding mode is unavailable");
#if defined(_M_X64) || defined(__x86_64__)
    // Guest arithmetic masks host traps and keeps subnormal inputs. Only NI
    // selects output flushing; incoming host FTZ/DAZ must not change the guest.
    const unsigned desired =
        (simd_saved_ & ~0xffc0u) | 0x1f80u | simd_modes[fpscr & 3] | ((fpscr & 4) ? 0x8000u : 0u);
    if (_mm_getcsr() != desired)
      _mm_setcsr(desired);
#endif
  }

  ~HostRounding() {
#if defined(_M_X64) || defined(__x86_64__)
    if (split_saved_valid_)
      std::fesetenv(&split_saved_);
    else if (changed_)
      std::fesetround(saved_);
    _mm_setcsr(simd_saved_);
#else
    if (changed_)
      std::fesetround(saved_);
#endif
  }

private:
  int saved_;
  bool changed_ = false;
#if defined(_M_X64) || defined(__x86_64__)
  unsigned simd_saved_ = 0;
  std::fenv_t split_saved_{};
  bool split_saved_valid_ = false;
#endif
};

constexpr u32 invalid_mask = 0x01f80700, exception_mask = 0x1ff80700;
constexpr u32 fx = 0x80000000, vx = 0x20000000, fex = 0x40000000;
constexpr u32 snan_flag = 0x01000000, compare_flag = 0x00080000;

bool signaling(u64 bits) {
  return (bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0xfffffffffffffull) &&
         !(bits & 0x8000000000000ull);
}

void summary(State &state, bool &checked) {
  checked = true;
  state.fpscr = (state.fpscr & ~(vx | fex)) | ((state.fpscr & invalid_mask) ? vx : 0);
  if (((state.fpscr >> 22) & state.fpscr & 0xf8) != 0)
    state.fpscr |= fex;
}

void signal(State &state, bool &checked, u32 flags) {
  if ((state.fpscr & flags) != flags)
    state.fpscr |= fx;
  state.fpscr |= flags;
  summary(state, checked);
}

void inexact(State &state, bool &checked, bool value) {
  if (value)
    signal(state, checked, 0x02000000);
  state.fpscr = (state.fpscr & ~0x20000u) | (value ? 0x20000u : 0);
}

void classify(State &state, double value, bool single) {
  const bool negative = std::signbit(value);
  u32 category;
  const int type = single ? std::fpclassify(static_cast<float>(value)) : std::fpclassify(value);
  switch (type) {
  case FP_NAN:
    category = 0x11;
    break;
  case FP_INFINITE:
    category = negative ? 9 : 5;
    break;
  case FP_ZERO:
    category = negative ? 0x12 : 2;
    break;
  case FP_SUBNORMAL:
    category = negative ? 0x18 : 0x14;
    break;
  default:
    category = negative ? 8 : 4;
    break;
  }
  state.fpscr = (state.fpscr & ~0x1f000u) | (category << 12);
}

double rounded_single(const State &state, double value) {
  const u64 magnitude = std::bit_cast<u64>(value) & 0x7fffffffffffffffull;
  if ((state.fpscr & 4) && magnitude < 0x3810000000000000ull)
    return std::copysign(0.0, value);
  return static_cast<double>(static_cast<float>(value));
}

double rounded_double(const State &state, double value) {
  if ((state.fpscr & 4) && std::fpclassify(value) == FP_SUBNORMAL)
    return std::copysign(0.0, value);
  return value;
}

double round_multiplier(double value) {
  u64 bits = std::bit_cast<u64>(value);
  unsigned discard = 28;
  const u64 fraction = bits & 0xfffffffffffffull;
  if (!(bits & 0x7ff0000000000000ull) && fraction) {
    const unsigned top = 63 - static_cast<unsigned>(std::countl_zero(fraction));
    discard = top > 24 ? top - 24 : 0;
  }
  if (!discard)
    return value;
  const u64 low_mask = (1ull << discard) - 1;
  const u64 half = 1ull << (discard - 1);
  bits = (bits & ~low_mask) + ((bits & half) ? (1ull << discard) : 0);
  return std::bit_cast<double>(bits);
}

struct ArithmeticResult {
  double value;
  u32 flags;
};

ArithmeticResult calculate(State &state, bool &checked, unsigned operation, double a, double b,
                           double c = 0, bool single = false) {
  const bool fused = operation >= 28;
  double result;
  switch (operation) {
  case 18:
    result = a / b;
    break;
  case 20:
    result = a - b;
    break;
  case 21:
    result = a + b;
    break;
  case 25:
    result = a * c;
    break;
  case 28:
  case 30:
    result =
        single ? single_fused_intermediate(a, round_multiplier(c), b, true) : std::fma(a, c, -b);
    break;
  case 29:
  case 31:
    result =
        single ? single_fused_intermediate(a, round_multiplier(c), b, false) : std::fma(a, c, b);
    break;
  default:
    throw std::logic_error("Invalid FP arithmetic operation");
  }
  if (!single && (state.fpscr & 4) &&
      (std::bit_cast<u64>(result) & 0x7fffffffffffffffull) == 0x0010000000000000ull &&
      binary64_arithmetic_is_tiny(operation, a, b, c, state.fpscr & 3, std::signbit(result)))
    result = std::copysign(0.0, result);
  u32 flags = 0;
  if (std::isnan(result)) {
    const double second = operation == 25 ? c : b;
    if (signaling(std::bit_cast<u64>(a)) || signaling(std::bit_cast<u64>(second)) ||
        (fused && signaling(std::bit_cast<u64>(c))))
      flags |= snan_flag;
    state.fpscr &= ~0x60000u;
    if (std::isnan(a))
      result = std::bit_cast<double>(std::bit_cast<u64>(a) | 0x8000000000000ull);
    else if (std::isnan(second))
      result = std::bit_cast<double>(std::bit_cast<u64>(second) | 0x8000000000000ull);
    else if (fused && std::isnan(c))
      result = std::bit_cast<double>(std::bit_cast<u64>(c) | 0x8000000000000ull);
    else {
      result = std::bit_cast<double>(0x7ff8000000000000ull);
      if (operation == 18)
        flags |= b == 0 ? 0x00200000 : 0x00400000;
      else if (operation == 25 || (fused && std::isnan(a * c)))
        flags |= 0x00100000;
      else
        flags |= 0x00800000;
    }
  } else if (operation == 18 && std::isinf(result) && b == 0)
    flags |= 0x04000000;
  else if ((operation == 20 || operation == 21 || fused) &&
           (std::isinf(a) || std::isinf(b) || (fused && std::isinf(c))))
    state.fpscr &= ~0x60000u;
  if (flags)
    signal(state, checked, flags);
  return {result, flags};
}

u32 floating_compare(State &state, bool &checked, u64 a_bits, u64 b_bits, bool ordered) {
  const double a = std::bit_cast<double>(a_bits), b = std::bit_cast<double>(b_bits);
  u32 result;
  if (std::isnan(a) || std::isnan(b)) {
    result = 1;
    const bool snan = signaling(a_bits) || signaling(b_bits);
    if (snan)
      signal(state, checked, snan_flag);
    if (ordered && (!snan || !(state.fpscr & 0x80)))
      signal(state, checked, compare_flag);
  } else
    result = a == b ? 2 : a < b ? 8 : 4;
  state.fpscr = (state.fpscr & ~0xf000u) | (result << 12);
  return result;
}

double nearest_integer(double value) {
  const double floor = std::floor(value), remainder = value - floor;
  if (remainder < 0.5)
    return floor;
  if (remainder > 0.5)
    return floor + 1;
  return std::fmod(floor, 2) == 0 ? floor : floor + 1;
}
} // namespace

void Interpreter::execute_float(u32 opcode) {
  if (!(state_.msr & msr::fp)) {
    exception(0x800, state_.pc);
    throw GuestFault{};
  }
  const HostRounding rounding(state_.fpscr);
  const bool single = (opcode >> 26) == 59, rc = opcode & 1;
  const unsigned d = (opcode >> 21) & 31, a = (opcode >> 16) & 31, b = (opcode >> 11) & 31,
                 c = (opcode >> 6) & 31;
  const unsigned xo = (opcode >> 1) & 1023, low = xo & 31;
  const u64 b_bits = state_.fpr[b].bits[0];
  const double av = state_.fpr[a].value(), bv = state_.fpr[b].value(), cv = state_.fpr[c].value();
  bool completed = false, checked = false;
  if (!single) {
    switch (xo) {
    case 0:
    case 32:
      field((opcode >> 23) & 7,
            floating_compare(state_, checked, state_.fpr[a].bits[0], b_bits, xo == 32));
      completed = true;
      break;
    case 40:
      state_.fpr[d].bits[0] = b_bits ^ 0x8000000000000000ull;
      completed = true;
      break;
    case 72:
      state_.fpr[d].bits[0] = b_bits;
      completed = true;
      break;
    case 136:
      state_.fpr[d].bits[0] = b_bits | 0x8000000000000000ull;
      completed = true;
      break;
    case 264:
      state_.fpr[d].bits[0] = b_bits & 0x7fffffffffffffffull;
      completed = true;
      break;
    case 583:
      state_.fpr[d].bits[0] = 0xfff8000000000000ull | state_.fpscr;
      completed = true;
      break;
    case 38:
    case 70: {
      const u32 mask = 0x80000000u >> d;
      if (xo == 38 && (mask & exception_mask))
        signal(state_, checked, mask);
      else if (xo == 38)
        state_.fpscr |= mask;
      else
        state_.fpscr &= ~mask;
      summary(state_, checked);
      completed = true;
      break;
    }
    case 134: {
      const unsigned shift = 28 - ((opcode >> 23) & 7) * 4;
      state_.fpscr = (state_.fpscr & ~(15u << shift)) | (((opcode >> 12) & 15) << shift);
      summary(state_, checked);
      completed = true;
      break;
    }
    case 711: {
      const unsigned fields = (opcode >> 17) & 255;
      for (unsigned n = 0; n < 8; ++n)
        if (fields & (0x80u >> n)) {
          const unsigned shift = 28 - n * 4;
          state_.fpscr =
              (state_.fpscr & ~(15u << shift)) | (static_cast<u32>(b_bits) & (15u << shift));
        }
      summary(state_, checked);
      completed = true;
      break;
    }
    case 64: {
      const unsigned shift = 28 - ((opcode >> 18) & 7) * 4;
      field((opcode >> 23) & 7, state_.fpscr >> shift);
      state_.fpscr &= ~((15u << shift) & (fx | exception_mask));
      summary(state_, checked);
      completed = true;
      break;
    }
    case 14:
    case 15: {
      const unsigned mode = xo == 15 ? 1 : state_.fpscr & 3;
      const double rounded = mode == 1   ? std::trunc(bv)
                             : mode == 2 ? std::ceil(bv)
                             : mode == 3 ? std::floor(bv)
                                         : nearest_integer(bv);
      u32 value;
      const bool invalid = std::isnan(bv) || rounded >= 2147483648.0 || rounded < -2147483648.0;
      if (invalid) {
        value = std::isnan(bv) || rounded < 0 ? 0x80000000 : 0x7fffffff;
        if (signaling(b_bits))
          signal(state_, checked, snan_flag);
        signal(state_, checked, 0x100);
        state_.fpscr &= ~0x60000u;
      } else {
        value = static_cast<u32>(static_cast<s32>(rounded));
        inexact(state_, checked, rounded != bv);
        state_.fpscr =
            (state_.fpscr & ~0x40000u) | (std::fabs(rounded) > std::fabs(bv) ? 0x40000u : 0);
      }
      if (!invalid || !(state_.fpscr & 0x80))
        state_.fpr[d].bits[0] =
            0xfff8000000000000ull | value | (value == 0 && std::signbit(bv) ? 0x100000000ull : 0);
      completed = true;
      break;
    }
    case 12: {
      const double result = rounded_single(state_, bv);
      if (std::isnan(bv)) {
        const bool invalid = signaling(b_bits);
        if (invalid)
          signal(state_, checked, snan_flag);
        if (!invalid || !(state_.fpscr & 0x80)) {
          state_.fpr[d].set(result);
          state_.fpr[d].set(result, 1);
          classify(state_, result, true);
        }
        state_.fpscr &= ~0x60000u;
      } else {
        inexact(state_, checked, result != bv);
        state_.fpscr =
            (state_.fpscr & ~0x40000u) | (std::fabs(result) > std::fabs(bv) ? 0x40000u : 0);
        state_.fpr[d].set(result);
        state_.fpr[d].set(result, 1);
        classify(state_, result, true);
      }
      completed = true;
      break;
    }
    default:
      break;
    }
  }
  if (!completed && low == 23 && !single) {
    state_.fpr[d].bits[0] = av >= 0 ? state_.fpr[c].bits[0] : b_bits;
    completed = true;
  }
  if (!completed && ((single && low == 24) || (!single && low == 26))) {
    const bool square_root = low == 26;
    u32 flags = 0;
    if (square_root && bv < 0)
      flags = 0x200;
    else if (bv == 0)
      flags = 0x04000000;
    else if (signaling(b_bits))
      flags = snan_flag;
    if (flags)
      signal(state_, checked, flags);
    if (flags || std::isnan(bv) || std::isinf(bv))
      state_.fpscr &= ~0x60000u;
    const bool inhibited = ((flags & invalid_mask) && (state_.fpscr & 0x80)) ||
                           ((flags & 0x04000000) && (state_.fpscr & 0x10));
    if (!inhibited) {
      const double value =
          square_root ? reciprocal_square_root_estimate(bv) : reciprocal_estimate(bv);
      state_.fpr[d].set(value);
      if (!square_root)
        state_.fpr[d].set(value, 1);
      classify(state_, value, !square_root);
    }
    completed = true;
  }
  if (!completed) {
    if (low != 18 && low != 20 && low != 21 && low != 25 && low < 28)
      program(0x80000);
    const auto result = calculate(state_, checked, low, av, bv,
                                  single && low == 25 ? round_multiplier(cv) : cv, single);
    const bool inhibited = ((result.flags & invalid_mask) && (state_.fpscr & 0x80)) ||
                           ((result.flags & 0x04000000) && (state_.fpscr & 0x10));
    if (!inhibited) {
      double value =
          single ? rounded_single(state_, result.value) : rounded_double(state_, result.value);
      if (low == 30 || low == 31) {
        if (!std::isnan(value))
          value = -value;
      }
      state_.fpr[d].set(value);
      if (single)
        state_.fpr[d].set(value, 1);
      if (low == 25)
        state_.fpscr &= ~0x60000u;
      if (single && low == 29)
        state_.fpscr = (state_.fpscr & ~0x60000u) | (result.value != value ? 0x20000u : 0);
      classify(state_, value, single);
    }
  }
  if (rc)
    field(1, state_.fpscr >> 28);
  if (checked && (state_.fpscr & fex) && (state_.msr & 0x900))
    program(0x100000);
}

void Interpreter::execute_paired(u32 opcode) {
  if (!(state_.spr[spr::hid2] & 0x20000000))
    program(0x80000);
  const unsigned xo = (opcode >> 1) & 1023, low = xo & 31, quant = xo & 63;
  if (quant == 6 || quant == 7 || quant == 38 || quant == 39) {
    quantized(opcode, true);
    return;
  }
  const unsigned d = (opcode >> 21) & 31, a = (opcode >> 16) & 31, b = (opcode >> 11) & 31,
                 c = (opcode >> 6) & 31;
  if (xo == 1014) {
    if (!(state_.spr[spr::hid2] & 0x10000000))
      program(0x80000);
    const u32 ea = (a ? state_.gpr[a] : 0) + state_.gpr[b];
    if (!(state_.spr[spr::hid0] & 0x4000))
      alignment(ea);
    clear_cache_line(ea);
    return;
  }
  if (!(state_.msr & msr::fp)) {
    exception(0x800, state_.pc);
    throw GuestFault{};
  }
  const HostRounding rounding(state_.fpscr);
  const auto aa = state_.fpr[a], bb = state_.fpr[b], cc = state_.fpr[c];
  bool completed = true, checked = false;
  switch (xo) {
  case 0:
  case 32:
  case 64:
  case 96: {
    const unsigned lane = xo >= 64 ? 1 : 0;
    field((opcode >> 23) & 7,
          floating_compare(state_, checked, aa.bits[lane], bb.bits[lane], (xo & 32) != 0));
    break;
  }
  case 40:
  case 72:
  case 136:
  case 264:
    for (unsigned n = 0; n < 2; ++n)
      state_.fpr[d].bits[n] = xo == 40    ? bb.bits[n] ^ 0x8000000000000000ull
                              : xo == 136 ? bb.bits[n] | 0x8000000000000000ull
                              : xo == 264 ? bb.bits[n] & 0x7fffffffffffffffull
                                          : bb.bits[n];
    break;
  case 528:
  case 560:
  case 592:
  case 624:
    state_.fpr[d].bits = {aa.bits[(xo - 528) / 64], bb.bits[((xo - 528) / 32) & 1]};
    break;
  default:
    completed = false;
    break;
  }
  if (!completed && low == 23) {
    for (unsigned n = 0; n < 2; ++n)
      state_.fpr[d].bits[n] = aa.value(n) >= 0 ? cc.bits[n] : bb.bits[n];
    completed = true;
  }
  if (!completed && (low == 24 || low == 26)) {
    const bool square_root = low == 26;
    const double first = bb.value(), second = bb.value(1);
    if (first == 0 || second == 0) {
      signal(state_, checked, 0x04000000);
      state_.fpscr &= ~0x60000u;
    }
    if (std::isnan(first) || std::isnan(second) || std::isinf(first) || std::isinf(second))
      state_.fpscr &= ~0x60000u;
    if (square_root && (first < 0 || second < 0)) {
      signal(state_, checked, 0x200);
      state_.fpscr &= ~0x60000u;
    }
    if (signaling(bb.bits[0]) || signaling(bb.bits[1]))
      signal(state_, checked, snan_flag);
    for (unsigned n = 0; n < 2; ++n) {
      const double estimate =
          square_root ? rounded_single(state_, reciprocal_square_root_estimate(bb.value(n)))
                      : reciprocal_estimate(bb.value(n));
      state_.fpr[d].set(estimate, n);
    }
    classify(state_, state_.fpr[d].value(), true);
    completed = true;
  }
  if (!completed) {
    if (low != 10 && low != 11 && low != 12 && low != 13 && low != 18 && low != 20 && low != 21 &&
        low != 25 && low != 14 && low != 15 && low < 28)
      program(0x80000);
    double values[2]{};
    for (unsigned n = 0; n < 2; ++n) {
      if (low == 10 || low == 11) {
        const double sum =
            rounded_single(state_, calculate(state_, checked, 21, aa.value(0), bb.value(1)).value);
        values[n] = n == (low == 10 ? 0u : 1u) ? sum : rounded_single(state_, cc.value(n));
      } else {
        const unsigned mul_lane = low == 12 || low == 14 ? 0 : low == 13 || low == 15 ? 1 : n;
        const unsigned operation = low == 12 || low == 13 ? 25 : low == 14 || low == 15 ? 29 : low;
        const double multiplier =
            operation >= 28 ? cc.value(mul_lane) : round_multiplier(cc.value(mul_lane));
        values[n] = rounded_single(state_, calculate(state_, checked, operation, aa.value(n),
                                                     bb.value(n), multiplier, true)
                                               .value);
        if ((low == 30 || low == 31) && !std::isnan(values[n]))
          values[n] = -values[n];
      }
    }
    state_.fpr[d].set(values[0]);
    state_.fpr[d].set(values[1], 1);
    classify(state_, low == 11 ? values[1] : values[0], true);
  }
  if (opcode & 1)
    field(1, state_.fpscr >> 28);
  if (checked && (state_.fpscr & fex) && (state_.msr & 0x900))
    program(0x100000);
}

void Interpreter::quantized(u32 opcode, bool indexed) {
  const u32 hid = state_.spr[spr::hid2];
  if (!(hid & 0x20000000) || (!indexed && !(hid & 0x80000000)))
    program(0x80000);
  if (!(state_.msr & msr::fp)) {
    exception(0x800, state_.pc);
    throw GuestFault{};
  }
  const HostRounding rounding(state_.fpscr);
  const unsigned primary = opcode >> 26, sub = (opcode >> 1) & 63;
  const bool read = indexed ? (sub == 6 || sub == 38) : primary == 56 || primary == 57;
  const bool update = indexed ? sub >= 32 : (primary & 1) != 0;
  const unsigned d = (opcode >> 21) & 31, a = (opcode >> 16) & 31, b = (opcode >> 11) & 31;
  const unsigned index = indexed ? (opcode >> 7) & 7 : (opcode >> 12) & 7;
  const bool scalar = indexed ? (opcode & 0x400) != 0 : (opcode & 0x8000) != 0;
  const u32 offset =
      indexed ? state_.gpr[b] : static_cast<u32>(static_cast<s32>((opcode & 4095) << 20) >> 20);
  const u32 address = ((a || update) ? state_.gpr[a] : 0) + offset;
  const u32 gqr = state_.spr[912 + index];
  const unsigned type = read ? (gqr >> 16) & 7 : gqr & 7;
  const unsigned encoded_scale = read ? (gqr >> 24) & 63 : (gqr >> 8) & 63;
  const int scale =
      encoded_scale >= 32 ? static_cast<int>(encoded_scale) - 64 : static_cast<int>(encoded_scale);
  if (type >= 1 && type <= 3)
    unsupported("Reserved quantization format");
  const unsigned width = type == 0 ? 4 : type == 4 || type == 6 ? 1 : 2;
  const unsigned lanes = scalar ? 1 : 2;
  if (read) {
    const u64 packed = load(address, width * lanes);
    u64 bits[2]{0, std::bit_cast<u64>(1.0)};
    for (unsigned lane = 0; lane < lanes; ++lane) {
      const u32 raw = static_cast<u32>(packed >> ((lanes - 1 - lane) * width * 8)) &
                      (width == 4 ? 0xffffffffu : (1u << (width * 8)) - 1);
      if (type == 0)
        bits[lane] = binary32_to_binary64(raw);
      else {
        const s32 integer = type == 6   ? static_cast<s8>(raw)
                            : type == 7 ? static_cast<s16>(raw)
                                        : static_cast<s32>(raw);
        bits[lane] = std::bit_cast<u64>(
            static_cast<double>(std::ldexp(static_cast<float>(integer), -scale)));
      }
    }
    state_.fpr[d].bits = {bits[0], bits[1]};
  } else {
    u64 packed = 0;
    for (unsigned lane = 0; lane < lanes; ++lane) {
      u32 raw;
      if (type == 0)
        raw = binary64_to_binary32_store(state_.fpr[d].bits[lane], true);
      else {
        const double value = state_.fpr[d].value(lane);
        if (std::isnan(value))
          unsupported("NaN integer quantization is not implemented");
        const float scaled = std::ldexp(static_cast<float>(value), scale);
        const float minimum = type == 6 ? -128.0f : type == 7 ? -32768.0f : 0.0f;
        const float maximum = type == 4   ? 255.0f
                              : type == 5 ? 65535.0f
                              : type == 6 ? 127.0f
                                          : 32767.0f;
        raw = static_cast<u32>(static_cast<s32>(std::clamp(scaled, minimum, maximum)));
      }
      packed = (packed << (width * 8)) | (width == 4 ? raw : raw & ((1u << (width * 8)) - 1));
    }
    store(address, packed, width * lanes);
  }
  if (update)
    state_.gpr[a] = address;
}
} // namespace cupid::cpu
