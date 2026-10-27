// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/host_float_environment.hpp"
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <array>
#include <stdexcept>
#include <string>

namespace cupid::gfx::detail {
template <typename Api>
GLuint compile_compute_program(Api &api, const char *source, const char *kind) {
  return HostFloatEnvironment::run([&] {
    // Driver compilation can change host exception flags. Lazy creation must
    // compile independently of guest rounding and restore the caller's environment.
    const GLuint shader = api.CreateShader(GL_COMPUTE_SHADER);
    if (!shader)
      throw std::runtime_error(std::string("Hardware ") + kind + " shader creation failed");
    api.ShaderSource(shader, 1, &source, nullptr);
    api.CompileShader(shader);
    GLint success = 0;
    api.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      std::array<char, 8192> log{};
      api.GetShaderInfoLog(shader, GLsizei(log.size()), nullptr, log.data());
      api.DeleteShader(shader);
      throw std::runtime_error(std::string("Hardware ") + kind +
                               " compilation failed: " + log.data());
    }
    const GLuint linked = api.CreateProgram();
    if (!linked) {
      api.DeleteShader(shader);
      throw std::runtime_error(std::string("Hardware ") + kind + " program creation failed");
    }
    api.AttachShader(linked, shader);
    api.LinkProgram(linked);
    api.DeleteShader(shader);
    success = 0;
    api.GetProgramiv(linked, GL_LINK_STATUS, &success);
    if (!success) {
      std::array<char, 8192> log{};
      api.GetProgramInfoLog(linked, GLsizei(log.size()), nullptr, log.data());
      api.DeleteProgram(linked);
      throw std::runtime_error(std::string("Hardware ") + kind + " link failed: " + log.data());
    }
    return linked;
  });
}

struct FragmentPrograms {
  GLuint scalar = 0, batch = 0;
  bool scalar_initialized = false, batch_initialized = false;
};

template <typename Use, typename Dispatch>
void dispatch_fragment_program(FragmentPrograms &programs, bool batch, Use use, Dispatch dispatch) {
  bool &initialized = batch ? programs.batch_initialized : programs.scalar_initialized;
  const auto run = [&] {
    use(batch ? programs.batch : programs.scalar);
    dispatch();
  };
  if (initialized) {
    run();
    return;
  }
  // Drivers can defer compilation until the first real dispatch. Protect only
  // initialization; cached programs execute without saving the host environment.
  HostFloatEnvironment::run([&] {
    run();
    initialized = true;
  });
}

template <typename Compile, typename Delete>
FragmentPrograms create_fragment_programs(Compile compile, Delete erase) {
  // Return both IDs together so callers cannot publish a partially created family.
  FragmentPrograms pending;
  try {
    pending.scalar = compile(false);
    if (!pending.scalar)
      throw std::runtime_error("Hardware fragment program creation failed");
    pending.batch = compile(true);
    if (!pending.batch)
      throw std::runtime_error("Hardware batch fragment program creation failed");
  } catch (...) {
    if (pending.scalar)
      erase(pending.scalar);
    if (pending.batch)
      erase(pending.batch);
    throw;
  }
  return pending;
}
} // namespace cupid::gfx::detail
