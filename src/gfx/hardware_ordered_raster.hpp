// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cupid/core/host_float_environment.hpp"
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cupid::gfx::detail {
inline constexpr const char *ordered_raster_vertex_source = R"glsl(#version 430 core
layout(std430,binding=3) readonly buffer Work { uint batch_work[]; };
flat out uint record_id;
void main() {
  record_id=uint(gl_InstanceID);
  uint base=5u+record_id*254u;
  vec2 corner=vec2((gl_VertexID&1)!=0 ? batch_work[base+2u] : batch_work[base],
                   (gl_VertexID&2)!=0 ? batch_work[base+3u] : batch_work[base+1u]);
  gl_Position=vec4(corner/vec2(640.0,528.0)*2.0-1.0,0.0,1.0);
}
)glsl";

inline std::string ordered_raster_fragment_source(std::string_view batch,
                                                  std::string_view extension,
                                                  std::string_view suffix) {
  std::string result(batch);
  const auto replace_once = [&](std::string_view before, std::string_view after) {
    const auto first = result.find(before);
    if (first == std::string::npos ||
        result.find(before, first + before.size()) != std::string::npos)
      throw std::logic_error("Ordered raster source wrapper marker is not unique");
    result.replace(first, before.size(), after);
  };
  replace_once("#version 430 core",
               "#version 430 core\n#extension " + std::string(extension) + " : require");
  replace_once("layout(local_size_x=8, local_size_y=8) in;",
               "layout(pixel_interlock_ordered) in;\nflat in uint record_id;");
  replace_once("binding=0) buffer Efb", "binding=0) coherent buffer Efb");
  replace_once("binding=5) buffer TextureState", "binding=5) coherent buffer TextureState");
  constexpr std::string_view start = "void main() {\n  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy)";
  constexpr std::string_view end = "  if (accepted_count!=0u) {";
  const auto first = result.find(start), last = result.find(end, first);
  if (first == std::string::npos || last == std::string::npos ||
      result.find(start, first + start.size()) != std::string::npos)
    throw std::logic_error("Ordered raster main wrapper marker is not unique");
  // Shade/coverage/depth/blend arithmetic remains the exact shared batch body.
  // Helper returns stay inside draw_pixel; main always reaches both interlocks.
  const std::string main = "void main() {\n  work_base=5u+record_id*254u;\n"
                           "  ivec2 pixel=ivec2(gl_FragCoord.xy);\n"
                           "  beginInvocationInterlock" +
                           std::string(suffix) +
                           "();\n"
                           "  draw_pixel(pixel);\n"
                           "  endInvocationInterlock" +
                           std::string(suffix) + "();\n";
  result.replace(first, last - first, main);
  return result;
}

template <typename Api>
GLuint compile_ordered_raster_program(Api &api, const char *vertex, const char *fragment) {
  return HostFloatEnvironment::run([&] {
    std::array<GLuint, 2> shaders{};
    GLuint program = 0;
    try {
      const std::array<GLenum, 2> kinds{GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
      const std::array<const char *, 2> sources{vertex, fragment};
      for (unsigned n = 0; n < shaders.size(); ++n) {
        shaders[n] = api.CreateShader(kinds[n]);
        if (!shaders[n])
          throw std::runtime_error("Ordered raster shader allocation failed");
        api.ShaderSource(shaders[n], 1, &sources[n], nullptr);
        api.CompileShader(shaders[n]);
        GLint success = 0;
        api.GetShaderiv(shaders[n], GL_COMPILE_STATUS, &success);
        if (!success) {
          std::array<char, 8192> log{};
          api.GetShaderInfoLog(shaders[n], GLsizei(log.size()), nullptr, log.data());
          throw std::runtime_error(std::string("Ordered raster shader compilation failed: ") +
                                   log.data());
        }
      }
      program = api.CreateProgram();
      if (!program)
        throw std::runtime_error("Ordered raster program allocation failed");
      for (GLuint shader : shaders)
        api.AttachShader(program, shader);
      api.LinkProgram(program);
      GLint success = 0;
      api.GetProgramiv(program, GL_LINK_STATUS, &success);
      if (!success) {
        std::array<char, 8192> log{};
        api.GetProgramInfoLog(program, GLsizei(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("Ordered raster program link failed: ") + log.data());
      }
    } catch (...) {
      for (GLuint shader : shaders)
        if (shader)
          api.DeleteShader(shader);
      if (program)
        api.DeleteProgram(program);
      throw;
    }
    for (GLuint shader : shaders)
      api.DeleteShader(shader);
    return program;
  });
}
} // namespace cupid::gfx::detail
