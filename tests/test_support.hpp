// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition))                                                                              \
      throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) +            \
                               ": " #condition);                                                   \
  } while (false)
#define CHECK_THROWS(expression)                                                                   \
  do {                                                                                             \
    bool caught_ = false;                                                                          \
    try {                                                                                          \
      (void)(expression);                                                                          \
    } catch (const std::exception &) {                                                             \
      caught_ = true;                                                                              \
    }                                                                                              \
    CHECK(caught_);                                                                                \
  } while (false)

template <class Function> int run_tests(Function function) {
  try {
    function();
    std::cout << "All checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
