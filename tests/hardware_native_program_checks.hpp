// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace {
namespace hardware_native_program_test {
enum Failure { ZeroVertex, ZeroFragment, CompileVertex, CompileFragment, ZeroProgram, Link, None };

struct FaultApi {
  Failure failure = None;
  unsigned shader_calls = 0, program_calls = 0, deleted_shaders = 0, deleted_programs = 0;
  std::array<GLuint, 2> deleted{};

  GLuint CreateShader(GLenum) {
    ++shader_calls;
    return (failure == ZeroVertex && shader_calls == 1) ||
                   (failure == ZeroFragment && shader_calls == 2)
               ? 0u
               : 100u + shader_calls;
  }

  void ShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *) {
  }

  void CompileShader(GLuint) {
  }

  void GetShaderiv(GLuint id, GLenum, GLint *value) {
    *value =
        !((failure == CompileVertex && id == 101) || (failure == CompileFragment && id == 102));
  }

  void GetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *text) {
    *text = 0;
  }

  void DeleteShader(GLuint id) {
    CHECK(id == 101 || id == 102);
    CHECK(deleted_shaders < deleted.size());
    CHECK(std::find(deleted.begin(), deleted.end(), id) == deleted.end());
    deleted[deleted_shaders++] = id;
  }

  GLuint CreateProgram() {
    ++program_calls;
    return failure == ZeroProgram ? 0u : 200u;
  }

  void AttachShader(GLuint, GLuint) {
  }

  void LinkProgram(GLuint) {
  }

  void GetProgramiv(GLuint, GLenum, GLint *value) {
    *value = failure != Link;
  }

  void GetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *text) {
    *text = 0;
  }

  void DeleteProgram(GLuint id) {
    CHECK(id == 200 && deleted_programs == 0);
    ++deleted_programs;
  }
};

struct RealApi : ProgramTestApi {
  std::array<GLuint, 2> shaders{};
  GLuint program = 0;
  unsigned count = 0;

  GLuint CreateShader(GLenum kind) {
    const auto id = ProgramTestApi::CreateShader(kind);
    CHECK(count < shaders.size());
    shaders[count++] = id;
    return id;
  }

  GLuint CreateProgram() {
    program = ProgramTestApi::CreateProgram();
    return program;
  }
};

void checks() {
  const TestFloatEnvironment restore;
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  const auto is_shader = graphics_function<PFNGLISSHADERPROC>("glIsShader");
  const auto is_program = graphics_function<PFNGLISPROGRAMPROC>("glIsProgram");
  constexpr int modes[]{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  unsigned fault_cases = 0, real_cases = 0;
  constexpr const char *vertex = "#version 430 core\nflat out vec4 color;void "
                                 "main(){color=vec4(1);gl_Position=vec4(0,0,0,1);}";
  constexpr const char *fragment =
      "#version 430 core\nflat in vec4 color;out vec4 result;void main(){result=color;}";
  constexpr const char *mismatch =
      "#version 430 core\nflat in ivec4 color;out vec4 result;void main(){result=vec4(color);}";
  constexpr const char *bad = "#version 430 core\n#error expected authored compiler rejection\n";
  for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal) {
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      CHECK(std::fesetround(modes[mode]) == 0);
      CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
      constexpr unsigned simd_modes[]{0u, 0x6000u, 0x4000u, 0x2000u};
      _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                 ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
      for (Failure failure :
           {ZeroVertex, ZeroFragment, CompileVertex, CompileFragment, ZeroProgram, Link, None}) {
        FaultApi api;
        api.failure = failure;
        bool caught = false;
        GLuint result = 0;
        const FloatEnvironmentBits before;
        try {
          result = gfx::detail::compile_ordered_raster_program(api, "unused", "unused");
        } catch (const std::runtime_error &) {
          caught = true;
        }
        CHECK(FloatEnvironmentBits{} == before);
        CHECK(caught == (failure != None) && result == (failure == None ? 200u : 0u));
        const unsigned wanted_calls = failure == ZeroVertex || failure == CompileVertex ? 1 : 2;
        const unsigned wanted_deletes = failure == ZeroVertex                                 ? 0
                                        : failure == ZeroFragment || failure == CompileVertex ? 1
                                                                                              : 2;
        CHECK(api.shader_calls == wanted_calls && api.deleted_shaders == wanted_deletes);
        CHECK(api.program_calls == (failure >= ZeroProgram ? 1u : 0u));
        CHECK(api.deleted_programs == (failure == Link ? 1u : 0u));
        if (result) {
          api.DeleteProgram(result);
          CHECK(api.deleted_programs == 1);
        }
        ++fault_cases;
      }
      for (unsigned kind = 0; kind < 4; ++kind) {
        RealApi api;
        GLuint result = 0;
        bool caught = false;
        const FloatEnvironmentBits before;
        try {
          result = gfx::detail::compile_ordered_raster_program(api, kind == 0 ? bad : vertex,
                                                               kind == 1   ? bad
                                                               : kind == 2 ? mismatch
                                                                           : fragment);
        } catch (const std::runtime_error &) {
          caught = true;
        }
        CHECK(FloatEnvironmentBits{} == before);
        CHECK(caught == (kind != 3));
        if (result) {
          CHECK(is_program(result));
          api.DeleteProgram(result);
        }
        if (api.program)
          CHECK(!is_program(api.program));
        for (unsigned n = 0; n < api.count; ++n)
          CHECK(!is_shader(api.shaders[n]));
        ++real_cases;
      }
    }
  CHECK(fault_cases == 112 && real_cases == 64);
  std::cout << "[native-program] 112 allocation/compile/link ownership cases and64 actual "
               "compile/link cases passed; caller FP controls/flags exact across16 environments\n";
}
} // namespace hardware_native_program_test
} // namespace
