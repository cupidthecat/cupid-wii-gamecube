// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in direct comparison of the encoded service-discovery records.
#include "Common/MsgHandler.h"
#include "Core/IOS/USB/Bluetooth/WiimoteHIDAttr.h"
#include "cupid/hw/sdp.hpp"
#include <algorithm>
#include <array>
#include <iostream>

namespace Common {

bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char *, int, fmt::string_view,
                     const fmt::format_args &) {
  throw std::runtime_error("Unexpected service discovery comparison diagnostic");
}

namespace Log {

void GenericLogFmtImpl(LogLevel, LogType, const char *, int, fmt::string_view,
                       const fmt::format_args &) {
  throw std::runtime_error("Unexpected service discovery comparison log");
}

} // namespace Log
} // namespace Common

int main() {
  using namespace cupid;
  u64 comparisons = 0, checked_bytes = 0;
  constexpr std::array<u16, 8> transactions{0, 1, 127, 255, 256, 0x1234, 0x8000, 0xffff};
  constexpr std::array<u16, 3> limits{118, 256, 65535};
  for (u16 transaction : transactions)
    for (u16 maximum : limits)
      for (u32 handle : {0x10000u, 0x10001u}) {
        u32 continuation = 0;
        do {
          std::vector<u8> request{4,
                                  u8(transaction >> 8),
                                  u8(transaction),
                                  0,
                                  0,
                                  u8(handle >> 24),
                                  u8(handle >> 16),
                                  u8(handle >> 8),
                                  u8(handle),
                                  u8(maximum >> 8),
                                  u8(maximum),
                                  0x35,
                                  5,
                                  0x0a,
                                  0,
                                  0,
                                  0xff,
                                  0xff};
          request.push_back(continuation ? 2 : 0);
          if (continuation) {
            request.push_back(u8(continuation >> 8));
            request.push_back(u8(continuation));
          }
          request[4] = static_cast<u8>(request.size() - 5);
          u32 packet_size = 0;
          const auto *packet = IOS::HLE::GetAttribPacket(handle, continuation, packet_size);
          if (!packet)
            throw std::runtime_error("Actual service discovery target rejected a defined offset");
          std::vector<u8> expected{5, u8(transaction >> 8), u8(transaction)};
          expected.insert(expected.end(), packet, packet + packet_size);
          const auto actual = hw::sdp_response(request);
          ++comparisons;
          checked_bytes += expected.size();
          if (actual != expected) {
            std::cerr << "SDP divergence handle=" << std::hex << handle
                      << " transaction=" << transaction << " continuation=" << continuation
                      << " maximum=" << maximum << " actual_size=" << std::dec << actual.size()
                      << " expected_size=" << expected.size() << '\n';
            for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i)
              if (actual[i] != expected[i]) {
                std::cerr << "First differing byte=" << i << " actual=" << unsigned(actual[i])
                          << " expected=" << unsigned(expected[i]) << '\n';
                break;
              }
            return 1;
          }
          const unsigned amount = (unsigned(actual[5]) << 8) | actual[6];
          continuation =
              actual[7 + amount] ? (unsigned(actual[8 + amount]) << 8) | actual[9 + amount] : 0;
        } while (continuation);
      }
  std::cout << "Passed " << comparisons << " SDP packet comparisons (" << checked_bytes
            << " bytes) across HID/identity records, transactions and continuations\n";
}
