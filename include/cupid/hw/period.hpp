// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"

namespace cupid::hw::detail {

// Period is positive. A rate change can leave the old remainder larger than it.
constexpr u64 elapsed_periods(u64 amount, u64 period, u64 &remainder) noexcept {
  if (remainder < period && amount < period - remainder) {
    remainder += amount;
    return 0;
  }
  const u64 partial = remainder + amount % period;
  remainder = partial % period;
  return amount / period + partial / period;
}

} // namespace cupid::hw::detail
