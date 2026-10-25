// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cfenv>
#include <stdexcept>
#if defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
#include <cstddef>
#endif

namespace cupid {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
namespace detail {
struct alignas(4) HostFloatState {
  unsigned char x87[28];
  unsigned mxcsr;
};

static_assert(sizeof(unsigned) == 4);
static_assert(sizeof(HostFloatState) == 32);
static_assert(offsetof(HostFloatState, mxcsr) == 28);
extern "C" void cupid_host_float_enter(HostFloatState *state) noexcept;
extern "C" void cupid_host_float_restore(const HostFloatState *state) noexcept;
} // namespace detail
#endif
// Host work entered from guest floating-point operations uses the default
// environment, then restores the caller's rounding, masks and exception flags.
class HostFloatEnvironment {
public:
  HostFloatEnvironment() {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
    detail::cupid_host_float_enter(&saved_);
#else
    if (std::fegetenv(&saved_) != 0)
      throw std::runtime_error("Host floating environment is unavailable");
    if (std::fesetenv(FE_DFL_ENV) != 0) {
      std::fesetenv(&saved_);
      throw std::runtime_error("Host default floating environment is unavailable");
    }
#endif
  }

  ~HostFloatEnvironment() {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
    detail::cupid_host_float_restore(&saved_);
#else
    std::fesetenv(&saved_);
#endif
  }

  HostFloatEnvironment(const HostFloatEnvironment &) = delete;
  HostFloatEnvironment &operator=(const HostFloatEnvironment &) = delete;

private:
#if defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
  detail::HostFloatState saved_;
#else
  std::fenv_t saved_{};
#endif
};
} // namespace cupid
