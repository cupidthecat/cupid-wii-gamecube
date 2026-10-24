// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cfenv>
#include <stdexcept>

namespace cupid {
// Host work entered from guest floating-point operations uses the default
// environment, then restores the caller's rounding, masks and exception flags.
class HostFloatEnvironment {
public:
  HostFloatEnvironment() {
    if (std::fegetenv(&saved_) != 0)
      throw std::runtime_error("Host floating environment is unavailable");
    if (std::fesetenv(FE_DFL_ENV) != 0) {
      std::fesetenv(&saved_);
      throw std::runtime_error("Host default floating environment is unavailable");
    }
  }

  ~HostFloatEnvironment() {
    std::fesetenv(&saved_);
  }

  HostFloatEnvironment(const HostFloatEnvironment &) = delete;
  HostFloatEnvironment &operator=(const HostFloatEnvironment &) = delete;

private:
  std::fenv_t saved_{};
};
} // namespace cupid
