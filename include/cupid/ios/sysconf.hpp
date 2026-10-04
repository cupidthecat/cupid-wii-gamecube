// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "cupid/core/types.hpp"
#include <span>
#include <stdexcept>
#include <vector>

namespace cupid::ios {
class SysconfError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// An empty input means the caller established that the file is absent.
// Existing malformed configurations fail without producing replacement bytes.
std::vector<u8> update_bluetooth_peers(std::span<const u8> existing);
} // namespace cupid::ios
