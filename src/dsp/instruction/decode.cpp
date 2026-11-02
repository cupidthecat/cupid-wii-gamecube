// SPDX-License-Identifier: GPL-2.0-or-later
#include "operations.hpp"

namespace cupid::dsp::detail {
const Decoded &decode(u16 instruction) {
  static const auto table = [] {
    std::array<Decoded, 65536> result{};
    for (unsigned word = 0; word < result.size(); ++word)
      for (const auto &encoding : primary_encodings)
        if ((word & encoding.mask) == encoding.value) {
          result[word] = {encoding.operation, encoding.words, encoding.extension};
          break;
        }
    return result;
  }();
  return table[instruction];
}

Operation decode_extension(u16 instruction) {
  static const auto table = [] {
    std::array<Operation, 256> result{};
    result.fill(Operation::nop_ext);
    for (unsigned word = 0; word < result.size(); ++word)
      for (const auto &encoding : extension_encodings)
        if ((word & encoding.mask) == encoding.value) {
          result[word] = encoding.operation;
          break;
        }
    return result;
  }();
  return table[instruction & ((instruction >> 12) == 3 ? 127 : 255)];
}
} // namespace cupid::dsp::detail
