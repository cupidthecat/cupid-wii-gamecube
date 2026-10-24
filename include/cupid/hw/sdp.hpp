// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/types.hpp"
#include <span>
#include <stdexcept>
#include <vector>

namespace cupid::hw {
class SdpError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// The PDU includes its big-endian transaction and parameter-length fields.
// Unsupported operations and malformed requests raise SdpError.
std::vector<u8> sdp_response(std::span<const u8> request);
} // namespace cupid::hw
