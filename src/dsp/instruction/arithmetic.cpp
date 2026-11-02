// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/instruction.hpp"
#include "operations.hpp"
#include <limits>
#include <stdexcept>

namespace cupid::dsp {
namespace {
s64 normalize(u64 value) {
  return s64((value & 0xffffffffffull) ^ 0x8000000000ull) - s64(0x8000000000ull);
}

int signed_shift(u16 value) {
  const int magnitude = value & 63;
  return magnitude == 0 ? 0 : magnitude - ((value & 64) ? 64 : 0);
}
} // namespace

void InstructionCore::execute_arithmetic(detail::Operation operation, u16 word) {
  using O = detail::Operation;
  auto &r = state_.registers;
  const unsigned destination = (word >> 8) & 1;
  const auto outside32 = [](s64 value) {
    return value < std::numeric_limits<s32>::min() || value > std::numeric_limits<s32>::max();
  };
  switch (operation) {
  case O::clr:
    accumulator((word >> 11) & 1, 0);
    status40(0);
    return;
  case O::tst:
    status40(accumulator((word >> 11) & 1));
    return;
  case O::tstaxh:
    status16(s16(r[26 + destination]), false);
    return;
  case O::andf:
  case O::andcf: {
    const u16 immediate = fetch();
    logic_zero((r[30 + destination] & immediate) == (operation == O::andf ? 0 : immediate));
    return;
  }
  case O::cmp:
  case O::cmpaxh:
  case O::cmpi:
  case O::cmpis: {
    const s64 left = accumulator(operation == O::cmp      ? 0
                                 : operation == O::cmpaxh ? ((word >> 11) & 1)
                                                          : destination);
    const s64 right = operation == O::cmp     ? accumulator(1)
                      : operation == O::cmpi  ? s64(s16(fetch())) * 65536
                      : operation == O::cmpis ? s64(s8(word)) * 65536
                                              : s64(s16(r[26 + ((word >> 12) & 1)])) * 65536;
    arithmetic_status(left, right, normalize(u64(left - right)), true);
    return;
  }
  case O::xorr:
  case O::andr:
  case O::orr:
  case O::xorc:
  case O::andc:
  case O::orc:
  case O::notc:
  case O::xori:
  case O::andi:
  case O::ori: {
    u16 operand;
    if (operation == O::xori || operation == O::andi || operation == O::ori)
      operand = fetch();
    else if (operation == O::xorr || operation == O::andr || operation == O::orr)
      operand = r[26 + ((word >> 9) & 1)];
    else if (operation == O::notc)
      operand = 0xffff;
    else
      operand = r[30 + (destination ^ 1)];
    auto &middle = r[30 + destination];
    if (operation == O::andr || operation == O::andc || operation == O::andi)
      middle &= operand;
    else if (operation == O::orr || operation == O::orc || operation == O::ori)
      middle |= operand;
    else
      middle ^= operand;
    status16(s16(middle), outside32(accumulator(destination)));
    return;
  }
  case O::addr:
  case O::subr:
  case O::addax:
  case O::subax:
  case O::add:
  case O::sub:
  case O::addp:
  case O::subp:
  case O::addaxl:
  case O::addi:
  case O::addis:
  case O::incm:
  case O::inc:
  case O::decm:
  case O::dec:
  case O::neg: {
    s64 left = accumulator(destination), right = 0;
    const bool subtract = operation == O::subr || operation == O::subax || operation == O::sub ||
                          operation == O::subp || operation == O::decm || operation == O::dec ||
                          operation == O::neg;
    if (operation == O::addr || operation == O::subr)
      right = s64(s16(r[24 + ((word >> 9) & 3)])) * 65536;
    else if (operation == O::addax || operation == O::subax)
      right = auxiliary((word >> 9) & 1);
    else if (operation == O::add || operation == O::sub)
      right = accumulator(destination ^ 1);
    else if (operation == O::addp || operation == O::subp)
      right = product();
    else if (operation == O::addaxl)
      right = r[24 + ((word >> 9) & 1)];
    else if (operation == O::addi)
      right = s64(s16(fetch())) * 65536;
    else if (operation == O::addis)
      right = s64(s8(word)) * 65536;
    else if (operation == O::incm || operation == O::decm)
      right = 65536;
    else if (operation == O::neg) {
      right = left;
      left = 0;
    } else {
      right = 1;
    }
    accumulator(destination, subtract ? left - right : left + right);
    arithmetic_status(left, right, accumulator(destination), subtract);
    return;
  }
  case O::abs: {
    const unsigned target = (word >> 11) & 1;
    const s64 value = accumulator(target);
    accumulator(target, value < 0 ? -value : value);
    status40(accumulator(target));
    return;
  }
  case O::movr:
  case O::movax:
  case O::mov:
  case O::movp:
  case O::movnp:
  case O::movpz:
  case O::clrl: {
    s64 value;
    if (operation == O::movr)
      value = s64(s16(r[24 + ((word >> 9) & 3)])) * 65536;
    else if (operation == O::movax)
      value = auxiliary((word >> 9) & 1);
    else if (operation == O::mov)
      value = accumulator(destination ^ 1);
    else if (operation == O::movpz)
      value = rounded_product();
    else if (operation == O::clrl) {
      value = accumulator(destination);
      value = (value + ((value & 0x10000) ? 0x8000 : 0x7fff)) & ~s64(0xffff);
    } else {
      value = operation == O::movnp ? -product() : product();
    }
    accumulator(destination, value);
    // These callers update flags from the untruncated result, including the
    // rounding boundary, before any parallel extension replaces registers.
    status40(value);
    return;
  }
  case O::lsl:
  case O::asl:
  case O::lsr:
  case O::asr:
  case O::lsl16:
  case O::lsr16:
  case O::asr16:
  case O::lsrn:
  case O::asrn:
  case O::lsrnrx:
  case O::asrnrx:
  case O::lsrnr:
  case O::asrnr: {
    const unsigned target = operation == O::lsrn || operation == O::asrn ? 0
                            : operation == O::asr16                      ? ((word >> 11) & 1)
                                                                         : destination;
    const bool arithmetic = operation == O::asr || operation == O::asr16 || operation == O::asrn ||
                            operation == O::asrnrx || operation == O::asrnr;
    int shift = 0;
    if (operation == O::lsl || operation == O::asl)
      shift = word & 63;
    else if (operation == O::lsr || operation == O::asr)
      shift = (word & 63) ? int(word & 63) - 64 : 0;
    else if (operation == O::lsl16)
      shift = 16;
    else if (operation == O::lsr16 || operation == O::asr16)
      shift = -16;
    else if (operation == O::lsrn || operation == O::asrn)
      shift = -signed_shift(r[31]);
    else if (operation == O::lsrnrx || operation == O::asrnrx)
      shift = signed_shift(r[26 + ((word >> 9) & 1)]);
    else
      shift = signed_shift(r[30 + (target ^ 1)]);
    const s64 original = accumulator(target);
    u64 value = u64(original);
    if (!arithmetic)
      value &= 0xffffffffffull;
    if (shift > 0)
      value <<= shift;
    else if (shift < 0)
      value = arithmetic ? u64(original >> -shift) : value >> -shift;
    accumulator(target, normalize(value));
    status40(accumulator(target));
    return;
  }
  case O::clrp:
    r[20] = 0;
    r[21] = 0xfff0;
    r[22] = 255;
    r[23] = 16;
    return;
  case O::tstprod:
    status40(product());
    return;
  case O::addpaxz: {
    const s64 before = product();
    const s64 value = rounded_product() + (s64(auxiliary((word >> 9) & 1)) & ~s64(0xffff));
    accumulator(destination, value);
    status40(accumulator(destination), u64(before) > u64(accumulator(destination)));
    return;
  }
  default:
    break;
  }

  // Multiplier families share operand selection, while accumulator updates
  // consume the old product before the newly computed product is published.
  const bool cross = operation == O::mulx || operation == O::mulxac || operation == O::mulxmv ||
                     operation == O::mulxmvz;
  const bool mixed_acc = operation == O::mulc || operation == O::mulcac || operation == O::mulcmv ||
                         operation == O::mulcmvz;
  const bool simple = operation == O::mul || operation == O::mulac || operation == O::mulmv ||
                      operation == O::mulmvz;
  if (cross || mixed_acc || simple || operation == O::mulaxh) {
    u16 left, right;
    unsigned sign = 0;
    if (cross) {
      const unsigned high_left = (word >> 12) & 1, high_right = (word >> 11) & 1;
      left = r[24 + 2 * high_left];
      right = r[25 + 2 * high_right];
      if (!high_left && !high_right)
        sign = 1;
      else if (high_left != high_right) {
        sign = 2;
        if (high_left)
          std::swap(left, right);
      }
    } else if (mixed_acc) {
      left = r[30 + ((word >> 12) & 1)];
      right = r[26 + ((word >> 11) & 1)];
    } else if (simple) {
      left = r[24 + ((word >> 11) & 1)];
      right = r[26 + ((word >> 11) & 1)];
    } else {
      left = right = r[26];
    }
    const bool add = operation == O::mulac || operation == O::mulxac || operation == O::mulcac;
    const bool rounded =
        operation == O::mulmvz || operation == O::mulxmvz || operation == O::mulcmvz;
    const bool move = operation == O::mulmv || operation == O::mulxmv || operation == O::mulcmv;
    const s64 previous = rounded ? rounded_product() : product();
    const s64 accumulated = (add ? accumulator(destination) : 0) + previous;
    const s64 next = multiply(left, right, sign);
    product(next);
    if (add || rounded || move) {
      accumulator(destination, accumulated);
      status40(accumulator(destination));
    }
    return;
  }
  const bool subtract = operation == O::msub || operation == O::msubc || operation == O::msubx;
  if (operation == O::madd || operation == O::msub || operation == O::maddc ||
      operation == O::msubc || operation == O::maddx || operation == O::msubx) {
    u16 left, right;
    if (operation == O::maddx || operation == O::msubx) {
      left = r[24 + 2 * ((word >> 9) & 1)];
      right = r[25 + 2 * destination];
    } else if (operation == O::maddc || operation == O::msubc) {
      left = r[30 + ((word >> 9) & 1)];
      right = r[26 + destination];
    } else {
      left = r[24 + destination];
      right = r[26 + destination];
    }
    const s64 delta = multiply(left, right, 0);
    product(subtract ? product() - delta : product() + delta);
    return;
  }
  throw std::logic_error("DSP arithmetic opcode has no implementation");
}
} // namespace cupid::dsp
