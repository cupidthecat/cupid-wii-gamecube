// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/core/host_float_environment.hpp"
#include "floating_binary_test_support.hpp"
#include <array>
#include <cfenv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <immintrin.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
namespace host = floating_binary_test;
using cupid::HostFloatEnvironment;
using cupid::u64;
using cupid::u8;

struct Raw {
  unsigned control = 0, status = 0, simd = 0, fx_control = 0, fx_status = 0, fx_simd = 0;
  bool operator==(const Raw &) const = default;

  static Raw capture() {
    // Independent capture precedes C fenv queries, reset, allocation and output.
    alignas(16) std::array<u8, 512> area{};
#ifdef _MSC_VER
    _fxsave64(area.data());
#else
    asm volatile("fxsave64 %0" : "=m"(area));
#endif
    unsigned short control = 0, status = 0;
    std::memcpy(&control, area.data(), 2);
    std::memcpy(&status, area.data() + 2, 2);
    Raw result{host::fp_x87_control(), host::fp_x87_status(), _mm_getcsr(), control, status, 0};
    std::memcpy(&result.fx_simd, area.data() + 24, 4);
    return result;
  }

  bool consistent() const {
    return control == fx_control && status == fx_status && simd == fx_simd;
  }
};

struct Marker {};

struct StaticFatal : std::exception {
  const char *what() const noexcept override {
    return "boundary static";
  }
};

struct ThrowingConstructor {
  ThrowingConstructor() {
    throw Marker{};
  }
};

struct ThrowingCopy {
  ThrowingCopy() = default;

  ThrowingCopy(const ThrowingCopy &) {
    throw Marker{};
  }

  ThrowingCopy(ThrowingCopy &&) {
    throw Marker{};
  }
};

struct MoveOnly {
  int value;
  explicit MoveOnly(int v) : value(v){};
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&) = default;
};

void perturb() {
  host::fp_x87_set_control((host::fp_x87_control() & ~0xc00u) | 0x800u | 0x3fu);
  host::fp_x87_set_flags(5);
  _mm_setcsr(0xffe2);
}

struct Cleanup {
  unsigned &count;
  unsigned mode;

  ~Cleanup() noexcept(false) {
    ++count;
    if (mode == 1)
      perturb();
    if (mode == 2)
      throw Marker{};
  }
};

struct Forwarding {
  Raw &entry;

  int operator()() & {
    entry = Raw::capture();
    return 19;
  }

  int operator()() && {
    entry = Raw::capture();
    return 17;
  }
};

struct Reference {
  int &value;

  int &operator()() const {
    return value;
  }
};

static_assert(
    std::is_same_v<decltype(HostFloatEnvironment::run(std::declval<Reference>())), int &>);
static_assert(std::is_same_v<decltype(HostFloatEnvironment::run([] {})), void>);
constexpr unsigned masks[]{0, 0x3f, 0x3b, 0x3e, 0x15, 0x2a};
constexpr unsigned x87_modes[]{0, 0xc00, 0x800, 0x400};
constexpr unsigned simd_modes[]{0, 0x6000, 0x4000, 0x2000};
constexpr unsigned flushes[]{0, 0x40, 0x8000, 0x8040};
Raw defaults;
std::ofstream records;
u64 rows = 0, failures = 0, default_entries = 0, nested_entries = 0, identities = 0;

void raw(const Raw &v) {
  records << '[' << v.control << ',' << v.status << ',' << v.simd << ',' << v.fx_control << ','
          << v.fx_status << ',' << v.fx_simd << ']';
}

void row(unsigned xm, unsigned sm, unsigned rounding, unsigned flush, unsigned flags,
         unsigned path) {
  const std::runtime_error prebuilt("boundary");
  const ThrowingCopy copy_source;
  ThrowingCopy move_source;
  MoveOnly move_only(73);
  int reference = 41;
  unsigned cleanup = 0, caught = 0;
  bool value_ok = true, nested_ok = true;
  const void *identity = nullptr, *observed = nullptr;
  Raw entry{}, nested{}, nested_after{};
  Forwarding forwarding{entry};
  host::require(std::fesetenv(FE_DFL_ENV) == 0, "Initial boundary default unavailable");
  host::fp_x87_set_control((host::fp_x87_control() & ~0xc3fu) | x87_modes[rounding] | xm);
  host::fp_x87_set_flags(flags & xm);
  _mm_setcsr((sm << 7) | simd_modes[(rounding + 1) % 4] | flush | (flags & sm));
  const auto before = Raw::capture();
  const auto enter = [&] { entry = Raw::capture(); };
  try {
    switch (path) {
    case 0:
      HostFloatEnvironment::run([&] { enter(); });
      break;
    case 1:
      value_ok = HostFloatEnvironment::run([&] {
                   enter();
                   return 23;
                 }) == 23;
      break;
    case 2: {
      int &result = HostFloatEnvironment::run([&]() -> int & {
        enter();
        return reference;
      });
      value_ok = &result == &reference;
      result = 42;
      value_ok = value_ok && reference == 42;
      break;
    }
    case 3:
      value_ok = HostFloatEnvironment::run(std::move(forwarding)) == 17;
      break;
    case 4:
      HostFloatEnvironment::run([&] {
        enter();
        throw Marker{};
      });
      break;
    case 5:
      HostFloatEnvironment::run([&] {
        enter();
        throw std::runtime_error("boundary");
      });
      break;
    case 6:
      HostFloatEnvironment::run([&] {
        enter();
        throw 7;
      });
      break;
    case 7:
      HostFloatEnvironment::run([&] {
        enter();
        const ThrowingConstructor object;
      });
      break;
    case 8:
      HostFloatEnvironment::run([&] {
        enter();
        const Cleanup object{cleanup, 0};
        throw Marker{};
      });
      break;
    case 9:
      value_ok = HostFloatEnvironment::run([&] {
                   enter();
                   const auto value = HostFloatEnvironment::run([&] {
                     nested = Raw::capture();
                     return 98;
                   });
                   nested_after = Raw::capture();
                   return value;
                 }) == 98;
      break;
    case 10:
      HostFloatEnvironment::run([&] {
        enter();
        try {
          HostFloatEnvironment::run([&] {
            nested = Raw::capture();
            throw Marker{};
          });
        } catch (const Marker &) {
          nested_after = Raw::capture();
        }
      });
      break;
    case 11:
      HostFloatEnvironment::run([&] {
        enter();
        HostFloatEnvironment::run([&] {
          nested = Raw::capture();
          throw Marker{};
        });
      });
      break;
    case 12:
      HostFloatEnvironment::run([&] {
        enter();
        try {
          throw Marker{};
        } catch (const Marker &error) {
          identity = &error;
          throw;
        }
      });
      break;
    case 13:
      HostFloatEnvironment::run([&] {
        enter();
        try {
          throw prebuilt;
        } catch (const std::runtime_error &error) {
          identity = &error;
          throw;
        }
      });
      break;
    case 14:
      HostFloatEnvironment::run([&]() -> ThrowingCopy {
        enter();
        return copy_source;
      });
      break;
    case 15:
      HostFloatEnvironment::run([&]() -> ThrowingCopy {
        enter();
        return std::move(move_source);
      });
      break;
    case 16: {
      const auto result = HostFloatEnvironment::run([&] {
        enter();
        return std::move(move_only);
      });
      value_ok = result.value == 73;
      break;
    }
    case 17:
      value_ok = HostFloatEnvironment::run(forwarding) == 19;
      break;
    case 18:
      HostFloatEnvironment::run([&] {
        enter();
        const Cleanup object{cleanup, 2};
      });
      break;
    case 19:
      HostFloatEnvironment::run([&] {
        enter();
        const Cleanup object{cleanup, 1};
        throw Marker{};
      });
      break;
    case 20:
      HostFloatEnvironment::run([&] {
        enter();
        throw StaticFatal{};
      });
      break;
    }
  } catch (const Marker &error) {
    caught = 1;
    observed = &error;
  } catch (const std::runtime_error &error) {
    caught = 2;
    observed = &error;
  } catch (int value) {
    caught = value == 7 ? 3u : 9u;
  } catch (const StaticFatal &) {
    caught = 4;
  } catch (...) {
    caught = 9;
  }
  // First observation after return/unwind. Reset only after raw capture.
  const auto after = Raw::capture();
  const int reset = std::fesetenv(FE_DFL_ENV);
  _mm_setcsr(defaults.simd);
  const unsigned expected =
      path == 5 || path == 13 ? 2
      : path == 6             ? 3
      : path == 20            ? 4
                              : (path == 4 || path == 7 || path == 8 || path == 11 || path == 12 ||
                              path == 14 || path == 15 || path == 18 || path == 19
                                     ? 1
                                     : 0);
  if (path >= 9 && path <= 11) {
    nested_ok = nested.consistent() && nested == defaults &&
                (path == 11 || (nested_after.consistent() && nested_after == defaults));
    ++nested_entries;
  }
  const bool identity_ok = (path != 12 && path != 13) || (identity && identity == observed);
  if (path == 12 || path == 13)
    ++identities;
  const bool pass = reset == 0 && before.consistent() && entry.consistent() && after.consistent() &&
                    before == after && entry == defaults && value_ok && nested_ok && identity_ok &&
                    caught == expected &&
                    cleanup == (path == 8 || path == 18 || path == 19 ? 1u : 0u);
  ++rows;
  ++default_entries;
  failures += !pass;
  records << "{\"row\":" << rows << ",\"x87_mask\":" << xm << ",\"SSE_mask\":" << sm
          << ",\"rounding\":" << rounding << ",\"flush\":" << flush << ",\"flags\":" << flags
          << ",\"path\":" << path << ",\"caught\":" << caught
          << ",\"pass\":" << (pass ? "true" : "false")
          << ",\"identity_equal\":" << (identity_ok ? "true" : "false") << ",\"before\":";
  raw(before);
  records << ",\"entry\":";
  raw(entry);
  records << ",\"after\":";
  raw(after);
  records << ",\"nested\":";
  raw(nested);
  records << ",\"nested_after\":";
  raw(nested_after);
  records << "}\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::filesystem::path output;
    if (argc == 3 && std::string(argv[1]) == "--output") {
      output = argv[2];
      host::require(!std::filesystem::exists(output), "Fresh boundary output required");
      std::filesystem::create_directories(output);
    } else {
      host::require(argc == 1, "Unknown boundary test argument");
      const auto base = std::filesystem::current_path() / "host-float-environment-boundary-results";
      std::filesystem::create_directories(base);
      for (u64 attempt = 0;; ++attempt) {
        output = base / std::to_string(attempt);
        if (std::filesystem::create_directory(output))
          break;
      }
    }
    records.open(output / "raw.jsonl");
    records.exceptions(std::ios::badbit | std::ios::failbit);
    const host::HostEnvironment caller;
    host::require(std::fesetenv(FE_DFL_ENV) == 0, "Boundary calibration unavailable");
    defaults = Raw::capture();
    host::require(defaults.consistent(), "Boundary calibration snapshots differ");
    for (unsigned xm : masks)
      for (unsigned sm : masks)
        for (unsigned rounding = 0; rounding < 4; ++rounding)
          for (unsigned flush : flushes)
            for (unsigned flags : {0u, 0x21u})
              for (unsigned path = 0; path < 21; ++path)
                row(xm, sm, rounding, flush, flags, path);
    records.flush();
    std::ofstream summary(output / "summary.json");
    summary << "{\"status\":\"" << (failures ? "FAIL" : "PASS") << "\",\"rows\":" << rows
            << ",\"failures\":" << failures << ",\"default_entries\":" << default_entries
            << ",\"nested_entries\":" << nested_entries << ",\"identity_checks\":" << identities
            << ",\"GPU_calls\":0}\n";
    host::require(rows == 24192 && failures == 0,
                  "Protected host boundary contract failed; raw rows retained");
    std::cout << "PASS protected host boundary rows=" << rows << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
