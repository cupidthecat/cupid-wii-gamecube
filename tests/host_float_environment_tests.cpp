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

namespace {
namespace host = floating_binary_test;
using cupid::HostFloatEnvironment;
using cupid::u64;
using cupid::u8;

struct Raw {
  unsigned control = 0, status = 0, simd = 0, fx_control = 0, fx_status = 0, fx_simd = 0;
  bool operator==(const Raw &) const = default;

  static Raw capture() {
    Raw result{host::fp_x87_control(), host::fp_x87_status(), _mm_getcsr(), 0, 0, 0};
    alignas(16) std::array<u8, 512> area{};
#ifdef _MSC_VER
    _fxsave64(area.data());
#else
    asm volatile("fxsave64 %0" : "=m"(area));
#endif
    unsigned short control = 0, status = 0;
    std::memcpy(&control, area.data(), 2);
    std::memcpy(&status, area.data() + 2, 2);
    std::memcpy(&result.fx_simd, area.data() + 24, 4);
    result.fx_control = control;
    result.fx_status = status;
    return result;
  }

  bool consistent() const {
    return control == fx_control && status == fx_status && simd == fx_simd;
  }
};

struct Marker {};

constexpr unsigned x87_modes[]{0, 0xc00, 0x800, 0x400};
constexpr unsigned simd_modes[]{0, 0x6000, 0x4000, 0x2000};
constexpr unsigned flushes[]{0, 0x40, 0x8000, 0x8040};
constexpr unsigned flags[]{0, 1, 2, 4, 8, 16, 32, 5, 0x22, 0x3f};
std::ofstream records;
u64 rows = 0;
Raw defaults;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void raw(std::ostream &out, const Raw &value) {
  out << '[' << value.control << ',' << value.status << ',' << value.simd << ',' << value.fx_control
      << ',' << value.fx_status << ',' << value.fx_simd << ']';
}

void install(unsigned x87, unsigned simd, unsigned flush, unsigned x87_flags, unsigned simd_flags,
             bool unmasked) {
  require(std::fesetenv(FE_DFL_ENV) == 0, "Initial default environment failed");
  const auto control = host::fp_x87_control();
  host::fp_x87_set_control((control & ~0xc3fu) | x87_modes[x87] | (unmasked ? 0u : 0x3fu));
  host::fp_x87_set_flags(x87_flags);
  _mm_setcsr(simd_modes[simd] | flush | (unmasked ? 0u : 0x1f80u) | simd_flags);
}

void perturb() {
  // Integer-only, deterministic changes under the guard's expected default
  // masks. Both units change independently; there is no fake GL arithmetic.
  host::fp_x87_set_control((host::fp_x87_control() & ~0xc00u) | 0x800u | 0x3fu);
  host::fp_x87_set_flags(0x05);
  _mm_setcsr(0x1f80u | 0x6000u | 0x8040u | 0x22u);
}

void row(unsigned x87, unsigned simd, unsigned flush, unsigned x87_flags, unsigned simd_flags,
         unsigned path, bool unmasked) {
  install(x87, simd, flush, x87_flags, simd_flags, unmasked);
  const auto before = Raw::capture();
  Raw entry, inner_before, inner_after, inner_entry;
  bool caught = false;
  try {
    const HostFloatEnvironment environment;
    entry = Raw::capture();
    perturb();
    if (path == 4) {
      inner_before = Raw::capture();
      {
        const HostFloatEnvironment nested;
        inner_entry = Raw::capture();
        perturb();
      }
      inner_after = Raw::capture();
    }
    if (path == 2 || path == 3)
      throw Marker{};
  } catch (const Marker &) {
    caught = true;
  }
  // Preserve both direct registers and independent FXSAVE before any assertion,
  // C fenv query/reset, formatting, allocation or output can alter observations.
  const auto after = Raw::capture();
  const int reset = std::fesetenv(FE_DFL_ENV);
  _mm_setcsr(defaults.simd);
  const bool pass = reset == 0 && before.consistent() && entry.consistent() && after.consistent() &&
                    before == after && entry == defaults && caught == (path == 2 || path == 3) &&
                    (path != 4 || (inner_before.consistent() && inner_entry.consistent() &&
                                   inner_after.consistent() && inner_before == inner_after &&
                                   inner_entry == defaults));
  records << "{\"row\":" << rows << ",\"x87_rounding\":" << x87 << ",\"SSE_rounding\":" << simd
          << ",\"flush\":" << flush << ",\"x87_flags\":" << x87_flags
          << ",\"SSE_flags\":" << simd_flags << ",\"path\":" << path << ",\"unmasked\":" << unmasked
          << ",\"before\":";
  raw(records, before);
  records << ",\"entry\":";
  raw(records, entry);
  records << ",\"after\":";
  raw(records, after);
  if (path == 4) {
    records << ",\"inner_before\":";
    raw(records, inner_before);
    records << ",\"inner_entry\":";
    raw(records, inner_entry);
    records << ",\"inner_after\":";
    raw(records, inner_after);
  }
  records << ",\"caught\":" << caught << ",\"reset\":" << reset
          << ",\"pass\":" << (pass ? "true" : "false") << "}\n";
  records.flush();
  ++rows;
  require(pass,
          "Exact split x87/SSE/default/exception/nested helper contract failed; raw row retained");
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::filesystem::path output;
    int unmasked = -1;
    for (int n = 1; n < argc; ++n) {
      const std::string argument = argv[n];
      if (argument == "--output" && n + 1 < argc)
        output = argv[++n];
      else if (argument == "--unmasked" && n + 1 < argc)
        unmasked = std::stoi(argv[++n]);
      else
        throw std::runtime_error("Unknown/missing component argument");
    }
    if (output.empty()) {
      // Public CTest runs retain separate raw evidence and can be repeated.
      const auto root = std::filesystem::current_path() / "host-float-environment-results";
      std::filesystem::create_directories(root);
      const std::string prefix =
          unmasked >= 0 ? "unmasked-" + std::to_string(unmasked) + "-" : "masked-";
      for (u64 attempt = 0;; ++attempt) {
        const auto fresh = root / (prefix + std::to_string(attempt));
        if (std::filesystem::create_directory(fresh)) {
          output = fresh;
          break;
        }
      }
    } else {
      require(!std::filesystem::exists(output), "Fresh component output required");
      std::filesystem::create_directories(output);
    }
    records.open(output / "raw.jsonl");
    records.exceptions(std::ios::badbit | std::ios::failbit);
    const host::HostEnvironment caller;
    require(std::fesetenv(FE_DFL_ENV) == 0, "Calibration default unavailable");
    defaults = Raw::capture();
    require(defaults.consistent(), "Independent calibration snapshots differ");
    if (unmasked >= 0) {
      require(unmasked < 16, "Representative unmasked index outside16");
      const auto index = static_cast<unsigned>(unmasked);
      for (unsigned path = 0; path < 5; ++path)
        row(index % 4, (index / 4) % 4, flushes[index % 4], 0, 0, path, true);
      require(rows == 5, "Unmasked child row count differs");
    } else {
      for (unsigned x87 = 0; x87 < 4; ++x87)
        for (unsigned simd = 0; simd < 4; ++simd)
          for (unsigned flush : flushes)
            for (unsigned x87_flags : flags)
              for (unsigned simd_flags : flags)
                for (unsigned path = 0; path < 5; ++path)
                  row(x87, simd, flush, x87_flags, simd_flags, path, false);
      require(rows == 32000, "Masked independent full cross-product row count differs");
    }
    std::ofstream summary(output / "summary.json");
    summary << "{\"status\":\"PASS\",\"rows\":" << rows
            << ",\"helper_bytes\":" << sizeof(HostFloatEnvironment)
            << ",\"GPU_calls\":0,\"timing_claim\":false}\n";
    std::cout << "PASS exact raw helper rows=" << rows << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
