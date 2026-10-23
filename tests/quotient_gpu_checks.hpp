// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "quotient_cases.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace quotient_test {
template <typename Function> Function division_function(const char *name) {
  const auto result = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
  if (!result)
    throw std::runtime_error(std::string("Missing division test entry point: ") + name);
  return result;
}

// The caller owns a current GL4.3 context. Expected answers remain on the host;
// shaders read only operands, the forced estimate and case-classification flags.
inline void verify_gpu_division(std::string_view helper_source) {
#define CUPID_TEST_GL(type, name) const auto name = division_function<type>("gl" #name)
  CUPID_TEST_GL(PFNGLCREATESHADERPROC, CreateShader);
  CUPID_TEST_GL(PFNGLSHADERSOURCEPROC, ShaderSource);
  CUPID_TEST_GL(PFNGLCOMPILESHADERPROC, CompileShader);
  CUPID_TEST_GL(PFNGLGETSHADERIVPROC, GetShaderiv);
  CUPID_TEST_GL(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
  CUPID_TEST_GL(PFNGLCREATEPROGRAMPROC, CreateProgram);
  CUPID_TEST_GL(PFNGLATTACHSHADERPROC, AttachShader);
  CUPID_TEST_GL(PFNGLLINKPROGRAMPROC, LinkProgram);
  CUPID_TEST_GL(PFNGLGETPROGRAMIVPROC, GetProgramiv);
  CUPID_TEST_GL(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
  CUPID_TEST_GL(PFNGLUSEPROGRAMPROC, UseProgram);
  CUPID_TEST_GL(PFNGLGENBUFFERSPROC, GenBuffers);
  CUPID_TEST_GL(PFNGLBINDBUFFERPROC, BindBuffer);
  CUPID_TEST_GL(PFNGLBINDBUFFERBASEPROC, BindBufferBase);
  CUPID_TEST_GL(PFNGLBUFFERDATAPROC, BufferData);
  CUPID_TEST_GL(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData);
  CUPID_TEST_GL(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);
  CUPID_TEST_GL(PFNGLMEMORYBARRIERPROC, MemoryBarrier);
  CUPID_TEST_GL(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
  CUPID_TEST_GL(PFNGLDELETESHADERPROC, DeleteShader);
  CUPID_TEST_GL(PFNGLDELETEPROGRAMPROC, DeleteProgram);
#undef CUPID_TEST_GL

  struct Cleanup {
    PFNGLDELETEBUFFERSPROC buffers_fn;
    PFNGLDELETESHADERPROC shader_fn;
    PFNGLDELETEPROGRAMPROC program_fn;
    std::array<GLuint, 2> buffers{};
    GLuint shader = 0, program = 0;

    ~Cleanup() {
      if (program)
        program_fn(program);
      if (shader)
        shader_fn(shader);
      buffers_fn(GLsizei(buffers.size()), buffers.data());
    }
  } cleanup{DeleteBuffers, DeleteShader, DeleteProgram};

  const auto cases = quotient_test::division_cases();
  std::vector<std::uint32_t> inputs(4 + cases.size() * 4), results(cases.size() * 3);
  inputs[0] = static_cast<std::uint32_t>(cases.size());
  for (std::size_t row = 0; row < cases.size(); ++row) {
    inputs[4 + row * 4] = cases[row].numerator;
    inputs[5 + row * 4] = cases[row].denominator;
    inputs[6 + row * 4] = cases[row].seed;
    inputs[7 + row * 4] = cases[row].flags;
  }
  GenBuffers(GLsizei(cleanup.buffers.size()), cleanup.buffers.data());
  BindBuffer(GL_SHADER_STORAGE_BUFFER, cleanup.buffers[0]);
  BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(inputs.size() * sizeof(std::uint32_t)),
             inputs.data(), GL_STATIC_DRAW);
  BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, cleanup.buffers[0]);
  BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cleanup.buffers[1]);
  std::uint64_t checks = 0;
  for (unsigned forced = 0; forced < 2; ++forced) {
    std::string helper(helper_source);
    if (forced) {
      const auto end_estimate = helper.find("double positive_float_value(");
      if (end_estimate == std::string::npos)
        throw std::runtime_error("Missing division helper boundary");
      helper.replace(0, end_estimate,
                     "float divide_estimate(float numerator,float denominator) {"
                     "return uintBitsToFloat(forced_seed);}\n");
    }
    const std::string source =
        "#version 430 core\nlayout(local_size_x=64) in;\n"
        "layout(std430,binding=0) readonly buffer Inputs {uint inputs[];};\n"
        "layout(std430,binding=1) buffer Results {uint results[];};\n"
        "uint forced_seed;\n" +
        helper +
        "void main() {uint row=gl_GlobalInvocationID.x;if(row>=inputs[0])return;"
        "uint base=4u+row*4u;forced_seed=inputs[base+2u];uint answer;" +
        (forced ? "if((inputs[base+3u]&4u)==0u)answer=0x5a17f00du;else " : "") +
        "answer=floatBitsToUint(rounded_divide(uintBitsToFloat(inputs[base]),"
        "uintBitsToFloat(inputs[base+1u])));"
        "results[row*3u]=answer;results[row*3u+1u]=~answer;"
        "results[row*3u+2u]=row^0x9e3779b9u;}\n";
    cleanup.shader = CreateShader(GL_COMPUTE_SHADER);
    const char *text = source.c_str();
    ShaderSource(cleanup.shader, 1, &text, nullptr);
    CompileShader(cleanup.shader);
    GLint compiled = 0;
    GetShaderiv(cleanup.shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      std::array<char, 8192> log{};
      GetShaderInfoLog(cleanup.shader, GLsizei(log.size()), nullptr, log.data());
      throw std::runtime_error(std::string("Division rounding test shader: ") + log.data());
    }
    cleanup.program = CreateProgram();
    AttachShader(cleanup.program, cleanup.shader);
    LinkProgram(cleanup.program);
    GLint linked = 0;
    GetProgramiv(cleanup.program, GL_LINK_STATUS, &linked);
    if (!linked) {
      std::array<char, 8192> log{};
      GetProgramInfoLog(cleanup.program, GLsizei(log.size()), nullptr, log.data());
      throw std::runtime_error(std::string("Division rounding test program: ") + log.data());
    }
    DeleteShader(cleanup.shader);
    cleanup.shader = 0;
    UseProgram(cleanup.program);
    for (std::uint32_t poison : {0x963c5aa5u, 0x69c3a55au}) {
      std::fill(results.begin(), results.end(), poison);
      BindBuffer(GL_SHADER_STORAGE_BUFFER, cleanup.buffers[1]);
      BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(results.size() * sizeof(std::uint32_t)),
                 results.data(), GL_DYNAMIC_DRAW);
      DispatchCompute(static_cast<GLuint>((cases.size() + 63) / 64), 1, 1);
      MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
      GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       GLsizeiptr(results.size() * sizeof(std::uint32_t)), results.data());
      for (std::size_t row = 0; row < cases.size(); ++row) {
        const std::uint32_t expected =
            forced && !(cases[row].flags & 4) ? 0x5a17f00d : cases[row].expected;
        const std::array<std::uint32_t, 3> values{expected, ~expected,
                                                  static_cast<std::uint32_t>(row) ^ 0x9e3779b9};
        for (std::size_t field = 0; field < values.size(); ++field) {
          ++checks;
          if (results[row * 3 + field] != values[field])
            throw std::runtime_error("Division rounding mismatch row=" + std::to_string(row) +
                                     " forced=" + std::to_string(forced) +
                                     " field=" + std::to_string(field) +
                                     " expected=" + std::to_string(values[field]) +
                                     " actual=" + std::to_string(results[row * 3 + field]));
        }
      }
    }
    UseProgram(0);
    DeleteProgram(cleanup.program);
    cleanup.program = 0;
  }
  std::cout << "Verified " << cases.size() << " division cases and " << checks
            << " exact GPU words with ordinary and forced estimates\n";
}

} // namespace quotient_test
