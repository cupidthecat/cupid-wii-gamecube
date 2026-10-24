// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gfx/hardware_program.hpp"
#include "cupid/gfx/command_processor.hpp"
#include "cupid/gfx/efb_copy.hpp"
#include "cupid/gfx/xfb_copy.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <chrono>
#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <vector>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include "quotient_gpu_checks.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

namespace {
using namespace cupid;

// One epoch for every renderer_stage template instantiation and fixture construction.
std::chrono::steady_clock::time_point renderer_run_started;

std::chrono::steady_clock::time_point renderer_stage_started(std::string_view name) {
  const auto started = std::chrono::steady_clock::now();
  const auto run_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(started - renderer_run_started).count();
  std::cout << "[renderer-time] begin " << name << " run_ms=" << run_ms << '\n';
  return started;
}

void renderer_stage_returned(std::string_view name, std::chrono::steady_clock::time_point started) {
  const auto returned = std::chrono::steady_clock::now();
  const auto run_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(returned - renderer_run_started)
          .count();
  const auto stage_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(returned - started).count();
  std::cout << "[renderer-time] returned " << name << " run_ms=" << run_ms
            << " stage_ms=" << stage_ms << '\n';
}

template <typename Function> void renderer_stage(std::string_view name, Function function) {
  std::cout << "[renderer] begin " << name << '\n';
  const auto started = renderer_stage_started(name);
  function();
  // The check's local objects have been destroyed before this marker.
  std::cout << "[renderer] returned " << name << '\n';
  renderer_stage_returned(name, started);
}

template <typename Function> Function graphics_function(const char *name) {
  const auto result = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
  if (!result)
    throw std::runtime_error(std::string("Missing graphics test entry point: ") + name);
  return result;
}

void division_rounding_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  constexpr std::string_view raster_source =
#include "../src/gfx/hardware_shader.inc"
      ;
  constexpr auto first = raster_source.find("float divide_estimate("),
                 last = raster_source.find("bool compare_value(");
  static_assert(first != std::string_view::npos && last > first);
  quotient_test::verify_gpu_division(raster_source.substr(first, last - first));
}

void division_identity_checks(bool exhaustive) {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
#define CUPID_TEST_GL(type, name) const auto name = graphics_function<type>("gl" #name)
  CUPID_TEST_GL(PFNGLCREATESHADERPROC, CreateShader);
  CUPID_TEST_GL(PFNGLSHADERSOURCEPROC, ShaderSource);
  CUPID_TEST_GL(PFNGLCOMPILESHADERPROC, CompileShader);
  CUPID_TEST_GL(PFNGLGETSHADERIVPROC, GetShaderiv);
  CUPID_TEST_GL(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
  CUPID_TEST_GL(PFNGLCREATEPROGRAMPROC, CreateProgram);
  CUPID_TEST_GL(PFNGLATTACHSHADERPROC, AttachShader);
  CUPID_TEST_GL(PFNGLLINKPROGRAMPROC, LinkProgram);
  CUPID_TEST_GL(PFNGLGETPROGRAMIVPROC, GetProgramiv);
  CUPID_TEST_GL(PFNGLUSEPROGRAMPROC, UseProgram);
  CUPID_TEST_GL(PFNGLGENBUFFERSPROC, GenBuffers);
  CUPID_TEST_GL(PFNGLBINDBUFFERPROC, BindBuffer);
  CUPID_TEST_GL(PFNGLBINDBUFFERBASEPROC, BindBufferBase);
  CUPID_TEST_GL(PFNGLBUFFERDATAPROC, BufferData);
  CUPID_TEST_GL(PFNGLBUFFERSUBDATAPROC, BufferSubData);
  CUPID_TEST_GL(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData);
  CUPID_TEST_GL(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);
  CUPID_TEST_GL(PFNGLMEMORYBARRIERPROC, MemoryBarrier);
  CUPID_TEST_GL(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
  CUPID_TEST_GL(PFNGLDELETESHADERPROC, DeleteShader);
  CUPID_TEST_GL(PFNGLDELETEPROGRAMPROC, DeleteProgram);
#undef CUPID_TEST_GL
  constexpr std::string_view raster_source =
#include "../src/gfx/hardware_shader.inc"
      ;
  constexpr auto first = raster_source.find("float divide_estimate("),
                 last = raster_source.find("bool compare_value(");
  static_assert(first != std::string_view::npos && last > first);
  // The renderer checks that both raster shaders contain identical helpers.
  const std::string source = "#version 430 core\nlayout(local_size_x=256) in;\n"
                             "layout(std430,binding=0) buffer Data {uint data[];};\n" +
                             std::string(raster_source.substr(first, last - first)) + R"glsl(
void check(uint numerator,uint denominator,uint expected) {
  uint actual=floatBitsToUint(rounded_divide(uintBitsToFloat(numerator),uintBitsToFloat(denominator)));
  if (actual!=expected && atomicOr(data[2],1u)==0u) {
    data[3]=numerator;data[4]=denominator;data[5]=expected;data[6]=actual;
  }
}
void main() {
  uint magnitude=data[0]+gl_GlobalInvocationID.x;
  // Independent host-written seeds keep the expected values opaque to the
  // compiler, so it cannot remove checks by proving the identity itself.
  uint expected_magnitude=data[1]+gl_GlobalInvocationID.x;
  for (uint signs=0u;signs<4u;++signs) {
    uint numerator=magnitude|((signs&1u)<<31u),denominator_sign=(signs&2u)<<30u;
    uint sign=((signs&1u)<<31u)^denominator_sign;
    check(numerator,1065353216u|denominator_sign,
          expected_magnitude>2139095040u ? 2143289344u : expected_magnitude|sign);
    check(numerator,magnitude|denominator_sign,
          expected_magnitude==0u || expected_magnitude>=2139095040u ? 2143289344u : data[7]|sign);
  }
}
)glsl";
  const char *text = source.c_str();
  const auto shader = CreateShader(GL_COMPUTE_SHADER);
  ShaderSource(shader, 1, &text, nullptr);
  CompileShader(shader);
  GLint compiled = 0;
  GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    std::array<char, 8192> log{};
    GetShaderInfoLog(shader, GLsizei(log.size()), nullptr, log.data());
    throw std::runtime_error(std::string("Division identity test shader: ") + log.data());
  }
  const auto program = CreateProgram();
  AttachShader(program, shader);
  LinkProgram(program);
  GLint linked = 0;
  GetProgramiv(program, GL_LINK_STATUS, &linked);
  CHECK(linked);
  DeleteShader(shader);
  UseProgram(program);
  GLuint buffer = 0;
  GenBuffers(1, &buffer);
  BindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
  BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
  std::array<u32, 8> result{};
  result[7] = 0x3f800000;
  BufferData(GL_SHADER_STORAGE_BUFFER, sizeof(result), result.data(), GL_DYNAMIC_DRAW);
  const auto dispatch = [&](u32 base, u32 expected, u32 count) {
    const std::array<u32, 2> seeds{base, expected};
    BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(seeds), seeds.data());
    DispatchCompute(count / 256, 1, 1);
    MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  };
  const auto read = [&] {
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result.data());
  };
  // A deliberately wrong oracle must fail before any positive result counts.
  dispatch(0x3f800000, 0x3f800001, 256);
  read();
  CHECK(result[2] == 1 && result[5] != result[6]);
  result = {};
  BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result.data());
  // Corrupt only the equal-magnitude oracle while leaving the unit-divisor
  // oracle correct, so both checks independently prove they can detect errors.
  dispatch(0x3f800000, 0x3f800000, 256);
  read();
  CHECK(result[2] == 1 && result[5] != result[6]);
  result = {};
  result[7] = 0x3f800000;
  BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(result), result.data());
  u64 cases = 0;
  const auto verify = [&](u32 base, u32 count) {
    dispatch(base, base, count);
    cases += u64(count) * 8;
  };
  if (exhaustive) {
    constexpr u32 batch = 1u << 22;
    for (u64 base = 0; base < 0x80000000ull; base += batch)
      verify(u32(base), batch);
  } else {
    // Sweep both ends and the midpoint of every exponent's mantissa range.
    for (u32 exponent = 0; exponent < 256; ++exponent)
      for (u32 mantissa : {0u, 0x3ff800u, 0x7ff000u})
        verify((exponent << 23) | mantissa, 4096);
  }
  read();
  if (result[2])
    throw std::runtime_error("Division identity mismatch numerator=" + std::to_string(result[3]) +
                             " denominator=" + std::to_string(result[4]) + " expected=" +
                             std::to_string(result[5]) + " actual=" + std::to_string(result[6]));
  DeleteBuffers(1, &buffer);
  DeleteProgram(program);
  std::cout << "Verified " << cases
            << " signed division identity cases, including subnormals, zero, infinity and NaN\n";
}

void word(std::vector<u8> &bytes, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes.push_back(static_cast<u8>(value >> (24 - i * 8)));
}

struct Fixture {
  Memory memory{Console::GameCube};
  hw::Devices devices{Console::GameCube};
  gfx::CommandProcessor graphics{memory, devices, Console::GameCube};

  void cp(u8 reg, u32 value) {
    std::vector<u8> bytes{8, reg};
    word(bytes, value);
    graphics.submit(bytes);
  }

  void bp(u8 reg, u32 value) {
    std::vector<u8> bytes{0x61};
    word(bytes, (u32(reg) << 24) | (value & 0xffffff));
    graphics.submit(bytes);
  }

  void xf(u32 address, std::initializer_list<u32> words) {
    std::vector<u8> bytes{0x10};
    word(bytes, (u32(words.size() - 1) << 16) | address);
    for (u32 value : words)
      word(bytes, value);
    graphics.submit(bytes);
  }

  void floats(u32 address, std::initializer_list<float> values) {
    std::vector<u8> bytes{0x10};
    word(bytes, (u32(values.size() - 1) << 16) | address);
    for (float value : values)
      word(bytes, std::bit_cast<u32>(value));
    graphics.submit(bytes);
  }

  Fixture() {
    cp(0x50, (1 << 9) | (1 << 13));
    cp(0x70, 1 | (4 << 1) | (5 << 14));
    floats(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    floats(0x1020, {1, 0, 1, 0, 1, 0});
    xf(0x1026, {1});
    floats(0x101a, {32, 32, 1000, 374, 374, 1000});
    xf(0x100e, {1});
    bp(0, 1 << 4);
    bp(0x20, (342 << 12) | 342);
    bp(0x21, (405 << 12) | 405);
    bp(0x59, 171 | (171 << 10));
    bp(0x41, 24);
    bp(0xf6, 4);
    bp(0xf7, 14);
    bp(0xf3, (7 << 16) | (7 << 19));
    bp(0xc0, 0xfffa | (1 << 19));
    bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  }

  void draw(u8 opcode, std::span<const std::array<float, 3>> positions,
            std::span<const u32> colors) {
    std::vector<u8> bytes{opcode, static_cast<u8>(positions.size() >> 8),
                          static_cast<u8>(positions.size())};
    for (unsigned i = 0; i < positions.size(); ++i) {
      for (float value : positions[i])
        word(bytes, std::bit_cast<u32>(value));
      word(bytes, colors[i % colors.size()]);
    }
    graphics.submit(bytes);
  }
};

void equal(Fixture &software, Fixture &hardware) {
  CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      if (software.graphics.color(x, y) != hardware.graphics.color(x, y) ||
          software.graphics.depth(x, y) != hardware.graphics.depth(x, y))
        throw std::runtime_error(
            "Hardware differential mismatch at " + std::to_string(x) + "," + std::to_string(y) +
            " software color=" + std::to_string(software.graphics.color(x, y)) +
            " hardware color=" + std::to_string(hardware.graphics.color(x, y)) +
            " software depth=" + std::to_string(software.graphics.depth(x, y)) +
            " hardware depth=" + std::to_string(hardware.graphics.depth(x, y)));
    }
  for (u32 address = 0x0c001010; address <= 0x0c001016; address += 2)
    CHECK(software.graphics.read(address, 2) == hardware.graphics.read(address, 2));
}

void wide_depth_origin_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  const auto get_integer = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
  std::array<std::array<GLuint, 2>, 2> programs{};
  u64 dispatches = 0;
  unsigned cases = 0;
  for (unsigned axis = 0; axis < 2; ++axis)
    for (float sign : {-1.0f, 1.0f})
      for (unsigned family = 0; family < 2; ++family)
        for (const bool batch : {false, true}) {
          std::array<u32, 256> bp{};
          std::array<u32, 0x1058> xf{};
          const auto put = [&](unsigned address, float value) {
            xf[address] = std::bit_cast<u32>(value);
          };
          const float distance = sign * 268435456.0f;
          std::vector<u32> cpu_colors(640 * 528, 0x315a87), cpu_depths(cpu_colors.size(), 0x54321);
          auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
          gfx::Rasterizer rasterizer(bp, xf);
          unsigned cpu_fragments = 0, hardware_work = 0;
          bool gpu_phase = false;
          rasterizer.fragment = [&](unsigned x, unsigned y, u32 depth,
                                    const gfx::FragmentInputs &inputs) {
            CHECK(!gpu_phase && x < 640 && y < 528);
            CHECK((inputs.colors[0] == gfx::Color{20, 40, 60, 255}));
            cpu_colors[std::size_t(y) * 640 + x] = 0x14283c;
            cpu_depths[std::size_t(y) * 640 + x] = depth;
            ++cpu_fragments;
          };
          // A culled, finite small-W triangle remembers an analytic depth
          // plane. Its +/-2^28 origin requires a scaled integer wider than 32 bits.
          bp[0] = (1u << 4) | (3u << 14);
          bp[0x21] = (639u << 12) | 527u;
          put(0x101a, 1);
          put(0x101b, 1);
          put(0x101c, 1000);
          put(0x101f, 1000);
          gfx::TransformedVertex a, b, c;
          a.clip_position = {0, 0, -0x1p-29f, 0x1p-28f};
          a.clip_position[axis] = sign;
          b.clip_position = {0, 0, -0.75f, 1};
          c.clip_position = {0, 0, -0.5f, 1};
          c.clip_position[axis ^ 1u] = 1;
          const std::array<std::optional<gfx::TransformedVertex>, 3> seed{a, b, c};
          rasterizer.draw(0x90, seed);
          CHECK(cpu_fragments == 0);
          // A conventional visible triangle consumes that plane through
          // zfreeze. Its extent selects either batch or scalar GPU dispatch.
          bp[0] = 1 | (1u << 4) | (1u << 19) | ((family == 0 ? 1u : 0u) << 16);
          bp[0x20] = (342u << 12) | 342u;
          bp[0x21] = (405u << 12) | 405u;
          bp[0x59] = 171u | (171u << 10);
          bp[0x40] = 1 | (7 << 1) | 16;
          bp[0x41] = 24;
          bp[0x88] = 6 << 20;
          bp[0x94] = 0x30000 >> 5;
          bp[0xc0] = 0xfffa | (1 << 19);
          bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
          bp[0xf3] = (7 << 16) | (7 << 19);
          bp[0xf6] = 4;
          bp[0xf7] = 14;
          put(0x101a, 32);
          put(0x101b, 32);
          put(0x101d, 374);
          put(0x101e, 374);
          xf[0x103f] = 1;
          const int extent = batch ? 4 : 17;
          const auto vertex = [](float x, float y) {
            gfx::TransformedVertex result;
            result.clip_position = {(x - 32) / 32, (y - 32) / 32, -0.9f, 1};
            result.colors[0] = {20, 40, 60, 255};
            return result;
          };
          const std::array<std::optional<gfx::TransformedVertex>, 3> visible{
              vertex(0, 0), vertex(float(extent), 0), vertex(0, float(extent))};
          auto expected_colors = gpu_colors, expected_depths = gpu_depths;
          // Coverage is the integer right triangle x+y<=extent-2. The frozen
          // depth is 373.75 + 250 * (the other axis), up to a tiny gradient.
          for (int y = 0; y + 1 < extent; ++y)
            for (int x = 0; x + y + 1 < extent; ++x) {
              expected_colors[std::size_t(y) * 640 + x] = 0x14283c;
              expected_depths[std::size_t(y) * 640 + x] = 373 + 250 * (axis ? x : y);
            }
          const unsigned fragments = unsigned(extent * (extent - 1) / 2);
          rasterizer.draw(0x90, visible);
          CHECK(cpu_fragments == fragments);
          CHECK(cpu_colors == expected_colors && cpu_depths == expected_depths);
          gpu_phase = true;
          renderer.upload(gpu_colors, gpu_depths);
          gfx::TevRegisters colors{}, konst{};
          CHECK(renderer.begin(bp, colors, konst));
          rasterizer.hardware = [&](const gfx::RasterWork &work) {
            ++hardware_work;
            CHECK(work.left == 0 && work.top == 0 && work.right == extent && work.bottom == extent);
            CHECK(work.depth.value == 500);
            CHECK(work.depth.dx == (axis ? 250 : sign * 0x1.f4p-21f));
            CHECK(work.depth.dy == (axis ? sign * 0x1.f4p-21f : 250));
            CHECK(work.depth.x_origin == (axis ? 0 : static_cast<int>(distance)));
            CHECK(work.depth.y_origin == (axis ? static_cast<int>(distance) : 0));
            CHECK(work.depth.x_offset == 0.495f && work.depth.y_offset == 0.495f);
            CHECK(renderer.draw(work));
            return true;
          };
          rasterizer.draw(0x90, visible);
          CHECK(hardware_work == 1 && cpu_fragments == fragments);
          const auto stats = renderer.synchronize(gpu_colors, gpu_depths);
          CHECK(stats.dispatches == ++dispatches && stats.fragments == fragments);
          CHECK(stats.fallback_rectangles == 0 && renderer.fallback_reason().empty());
          CHECK(gpu_colors == expected_colors && gpu_depths == expected_depths);
          GLint program = 0;
          get_integer(GL_CURRENT_PROGRAM, &program);
          CHECK(program != 0);
          auto &observed = programs[family][batch];
          if (!observed)
            observed = static_cast<GLuint>(program);
          CHECK(observed == static_cast<GLuint>(program));
          // Turning zfreeze off must replace the remembered plane: a fresh
          // constant-depth primitive is 100 rather than the frozen 373/623/... .
          gpu_phase = false;
          rasterizer.hardware = {};
          bp[0] &= ~(1u << 19);
          cpu_fragments = 0;
          std::fill(cpu_depths.begin(), cpu_depths.end(), 0x54321);
          for (int y = 0; y + 1 < extent; ++y)
            for (int x = 0; x + y + 1 < extent; ++x)
              expected_depths[std::size_t(y) * 640 + x] = 100;
          rasterizer.draw(0x90, visible);
          CHECK(cpu_fragments == fragments && cpu_depths == expected_depths);
          ++cases;
        }
  const std::array<GLuint, 4> ids{programs[0][0], programs[0][1], programs[1][0], programs[1][1]};
  for (unsigned i = 0; i < ids.size(); ++i)
    for (unsigned j = i + 1; j < ids.size(); ++j)
      CHECK(ids[i] != ids[j]);
  CHECK(cases == 16);
  std::cout << "Verified authored wide depth origins through CPU and four GPU raster programs\n";
}

void projection_precision_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::array<u8, 32> texels{};
  for (unsigned row = 0; row < 4; ++row)
    texels[row * 8 + 3] = 255;
  memory.copy_in(0x30000, texels);
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0x80] = 16 | 1;
  bp[0x88] = 7 | (3 << 10) | (1 << 20);
  bp[0x94] = 0x30000 >> 5;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = (7 << 16) | (7 << 19);
  gfx::RasterWork work{};
  work.right = work.bottom = 1;
  work.edges.fill({0, 0, 1});
  work.reciprocal_w.value = 0x1.e0872ap-20f;
  work.textures[0][0].value = 0x1.38a0d6p+7f;
  work.textures[0][1].value = 0;
  work.textures[0][2].value = 0x1.9df960p-2f;
  const float reciprocal = 1.0f / work.reciprocal_w.value;
  const float q = work.textures[0][2].value * reciprocal;
  const float projection = reciprocal / q;
  gfx::FragmentInputs inputs;
  inputs.uv[0][0] = work.textures[0][0].value * projection;
  CHECK(static_cast<s32>(inputs.uv[0][0] * 128.0f) == 49492);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  const auto expected = software.shade(inputs);
  CHECK(expected.has_value());
  CHECK((*expected == gfx::Color{39, 39, 39, 39}));
  gfx::HardwareRenderer hardware(memory, tmem);
  std::vector<u32> efb(640 * 528), depths(efb.size());
  hardware.upload(efb, depths);
  CHECK(hardware.begin(bp, colors, konst));
  CHECK(hardware.draw(work));
  const auto statistics = hardware.synchronize(efb, depths);
  CHECK(statistics.dispatches == 1);
  CHECK(statistics.fragments == 1);
  CHECK(statistics.fallback_rectangles == 0);
  if (efb[0] != 0x272727)
    throw std::runtime_error(
        "Projected division boundary software color=" + std::to_string(0x272727) +
        " hardware color=" + std::to_string(efb[0]) + " device=" + hardware.device());
  CHECK(std::all_of(efb.begin() + 1, efb.end(), [](u32 value) { return value == 0; }));
  // Exercise the individual dispatch as well as the queued batch shader.
  work.right = work.bottom = 17;
  CHECK(hardware.draw(work));
  const auto individual_statistics = hardware.synchronize(efb, depths);
  CHECK(individual_statistics.fragments == 17 * 17);
  CHECK(individual_statistics.fallback_rectangles == 0);
  for (unsigned y = 0; y < 17; ++y)
    for (unsigned x = 0; x < 17; ++x)
      CHECK(efb[y * 640 + x] == 0x272727);
  work.right = work.bottom = 1;
  std::mt19937 generator(0x338e1cb);
  for (unsigned profile = 0; profile < 512; ++profile) {
    const u32 exponent = u32(100 + generator() % 20);
    const u32 mantissa = u32(generator() & 0x7fffff);
    work.reciprocal_w.value = std::bit_cast<float>((exponent << 23) | mantissa);
    const float expected_u = float(64 + generator() % 1024) / 128.0f;
    const float inverse = 1.0f / work.reciprocal_w.value;
    work.textures[0][2].value = profile < 256
                                    ? 0
                                    : std::bit_cast<float>(u32(((110 + generator() % 20) << 23) |
                                                               (generator() & 0x7fffff)));
    const float projected_q = work.textures[0][2].value * inverse;
    const float scale = projected_q != 0 ? inverse / projected_q : inverse;
    work.textures[0][0].value = expected_u / scale;
    inputs.uv[0][0] = work.textures[0][0].value * scale;
    const auto sampled = software.shade(inputs);
    CHECK(sampled.has_value());
    CHECK(hardware.draw(work));
    const auto profile_statistics = hardware.synchronize(efb, depths);
    CHECK(profile_statistics.fragments == 1);
    CHECK(profile_statistics.fallback_rectangles == 0);
    const u32 wanted = (u32((*sampled)[0]) << 16) | (u32((*sampled)[1]) << 8) | (*sampled)[2];
    if (efb[0] != wanted)
      throw std::runtime_error("Projected reciprocal profile " + std::to_string(profile) +
                               " software color=" + std::to_string(wanted) +
                               " hardware color=" + std::to_string(efb[0]));
  }
  std::cout << "Verified projected division at a fixed-point texture boundary\n";
}

void division_fragment_checks(bool projection, bool fog) {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  // One constant RGBA8 tile makes the coordinate latch an independent oracle:
  // the color remains fixed even when the projected coordinates change.
  std::array<u8, 64> texels{};
  for (unsigned texel = 0; texel < 16; ++texel) {
    texels[texel * 2] = 255;
    texels[texel * 2 + 1] = 200;
    texels[32 + texel * 2] = 20;
    texels[33 + texel * 2] = 40;
  }
  memory.copy_in(0x30000, texels);
  gfx::HardwareRenderer renderer(memory, tmem);
  const auto get_integer = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
  std::array<std::array<GLuint, 2>, 2> programs{};
  std::vector<u32> pixels(640 * 528), depths(pixels.size());
  u64 dispatches = 0;
  unsigned cases = 0;
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    if ((scenario == 0 && !projection) || (scenario != 0 && !fog))
      continue;
    for (unsigned family = 0; family < 2; ++family)
      for (const bool batch : {false, true})
        for (unsigned member = 0; member < 2; ++member) {
          std::array<u32, 256> bp{};
          gfx::TevRegisters colors{}, konst{};
          // An unused indirect prefetch selects the generic family. Its map is
          // valid, and no indirect matrix changes the direct coordinates.
          bp[0] = 1 | (1 << 4) | ((family == 0 ? 1u : 0u) << 16);
          bp[0x28] = scenario == 0 ? 64 : 0;
          bp[0x40] = 1 | (7 << 1) | 16;
          bp[0x41] = 24;
          bp[0x88] = 6 << 20;
          bp[0x94] = 0x30000 >> 5;
          bp[0xc0] = (scenario == 0 ? 0xfff8 : 0xfffa) | (1 << 19);
          bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | ((scenario == 0 ? 4 : 5) << 4) | (1 << 19);
          bp[0xf3] = (7 << 16) | (7 << 19);
          bp[0xf6] = 4;
          bp[0xf7] = 14;
          if (scenario != 0) {
            bp[0xee] = (scenario == 1 ? 127u : 125u) << 11;
            bp[0xf1] = (1 << 21) | (scenario == 1 ? 1 << 20 : 0);
            if (scenario == 2)
              bp[0xef] = member == 0 ? 8388707 : 8388709;
          }
          gfx::RasterWork work{};
          work.left = 13;
          work.top = 11;
          work.right = work.left + (batch ? 1 : 17);
          work.bottom = work.top + (batch ? 1 : 17);
          work.edges.fill({0, 0, 1});
          const u32 depth = scenario == 1 ? 8388607 + member : 100;
          work.depth.value = float(depth);
          work.reciprocal_w.value = scenario == 0 ? 4.0f : 1.0f;
          for (unsigned channel = 0; channel < 4; ++channel)
            work.colors[0][channel].value = 255;
          work.textures[0][0].value = 1;
          work.textures[0][1].value = 2;
          work.textures[0][2].value = scenario == 0 && member == 0 ? 3.0f : 4.0f;
          // F1 divides adjacent depths by16777215; its factors are127,128.
          // F2 divides4194303.75 by8388607/8388609; its factors are128,127.
          // White-to-black fog therefore has these exact gray outputs.
          const u32 expected_color = scenario == 0   ? 0xc81428
                                     : scenario == 1 ? (member == 0 ? 0x808080 : 0x7f7f7f)
                                                     : (member == 0 ? 0x7f7f7f : 0x808080);
          std::fill(pixels.begin(), pixels.end(), 0x315a87);
          std::fill(depths.begin(), depths.end(), 0x54321);
          auto expected_pixels = pixels, expected_depths = depths;
          for (int y = work.top; y < work.bottom; ++y)
            for (int x = work.left; x < work.right; ++x) {
              expected_pixels[std::size_t(y) * 640 + x] = expected_color;
              expected_depths[std::size_t(y) * 640 + x] = depth;
            }
          renderer.upload(pixels, depths);
          CHECK(renderer.begin(bp, colors, konst));
          CHECK(renderer.draw(work));
          // This object is only a recipient for GPU retained-state readback.
          // It never shades a pixel or computes an expected result.
          gfx::FragmentShader retained(memory, bp, colors, konst, tmem);
          const auto stats = renderer.synchronize(pixels, depths, &retained);
          CHECK(stats.dispatches == ++dispatches);
          CHECK(stats.fragments == (batch ? 1u : 17u * 17u));
          CHECK(stats.fallback_rectangles == 0);
          CHECK(renderer.fallback_reason().empty());
          CHECK(pixels == expected_pixels);
          CHECK(depths == expected_depths);
          if (scenario == 0) {
            // Q=3 gives RN(1/3)*128 and RN(2/3)*128, truncated to42,85.
            // Q=4 is an independent input sensitivity control:32,64.
            const std::array<s32, 2> coordinates =
                member == 0 ? std::array<s32, 2>{42, 85} : std::array<s32, 2>{32, 64};
            const auto state = retained.texture_state();
            CHECK(state.coordinates == coordinates);
            CHECK((state.texture == gfx::Color{200, 20, 40, 255}));
            CHECK((state.raw == gfx::Color{200, 20, 40, 255}));
          }
          GLint program = 0;
          get_integer(GL_CURRENT_PROGRAM, &program);
          CHECK(program != 0);
          auto &observed = programs[family][batch];
          if (!observed)
            observed = static_cast<GLuint>(program);
          CHECK(observed == static_cast<GLuint>(program));
          ++cases;
        }
  }
  const std::array<GLuint, 4> ids{programs[0][0], programs[0][1], programs[1][0], programs[1][1]};
  for (unsigned i = 0; i < ids.size(); ++i)
    for (unsigned j = i + 1; j < ids.size(); ++j)
      CHECK(ids[i] != ids[j]);
  CHECK(cases == 8 * ((projection ? 1u : 0u) + (fog ? 2u : 0u)));
  std::cout << "Verified " << cases
            << " analytic unequal-division projection/fog draws across four raster programs\n";
}

void projective_mip_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024), texels;
  for (unsigned level = 0; level < 6; ++level) {
    const unsigned size = 32 >> level;
    for (unsigned block_y = 0; block_y < (size + 3) / 4; ++block_y)
      for (unsigned block_x = 0; block_x < (size + 7) / 8; ++block_x)
        for (unsigned y = 0; y < 4; ++y)
          for (unsigned x = 0; x < 8; ++x)
            texels.push_back(
                static_cast<u8>(level * 41 + (block_x * 8 + x) * 13 + (block_y * 4 + y) * 23));
  }
  memory.copy_in(0x30000, texels);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0x84] = 80 << 8;
  bp[0x88] = 31 | (31 << 10) | (1 << 20);
  bp[0x94] = 0x30000 >> 5;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = (7 << 16) | (7 << 19);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer hardware(memory, tmem);
  std::vector<u32> efb(640 * 528), depths(efb.size());
  hardware.upload(efb, depths);
  std::mt19937 generator(0x574d6970);
  const auto fraction = [&] { return float(generator() & 65535) / 65536.0f; };
  for (unsigned profile = 0; profile < 1280; ++profile) {
    bp[0x80] = (profile % 3) | (((profile / 3) % 3) << 2) | ((profile & 1) << 4) |
               ((1 + profile % 2) << 5) | (((profile >> 1) & 1) << 7) |
               (((profile >> 2) & 1) << 8) | ((generator() & 255) << 9);
    bp[0x84] = 80 << 8;
    if (profile >= 512 && profile < 1024) {
      // Matched filters make a fixed mip independent of derivatives. Keep the
      // neighboring mismatched-filter states in the same differential sweep:
      // filter selection still uses the unclamped LOD, including at LOD zero.
      constexpr std::array<unsigned, 16> lods{0,  1,  7,  8,  15, 16, 23, 24,
                                              31, 32, 47, 48, 63, 64, 79, 80};
      const unsigned mode = (profile >> 1) & 3, lod = lods[(profile >> 3) & 15];
      const unsigned mag = profile & 1, min = profile & 128 ? mag ^ 1 : mag;
      bp[0x80] = (bp[0x80] & ~u32(16 | 96 | 128)) | (mag << 4) | (mode << 5) | (min << 7);
      bp[0x84] = lod | (lod << 8);
    }
    if (profile >= 1024) {
      // With no mip selection, matched filters also ignore unequal or inverted
      // LOD bounds. Neither bound may change the fixed-point bilinear taps.
      const unsigned linear = profile & 1;
      bp[0x80] = (bp[0x80] & ~u32(16 | 96 | 128)) | (linear << 4) | (linear << 7);
      bp[0x84] = (profile & 255) | (((profile * 17) & 255) << 8);
    }
    gfx::RasterWork work{};
    work.right = work.bottom = 4;
    work.edges.fill({0, 0, 1});
    const float w = std::ldexp(1.0f + fraction(), int(generator() % 24) - 20);
    work.reciprocal_w = {w, w * (fraction() - 0.5f) / 16.0f, w * (fraction() - 0.5f) / 16.0f};
    work.textures[0][2] = {0.125f + fraction(), (fraction() - 0.5f) / 64.0f,
                           (fraction() - 0.5f) / 64.0f};
    for (unsigned component = 0; component < 2; ++component)
      work.textures[0][component] = {(fraction() - 0.5f) * 64.0f,
                                     (fraction() - 0.5f) * 8.0f,
                                     (fraction() - 0.5f) * 8.0f,
                                     fraction(),
                                     fraction(),
                                     int(generator() % 5),
                                     int(generator() % 5)};
    const auto projected = [&](int x, int y) {
      const float reciprocal = 1.0f / work.reciprocal_w.at(x, y);
      const float q = work.textures[0][2].at(x, y) * reciprocal;
      const float projection = reciprocal / q;
      return std::array{work.textures[0][0].at(x, y) * projection,
                        work.textures[0][1].at(x, y) * projection};
    };
    std::array<u32, 16> expected{};
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        gfx::FragmentInputs inputs;
        inputs.uv[0][0] = projected(x, y)[0];
        inputs.uv[0][1] = projected(x, y)[1];
        const auto origin = projected(x & ~1, y & ~1);
        const auto dx = projected((x & ~1) + 1, y & ~1);
        const auto dy = projected(x & ~1, (y & ~1) + 1);
        for (unsigned component = 0; component < 2; ++component) {
          inputs.dx[0][component] = dx[component] - origin[component];
          inputs.dy[0][component] = dy[component] - origin[component];
        }
        const auto result = software.shade(inputs);
        CHECK(result.has_value());
        expected[static_cast<unsigned>(y * 4 + x)] =
            (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
      }
    CHECK(hardware.begin(bp, colors, konst));
    CHECK(hardware.draw(work));
    const auto stats = hardware.synchronize(efb, depths);
    CHECK(stats.dispatches == profile + 1);
    CHECK(stats.fragments == 16);
    CHECK(stats.fallback_rectangles == 0);
    for (unsigned y = 0; y < 4; ++y)
      for (unsigned x = 0; x < 4; ++x)
        if (efb[y * 640 + x] != expected[y * 4 + x])
          throw std::runtime_error("Projective mip profile " + std::to_string(profile) +
                                   " pixel=" + std::to_string(x) + "," + std::to_string(y) +
                                   " software color=" + std::to_string(expected[y * 4 + x]) +
                                   " hardware color=" + std::to_string(efb[y * 640 + x]));
  }
  std::cout << "Verified 1,280 varying-W/Q mip and fixed-LOD profiles with exact sampled colors\n";
}

void projective_stage_benchmark(bool fixed_lod = false) {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u8, 32> texels{};
  for (unsigned row = 0; row < 4; ++row)
    texels[row * 8 + 3] = 255;
  memory.copy_in(0x30000, texels);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  bp[0] = (fixed_lod ? 8 : 1) | (15 << 10);
  bp[0x41] = 24;
  bp[0x80] = fixed_lod ? 0x191 : 16 | 1;
  bp[0x88] = 7 | (3 << 10) | (1 << 20);
  bp[0x94] = 0x30000 >> 5;
  bp[0xf3] = (7 << 16) | (7 << 19);
  for (unsigned stage = 0; stage < 16; ++stage) {
    const u32 coordinate = fixed_lod ? (stage & 7) << 3 : 0;
    bp[0x28 + stage / 2] |= (64 | coordinate) << ((stage & 1) * 12);
    bp[0xc0 + stage * 2] = 0xfff8 | (1 << 19);
    bp[0xc1 + stage * 2] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  }
  gfx::RasterWork work{};
  work.right = work.bottom = 128;
  work.edges.fill({0, 0, 1});
  work.reciprocal_w.value = 0x1.e0872ap-20f;
  for (auto &texture : work.textures) {
    texture[0].value = 0x1.38a0d6p+7f;
    texture[1].value = 0;
    texture[2].value = 0x1.9df960p-2f;
  }
  gfx::HardwareRenderer hardware(memory, tmem);
  std::vector<u32> efb(640 * 528), depths(efb.size());
  hardware.upload(efb, depths);
  CHECK(hardware.begin(bp, colors, konst));
  // Compile and warm the exact sampled pipeline before timing submission and completion.
  CHECK(hardware.draw(work));
  hardware.synchronize(efb, depths);
  const auto started = std::chrono::steady_clock::now();
  for (unsigned draw = 0; draw < 64; ++draw)
    CHECK(hardware.draw(work));
  const auto stats = hardware.synchronize(efb, depths);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  CHECK(stats.fragments == 64 * 128 * 128);
  CHECK(stats.fallback_rectangles == 0);
  for (unsigned y = 0; y < 528; ++y)
    for (unsigned x = 0; x < 640; ++x)
      CHECK(efb[y * 640 + x] == (x < 128 && y < 128 ? 0x272727u : 0));
  std::cout << "Verified 64 projected " << (fixed_lod ? "fixed-LOD " : "")
            << "16-stage rectangles: seconds=" << seconds << '\n';
}

void state_upload_checks(bool measure) {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  bp[0x41] = 24;
  bp[0xc0] = 0xfff0 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (1 << 19);
  bp[0xf3] = (7 << 16) | (7 << 19);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer hardware(memory, tmem);
  std::vector<u32> efb(640 * 528), depths(efb.size());
  hardware.upload(efb, depths);
  std::array<u32, 9> expected{};
  for (unsigned profile = 0; profile < expected.size(); ++profile) {
    colors[0] = {s16(19 + profile * 11), s16(37 + profile * 7), s16(53 + profile * 3), 255};
    konst[0] = {s16(13 + profile * 9), s16(29 + profile * 5), s16(43 + profile * 13), 255};
    bp[0xc0] = (profile % 3 == 2 ? 0xfffe : 0xfff0) | (1 << 19);
    bp[0xf6] = 12 << 4;
    if (profile == 4) {
      CHECK(hardware.begin(bp, colors, konst));
      const u32 valid = bp[0x10];
      bp[0x10] = 1 << 20;
      CHECK(!hardware.begin(bp, colors, konst));
      bp[0x10] = valid;
    }
    for (unsigned repeat = 0; repeat < 8; ++repeat)
      CHECK(hardware.begin(bp, colors, konst));
    gfx::RasterWork work{};
    work.left = int(profile);
    work.right = work.left + 1;
    work.bottom = 1;
    work.edges.fill({0, 0, 1});
    CHECK(hardware.draw(work));
    const auto result = software.shade(gfx::FragmentInputs{});
    CHECK(result.has_value());
    expected[profile] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
  }
  const auto stats = hardware.synchronize(efb, depths);
  CHECK(stats.dispatches == expected.size());
  CHECK(stats.fragments == expected.size());
  CHECK(stats.fallback_rectangles == 0);
  CHECK(std::equal(expected.begin(), expected.end(), efb.begin()));
  CHECK(
      std::all_of(efb.begin() + expected.size(), efb.end(), [](u32 value) { return value == 0; }));
  // A rectangle-level diagnostic must not invalidate an otherwise reusable BP
  // state or preserve that diagnostic into its next successful begin.
  gfx::RasterWork recovery{};
  recovery.top = 1;
  recovery.right = 1;
  recovery.bottom = 2;
  recovery.edges.fill({0, 0, 1});
  recovery.depth.value = std::numeric_limits<float>::infinity();
  CHECK(hardware.begin(bp, colors, konst));
  CHECK(!hardware.draw(recovery));
  CHECK(!hardware.fallback_reason().empty());
  CHECK(hardware.begin(bp, colors, konst));
  CHECK(hardware.fallback_reason().empty());
  recovery.depth.value = 0;
  CHECK(hardware.draw(recovery));
  const auto recovered = hardware.synchronize(efb, depths);
  CHECK(recovered.fragments == 1 && recovered.dispatches == expected.size() + 1);
  CHECK(recovered.fallback_rectangles == 1);
  CHECK(efb[640] == expected.back());
  if (measure) {
    for (unsigned stages : {1u, 4u, 16u}) {
      bp[0] = (stages - 1) << 10;
      for (unsigned stage = 1; stage < stages; ++stage) {
        bp[0xc0 + stage * 2] = bp[0xc0];
        bp[0xc1 + stage * 2] = bp[0xc1];
      }
      CHECK(hardware.begin(bp, colors, konst));
      const auto started = std::chrono::steady_clock::now();
      for (unsigned repeat = 0; repeat < 200000; ++repeat)
        CHECK(hardware.begin(bp, colors, konst));
      const double seconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::cout << "Verified 200,000 unchanged hardware-state begins: stages=" << stages
                << ", seconds=" << seconds << '\n';
    }
  }
  std::cout << "Verified queued BP/TEV/konst state changes and rejected-state continuation\n";
}

void checks(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto draw = [&](u8 opcode, std::span<const std::array<float, 3>> positions) {
    software.draw(opcode, positions, colors);
    hardware.draw(opcode, positions, colors);
    equal(software, hardware);
  };
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches >= 2);
  CHECK(hardware.graphics.hardware_stats().fragments == 4096);
  bp(0x22, 18 | (24 << 8));
  draw(0xa8, quad);
  draw(0xb0, quad);
  draw(0xb8, quad);
  draw(0x98, quad);
  draw(0xa0, quad);
  bp(0, (1 << 4) | (1 << 19));
  draw(0x90, std::span(quad).first(3));
  bp(0, 1 << 4);
  bp(0x43, 1);
  draw(0x80, quad);
  for (u32 logic = 0; logic < 16; ++logic) {
    bp(0x41, 24 | 2 | (logic << 12));
    draw(0x80, quad);
  }
  for (u32 source = 0; source < 8; ++source)
    for (u32 destination = 0; destination < 8; ++destination) {
      bp(0x41, 24 | 1 | (source << 8) | (destination << 5));
      draw(0x80, quad);
    }
  bp(0x41, 24 | 1 | 2048);
  draw(0x80, quad);
  bp(0x41, 24 | 4);
  draw(0x80, quad);
  bp(0x41, 16);
  bp(0x42, 256 | 91);
  draw(0x80, quad);
  bp(0x42, 0);
  bp(0x41, 24);
  std::mt19937 generator(0x457b291);
  for (unsigned test = 0; test < 96; ++test) {
    const unsigned stages = 1 + generator() % 4;
    bp(0, (1 << 4) | ((stages - 1) << 10));
    for (unsigned i = 0; i < 8; ++i)
      bp(static_cast<u8>(0xe0 + i), generator() & 0x7fffff);
    for (unsigned stage = 0; stage < stages; ++stage) {
      u32 cc = generator() & 0xffffff, ac = generator() & 0xffffff;
      for (unsigned operand = 0; operand < 4; ++operand) {
        unsigned color = (cc >> (operand * 4)) & 15;
        if (color == 8 || color == 9)
          cc = (cc & ~(15u << (operand * 4))) | (10u << (operand * 4));
        if (((ac >> (4 + operand * 3)) & 7) == 4)
          ac = (ac & ~(7u << (4 + operand * 3))) | (5u << (4 + operand * 3));
      }
      bp(static_cast<u8>(0xc0 + stage * 2), cc);
      bp(static_cast<u8>(0xc1 + stage * 2), ac & ~15u);
    }
    bp(0xf3, generator() & 0xffffff);
    bp(0x40, generator() & 31);
    bp(0x43, (generator() & 1) | ((generator() & 1) << 6));
    const std::array<std::array<float, 3>, 3> triangle{
        {{-1.2f, -0.85f, -0.4f}, {0.7f, -1.2f, -0.6f}, {0.9f, 0.8f, -0.3f}}};
    draw(0x90, triangle);
  }
  // Synchronization preserves raw EFB interpretation, CPU pokes, and clear/copy ordering.
  for (auto *fixture : {&software, &hardware}) {
    fixture->graphics.write_efb(0x08000000, 0x7f123456);
    fixture->graphics.write_efb(0x08400000, 0xab123456);
  }
  equal(software, hardware);
  bp(0, 1 << 4);
  bp(0xc0, 0xfffa | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  bp(0x40, 31);
  bp(0xf3, (7 << 16) | (7 << 19));
  bp(0x49, 0);
  bp(0x4a, 63 | (63 << 10));
  bp(0x4d, 4);
  bp(0x4e, 256);
  bp(0x4b, 0x10000 >> 5);
  bp(0x4f, 0x1234);
  bp(0x50, 0xabcd);
  bp(0x51, 0x654321);
  bp(0x52, (1 << 14) | (1 << 11));
  CHECK(std::equal(software.memory.range(0x10000, 64 * 128).begin(),
                   software.memory.range(0x10000, 64 * 128).end(),
                   hardware.memory.range(0x10000, 64 * 128).begin()));
  equal(software, hardware);
  draw(0x80, quad);
  // Exponential fog executes in hardware and preserves the CPU's final factor.
  bp(0xf1, 4 << 21);
  const auto before_exponential = hardware.graphics.hardware_stats().dispatches;
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches > before_exponential);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  bp(0xf1, 0);
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  const auto fog_word = [](float value) {
    const u32 bits = std::bit_cast<u32>(value);
    return ((bits >> 12) & 0x7ff) | (((bits >> 23) & 255) << 11) | ((bits >> 12) & 0x80000);
  };
  for (unsigned i = 0; i < 1000; ++i) {
    const float a = float(generator() % 10000) / 1000.0f;
    const float c = float(generator() % 4000) / 1000.0f;
    bp(0xee, fog_word(a));
    bp(0xf1, fog_word(c) | (2 << 21) | ((i & 1) << 20));
    bp(0xef, 16777215);
    bp(0xf0, i % 16);
    bp(0xf2, generator() & 0xffffff);
    const auto before = hardware.graphics.hardware_stats().dispatches;
    const std::array<std::array<float, 3>, 3> triangle{
        {{-1.0f, -1.0f, -0.99f}, {0.9f, -0.8f, -0.01f}, {0.8f, 0.95f, -0.31f}}};
    draw(0x90, triangle);
    CHECK(hardware.graphics.hardware_stats().dispatches > before);
  }
  bp(0xf1, 0);
  // Every implemented tiled texture encoding feeds integer GPU wrapping/filtering.
  std::vector<u8> texture_bytes(16384), palette_bytes(32768);
  for (auto &byte : texture_bytes)
    byte = static_cast<u8>(generator());
  for (auto &byte : palette_bytes)
    byte = static_cast<u8>(generator());
  for (auto *fixture : {&software, &hardware}) {
    fixture->memory.copy_in(0x30000, texture_bytes);
    fixture->memory.copy_in(0x40000, palette_bytes);
    fixture->bp(0x64, 0x40000 >> 5);
    fixture->bp(0x65, 1024 << 10);
    fixture->xf(0x1018, {3 << 6});
    fixture->xf(0x103f, {1});
    fixture->xf(0x1040, {0});
    fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
  }
  bp(0, 1 | (1 << 4));
  bp(0x30, 7);
  bp(0x31, 7);
  bp(0x28, 64);
  bp(0xc0, 0xfff8 | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bp(0x40, 0);
  bp(0x43, 1);
  bp(0x41, 24);
  bp(0x94, 0x30000 >> 5);
  bp(0x98, 1 << 10);
  for (unsigned format : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 10u, 14u}) {
    bp(0x88, 7 | (7 << 10) | (format << 20));
    for (unsigned filter : {0u, 16u, 128u | (1u << 5), 16u | 128u | (2u << 5)})
      for (unsigned wrap = 0; wrap < 3; ++wrap) {
        bp(0x80, filter | wrap | (((wrap + 1) % 3) << 2));
        bp(0x84, (4 << 4) << 8);
        const auto before = hardware.graphics.hardware_stats().dispatches;
        draw(0x80, quad);
        CHECK(hardware.graphics.hardware_stats().dispatches > before);
        CHECK(hardware.graphics.hardware_fallback_reason().empty());
      }
    // A RAM write invalidates cached pixels while unchanged state reuses them.
    for (auto *fixture : {&software, &hardware})
      fixture->memory.write8(0x30000, static_cast<u8>(format * 17));
    draw(0x80, quad);
  }
  // A sampled register survives a subsequent unsupported draw that retains it.
  bp(0x28, 0);
  draw(0x80, quad);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0x28, 64);
  draw(0x80, quad);
  // Split-bank manually preloaded RGBA8 shares the same decoded GPU sampling path.
  bp(0x60, 0x30000 >> 5);
  bp(0x61, 0);
  bp(0x62, 0x10000 >> 5);
  bp(0x63, (3 << 15) | 4);
  bp(0x88, 7 | (7 << 10) | (6 << 20));
  bp(0x8c, 1 << 21);
  bp(0x90, 0x10000 >> 5);
  bp(0x80, 16 | (2 << 2));
  bp(0x84, 0);
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  bp(0x8c, 0);
  bp(0x90, 0);
  // Depth textures execute after alpha and before late depth; early depth keeps
  // the unmodified raster depth while the shader still evaluates texture depth.
  bp(0x40, 1 | 14 | 16);
  for (unsigned encoding = 0; encoding < 3; ++encoding)
    for (unsigned operation = 1; operation < 3; ++operation)
      for (unsigned early : {0u, 64u}) {
        bp(0xf4, 0x153729);
        bp(0xf5, encoding | (operation << 2));
        bp(0x43, 1 | early);
        draw(0x80, quad);
      }
  bp(0xf5, 0);
  bp(0x40, 0);
  for (bool accelerated : {false, true}) {
    Fixture invalid;
    if (accelerated)
      invalid.graphics.set_hardware_rendering(true);
    invalid.bp(0xee, 0);
    invalid.bp(0xef, 500);
    invalid.bp(0xf0, 0);
    invalid.bp(0xf1, 2 << 21);
    CHECK_THROWS(invalid.draw(0x80, quad, colors));
    if (accelerated)
      CHECK(!invalid.graphics.hardware_fallback_reason().empty());
  }
  bp(0x28, 0);
  bp(0xc0, 0xfffa | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  bp(0xf5, 2 | (2 << 2));
  bp(0x40, 1 | 14 | 16);
  const auto before_retained_depth = hardware.graphics.hardware_stats();
  draw(0x80, quad);
  CHECK(hardware.graphics.hardware_stats().dispatches == before_retained_depth.dispatches);
  CHECK(hardware.graphics.hardware_stats().fallback_rectangles >
        before_retained_depth.fallback_rectangles);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0xf5, 0);
  bp(0x40, 0);
  std::cout << "Hardware device: " << hardware.graphics.hardware_device() << '\n';
  std::cout << "Verified dispatches: " << hardware.graphics.hardware_stats().dispatches
            << ", hardware fragments: " << hardware.graphics.hardware_stats().fragments << '\n';
}

void indirect_checks(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto submit = [&] {
    software.draw(0x80, quad, colors);
    hardware.draw(0x80, quad, colors);
  };
  const auto draw = [&] {
    const auto before = hardware.graphics.hardware_stats().dispatches;
    submit();
    equal(software, hardware);
    CHECK(hardware.graphics.hardware_stats().dispatches > before);
    CHECK(hardware.graphics.hardware_fallback_reason().empty());
  };
  std::mt19937 generator(0x318429ba);
  std::vector<u8> pixels(32768);
  for (unsigned unit = 0; unit < 8; ++unit) {
    for (auto &value : pixels)
      value = static_cast<u8>(generator());
    for (auto *fixture : {&software, &hardware})
      fixture->memory.copy_in(0x80000 + unit * 32768, pixels);
    const auto address = static_cast<u8>(0x80 + (unit & 3) + ((unit & 4) << 3));
    bp(static_cast<u8>(address + 8), 7 | (7 << 10) |
                                         ((unit % 3 == 0   ? 6
                                           : unit % 3 == 1 ? 5
                                                           : 14)
                                          << 20));
    bp(static_cast<u8>(address + 12), 0);
    bp(static_cast<u8>(address + 16), 0);
    bp(static_cast<u8>(address + 20), (0x80000 + unit * 32768) >> 5);
  }
  for (auto *fixture : {&software, &hardware}) {
    fixture->xf(0x1018, {(3 << 6) | (6 << 12)});
    fixture->xf(0x103f, {2});
    fixture->xf(0x1040, {0, 0});
    fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
    fixture->floats(24, {0, -2, 0, 0.3f, 3, 0, 0, -0.7f, 0, 0, 0, 1});
  }
  bp(0x30, 7);
  bp(0x31, 7);
  bp(0x32, 7);
  bp(0x33, 7);
  bp(0xf1, 0);
  bp(0xf5, 0);
  bp(0x40, 0);
  bp(0x41, 24);
  bp(0x43, 1);
  for (unsigned test = 0; test < 1024; ++test) {
    const unsigned stages = 1 + generator() % 16, maps = 1 + test % 4;
    bp(0, 2 | (1 << 4) | ((stages - 1) << 10) | (maps << 16));
    u32 orders = 0;
    for (unsigned map = 0; map < 4; ++map)
      orders |= ((1 + map) | ((generator() % 8) << 3)) << (map * 6);
    bp(0x27, orders);
    bp(0x25, generator() & 0xffff);
    bp(0x26, generator() & 0xffff);
    for (unsigned unit = 0; unit < 8; ++unit) {
      const auto address = static_cast<u8>(0x80 + (unit & 3) + ((unit & 4) << 3));
      bp(address, (generator() % 3) | ((generator() % 3) << 2) | (generator() & 400) |
                      ((generator() % 3) << 5) | ((generator() % 256) << 9));
      bp(static_cast<u8>(address + 4), (test % 5 == 0 ? 32 : 0) | (64 << 8));
    }
    for (unsigned matrix = 0; matrix < 3; ++matrix) {
      const unsigned scale = (test + matrix * 7) % 32;
      for (unsigned column = 0; column < 3; ++column) {
        const u32 coefficients = generator() & 0x3fffff;
        bp(static_cast<u8>(6 + matrix * 3 + column),
           coefficients | (((scale >> (column * 2)) & (column == 2 ? 1u : 3u)) << 22));
      }
    }
    for (unsigned stage = 0; stage < stages; ++stage) {
      const unsigned matrix = (test + stage) % 4;
      const u32 indirect = generator() % maps | ((generator() % 4) << 2) |
                           ((generator() % 8) << 4) | ((generator() % 4) << 7) | (matrix << 9) |
                           ((matrix ? generator() % 3 : 0) << 11) | ((generator() % 7) << 13) |
                           ((generator() % 7) << 16) | ((generator() & 1) << 19) |
                           ((stage ? generator() & 1 : 0) << 20);
      bp(static_cast<u8>(0x10 + stage), indirect);
      const unsigned channel = (test + stage) % 4;
      const u32 order = (generator() % 8) | ((generator() % 8) << 3) | 64 |
                        ((channel < 2 ? channel : channel + 3) << 7);
      const auto address = static_cast<u8>(0x28 + stage / 2);
      if ((stage & 1) == 0)
        bp(address, order);
      else {
        // Keep the first stage's order while setting the second packed order.
        const auto first = software.graphics.bp_state(address) & 0xfff;
        bp(address, first | (order << 12));
      }
      const u32 cc = (test % 3 == 0 ? 0xfffa : test % 3 == 1 ? 0xfff8 : 0x08fa) | (1 << 19);
      bp(static_cast<u8>(0xc0 + stage * 2), cc);
      bp(static_cast<u8>(0xc1 + stage * 2), (7 << 13) | (7 << 10) | (7 << 7) |
                                                ((test % 3 == 0 ? 5 : 4) << 4) |
                                                (generator() & 15) | (1 << 19));
    }
    bp(0xf3, test % 11 == 0 ? 0 : (7 << 16) | (7 << 19));
    if (test % 17 == 0)
      for (auto *fixture : {&software, &hardware}) {
        const float magnitude = (test & 1) ? 900000.0f : -900000.0f;
        fixture->floats(12, {magnitude, 0, 0, -5, 0, -magnitude, 0, 7, 0, 0, 0, 1});
      }
    else if (test % 17 == 1)
      for (auto *fixture : {&software, &hardware})
        fixture->floats(12, {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 0, 1});
    try {
      draw();
    } catch (const std::exception &error) {
      throw std::runtime_error("Indirect generated profile " + std::to_string(test) + ": " +
                               error.what());
    }
  }
  bp(0xf3, (7 << 16) | (7 << 19));
  bp(0, 2 | (1 << 4) | (4 << 16));
  bp(0x28, 64);
  bp(0xc0, 0xfff8 | (1 << 19));
  bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19));
  bp(0x10, 3 | (1 << 9));
  submit();
  // A later pending draw only updates map zero. The retained map-three sample
  // from the earlier draw must survive until the subsequent CPU fallback.
  bp(0, 2 | (1 << 4) | (1 << 16));
  bp(0x10, 0);
  submit();
  bp(0, 2 | (1 << 4));
  bp(0x10, 3 | (1 << 9));
  submit();
  equal(software, hardware);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  // First-stage add-previous depends on the previous fragment's final
  // coordinates, so it stays on the ordered CPU path after synchronization.
  bp(0x10, 1 << 20);
  submit();
  equal(software, hardware);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0x10, 0);
  draw();
  for (u32 invalid : {7u << 13, 7u << 16, 1u << 11, (1u << 9) | (3u << 11)}) {
    Fixture invalid_software, invalid_hardware;
    invalid_hardware.graphics.set_hardware_rendering(true);
    invalid_software.bp(0x10, invalid);
    invalid_hardware.bp(0x10, invalid);
    CHECK_THROWS(invalid_software.draw(0x80, quad, colors));
    CHECK_THROWS(invalid_hardware.draw(0x80, quad, colors));
    CHECK(invalid_hardware.graphics.hardware_stats().dispatches == 0);
    CHECK(invalid_hardware.graphics.hardware_stats().fallback_rectangles > 0);
    CHECK(!invalid_hardware.graphics.hardware_fallback_reason().empty());
  }
  bp(0x10, 0);
  draw();
  const auto stats = hardware.graphics.hardware_stats();
  std::cout
      << "Verified 1,024 generated indirect profiles and retained-state fallback boundaries\n";
  std::cout << "Total verified dispatches: " << stats.dispatches
            << ", hardware fragments: " << stats.fragments << '\n';
}

void fog_checks() {
  Fixture software, hardware;
  hardware.graphics.set_hardware_rendering(true);
  const auto bp = [&](u8 reg, u32 value) {
    software.bp(reg, value);
    hardware.bp(reg, value);
  };
  const auto fog_word = [](float value) {
    const u32 bits = std::bit_cast<u32>(value);
    return ((bits >> 12) & 0x7ff) | (((bits >> 23) & 255) << 11) | ((bits >> 12) & 0x80000);
  };
  const std::array<std::array<float, 3>, 3> triangle{
      {{-1.0f, -1.0f, -0.99f}, {0.9f, -0.8f, -0.01f}, {0.8f, 0.95f, -0.31f}}};
  const std::array<u32, 3> colors{0x124878d2, 0xf0d0b090, 0x3456ef78};
  for (auto *fixture : {&software, &hardware})
    fixture->floats(0x101a, {32, 32, 16777215, 374, 374, 16777215});
  std::mt19937 generator(0x77fab389);
  for (unsigned profile = 0; profile < 2048; ++profile) {
    const unsigned type = 1 + profile % 7;
    const bool range = (profile / 7) & 1;
    bp(0xee, fog_word(float(int(generator() % 9217) - 1024) / 4096));
    bp(0xef, 16777215);
    bp(0xf0, generator() % 24);
    bp(0xf1, fog_word(float(int(generator() % 8193) - 2048) / 4096) | (type << 21) |
                 (((profile / 14) & 1) << 20));
    bp(0xf2, generator() & 0xffffff);
    bp(0xe8, (profile % 19 == 0 ? 342 : generator() % 1024) | (range ? 1024 : 0));
    for (unsigned i = 0; i < 5; ++i)
      bp(static_cast<u8>(0xe9 + i), (1 + generator() % 4095) | ((1 + generator() % 4095) << 12));
    if (profile % 29 == 0) {
      bp(0xee, 0x7f800 | ((generator() & 1) << 19));
      bp(0xf1, 0x7f800 | ((generator() & 1) << 19) | (type << 21) | (1 << 20));
    }
    const auto before = hardware.graphics.hardware_stats();
    software.draw(0x90, triangle, colors);
    hardware.draw(0x90, triangle, colors);
    try {
      equal(software, hardware);
    } catch (const std::exception &error) {
      throw std::runtime_error("Fog profile " + std::to_string(profile) +
                               " type=" + std::to_string(type) + " range=" + std::to_string(range) +
                               ": " + error.what());
    }
    CHECK(hardware.graphics.hardware_stats().dispatches > before.dispatches);
    CHECK(hardware.graphics.hardware_stats().fallback_rectangles == before.fallback_rectangles);
    CHECK(hardware.graphics.hardware_fallback_reason().empty());
  }
  // Unused zero table entries stay on the CPU. Valid draws then resume on the
  // GPU after the exact CPU EFB and bounding-box state have been synchronized.
  bp(0xee, fog_word(1));
  bp(0xf1, (2 << 21) | (1 << 20));
  bp(0xe8, 1023 | 1024);
  for (unsigned i = 0; i < 5; ++i)
    bp(static_cast<u8>(0xe9 + i), i == 0 ? (64 << 12) : 0);
  const auto before_fallback = hardware.graphics.hardware_stats();
  software.draw(0x90, triangle, colors);
  hardware.draw(0x90, triangle, colors);
  equal(software, hardware);
  CHECK(hardware.graphics.hardware_stats().dispatches == before_fallback.dispatches);
  CHECK(hardware.graphics.hardware_stats().fallback_rectangles >
        before_fallback.fallback_rectangles);
  CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  bp(0xe8, 0);
  software.draw(0x90, triangle, colors);
  hardware.draw(0x90, triangle, colors);
  equal(software, hardware);
  CHECK(hardware.graphics.hardware_fallback_reason().empty());
  std::cout
      << "Verified 2,048 generated fog profiles, range-table endpoints and fallback ordering\n";
  std::cout << "Fog fixture dispatches: " << hardware.graphics.hardware_stats().dispatches
            << ", hardware fragments: " << hardware.graphics.hardware_stats().fragments << '\n';
}

void fog_bucket_checks() {
  Memory memory(Console::GameCube);
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  bp[0] = 1 << 4;
  bp[0x41] = 24;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xee] = 0x3f800;
  bp[0xf2] = 0x0771e5;
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  gfx::FragmentInputs input;
  input.colors[0] = {255, 173, 67, 203};
  unsigned cases = 0;
  for (unsigned type = 4; type < 8; ++type) {
    bp[0xf1] = (type << 21) | (1 << 20);
    CHECK(renderer.begin(bp, colors, konst));
    gfx::RasterWork work{};
    work.edges.fill({0, 0, 1});
    for (unsigned c = 0; c < 4; ++c)
      work.colors[0][c].value = input.colors[0][c];
    unsigned index = 0;
    for (unsigned bucket = 1; bucket <= 256; ++bucket) {
      // Analytic double-precision crossings locate adjacent 24-bit depths.
      // The expected pixels come from the actual CPU fragment implementation.
      const double magnitude =
          -std::log2(type < 6 ? 1 - double(bucket) / 256 : double(bucket) / 256) / 8;
      double crossing = type & 1 ? std::sqrt(magnitude) : magnitude;
      if (type >= 6)
        crossing = 1 - crossing;
      if (!std::isfinite(crossing) || crossing < 0 || crossing > 1)
        continue;
      const s64 center = static_cast<s64>(std::llround(crossing * 16777215));
      for (s64 delta = -4; delta <= 4; ++delta) {
        u32 depth = static_cast<u32>(std::clamp<s64>(center + delta, 0, 16777215));
        work.left = int(index % 640);
        work.top = int(index / 640);
        work.right = work.left + 1;
        work.bottom = work.top + 1;
        work.depth.value = float(depth);
        const auto result = software.shade(input, &depth);
        CHECK(result.has_value());
        expected[std::size_t(work.top) * 640 + work.left] =
            (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
        CHECK(renderer.draw(work));
        ++index;
        ++cases;
      }
    }
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(stats.fragments == index);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
  }
  bp[0xf1] = (2 << 21) | (1 << 20);
  bp[0xe8] = 342 | 1024;
  CHECK(renderer.begin(bp, colors, konst));
  gfx::RasterWork invalid{};
  invalid.left = invalid.top = 0;
  invalid.right = invalid.bottom = 1;
  invalid.edges.fill({0, 0, 1});
  CHECK(!renderer.draw(invalid));
  u32 invalid_depth = 0;
  CHECK_THROWS(software.shade(input, &invalid_depth));
  for (unsigned i = 0; i < 5; ++i)
    bp[0xe9 + i] = 64 | (64 << 12);
  CHECK(renderer.begin(bp, colors, konst));
  invalid.viewport_width = input.viewport_width = 0;
  CHECK(!renderer.draw(invalid));
  CHECK_THROWS(software.shade(input, &invalid_depth));
  std::cout << "Verified " << cases << " exponential fog bucket-boundary pixels without fallback\n";
}

void latch_state_readback_checks() {
  // One context covers both dispatch paths; each row starts with an explicit upload.
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  constexpr std::array<std::array<gfx::Color, 5>, 2> texels{{{{{17, 51, 85, 119},
                                                               {23, 47, 71, 95},
                                                               {29, 53, 79, 101},
                                                               {31, 59, 83, 107},
                                                               {37, 61, 89, 109}}},
                                                             {{{129, 65, 33, 241},
                                                               {43, 67, 91, 113},
                                                               {47, 71, 97, 127},
                                                               {53, 73, 101, 131},
                                                               {59, 79, 103, 137}}}}};
  const gfx::FragmentTextureState first{
      {85, 51, 17, 119},
      {17, 51, 85, 119},
      {{{23, 47, 71, 95}, {29, 53, 79, 101}, {31, 59, 83, 107}, {37, 61, 89, 109}}},
      {160, -288}};
  const gfx::FragmentTextureState second{
      {33, 65, 129, 241},
      {129, 65, 33, 241},
      {{{43, 67, 91, 113}, {47, 71, 97, 127}, {53, 73, 101, 131}, {59, 79, 103, 137}}},
      {-192, 352}};
  const gfx::FragmentTextureState sentinel{
      {1, 2, 3, 4},
      {5, 6, 7, 8},
      {{{9, 10, 11, 12}, {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}}},
      {-777, 888}};
  const auto seed_recipient = [&] {
    shader.restore_texture_state(sentinel.texture, sentinel.raw);
    shader.restore_indirect_coordinates(sentinel.coordinates);
    for (unsigned stage = 0; stage < 4; ++stage)
      shader.restore_indirect_texture(stage, sentinel.indirect[stage]);
  };
  const auto fill_textures = [&](unsigned generation) {
    for (unsigned unit = 0; unit < 5; ++unit) {
      std::array<u8, 64> tile{};
      const auto &value = texels[generation][unit];
      for (unsigned pixel = 0; pixel < 16; ++pixel) {
        tile[pixel * 2] = value[3];
        tile[pixel * 2 + 1] = value[0];
        tile[32 + pixel * 2] = value[1];
        tile[33 + pixel * 2] = value[2];
      }
      memory.copy_in(0x80000 + unit * 64, tile);
      const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
      bp[address + 8] = 3 | (3 << 10) | (6 << 20);
      bp[address + 20] = (0x80000 + unit * 64) >> 5;
    }
  };
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0x40] = 1 | 14 | 16;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf8] = 6;
  bp[0xf9] = 12;
  // Raster output stays constant; texture swap1 exchanges red and blue.
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19) | 4;
  u64 completed_draws = 0, completed_readbacks = 0;
  for (bool scalar : {false, true}) {
    std::vector<u32> expected_colors(640 * 528, 0x13579b);
    std::vector<u32> expected_depths(expected_colors.size(), 0x2468ac);
    auto gpu_colors = expected_colors, gpu_depths = expected_depths;
    renderer.upload(gpu_colors, gpu_depths);
    u64 draws = 0, pending = 0, readbacks = 0;
    u32 left = 0xffffffff, right = 0, top = 0xffffffff, bottom = 0;
    const auto submit = [&](int x, int y, bool direct, unsigned maps, float s, float t) {
      bp[0] = 1 | (1 << 4) | (maps << 16);
      bp[0x28] = direct ? 64 : 0;
      gfx::RasterWork work{};
      work.left = x;
      work.top = y;
      work.right = x + (scalar ? 17 : 1);
      work.bottom = y + (scalar ? 17 : 1);
      // Integer samples satisfying x>=X, y>=Y and x+y<=X+Y select only (X,Y).
      work.edges = {{{0, -1, 1 - 16 * x}, {1, 0, 1 - 16 * y}, {-1, 1, 1 + 16 * (x + y)}}};
      unsigned covered = 0;
      for (int py = work.top; py < work.bottom; ++py)
        for (int px = work.left; px < work.right; ++px)
          covered += std::all_of(work.edges.begin(), work.edges.end(), [&](const auto &edge) {
            return edge.constant + edge.dx * py * 16 - edge.dy * px * 16 > 0;
          });
      CHECK(covered == 1);
      work.depth.value = 73;
      work.reciprocal_w.value = 1;
      for (unsigned channel = 0; channel < 4; ++channel)
        work.colors[0][channel].value = gfx::Color{73, 151, 217, 255}[channel];
      work.textures[0][0].value = s;
      work.textures[0][1].value = t;
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      expected_colors[y * 640 + x] = 0x4997d9;
      expected_depths[y * 640 + x] = 73;
      ++draws;
      ++pending;
      left = std::min(left, u32(x) & ~1u);
      right = std::max(right, u32(x) | 1u);
      top = std::min(top, u32(y) & ~1u);
      bottom = std::max(bottom, u32(y) | 1u);
    };
    const auto synchronize = [&](const gfx::FragmentTextureState &expected, bool recipient = true) {
      if (pending)
        ++readbacks;
      const auto stats =
          renderer.synchronize(gpu_colors, gpu_depths, recipient ? &shader : nullptr);
      CHECK(stats.dispatches == completed_draws + draws && stats.fragments == pending);
      CHECK(stats.fallback_rectangles == 0);
      CHECK(stats.left == left && stats.right == right && stats.top == top &&
            stats.bottom == bottom);
      CHECK(stats.efb_readbacks == completed_readbacks + readbacks);
      CHECK(stats.efb_readback_bytes == (completed_readbacks + readbacks) *
                                            (gpu_colors.size() + gpu_depths.size()) * sizeof(u32));
      CHECK(gpu_colors == expected_colors && gpu_depths == expected_depths);
      CHECK(shader.texture_state() == expected);
      pending = 0;
      left = top = 0xffffffff;
      right = bottom = 0;
    };
    fill_textures(0);
    seed_recipient();
    submit(7, 7, true, 4, 1.25f, -2.25f);
    synchronize(first); // All six restorations select one pixel.

    submit(7, 7, true, 4, 1.25f, -2.25f);
    submit(7, 7, false, 0, 3.25f, -2.5f);
    auto expected = first;
    expected.coordinates = {416, -320};
    synchronize(expected); // New coordinate key, older texture/map keys, same pixel.
    seed_recipient();
    synchronize(sentinel); // A clean synchronization must not restore old keys.
    fill_textures(1);
    submit(7, 7, true, 4, -1.5f, 2.75f);
    synchronize(second); // Same pixel, new dirty synchronization and new values.

    fill_textures(0);
    submit(7, 7, true, 0, 1.25f, -2.25f);
    submit(8, 7, false, 4, 1, 1);
    fill_textures(1);
    submit(7, 8, false, 3, 2, 2);
    fill_textures(0);
    submit(8, 8, false, 2, 3, 3);
    fill_textures(1);
    submit(9, 8, false, 1, 4, 4);
    submit(10, 8, false, 0, 4.5f, -3.75f);
    expected = first;
    expected.indirect = {
        {{43, 67, 91, 113}, {29, 53, 79, 101}, {53, 73, 101, 131}, {37, 61, 89, 109}}};
    expected.coordinates = {576, -480};
    synchronize(expected); // Six distinct sources span both 2x2 parity bits and blocks.

    fill_textures(1);
    submit(7, 7, true, 4, -1.5f, 2.75f);
    synchronize(expected, false); // A null recipient drains, but does not restore.
    seed_recipient();
    synchronize(sentinel);
    submit(7, 7, true, 4, 2.5f, -1.5f);
    expected = second;
    expected.coordinates = {320, -192};
    synchronize(expected);
    completed_draws += draws;
    completed_readbacks += readbacks;
  }
  std::cout << "Verified coincident, distinct and sequence-prefixed latch state across "
               "scalar/batch synchronizations and null-recipient drains\n";
}

void indirect_state_checks() {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> texture_memory(1024 * 1024);
  gfx::FragmentShader software(memory, bp, colors, konst, texture_memory);
  gfx::FragmentShader hardware(memory, bp, colors, konst, texture_memory);
  gfx::HardwareRenderer renderer(memory, texture_memory);
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(640 * 528, 500);
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  std::mt19937 generator(0x4928301);
  std::array<u8, 256> pixels{};
  for (unsigned unit = 0; unit < 5; ++unit) {
    for (auto &byte : pixels)
      byte = static_cast<u8>(generator());
    memory.copy_in(0x80000 + unit * 1024, pixels);
    const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
    bp[address] = 1 | (1 << 2) | 16;
    bp[address + 8] = 7 | (7 << 10) | (6 << 20);
    bp[address + 20] = (0x80000 + unit * 1024) >> 5;
  }
  bp[0x27] = 1 | (2 << 6) | (3 << 12) | (4 << 18);
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[6] = 513 | (127 << 11) | (1 << 22);
  bp[7] = 65 | (171 << 11) | (3 << 22);
  bp[8] = 8 | (768 << 11) | (1 << 22);
  gfx::RasterWork work{};
  work.left = 3;
  work.top = 5;
  work.right = 13;
  work.bottom = 15;
  work.edges.fill({0, 0, 1});
  work.depth.value = 100;
  work.depth.dx = 5;
  work.depth.dy = 3;
  work.reciprocal_w.value = 1;
  for (auto &color : work.colors)
    for (auto &plane : color) {
      plane.value = 96;
      plane.dx = 2;
      plane.dy = 1;
    }
  work.textures[0][0].value = -4;
  work.textures[0][0].dx = 0.75f;
  work.textures[0][0].dy = 1.25f;
  work.textures[0][1].value = 7;
  work.textures[0][1].dx = -0.5f;
  work.textures[0][1].dy = 0.875f;
  work.textures[1] = work.textures[0];
  work.textures[1][0].value = 3;
  work.textures[1][1].dx = 1.5f;
  u64 hardware_fragments = 0;
  const auto synchronize = [&] {
    const auto stats = renderer.synchronize(gpu_colors, gpu_depths, &hardware);
    hardware_fragments += stats.fragments;
    return stats;
  };
  const auto shade = [&](gfx::FragmentShader &shader, std::vector<u32> &efb,
                         std::vector<u32> &depths) {
    for (int block_y = work.top & ~1; block_y < work.bottom; block_y += 2)
      for (int block_x = work.left & ~1; block_x < work.right; block_x += 2)
        for (int iy = 0; iy < 2; ++iy)
          for (int ix = 0; ix < 2; ++ix) {
            const int x = block_x + ix, y = block_y + iy;
            if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
              continue;
            const unsigned offset = static_cast<unsigned>(y * 640 + x);
            u32 depth = static_cast<u32>(work.depth.at(x, y));
            const auto depth_test = [&] {
              if (!(bp[0x40] & 1))
                return true;
              if (!gfx::compare((bp[0x40] >> 1) & 7, depth, depths[offset]))
                return false;
              if (bp[0x40] & 16)
                depths[offset] = depth;
              return true;
            };
            const bool early = (bp[0x43] & 64) != 0;
            if (early && !depth_test())
              continue;
            gfx::FragmentInputs inputs;
            for (unsigned channel = 0; channel < 4; ++channel)
              inputs.colors[0][channel] = static_cast<u8>(work.colors[0][channel].at(x, y));
            for (unsigned coordinate = 0; coordinate < (bp[0] & 15); ++coordinate)
              for (unsigned component = 0; component < 2; ++component) {
                const auto &plane = work.textures[coordinate][component];
                inputs.uv[coordinate][component] = plane.at(x, y);
                inputs.dx[coordinate][component] =
                    plane.at(block_x + 1, block_y) - plane.at(block_x, block_y);
                inputs.dy[coordinate][component] =
                    plane.at(block_x, block_y + 1) - plane.at(block_x, block_y);
              }
            const auto result = shader.shade(inputs, &depth);
            if (!result || (!early && !depth_test()))
              continue;
            efb[offset] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
          }
  };
  const auto submit = [&](bool expect_hardware = true) {
    const bool supported = renderer.begin(bp, colors, konst);
    CHECK(supported == expect_hardware);
    if (supported)
      CHECK(renderer.draw(work));
    else {
      synchronize();
      shade(hardware, gpu_colors, gpu_depths);
    }
    shade(software, cpu_colors, cpu_depths);
  };
  const auto equal_state = [&] {
    synchronize();
    CHECK(software.texture_state() == hardware.texture_state());
    CHECK(cpu_colors == gpu_colors);
    CHECK(cpu_depths == gpu_depths);
  };
  renderer.upload(gpu_colors, gpu_depths);
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (7 << 4) | (3 << 7);
  submit();
  // An XFB copy uses transient work storage while queued texture/map latches
  // remain available to the next draw and later CPU continuation.
  auto copy_bp = bp;
  copy_bp[0x49] = 0;
  copy_bp[0x4a] = 15 | (15 << 10);
  copy_bp[0x4b] = 0x10000 >> 5;
  copy_bp[0x4d] = 1;
  copy_bp[0x4e] = 256;
  copy_bp[0x52] = (1 << 14) | 3;
  copy_bp[0x53] = (1 << 12) | (63 << 18);
  const auto layout = gfx::copy_efb_xfb(memory, copy_bp, [&](unsigned x, unsigned y) {
    return (cpu_colors[y * 640 + x] << 8) | 255;
  });
  const auto bytes = memory.range(layout.destination, layout.height * layout.stride);
  const std::vector<u8> expected_copy(bytes.begin(), bytes.end());
  CHECK(renderer.copy_xfb(copy_bp));
  CHECK(std::equal(bytes.begin(), bytes.end(), expected_copy.begin()));
  // Queue a disabled direct stage and only one indirect stage before reading
  // back. Unwritten texture/map slots must keep the older dispatch's samples.
  bp[0] = 2 | (1 << 4) | (1 << 16);
  bp[0x28] = 0;
  bp[0x10] = 0;
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  work.textures[0][0].value += 9;
  submit();
  equal_state();
  const auto retained = software.texture_state();
  bp[0] = 2 | (1 << 4) | (4 << 16);
  bp[0x28] = 64;
  bp[0x10] = 3 | (1 << 9) | (1 << 7);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0xf3] = 0;
  work.textures[0][0].value += 3;
  submit();
  equal_state();
  CHECK(software.texture_state() != retained);
  const auto alpha_rejected = software.texture_state();
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0x40] = 1;
  bp[0x43] = 64;
  work.textures[0][0].value += 4;
  submit();
  equal_state();
  CHECK(software.texture_state() == alpha_rejected);
  bp[0x40] = 1 | 2 | 16;
  for (int y = work.top; y < work.bottom; ++y)
    for (int x = work.left; x < work.right; ++x)
      cpu_depths[static_cast<unsigned>(y * 640 + x)] = (x + y) % 3 == 0 ? 50 : 500;
  gpu_depths = cpu_depths;
  renderer.upload(gpu_colors, gpu_depths);
  submit();
  equal_state();
  CHECK(software.texture_state() != alpha_rejected);
  const auto early_partial = software.texture_state();
  bp[0x40] = 1;
  bp[0x43] = 0;
  work.textures[0][0].value += 5;
  submit();
  equal_state();
  CHECK(software.texture_state() != early_partial);
  // Disabled retained-map sampling and cross-fragment accumulation each
  // continue on the CPU with the complete GPU latch restored.
  bp[0] = 2 | (1 << 4);
  bp[0x40] = 0;
  bp[0x10] = 3 | (1 << 9) | (3 << 7);
  submit(false);
  equal_state();
  bp[0x10] = 1 << 20;
  submit(false);
  equal_state();
  std::cout << "Verified full texture/map/coordinate latches across disabled stages and "
               "alpha/early/late depth rejection\n";
  std::cout << "Latch fixture dispatches: " << synchronize().dispatches
            << ", hardware fragments: " << hardware_fragments << '\n';
}

void texture_cache_checks(bool measure) {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0x88] = 255 | (255 << 10) | (9 << 20);
  bp[0x94] = 0x80000 >> 5;
  bp[0x98] = 9;
  std::vector<u8> source(256 * 256);
  for (unsigned i = 0; i < source.size(); ++i)
    source[i] = u8((i * 29) ^ (i >> 8));
  memory.copy_in(0x80000, source);
  for (unsigned i = 0; i < 256; ++i) {
    tmem[9 * 512 + i * 2] = u8(255 - i);
    tmem[9 * 512 + i * 2 + 1] = u8(i);
  }
  gfx::RasterWork work{};
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = 7.25f;
  work.textures[0][1].value = 3.75f;
  work.textures[0][2].value = 0;
  unsigned index = 0;
  const auto submit = [&] {
    work.left = int(index % 640);
    work.top = int(index / 640);
    work.right = work.left + 1;
    work.bottom = work.top + 1;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    gfx::FragmentInputs input;
    input.uv[0] = {work.textures[0][0].value, work.textures[0][1].value};
    u32 depth = 0;
    const auto result = software.shade(input, &depth);
    CHECK(result.has_value());
    expected[index++] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
  };
  const auto verify = [&] {
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
    return stats;
  };
  submit();
  CHECK(verify().texture_decodes == 1);
  CHECK(verify().texture_uploads == 1);
  // Every mode update reaches the shader. Decoder reuse must still invalidate
  // changed RAM or palette bytes, and neither source may be replaced by a hash.
  for (unsigned mode = 0; mode < 24; ++mode) {
    bp[0x80] = mode % 3 | (((mode / 3) % 3) << 2) | ((mode & 1) ? 16 : 0);
    bp[0x84] = mode << 16;
    submit();
  }
  CHECK(verify().texture_decodes == 1);
  CHECK(verify().texture_snapshot_bytes == 24 * (256 * 256 + 512));
  memory.write8(0x80000, 129);
  work.textures[0][0].value = work.textures[0][1].value = 0;
  submit();
  CHECK(verify().texture_decodes == 2);
  tmem[9 * 512 + 129 * 2 + 1] ^= 0x5a;
  submit();
  CHECK(verify().texture_decodes == 3);
  tmem[9 * 512 - 1] ^= 0x5a;
  tmem[9 * 512 + 512] ^= 0x5a;
  submit();
  CHECK(verify().texture_decodes == 3);
  CHECK(verify().texture_uploads == 3);
  std::cout << "Verified exact indexed-texture cache invalidation and sampler updates\n";
  if (!measure)
    return;
  for (unsigned repetition = 0; repetition < 3; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned draw = 0; draw < 1024; ++draw)
      submit();
    verify();
    const auto steady = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    const auto varied_start = std::chrono::steady_clock::now();
    // Sampler changes affect filtering without changing decoded source pixels.
    for (unsigned draw = 0; draw < 64; ++draw) {
      bp[0x80] ^= 16;
      submit();
    }
    verify();
    const auto varied =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - varied_start);
    std::cout << "Texture cache repetition " << repetition + 1
              << ": 1,024 steady draws seconds=" << steady.count()
              << ", 64 sampler updates seconds=" << varied.count() << '\n';
  }
}

void texture_partial_upload_checks() {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  bp[0] = 1 | (2 << 10);
  bp[0x28] = 64 | (65 << 12);
  bp[0x29] = 66;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  for (unsigned unit = 0; unit < 3; ++unit) {
    const unsigned address = 0x80 + unit;
    bp[address + 8] = 7 | (7 << 10) | (1 << 20);
    bp[address + 20] = (0x80000 + unit * 0x1000) >> 5;
    std::vector<u8> source(1024, u8(20 + unit * 10));
    memory.copy_in(0x80000 + unit * 0x1000, source);
    // Later stages add half their texture to the preceding result, making
    // every resident unit contribute to the checked output.
    bp[0xc0 + unit * 2] = (unit ? 0xf8d0 : 0xfff8) | (1 << 19);
    bp[0xc1 + unit * 2] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  }
  gfx::RasterWork work{};
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = work.textures[0][1].value = 0;
  unsigned index = 0;
  const auto submit = [&](bool synchronize = true) {
    work.left = int(index);
    work.right = work.left + 1;
    work.bottom = 1;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    gfx::FragmentInputs input;
    input.uv[0] = {work.textures[0][0].value, work.textures[0][1].value};
    u32 depth = 0;
    const auto result = software.shade(input, &depth);
    CHECK(result.has_value());
    expected[index++] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
    if (!synchronize)
      return gfx::HardwareStats{};
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(pixels == expected);
    return stats;
  };
  constexpr u64 table_bytes = 88 * sizeof(u32), unit_bytes = 64 * sizeof(u32);
  auto stats = submit();
  CHECK(stats.texture_upload_bytes == table_bytes + 3 * unit_bytes);
  CHECK(submit().texture_upload_bytes == stats.texture_upload_bytes);
  for (unsigned unit = 0; unit < 3; ++unit) {
    memory.write8(0x80000 + unit * 0x1000, u8(61 + unit));
    const auto next = submit();
    CHECK(next.texture_decodes == stats.texture_decodes + 1);
    CHECK(next.texture_uploads == stats.texture_uploads + 1);
    CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == table_bytes + unit_bytes);
    stats = next;
  }
  // A queued rectangle must see the old source before an in-place upload
  // changes the unit. Check both pixels only after the second submission.
  memory.write8(0x80000, 17);
  submit(false);
  memory.write8(0x80000, 18);
  auto next = submit();
  CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == 2 * (table_bytes + unit_bytes));
  stats = next;
  // Changing a middle unit's dimensions shifts the following packed range.
  bp[0x89] = 15 | (7 << 10) | (1 << 20);
  next = submit();
  CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == table_bytes + 4 * unit_bytes);
  stats = next;
  // Removing and restoring a middle unit must rebuild even if others reuse
  // their cached decodes and the final texture still occupies the last stage.
  bp[0x28] = 64 | (64 << 12);
  next = submit();
  CHECK(next.texture_decodes == stats.texture_decodes);
  CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == table_bytes + 2 * unit_bytes);
  stats = next;
  bp[0x28] = 64 | (65 << 12);
  next = submit();
  CHECK(next.texture_decodes == stats.texture_decodes);
  CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == table_bytes + 4 * unit_bytes);
  // Equal decoded sizes can hide a changed mip layout. The mip table must
  // update even when the unit and all following units retain their ranges.
  std::vector<u8> mip_source(1024);
  for (unsigned i = 0; i < mip_source.size(); ++i)
    mip_source[i] = u8((i * 7) % 73);
  memory.copy_in(0x80000, mip_source);
  bp[0x88] = (15 << 10) | (1 << 20);
  bp[0x80] = 1 << 5;
  bp[0x84] = 32 | (32 << 8);
  stats = submit();
  bp[0x88] = 1 | (10 << 10) | (1 << 20);
  bp[0x84] = 16 | (16 << 8);
  next = submit();
  CHECK(next.texture_decodes == stats.texture_decodes + 1);
  CHECK(next.texture_upload_bytes - stats.texture_upload_bytes == table_bytes + 28 * sizeof(u32));
  std::cout << "Verified partial texture uploads and packed-layout rebuilds\n";
}

void texture_cache_bank_checks() {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  gfx::HardwareRenderer renderer(memory, tmem);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size()), expected(pixels.size());
  renderer.upload(pixels, depths);
  bp[0] = 1;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (4 << 4) | (1 << 19);
  bp[0x94] = 0x80000 >> 5;
  bp[0x98] = 9;
  gfx::RasterWork work{};
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = work.textures[0][1].value = work.textures[0][2].value = 0;
  unsigned index = 0;
  const auto submit = [&] {
    work.left = int(index);
    work.right = work.left + 1;
    work.bottom = 1;
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    gfx::FragmentInputs input;
    u32 depth = 0;
    const auto result = software.shade(input, &depth);
    CHECK(result.has_value());
    expected[index++] = (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
    const auto stats = renderer.synchronize(pixels, depths);
    CHECK(stats.fallback_rectangles == 0);
    CHECK(pixels == expected);
    CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 0; }));
    return stats;
  };
  for (unsigned format : {8u, 9u, 10u}) {
    const unsigned palette_bytes = format == 8 ? 32 : format == 9 ? 512 : 32768;
    bp[0x88] = 7 | (7 << 10) | (format << 20);
    memory.write16(0x80000, format == 10 ? 0x3fff : 0xffff);
    auto stats = submit();
    tmem[9 * 512 + palette_bytes - 1] ^= 0x3d;
    CHECK(submit().texture_decodes == stats.texture_decodes + 1);
    stats = renderer.synchronize(pixels, depths);
    tmem[9 * 512 - 1] ^= 0x7a;
    tmem[9 * 512 + palette_bytes] ^= 0x6b;
    CHECK(submit().texture_decodes == stats.texture_decodes);
    bp[0x98] |= 1 << 10;
    CHECK(submit().texture_decodes == stats.texture_decodes + 1);
    bp[0x98] &= ~(1 << 10);
  }
  // RGBA8's two preloaded banks have independent byte ranges. Source RAM,
  // padding after either bank and palette state cannot invalidate this decode.
  bp[0x88] = 3 | (3 << 10) | (6 << 20);
  bp[0x8c] = (1 << 21) | (0x10000 >> 5);
  bp[0x90] = 0x20000 >> 5;
  tmem[0x10000] = 197;
  tmem[0x10001] = 13;
  tmem[0x20000] = 41;
  tmem[0x20001] = 103;
  auto stats = submit();
  tmem[0x10001] ^= 0x1f;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  tmem[0x20000] ^= 0x2b;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  tmem[0x10000 - 1] ^= 1;
  tmem[0x10000 + 32] ^= 1;
  tmem[0x20000 - 1] ^= 1;
  tmem[0x20000 + 32] ^= 1;
  memory.write8(0x80000, 27);
  bp[0x94] ^= 1;
  bp[0x98] ^= 7;
  CHECK(submit().texture_decodes == stats.texture_decodes);
  bp[0x90] += 1;
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  // Uneven mip dimensions use the decoder's minimum-tile offsets. The checked
  // source extent also includes every tile reached by each mip's texel reads.
  bp[0x8c] = bp[0x90] = 0;
  bp[0x94] = 0x80000 >> 5;
  bp[0x88] = 10 | (8 << 10) | (1 << 20);
  bp[0x80] = 1 << 5;
  bp[0x84] = 32 | (32 << 8);
  stats = submit();
  // Point and linear mip modes with the same reachable level count reuse data.
  bp[0x80] = 2 << 5;
  CHECK(submit().texture_decodes == stats.texture_decodes);
  memory.write8(0x80000 + 99 + 32, 233);
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  stats = renderer.synchronize(pixels, depths);
  bp[0x84] = 32 | (48 << 8);
  CHECK(submit().texture_decodes == stats.texture_decodes + 1);
  std::cout
      << "Verified C4/C8/C14X2 palette bounds, split-bank RGBA8 and uneven mip invalidation\n";
}

void xfb_checks(bool measure) {
  Memory cpu_memory{Console::Wii}, gpu_memory{Console::Wii};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(gpu_memory, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size(), 0x176543), rgba(pixels.size());
  std::array<u32, 256> bp{};
  std::mt19937 generator(0x9182342);
  const auto color = [&](unsigned x, unsigned y) { return rgba[y * 640 + x]; };
  const auto fill = [&](unsigned format) {
    for (unsigned i = 0; i < pixels.size(); ++i) {
      pixels[i] = generator() & 0xffffff;
      if (format == 1) {
        rgba[i] = 0;
        for (unsigned c = 0; c < 4; ++c) {
          const u32 value = (pixels[i] >> (18 - c * 6)) & 63;
          rgba[i] = (rgba[i] << 8) | (value << 2) | (value >> 4);
        }
      } else
        rgba[i] = (pixels[i] << 8) | 255;
    }
    renderer.upload(pixels, depths);
    bp[0x43] = format;
  };
  constexpr std::array<unsigned, 9> widths{16, 18, 20, 32, 48, 64, 128, 320, 640};
  constexpr std::array<unsigned, 8> heights{1, 3, 4, 7, 16, 32, 64, 128};
  constexpr std::array<unsigned, 12> scales{64,  85,  128, 170, 192, 256,
                                            257, 320, 384, 512, 768, 1024};
  constexpr std::size_t checked_bytes = 640 * 528 * 8;
  unsigned valid = 0, invalid = 0;
  u64 compared = 0;
  for (unsigned format = 0; format < 4; ++format) {
    fill(format);
    for (unsigned profile = 0; profile < 512; ++profile) {
      const unsigned width = widths[generator() % widths.size()];
      const unsigned height = heights[generator() % heights.size()];
      const unsigned top = generator() % (528 - height + 1);
      const unsigned row_bytes = ((width + 15) & ~15u) * 2;
      const unsigned stride_mode = generator() % 3;
      const unsigned stride = stride_mode == 0 ? row_bytes : stride_mode == 1 ? 32 : row_bytes + 32;
      bp[0x49] = top << 10;
      bp[0x4a] = (width - 1) | ((height - 1) << 10);
      bp[0x4b] = (profile & 1 ? 0x10010000 : 0x10000) >> 5;
      bp[0x4d] = stride >> 5;
      bp[0x4e] = scales[generator() % scales.size()];
      bp[0x52] = (1 << 14) | ((generator() & 1) ? 1 << 10 : 0) | ((generator() & 3) << 7) |
                 (generator() & 3);
      bp[0x53] = bp[0x54] = 0;
      for (unsigned tap = 0; tap < 7; ++tap) {
        const unsigned weight = profile % 8 == 0 ? (tap == 2   ? 1
                                                    : tap == 3 ? 63
                                                               : 0)
                                                 : generator() & 63;
        bp[tap < 4 ? 0x53 : 0x54] |= weight << ((tap < 4 ? tap : tap - 4) * 6);
      }
      auto cpu_bytes = cpu_memory.range(bp[0x4b] << 5, checked_bytes);
      auto gpu_bytes = gpu_memory.range(bp[0x4b] << 5, checked_bytes);
      std::fill(cpu_bytes.begin(), cpu_bytes.end(), 0xa7);
      std::fill(gpu_bytes.begin(), gpu_bytes.end(), 0xa7);
      bool rejected = false;
      try {
        gfx::copy_efb_xfb(cpu_memory, bp, color);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (rejected) {
        CHECK_THROWS(renderer.copy_xfb(bp));
        ++invalid;
      } else {
        CHECK(renderer.copy_xfb(bp));
        ++valid;
      }
      if (!std::equal(cpu_bytes.begin(), cpu_bytes.end(), gpu_bytes.begin()))
        throw std::runtime_error("XFB GPU profile mismatch format=" + std::to_string(format) +
                                 " profile=" + std::to_string(profile));
      compared += checked_bytes;
    }
  }
  const auto stats = renderer.synchronize(pixels, depths);
  CHECK(stats.xfb_copies == valid);
  CHECK(stats.efb_readbacks == 0);
  std::cout << "Verified " << valid << " GPU XFB profiles and " << invalid
            << " rejected staging boundaries; compared " << compared << " RAM bytes\n";
  if (!measure)
    return;
  fill(1);
  bp[0x49] = 0;
  bp[0x4a] = 639 | (223 << 10);
  bp[0x4b] = 0x10000 >> 5;
  bp[0x4d] = 1280 >> 5;
  bp[0x4e] = 256;
  bp[0x52] = (1 << 14) | 3;
  bp[0x53] = (8 << 0) | (8 << 6) | (16 << 12) | (16 << 18);
  bp[0x54] = (8 << 6) | (8 << 12);
  std::array<std::vector<u8>, 4> encoded;
  for (unsigned gamma = 0; gamma < 4; ++gamma) {
    bp[0x52] = (1 << 14) | 3 | (gamma << 7);
    gfx::copy_efb_xfb(cpu_memory, bp, color);
    const auto bytes = cpu_memory.range(0x10000, 640 * 224 * 2);
    encoded[gamma].assign(bytes.begin(), bytes.end());
  }
  for (unsigned repetition = 0; repetition < 3; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    for (unsigned copy = 0; copy < 32; ++copy) {
      bp[0x52] = (1 << 14) | 3 | ((copy & 3) << 7);
      gfx::copy_efb_xfb(cpu_memory, bp, color);
    }
    const double cpu =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto gpu_start = std::chrono::steady_clock::now();
    for (unsigned copy = 0; copy < 32; ++copy) {
      bp[0x52] = (1 << 14) | 3 | ((copy & 3) << 7);
      CHECK(renderer.copy_xfb(bp));
      CHECK(std::equal(encoded[copy & 3].begin(), encoded[copy & 3].end(),
                       gpu_memory.range(0x10000, 640 * 224 * 2).begin()));
    }
    const double gpu =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - gpu_start).count();
    CHECK(std::equal(cpu_memory.range(0x10000, 640 * 224 * 2).begin(),
                     cpu_memory.range(0x10000, 640 * 224 * 2).end(),
                     gpu_memory.range(0x10000, 640 * 224 * 2).begin()));
    std::cout << "XFB copy repetition " << repetition + 1 << ": CPU seconds=" << cpu
              << ", GPU seconds=" << gpu << '\n';
  }
}

void buffer_fragment_fallback_checks() {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 1> first{0x316593b7}, second{0xd37b2951};
  Fixture software, hardware;
  hardware.graphics.set_hardware_rendering(true);
  for (unsigned mask = 0; mask < 4; ++mask) {
    const auto before = hardware.graphics.hardware_stats();
    for (auto *fixture : {&software, &hardware}) {
      fixture->bp(0x43, 1);
      fixture->bp(0x10, 0);
      fixture->bp(0x41, 24);
      fixture->draw(0x80, quad, first);
      // A retained indirect accumulator requires CPU fragments. The preceding
      // GPU draw remains queued until this fallback restores its destination.
      fixture->bp(0x10, 1 << 20);
      fixture->bp(0x41, 1 | (4 << 8) | (5 << 5) | ((mask & 1) ? 8 : 0) | ((mask & 2) ? 16 : 0));
      fixture->draw(0x80, quad, second);
    }
    equal(software, hardware);
    const auto after = hardware.graphics.hardware_stats();
    CHECK(after.dispatches > before.dispatches);
    CHECK(after.fallback_rectangles > before.fallback_rectangles);
    CHECK(after.efb_readbacks == before.efb_readbacks + 1);
    CHECK(!hardware.graphics.hardware_fallback_reason().empty());
  }
  std::cout << "Verified queued GPU destinations through CPU blending and all color/alpha masks\n";
}

void bounding_box_reset_checks() {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<std::array<float, 3>, 4> later{
      {{0, 0, -0.5f}, {0.5f, 0, -0.5f}, {0.5f, 0.5f, -0.5f}, {0, 0.5f, -0.5f}}};
  const std::array<u32, 1> colors{0x316593b7};
  unsigned cases = 0;
  for (const u8 reg : std::array<u8, 2>{0x55, 0x56})
    for (u32 mask : {0xffffffu, 0x3ffu, 0xffc00u, 0u})
      for (bool draw_after_reset : {false, true})
        for (bool repeated_reset : {false, true}) {
          Fixture software, hardware;
          hardware.graphics.set_hardware_rendering(true);
          for (auto *fixture : {&software, &hardware}) {
            fixture->bp(0x55, 500 | (501 << 10));
            fixture->bp(0x56, 600 | (601 << 10));
            fixture->draw(0x80, quad, colors);
            // A BP reset replaces its selected pair after earlier fragments,
            // including an identical-value or fully masked register write.
            fixture->bp(0xfe, mask);
            fixture->bp(reg, 700 | (701 << 10));
            if (repeated_reset) {
              fixture->draw(0x80, later, colors);
              fixture->bp(0xfe, mask);
              fixture->bp(reg, 700 | (701 << 10));
            }
            if (draw_after_reset)
              fixture->draw(0x80, later, colors);
          }
          for (u32 address = 0x0c001010; address <= 0x0c001016; address += 2)
            CHECK(software.graphics.read(address, 2) == hardware.graphics.read(address, 2));
          const auto stats = hardware.graphics.hardware_stats();
          CHECK(stats.fragments == software.graphics.fragment_count());
          CHECK(stats.efb_readbacks == 1);
          ++cases;
        }
  std::cout << "Verified " << cases << " queued fragment bounding-box reset boundaries\n";
}

void efb_texture_copy_checks() {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  Fixture software, hardware;
  Memory expected(Console::GameCube);
  hardware.graphics.set_hardware_rendering(true);
  unsigned cases = 0;
  for (unsigned source_format = 0; source_format < 4; ++source_format)
    for (unsigned copy_format = 0; copy_format <= 12; ++copy_format) {
      if (source_format == 3 && (copy_format == 2 || copy_format == 3 || copy_format == 4 ||
                                 copy_format == 5 || copy_format == 7))
        continue;
      for (bool half : {false, true})
        for (unsigned mask = 0; mask < 8; ++mask) {
          std::array<u32, 256> bp{};
          bp[0x43] = source_format;
          bp[0x49] = 7 | (9 << 10);
          bp[0x4a] = 12 | (6 << 10);
          bp[0x4b] = 0x20000 >> 5;
          bp[0x4d] = 12;
          bp[0x52] = ((((copy_format << 1) & 15) | (copy_format >> 3)) << 3) | (half ? 1 << 9 : 0) |
                     (1 << 11);
          const auto before = hardware.graphics.hardware_stats();
          for (auto *fixture : {&software, &hardware}) {
            fixture->bp(0x43, source_format);
            fixture->bp(0x40, 1 | 14 | 16);
            fixture->bp(0x41, 24);
            fixture->draw(0x80, quad, colors);
            fixture->bp(0x40, mask & 4 ? 16 : 0);
            fixture->bp(0x41, ((mask & 1) ? 8 : 0) | ((mask & 2) ? 16 : 0));
            for (u8 reg : std::array<u8, 4>{0x49, 0x4a, 0x4b, 0x4d})
              fixture->bp(reg, bp[reg]);
            fixture->bp(0x4f, 0x71f4);
            fixture->bp(0x50, 0x38d2);
            fixture->bp(0x51, 0x815392);
            auto bytes = fixture->memory.range(0x20000, 1024);
            std::fill(bytes.begin(), bytes.end(), 0xa5);
          }
          auto expected_bytes = expected.range(0x20000, 1024);
          std::fill(expected_bytes.begin(), expected_bytes.end(), 0xa5);
          // Encode from the pre-clear software image independently of the command
          // processor copy path. Odd extents exercise padded source sampling.
          gfx::copy_efb_texture(
              expected, bp,
              {[&](unsigned x, unsigned y) { return software.graphics.color(x, y); },
               [&](unsigned x, unsigned y) { return software.graphics.depth(x, y); }});
          for (auto *fixture : {&software, &hardware}) {
            fixture->bp(0x52, bp[0x52]);
            const auto bytes = fixture->memory.range(0x20000, 1024);
            CHECK(std::equal(bytes.begin(), bytes.end(), expected_bytes.begin()));
          }
          const auto after = hardware.graphics.hardware_stats();
          CHECK(after.dispatches > before.dispatches);
          CHECK(after.efb_readbacks == before.efb_readbacks + 1);
          CHECK(after.fallback_rectangles == before.fallback_rectangles);
          // Include a one-pixel guard around the clear rectangle. Masked clear
          // must preserve the other channels and every pixel beyond its extent.
          for (unsigned y = 8; y <= 16; ++y)
            for (unsigned x = 6; x <= 20; ++x) {
              CHECK(software.graphics.color(x, y) == hardware.graphics.color(x, y));
              CHECK(software.graphics.depth(x, y) == hardware.graphics.depth(x, y));
            }
          ++cases;
        }
    }
  std::cout << "Verified " << cases
            << " queued GPU color/depth texture copies with padding, half scale, stride guards "
               "and masked copy-before-clear ordering\n";
}

void efb_texture_copy_benchmark() {
  Fixture fixture;
  fixture.graphics.set_hardware_rendering(true);
  fixture.bp(0x40, 16);
  fixture.bp(0x41, 24);
  fixture.bp(0x49, 0);
  fixture.bp(0x4a, 639 | (527 << 10));
  fixture.bp(0x4f, 0x71f4);
  fixture.bp(0x50, 0x38d2);
  fixture.bp(0x51, 0x815392);
  fixture.bp(0x52, 1 << 11);
  fixture.bp(0x4b, 0x20000 >> 5);
  Memory expected(Console::GameCube);
  for (unsigned source_format : {1u, 3u})
    for (bool half : {false, true}) {
      std::array<u32, 256> bp{};
      bp[0x43] = source_format;
      bp[0x4a] = 639 | (527 << 10);
      bp[0x4b] = 0x20000 >> 5;
      bp[0x4d] = (half ? 5120 : 10240) >> 5;
      bp[0x52] = 12 << 3 | (half ? 1 << 9 : 0);
      fixture.bp(0x43, source_format);
      fixture.bp(0x4d, bp[0x4d]);
      const auto color = fixture.graphics.color(0, 0), depth = fixture.graphics.depth(0, 0);
      const auto layout = gfx::copy_efb_texture(
          expected, bp,
          {[&](unsigned, unsigned) { return color; }, [&](unsigned, unsigned) { return depth; }});
      const auto extent = std::size_t(layout.block_rows - 1) * layout.stride + layout.row_bytes;
      const auto expected_bytes = expected.range(0x20000, extent);
      fixture.bp(0x52, bp[0x52]);
      const auto bytes = fixture.memory.range(0x20000, extent);
      CHECK(std::equal(bytes.begin(), bytes.end(), expected_bytes.begin()));
      for (unsigned repetition = 0; repetition < 3; ++repetition) {
        const auto started = std::chrono::steady_clock::now();
        for (unsigned copy = 0; copy < 32; ++copy)
          fixture.bp(0x52, bp[0x52]);
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        CHECK(std::equal(bytes.begin(), bytes.end(), expected_bytes.begin()));
        u64 hash = 14695981039346656037ull;
        for (const auto byte : bytes)
          hash = (hash ^ byte) * 1099511628211ull;
        std::cout << "EFB texture copy source_format=" << source_format << " half=" << half
                  << " repetition=" << repetition + 1 << " copies=32 seconds=" << elapsed
                  << " bytes=" << extent << " hash=" << hash << '\n';
      }
    }
}

void xfb_clear_checks() {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  Fixture software, hardware;
  hardware.graphics.set_hardware_rendering(true);
  for (unsigned format = 0; format < 4; ++format)
    for (unsigned mask = 0; mask < 8; ++mask) {
      for (auto *fixture : {&software, &hardware}) {
        fixture->bp(0x43, format);
        fixture->bp(0x40, 1 | 14 | 16);
        fixture->bp(0x41, 24);
        fixture->draw(0x80, quad, colors);
        fixture->bp(0x40, mask & 4 ? 16 : 0);
        fixture->bp(0x41, ((mask & 1) ? 8 : 0) | ((mask & 2) ? 16 : 0));
        fixture->bp(0x49, 5 << 10);
        fixture->bp(0x4a, 15 | (15 << 10));
        fixture->bp(0x4b, 0x20000 >> 5);
        fixture->bp(0x4d, 1);
        fixture->bp(0x4e, 256);
        fixture->bp(0x4f, 0x71f4);
        fixture->bp(0x50, 0x38d2);
        fixture->bp(0x51, 0x815392);
        fixture->bp(0x53, (1 << 12) | (63 << 18));
        fixture->bp(0x54, 0);
        fixture->bp(0x52, (1 << 14) | (1 << 11) | 3 | ((mask & 3) << 7));
      }
      equal(software, hardware);
      CHECK(std::equal(software.memory.range(0x20000, 512).begin(),
                       software.memory.range(0x20000, 512).end(),
                       hardware.memory.range(0x20000, 512).begin()));
      CHECK(hardware.graphics.hardware_stats().xfb_copies == format * 8 + mask + 1);
    }
  const auto before = hardware.graphics.hardware_stats();
  for (unsigned copy = 0; copy < 8; ++copy)
    for (auto *fixture : {&software, &hardware}) {
      fixture->bp(0x43, 1);
      fixture->bp(0x40, 16);
      fixture->bp(0x41, 24);
      if (copy == 0)
        fixture->draw(0x80, quad, colors);
      fixture->bp(0x4f, copy * 913 + 127);
      fixture->bp(0x50, copy * 1777 + 83);
      fixture->bp(0x51, copy * 8153 + 391);
      fixture->bp(0x52, (1 << 14) | (1 << 11) | 3 | ((copy & 3) << 7));
    }
  const auto after = hardware.graphics.hardware_stats();
  CHECK(after.xfb_copies == before.xfb_copies + 8);
  CHECK(after.efb_readbacks == before.efb_readbacks + 1);
  equal(software, hardware);
  CHECK(std::equal(software.memory.range(0x20000, 512).begin(),
                   software.memory.range(0x20000, 512).end(),
                   hardware.memory.range(0x20000, 512).begin()));
  std::cout << "Verified 32 GPU XFB copy/clear states across EFB formats and write masks\n";
  std::cout << "Verified eight queued GPU copy/clear commands with one later EFB readback\n";
}

struct ProgramTestApi {
#define CUPID_PROGRAM_TEST_GL(type, name) type name = graphics_function<type>("gl" #name)
  CUPID_PROGRAM_TEST_GL(PFNGLCREATESHADERPROC, CreateShader);
  CUPID_PROGRAM_TEST_GL(PFNGLSHADERSOURCEPROC, ShaderSource);
  CUPID_PROGRAM_TEST_GL(PFNGLCOMPILESHADERPROC, CompileShader);
  CUPID_PROGRAM_TEST_GL(PFNGLGETSHADERIVPROC, GetShaderiv);
  CUPID_PROGRAM_TEST_GL(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
  CUPID_PROGRAM_TEST_GL(PFNGLDELETESHADERPROC, DeleteShader);
  CUPID_PROGRAM_TEST_GL(PFNGLCREATEPROGRAMPROC, CreateProgram);
  CUPID_PROGRAM_TEST_GL(PFNGLATTACHSHADERPROC, AttachShader);
  CUPID_PROGRAM_TEST_GL(PFNGLLINKPROGRAMPROC, LinkProgram);
  CUPID_PROGRAM_TEST_GL(PFNGLGETPROGRAMIVPROC, GetProgramiv);
  CUPID_PROGRAM_TEST_GL(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
  CUPID_PROGRAM_TEST_GL(PFNGLDELETEPROGRAMPROC, DeleteProgram);
#undef CUPID_PROGRAM_TEST_GL
};

struct AllocationTestApi {
  GLuint shader = 0, program = 0;
  unsigned shader_creations = 0, program_creations = 0, shader_deletions = 0;

  GLuint CreateShader(GLenum) {
    ++shader_creations;
    return shader;
  }

  void ShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *) {
  }

  void CompileShader(GLuint) {
  }

  void GetShaderiv(GLuint, GLenum, GLint *value) {
    *value = 1;
  }

  void GetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *) {
  }

  void DeleteShader(GLuint value) {
    CHECK(value == shader && value != 0);
    ++shader_deletions;
  }

  GLuint CreateProgram() {
    ++program_creations;
    return program;
  }

  void AttachShader(GLuint, GLuint) {
  }

  void LinkProgram(GLuint) {
  }

  void GetProgramiv(GLuint, GLenum, GLint *value) {
    *value = 1;
  }

  void GetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *) {
  }

  void DeleteProgram(GLuint) {
    CHECK(false);
  }
};

void zero_program_creation_checks() {
  AllocationTestApi allocation;
  for (const GLuint shader : {0u, 17u}) {
    allocation = {};
    allocation.shader = shader;
    bool failed = false;
    try {
      gfx::detail::compile_compute_program(allocation, "unused", "test");
    } catch (const std::runtime_error &error) {
      failed = std::string_view(error.what())
                   .find(shader ? "program creation failed" : "shader creation failed") !=
               std::string_view::npos;
    }
    CHECK(failed && allocation.shader_creations == 1);
    CHECK(allocation.program_creations == (shader ? 1u : 0u));
    CHECK(allocation.shader_deletions == (shader ? 1u : 0u));
  }
  std::vector<GLuint> deleted;
  bool failed = false;
  try {
    gfx::detail::create_fragment_programs([](bool batch) { return batch ? 0u : 23u; },
                                          [&](GLuint id) { deleted.push_back(id); });
  } catch (const std::runtime_error &) {
    failed = true;
  }
  CHECK(failed && deleted == std::vector<GLuint>{23});

  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  ProgramTestApi api;
  const auto is_program = graphics_function<PFNGLISPROGRAMPROC>("glIsProgram");
  GLuint first = 0;
  failed = false;
  try {
    gfx::detail::create_fragment_programs(
        [&](bool batch) {
          const char *source = batch ? "#version 430 core\n#error expected test failure\n"
                                     : "#version 430 core\nlayout(local_size_x=1) in;void main(){}";
          const GLuint id = gfx::detail::compile_compute_program(api, source, "test");
          if (!batch)
            first = id;
          return id;
        },
        [&](GLuint id) { api.DeleteProgram(id); });
  } catch (const std::runtime_error &error) {
    failed = std::string_view(error.what()).find("compilation failed") != std::string_view::npos;
  }
  CHECK(failed && first != 0 && !is_program(first));
  const auto retry = gfx::detail::create_fragment_programs(
      [&](bool) {
        return gfx::detail::compile_compute_program(
            api, "#version 430 core\nlayout(local_size_x=1) in;void main(){}", "test");
      },
      [&](GLuint id) { api.DeleteProgram(id); });
  CHECK(retry.scalar != retry.batch && is_program(retry.scalar) && is_program(retry.batch));
  api.DeleteProgram(retry.scalar);
  api.DeleteProgram(retry.batch);
  CHECK(!is_program(retry.scalar) && !is_program(retry.batch));
  std::cout << "Verified zero shader/program allocation IDs, partial-pair cleanup after real "
               "compilation failure, and successful retry\n";
}

struct TestFloatEnvironment {
  std::fenv_t saved{};
#if defined(_M_X64) || defined(__x86_64__)
  unsigned simd = _mm_getcsr();
#endif
  TestFloatEnvironment() {
    CHECK(std::fegetenv(&saved) == 0);
  }

  ~TestFloatEnvironment() {
    std::fesetenv(&saved);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(simd);
#endif
  }
};

struct FloatEnvironmentBits {
  int round = std::fegetround(), flags = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(_M_X64) || defined(__x86_64__)
  unsigned simd = _mm_getcsr();
  unsigned short control = 0, x87_flags = 0;

  FloatEnvironmentBits() {
    alignas(16) std::array<unsigned char, 512> state{};
    _fxsave64(state.data());
    std::memcpy(&control, state.data(), 2);
    std::memcpy(&x87_flags, state.data() + 2, 2);
    x87_flags &= 0x3f;
  }
#endif
  bool operator==(const FloatEnvironmentBits &) const = default;
};

void zero_first_use_environment_checks() {
  const TestFloatEnvironment restore;
  constexpr std::array<int, 4> modes{FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
  unsigned draws = 0, failures = 0;
  for (unsigned mode = 0; mode < modes.size(); ++mode)
    for (unsigned denormal = 0; denormal < 4; ++denormal) {
      std::cout << "[renderer] first-use row mode=" << mode << " denormal=" << denormal << '\n';
      CHECK(std::fesetenv(FE_DFL_ENV) == 0);
      Memory memory{Console::GameCube}, cpu_memory{Console::GameCube};
      std::vector<u8> tmem(1024 * 1024);
      gfx::HardwareRenderer renderer(memory, tmem);
      const auto get_integer =
          graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
      std::vector<u32> pixels(640 * 528), depths(pixels.size());
      renderer.upload(pixels, depths);
      std::array<u32, 256> copy{};
      copy[0x40] = 16;
      copy[0x41] = 24;
      copy[0x4a] = 15 | (15 << 10);
      copy[0x4b] = 0x20000 >> 5;
      copy[0x4d] = 1;
      copy[0x4e] = 256;
      copy[0x53] = (1 << 12) | (63 << 18);
      copy[0x52] = (1 << 14) | 3;
      // Initialize the unchanged XFB program before seeding the host environment.
      CHECK(renderer.copy_xfb(copy));
      std::array<u32, 256> bp{};
      gfx::TevRegisters colors{}, konst{};
      bp[0xf6] = 4;
      bp[0xf7] = 14;
      bp[0x41] = 24;
      bp[0xf3] = (7 << 16) | (7 << 19);
      bp[0xc0] = 0xfffa | (1 << 19);
      bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
      gfx::RasterWork work{};
      work.left = 13;
      work.top = 11;
      work.right = 30;
      work.bottom = 28;
      work.edges.fill({0, 0, 1});
      work.reciprocal_w.value = 1;
      work.depth.value = 12345;
      for (unsigned c = 0; c < 4; ++c)
        work.colors[0][c].value = gfx::Color{211, 105, 65, 177}[c];
      unsigned expected_dispatches = 0;
      std::array<bool, 2> scalar_used{};
      for (unsigned maps : {0u, 1u, 0u, 1u})
        for (const bool batch : {true, false}) {
          const unsigned extent = batch ? 7 : 17;
          work.right = work.left + extent;
          work.bottom = work.top + extent;
          if (batch) {
            // The existing GPU XFB table was initialized before the host modes
            // were seeded. Compute its byte oracle in that same environment.
            const TestFloatEnvironment oracle_environment;
            CHECK(std::fesetenv(FE_DFL_ENV) == 0);
            gfx::copy_efb_xfb(cpu_memory, copy, [&](unsigned x, unsigned y) {
              const u32 color =
                  x >= 13 && x < 20 && y >= 11 && y < 18 ? 0xd36941 : pixels[y * 640 + x];
              return (color << 8) | 255;
            });
          }
          CHECK(std::fesetround(modes[mode]) == 0);
          CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
          CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
          constexpr unsigned simd_modes[] = {0u, 0x6000u, 0x4000u, 0x2000u};
          _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(mode + 1) % 4] |
                     ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
          const FloatEnvironmentBits before;
          bp[0] = 1 | (1 << 4) | (maps << 16);
          CHECK(renderer.begin(bp, colors, konst));
          CHECK(renderer.draw(work));
          if (batch) {
            // Flush through an ordinary state transition so the initialized
            // program can be observed before any independent readback calls.
            auto changed_konst = konst;
            changed_konst[0][0] ^= 1;
            CHECK(renderer.begin(bp, colors, changed_konst));
            CHECK(renderer.begin(bp, colors, konst));
          } else
            scalar_used[maps == 0 ? 1 : 0] = true;
          if (!(FloatEnvironmentBits{} == before)) {
            const FloatEnvironmentBits after;
            std::cerr << "environment changed mode=" << mode << " denormal=" << denormal
                      << " maps=" << maps << " before=" << before.round << ',' << before.flags;
#if defined(_M_X64) || defined(__x86_64__)
            std::cerr << ',' << before.simd << ',' << before.control << ',' << before.x87_flags
                      << " after=" << after.round << ',' << after.flags << ',' << after.simd << ','
                      << after.control << ',' << after.x87_flags;
#endif
            std::cerr << '\n';
          }
          CHECK(FloatEnvironmentBits{} == before);
          const auto stats = renderer.synchronize(pixels, depths);
          // Existing readback APIs can set host exception flags independently
          // of program initialization. They must retain the caller's controls.
          const FloatEnvironmentBits after_readback;
          CHECK(after_readback.round == before.round);
#if defined(_M_X64) || defined(__x86_64__)
          CHECK(after_readback.control == before.control);
          CHECK((after_readback.simd & ~0x3fu) == (before.simd & ~0x3fu));
#endif
          ++expected_dispatches;
          if (stats.dispatches != expected_dispatches || stats.fragments != extent * extent ||
              stats.fallback_rectangles != 0)
            std::cerr << "first-use mode=" << mode << " denormal=" << denormal << " maps=" << maps
                      << " dispatches=" << stats.dispatches << " fragments=" << stats.fragments
                      << " fallbacks=" << stats.fallback_rectangles << '\n';
          CHECK(stats.dispatches == expected_dispatches && stats.fragments == extent * extent &&
                stats.fallback_rectangles == 0);
          for (unsigned y = 11; y < 11 + extent; ++y)
            for (unsigned x = 13; x < 13 + extent; ++x)
              CHECK(pixels[y * 640 + x] == 0xd36941);
          if (batch) {
            CHECK(renderer.copy_xfb(copy));
            GLint program = -1;
            get_integer(GL_CURRENT_PROGRAM, &program);
            if (!scalar_used[maps == 0 ? 1 : 0])
              CHECK(program == 0);
            for (unsigned offset = 0; offset < 512; ++offset)
              if (memory.read8(0x20000 + offset) != cpu_memory.read8(0x20000 + offset)) {
                std::cerr << "batch-first XFB mismatch mode=" << mode << " denormal=" << denormal
                          << " maps=" << maps << " offset=" << offset
                          << " gpu=" << unsigned(memory.read8(0x20000 + offset))
                          << " cpu=" << unsigned(cpu_memory.read8(0x20000 + offset)) << '\n';
                break;
              }
            CHECK(std::equal(memory.range(0x20000, 512).begin(), memory.range(0x20000, 512).end(),
                             cpu_memory.range(0x20000, 512).begin()));
            const FloatEnvironmentBits after_copy;
            CHECK(after_copy.round == before.round);
#if defined(_M_X64) || defined(__x86_64__)
            CHECK(after_copy.control == before.control);
            CHECK((after_copy.simd & ~0x3fu) == (before.simd & ~0x3fu));
#endif
          }
          ++draws;
        }
      ProgramTestApi api;
      for (unsigned index = 0; index < modes.size(); ++index) {
        CHECK(std::fesetround(modes[index]) == 0);
        CHECK(std::feclearexcept(FE_ALL_EXCEPT) == 0);
        CHECK(std::feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0);
#if defined(_M_X64) || defined(__x86_64__)
        constexpr unsigned simd_modes[] = {0u, 0x6000u, 0x4000u, 0x2000u};
        _mm_setcsr((_mm_getcsr() & ~0xe040u) | simd_modes[(index + 1) % 4] |
                   ((denormal & 1) ? 0x8000u : 0u) | ((denormal & 2) ? 0x40u : 0u));
#endif
        const FloatEnvironmentBits before;
        bool failed = false;
        try {
          gfx::detail::compile_compute_program(
              api, "#version 430 core\n#error expected environment test failure\n", "test");
        } catch (const std::runtime_error &error) {
          failed =
              std::string_view(error.what()).find("compilation failed") != std::string_view::npos;
        }
        CHECK(failed && FloatEnvironmentBits{} == before);
        gfx::detail::FragmentPrograms programs{17, 23};
        for (const bool batch : {false, true}) {
          for (unsigned failure_stage = 0; failure_stage < 2; ++failure_stage) {
            volatile unsigned selected_stage = failure_stage;
            failed = false;
            const auto fail = [&] {
              CHECK(std::fegetround() == FE_TONEAREST);
              std::feraiseexcept(FE_INEXACT);
              throw std::runtime_error("Expected initialization failure");
            };
            try {
              gfx::detail::dispatch_fragment_program(
                  programs, batch,
                  [&](GLuint id) {
                    CHECK(id == (batch ? 23u : 17u));
                    if (selected_stage == 0)
                      fail();
                  },
                  [&] {
                    if (selected_stage == 1)
                      fail();
                  });
            } catch (const std::runtime_error &error) {
              failed = std::string_view(error.what()) == "Expected initialization failure";
            }
            CHECK(failed && !programs.scalar_initialized && !programs.batch_initialized);
            CHECK(FloatEnvironmentBits{} == before);
          }
        }
        ++failures;
      }
    }
  std::cout << "Verified " << draws << " first-use/cached draws and " << failures
            << " real compiler failures across host rounding, mixed x87/SSE and FTZ/DAZ; "
               "pixels, counters and caller environment preserved\n";
}

void zero_family_lifetime_checks() {
  Memory memory{Console::GameCube}, cpu_memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  const auto get_integer = graphics_function<void(APIENTRY *)(GLenum, GLint *)>("glGetIntegerv");
  const auto current_program = [get_integer] {
    GLint value = -1;
    get_integer(GL_CURRENT_PROGRAM, &value);
    return static_cast<GLuint>(value);
  };
  CHECK(current_program() == 0);
  std::vector<u32> pixels(640 * 528, 0x315a87), depths(pixels.size(), 12345);
  renderer.upload(pixels, depths);
  std::array<u32, 256> copy{};
  copy[0x40] = 16;
  copy[0x41] = 24;
  copy[0x4a] = 15 | (15 << 10);
  copy[0x4b] = 0x20000 >> 5;
  copy[0x4d] = 1;
  copy[0x4e] = 256;
  copy[0x53] = (1 << 12) | (63 << 18);
  copy[0x52] = (1 << 14) | (1 << 11) | 3;
  gfx::copy_efb_xfb(cpu_memory, copy, [](unsigned, unsigned) { return 0x315a87ffu; });
  CHECK(renderer.copy_xfb(copy));
  CHECK(current_program() == 0);
  CHECK(std::equal(memory.range(0x20000, 512).begin(), memory.range(0x20000, 512).end(),
                   cpu_memory.range(0x20000, 512).begin()));
  renderer.synchronize(pixels, depths);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0x41] = 24;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  gfx::RasterWork work{};
  work.left = 13;
  work.top = 11;
  work.edges.fill({0, 0, 1});
  work.reciprocal_w.value = 1;
  work.depth.value = 12345;
  for (unsigned c = 0; c < 4; ++c)
    work.colors[0][c].value = gfx::Color{211, 105, 65, 177}[c];
  std::array<std::array<GLuint, 2>, 2> programs{};
  for (unsigned maps : {0u, 4u, 0u, 1u, 0u, 2u, 0u, 3u, 0u, 4u}) {
    bp[0] = 1 | (1 << 4) | (maps << 16);
    CHECK(renderer.begin(bp, colors, konst));
    if (!programs[0][0] && !programs[1][0])
      CHECK(current_program() == 0);
    const unsigned family = maps == 0 ? 1 : 0;
    for (unsigned batch = 0; batch < 2; ++batch) {
      work.right = work.left + (batch ? 7 : 17);
      work.bottom = work.top + (batch ? 7 : 17);
      CHECK(renderer.draw(work));
      renderer.synchronize(pixels, depths);
      const GLuint id = current_program();
      CHECK(id != 0);
      if (!programs[family][batch])
        programs[family][batch] = id;
      CHECK(programs[family][batch] == id);
    }
  }
  const std::array<GLuint, 4> ids{programs[0][0], programs[0][1], programs[1][0], programs[1][1]};
  for (unsigned i = 0; i < ids.size(); ++i)
    for (unsigned j = i + 1; j < ids.size(); ++j)
      CHECK(ids[i] != ids[j]);
  std::cout << "Verified XFB/clear before raster use and reuse of four lazy scalar/batch "
               "program IDs across zero/nonzero indirect families\n";
}

void zero_family_transitions() {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  gfx::FragmentShader hardware(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(cpu_colors.size());
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  std::mt19937 generator(0x4120398);
  for (unsigned i = 0; i < 256; ++i)
    memory.write8(0x80000 + i, static_cast<u8>(i * 37 + 19));
  bp[0x80] = 1 | (1 << 2);
  bp[0x88] = 7 | (7 << 10) | (6 << 20);
  bp[0x94] = 0x80000 >> 5;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  constexpr std::array<int, 8> extents{1, 7, 8, 9, 15, 16, 17, 63};
  std::fill(cpu_colors.begin(), cpu_colors.end(), 0x315a87);
  std::fill(cpu_depths.begin(), cpu_depths.end(), 128);
  gpu_colors = cpu_colors;
  gpu_depths = cpu_depths;
  renderer.upload(gpu_colors, gpu_depths);
  gfx::HardwareStats expected;
  u64 expected_raster_calls = 0;
  for (unsigned profile = 0; profile < 100; ++profile) {
    constexpr std::array<unsigned, 10> map_counts{4, 0, 1, 0, 2, 0, 3, 0, 4, 0};
    const unsigned maps = map_counts[profile % 10];
    bp[0] = 1 | (1 << 4) | (1 << 10) | (maps << 16);
    bp[0x28] = 64 | (64 << 12);
    bp[0xc2] = bp[0xc0];
    bp[0xc3] = bp[0xc1];
    bp[6] = (1 << 22);
    bp[7] = 0;
    bp[8] = (1 << 22);
    bp[0x10] = (maps ? maps - 1 : 3) | ((profile % 7) << 13) | (((profile / 7) % 7) << 16);
    if (maps)
      bp[0x10] |= (1 << 9) | (1 << 11) | (1 << 7);
    bp[0x11] = (1 << 20) | (((profile + 3) % 7) << 13);
    bp[0x43] = profile & 1 ? 64 : 0;
    bp[0x40] = profile < 2 ? 0 : 1 | (((profile >> 1) & 7) << 1) | 16;
    bp[0xf3] = profile < 2 ? (7 << 16) | (7 << 19)
                           : 137 | (205 << 8) | (((profile >> 3) & 7) << 16) |
                                 (((profile >> 5) & 7) << 19) | (((profile >> 6) & 3) << 22);
    gfx::RasterWork work{};
    const int width = extents[profile % extents.size()];
    const int height = extents[(profile / extents.size()) % extents.size()];
    work.left = static_cast<int>(generator() % (641 - width));
    work.top = static_cast<int>(generator() % (529 - height));
    work.right = work.left + width;
    work.bottom = work.top + height;
    work.edges.fill({0, 0, 1});
    if (profile % 4 == 2)
      work.edges[0] = {1, 1, (work.left - work.top) * 16 + 1};
    if (profile % 4 == 3)
      work.edges[1] = {0, -1, -(work.left + width / 2) * 16 + 1};
    work.depth.value = 97;
    work.depth.dx = 7;
    work.depth.dy = 11;
    work.depth.x_origin = work.left;
    work.depth.y_origin = work.top;
    work.reciprocal_w.value = 1;
    work.colors[0][3] = work.depth;
    work.textures[0][0].value = -3;
    work.textures[0][0].dx = 0.75f;
    work.textures[0][0].dy = 0.25f;
    work.textures[0][1].value = 5;
    work.textures[0][1].dx = -0.5f;
    work.textures[0][1].dy = 1.25f;
    for (unsigned draw = 0; draw < 2; ++draw) {
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      for (int by = work.top & ~1; by < work.bottom; by += 2)
        for (int bx = work.left & ~1; bx < work.right; bx += 2)
          for (int iy = 0; iy < 2; ++iy)
            for (int ix = 0; ix < 2; ++ix) {
              const int x = bx + ix, y = by + iy;
              if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
                continue;
              bool covered = true;
              for (const auto &edge : work.edges)
                covered &= edge.constant + edge.dx * y * 16 - edge.dy * x * 16 > 0;
              if (!covered)
                continue;
              const auto offset = static_cast<unsigned>(y * 640 + x);
              u32 depth = static_cast<u32>(work.depth.at(x, y));
              const auto depth_test = [&] {
                if (!(bp[0x40] & 1))
                  return true;
                if (!gfx::compare((bp[0x40] >> 1) & 7, depth, cpu_depths[offset]))
                  return false;
                if (bp[0x40] & 16)
                  cpu_depths[offset] = depth;
                return true;
              };
              const bool early = (bp[0x43] & 64) != 0;
              if (early && !depth_test())
                continue;
              gfx::FragmentInputs inputs;
              inputs.colors[0][3] =
                  static_cast<u8>(std::clamp(work.colors[0][3].at(x, y), 0.0f, 255.0f));
              for (unsigned component = 0; component < 2; ++component) {
                const auto &plane = work.textures[0][component];
                inputs.uv[0][component] = plane.at(x, y);
                inputs.dx[0][component] = plane.at(bx + 1, by) - plane.at(bx, by);
                inputs.dy[0][component] = plane.at(bx, by + 1) - plane.at(bx, by);
              }
              const auto result = software.shade(inputs, &depth);
              if (!result || (!early && !depth_test()))
                continue;
              cpu_colors[offset] =
                  (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
              ++expected.fragments;
              expected.left = std::min(expected.left, u32(x) & ~1u);
              expected.right = std::max(expected.right, u32(x) | 1u);
              expected.top = std::min(expected.top, u32(y) & ~1u);
              expected.bottom = std::max(expected.bottom, u32(y) | 1u);
            }
      work.textures[0][0].value += 2.5f;
    }
    expected_raster_calls += (work.right - work.left) * (work.bottom - work.top) <= 256 ? 1 : 2;
    if (profile == 49 || profile == 79) {
      auto rejected = bp;
      rejected[0] &= ~(7u << 16);
      rejected[0x10] = 1u << 9;
      CHECK(!renderer.begin(rejected, colors, konst));
      CHECK(!renderer.fallback_reason().empty());
    }
  }
  const auto actual = renderer.synchronize(gpu_colors, gpu_depths, &hardware);
  CHECK(actual.dispatches == expected_raster_calls);
  CHECK(actual.fallback_rectangles == 0);
  CHECK(actual.fragments == expected.fragments);
  CHECK(actual.left == expected.left && actual.right == expected.right);
  CHECK(actual.top == expected.top && actual.bottom == expected.bottom);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(software.texture_state() == hardware.texture_state());
  std::cout << "Verified 200 mixed0-to-1..4 indirect draws, wrapping/accumulation, partial groups, "
               "rejection, retained maps and rejected-BP continuation; fragments="
            << actual.fragments << '\n';
}

void counter_checks() {
  Memory memory{Console::GameCube};
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader software(memory, bp, colors, konst, tmem);
  gfx::FragmentShader hardware(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(cpu_colors.size());
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  std::mt19937 generator(0x4120398);
  for (unsigned i = 0; i < 256; ++i)
    memory.write8(0x80000 + i, static_cast<u8>(i * 37 + 19));
  bp[0x80] = 1 | (1 << 2);
  bp[0x88] = 7 | (7 << 10) | (6 << 20);
  bp[0x94] = 0x80000 >> 5;
  bp[0x28] = 64;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xc0] = 0xfff8 | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  constexpr std::array<int, 8> extents{1, 7, 8, 9, 15, 16, 17, 63};
  u64 checked_fragments = 0;
  u64 expected_raster_calls = 0;
  for (unsigned profile = 0; profile < 256; ++profile) {
    std::fill(cpu_colors.begin(), cpu_colors.end(), 0x315a87);
    for (unsigned i = 0; i < cpu_depths.size(); ++i)
      cpu_depths[i] = 128 + (i % 7) * 32;
    gpu_colors = cpu_colors;
    gpu_depths = cpu_depths;
    renderer.upload(gpu_colors, gpu_depths);
    const unsigned maps = profile % 5;
    bp[0] = 1 | (1 << 4) | (maps << 16);
    bp[0x43] = profile & 1 ? 64 : 0;
    bp[0x40] = profile < 2 ? 0 : 1 | (((profile >> 1) & 7) << 1) | 16;
    bp[0xf3] = profile < 2 ? (7 << 16) | (7 << 19)
                           : 137 | (205 << 8) | (((profile >> 3) & 7) << 16) |
                                 (((profile >> 5) & 7) << 19) | (((profile >> 6) & 3) << 22);
    gfx::RasterWork work{};
    const int width = extents[profile % extents.size()];
    const int height = extents[(profile / extents.size()) % extents.size()];
    work.left = static_cast<int>(generator() % (641 - width));
    work.top = static_cast<int>(generator() % (529 - height));
    work.right = work.left + width;
    work.bottom = work.top + height;
    work.edges.fill({0, 0, 1});
    if (profile % 4 == 2)
      work.edges[0] = {1, 1, (work.left - work.top) * 16 + 1};
    if (profile % 4 == 3)
      work.edges[1] = {0, -1, -(work.left + width / 2) * 16 + 1};
    work.depth.value = 97;
    work.depth.dx = 7;
    work.depth.dy = 11;
    work.depth.x_origin = work.left;
    work.depth.y_origin = work.top;
    work.reciprocal_w.value = 1;
    work.colors[0][3] = work.depth;
    work.textures[0][0].value = -3;
    work.textures[0][0].dx = 0.75f;
    work.textures[0][0].dy = 0.25f;
    work.textures[0][1].value = 5;
    work.textures[0][1].dx = -0.5f;
    work.textures[0][1].dy = 1.25f;
    gfx::HardwareStats expected;
    for (unsigned draw = 0; draw < 2; ++draw) {
      CHECK(renderer.begin(bp, colors, konst));
      CHECK(renderer.draw(work));
      for (int by = work.top & ~1; by < work.bottom; by += 2)
        for (int bx = work.left & ~1; bx < work.right; bx += 2)
          for (int iy = 0; iy < 2; ++iy)
            for (int ix = 0; ix < 2; ++ix) {
              const int x = bx + ix, y = by + iy;
              if (x < work.left || x >= work.right || y < work.top || y >= work.bottom)
                continue;
              bool covered = true;
              for (const auto &edge : work.edges)
                covered &= edge.constant + edge.dx * y * 16 - edge.dy * x * 16 > 0;
              if (!covered)
                continue;
              const auto offset = static_cast<unsigned>(y * 640 + x);
              u32 depth = static_cast<u32>(work.depth.at(x, y));
              const auto depth_test = [&] {
                if (!(bp[0x40] & 1))
                  return true;
                if (!gfx::compare((bp[0x40] >> 1) & 7, depth, cpu_depths[offset]))
                  return false;
                if (bp[0x40] & 16)
                  cpu_depths[offset] = depth;
                return true;
              };
              const bool early = (bp[0x43] & 64) != 0;
              if (early && !depth_test())
                continue;
              gfx::FragmentInputs inputs;
              inputs.colors[0][3] =
                  static_cast<u8>(std::clamp(work.colors[0][3].at(x, y), 0.0f, 255.0f));
              for (unsigned component = 0; component < 2; ++component) {
                const auto &plane = work.textures[0][component];
                inputs.uv[0][component] = plane.at(x, y);
                inputs.dx[0][component] = plane.at(bx + 1, by) - plane.at(bx, by);
                inputs.dy[0][component] = plane.at(bx, by + 1) - plane.at(bx, by);
              }
              const auto result = software.shade(inputs, &depth);
              if (!result || (!early && !depth_test()))
                continue;
              cpu_colors[offset] =
                  (u32((*result)[0]) << 16) | (u32((*result)[1]) << 8) | (*result)[2];
              ++expected.fragments;
              expected.left = std::min(expected.left, u32(x) & ~1u);
              expected.right = std::max(expected.right, u32(x) | 1u);
              expected.top = std::min(expected.top, u32(y) & ~1u);
              expected.bottom = std::max(expected.bottom, u32(y) | 1u);
            }
      work.textures[0][0].value += 2.5f;
    }
    const auto actual = renderer.synchronize(gpu_colors, gpu_depths, &hardware);
    expected_raster_calls += width * height <= 256 ? 1 : 2;
    CHECK(actual.dispatches == expected_raster_calls);
    CHECK(actual.fallback_rectangles == 0);
    CHECK(actual.fragments == expected.fragments);
    CHECK(actual.left == expected.left && actual.right == expected.right);
    CHECK(actual.top == expected.top && actual.bottom == expected.bottom);
    CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
    CHECK(software.texture_state() == hardware.texture_state());
    checked_fragments += expected.fragments;
  }
  std::cout << "Verified 512 queued counter draws with partial groups, coverage/alpha/depth "
               "rejection and all map latches; accepted fragments="
            << checked_fragments << '\n';
}

void counter_benchmark() {
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  bp[0] = 1 << 4;
  bp[0x40] = 1 | 14 | 16;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  gfx::RasterWork work{};
  work.right = 640;
  work.bottom = 528;
  work.edges.fill({0, 0, 1});
  work.depth.value = 12345;
  for (unsigned c = 0; c < 4; ++c)
    work.colors[0][c].value = gfx::Color{211, 105, 65, 177}[c];
  std::vector<u32> pixels(640 * 528), depths(pixels.size());
  renderer.upload(pixels, depths);
  CHECK(renderer.begin(bp, colors, konst));
  CHECK(renderer.draw(work));
  renderer.synchronize(pixels, depths);
  const auto start = std::chrono::steady_clock::now();
  for (unsigned draw = 0; draw < 64; ++draw)
    CHECK(renderer.draw(work));
  const auto stats = renderer.synchronize(pixels, depths);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(stats.fragments == 64 * pixels.size());
  CHECK(stats.left == 0 && stats.right == 639 && stats.top == 0 && stats.bottom == 527);
  CHECK(stats.fallback_rectangles == 0);
  CHECK(std::all_of(pixels.begin(), pixels.end(), [](u32 value) { return value == 0xd36941; }));
  CHECK(std::all_of(depths.begin(), depths.end(), [](u32 value) { return value == 12345; }));
  std::cout << "Verified 64 warm full EFB rectangles: OpenGL seconds=" << elapsed
            << ", device=" << renderer.device() << '\n';
}

void logical_sequence_cap_checks() {
  auto software = std::make_unique<Fixture>(), hardware = std::make_unique<Fixture>();
  hardware->graphics.set_hardware_rendering(true);
  const std::array<std::array<float, 3>, 3> triangle{
      {{-1, -1, -0.5f}, {-0.9375f, -1, -0.5f}, {-1, -0.9375f, -0.5f}}};
  const std::array<u32, 3> color{0x4997d9ff, 0x4997d9ff, 0x4997d9ff};
  for (unsigned i = 0; i < 4095; ++i) {
    software->draw(0x90, triangle, color);
    hardware->draw(0x90, triangle, color);
  }
  const auto total = software->graphics.fragment_count();
  CHECK(total > 0 && total % 4095 == 0);
  const auto per_triangle = total / 4095;
  auto stats = hardware->graphics.hardware_stats();
  CHECK(hardware->graphics.fragment_count() == total);
  CHECK(stats.fragments == 4094 * per_triangle);
  CHECK(total - stats.fragments == per_triangle);
  CHECK(stats.fallback_rectangles == 1);
  CHECK(stats.dispatches == 256);
  for (unsigned y = 0; y < 528; ++y)
    for (unsigned x = 0; x < 640; ++x) {
      CHECK(software->graphics.color(x, y) == hardware->graphics.color(x, y));
      CHECK(software->graphics.depth(x, y) == hardware->graphics.depth(x, y));
    }
  for (u32 address = 0x0c001010; address < 0x0c001018; address += 2)
    CHECK(software->graphics.read(address, 2) == hardware->graphics.read(address, 2));
  const auto previous_calls = stats.dispatches;
  software->draw(0x90, triangle, color);
  hardware->draw(0x90, triangle, color);
  stats = hardware->graphics.hardware_stats();
  CHECK(hardware->graphics.fragment_count() == 4096 * per_triangle);
  CHECK(stats.fragments == 4095 * per_triangle && stats.fallback_rectangles == 1);
  CHECK(stats.dispatches == previous_calls + 1);
  equal(*software, *hardware);
  std::cout << "Verified 4094 logical latch-sequence cap, one CPU fallback triangle, exact CPU/GPU "
               "accepted-fragment sum and physical raster calls\n";
}

void empty_coverage_checks() {
  Memory memory(Console::GameCube);
  std::array<u32, 256> bp{};
  gfx::TevRegisters colors{}, konst{};
  std::vector<u8> tmem(1024 * 1024);
  gfx::FragmentShader shader(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  bp[0] = 1 << 4;
  bp[0x40] = 1 | 14 | 16;
  bp[0x41] = 24;
  bp[0xf6] = 4;
  bp[0xf7] = 14;
  bp[0xf3] = (7 << 16) | (7 << 19);
  bp[0xc0] = 0xfffa | (1 << 19);
  bp[0xc1] = (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19);
  std::vector<u32> cpu_colors(640 * 528), cpu_depths(cpu_colors.size());
  for (unsigned i = 0; i < cpu_colors.size(); ++i) {
    cpu_colors[i] = (i * 103 + 19) & 0xffffff;
    cpu_depths[i] = (i * 97 + 23) & 0xffffff;
  }
  auto gpu_colors = cpu_colors, gpu_depths = cpu_depths;
  renderer.upload(gpu_colors, gpu_depths);
  gfx::RasterWork work{};
  work.left = 17;
  work.top = 13;
  work.right = 18;
  work.bottom = 14;
  work.edges.fill({0, 0, 1});
  work.depth.value = 0x123456;
  for (unsigned c = 0; c < 4; ++c)
    work.colors[0][c].value = gfx::Color{73, 151, 217, 255}[c];
  CHECK(renderer.begin(bp, colors, konst));
  CHECK(renderer.draw(work));
  auto stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  cpu_colors[13 * 640 + 17] = 0x4997d9;
  cpu_depths[13 * 640 + 17] = 0x123456;
  CHECK(stats.dispatches == 1 && stats.fragments == 1 && stats.fallback_rectangles == 0);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  const auto retained = shader.texture_state();

  struct CapturedRectangle {
    std::array<int, 4> bounds;
    std::array<gfx::RasterWork::Edge, 3> edges;
    unsigned adjusted_edge;
    s64 positive_adjustment;
  };

  // Fixed-point edge sets from small clipped title-animation rectangles.
  constexpr std::array<CapturedRectangle, 6> captured{{
      {{638, 226, 640, 227}, {{{32, 0, -115487}, {-32, -8, 33897}, {0, 8, 81848}}}, 1, 24},
      {{639, 128, 640, 129}, {{{-14, -6, -32685}, {0, 8, 81848}, {14, -2, -49049}}}, 0, 14},
      {{639, 9, 640, 10}, {{{-16, -9, -89566}, {0, 15, 153465}, {16, -6, -63657}}}, 2, 10},
      {{483, 455, 485, 456}, {{{27, 10, -119199}, {-3, -10, -55448}, {-24, 0, 174888}}}, 1, 9},
      {{0, 15, 1, 17}, {{{-19, -8, 4842}, {19, 30, -4621}, {0, -22, 199}}}, 1, 62},
      {{0, 205, 1, 233}, {{{10, -441, -33160}, {-1, 0, 3713}, {-9, 441, 29889}}}, 0, 41},
  }};
  const auto configure = [&](const CapturedRectangle &rectangle) {
    work.left = rectangle.bounds[0];
    work.top = rectangle.bounds[1];
    work.right = rectangle.bounds[2];
    work.bottom = rectangle.bounds[3];
    work.edges = rectangle.edges;
  };
  const auto covered = [&](int x, int y) {
    return std::all_of(work.edges.begin(), work.edges.end(), [&](const auto &edge) {
      return edge.constant + edge.dx * y * 16 - edge.dy * x * 16 > 0;
    });
  };
  for (unsigned i = 0; i < captured.size(); ++i) {
    configure(captured[i]);
    for (int y = work.top; y < work.bottom; ++y)
      for (int x = work.left; x < work.right; ++x)
        CHECK(!covered(x, y));
    CHECK(renderer.draw(work));
    if (i < 3) {
      work.edges[captured[i].adjusted_edge].constant += captured[i].positive_adjustment - 1;
      for (int y = work.top; y < work.bottom; ++y)
        for (int x = work.left; x < work.right; ++x)
          CHECK(!covered(x, y));
      CHECK(renderer.draw(work));
    }
  }
  stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  CHECK(stats.dispatches == 1 && stats.fragments == 0 && stats.fallback_rectangles == 0);
  CHECK(stats.left == 0xffffffff && stats.right == 0 && stats.top == 0xffffffff &&
        stats.bottom == 0);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(shader.texture_state() == retained);

  for (const auto &rectangle : captured) {
    configure(rectangle);
    work.edges[rectangle.adjusted_edge].constant += rectangle.positive_adjustment;
    unsigned samples = 0;
    for (int y = work.top; y < work.bottom; ++y)
      for (int x = work.left; x < work.right; ++x)
        if (covered(x, y)) {
          ++samples;
          cpu_colors[y * 640 + x] = 0x4997d9;
          cpu_depths[y * 640 + x] = 0x123456;
        }
    CHECK(samples == 1);
    CHECK(renderer.draw(work));
  }
  stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  CHECK(stats.dispatches == 7 && stats.fragments == 6 && stats.fallback_rectangles == 0);
  CHECK(stats.left == 0 && stats.right == 639 && stats.top == 8 && stats.bottom == 455);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(shader.texture_state() == retained);

  for (unsigned i = 0; i < 256; ++i)
    memory.write8(0x80000 + i, static_cast<u8>(i * 37 + 19));
  bp[0] |= 1;
  bp[0x28] = 64;
  bp[0x88] = 7 | (7 << 10) | (6 << 20);
  bp[0x94] = 0x80000 >> 5;
  bp[0xc0] = 0xfff8 | (1 << 19);
  work.left = 41;
  work.top = 43;
  work.right = 42;
  work.bottom = 44;
  work.edges.fill({0, 0, 1});
  work.textures[0][0].value = 0.5f;
  work.textures[0][1].value = 1.5f;
  gfx::FragmentShader textured_software(memory, bp, colors, konst, tmem);
  gfx::FragmentInputs input;
  input.colors[0] = {73, 151, 217, 255};
  input.uv[0] = {0.5f, 1.5f};
  u32 depth = 0x123456;
  const auto sampled = textured_software.shade(input, &depth);
  CHECK(sampled.has_value());
  const u32 sampled_color = (u32((*sampled)[0]) << 16) | (u32((*sampled)[1]) << 8) | (*sampled)[2];
  CHECK(renderer.begin(bp, colors, konst));
  CHECK(renderer.draw(work));
  stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  cpu_colors[43 * 640 + 41] = sampled_color;
  cpu_depths[43 * 640 + 41] = depth;
  CHECK(stats.dispatches == 8 && stats.fragments == 1 && stats.fallback_rectangles == 0);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(shader.texture_state().texture != gfx::Color{});
  CHECK(shader.texture_state() == textured_software.texture_state());
  for (const auto &rectangle : captured) {
    configure(rectangle);
    CHECK(renderer.draw(work));
  }
  stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  CHECK(stats.dispatches == 8 && stats.fragments == 0 && stats.fallback_rectangles == 0);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(shader.texture_state() == textured_software.texture_state());
  configure(captured[0]);
  work.edges[captured[0].adjusted_edge].constant += captured[0].positive_adjustment;
  CHECK(textured_software.shade(input, &depth).has_value());
  CHECK(renderer.draw(work));
  stats = renderer.synchronize(gpu_colors, gpu_depths, &shader);
  cpu_colors[226 * 640 + 639] = sampled_color;
  cpu_depths[226 * 640 + 639] = depth;
  CHECK(stats.dispatches == 9 && stats.fragments == 1 && stats.fallback_rectangles == 0);
  CHECK(cpu_colors == gpu_colors && cpu_depths == gpu_depths);
  CHECK(shader.texture_state() == textured_software.texture_state());
  std::cout
      << "Verified six captured empty rectangles, strict boundaries and six visible neighbors, "
         "nonzero retained texture state, device="
      << renderer.device() << '\n';
}

void benchmark(Fixture &software, Fixture &hardware) {
  const std::array<std::array<float, 3>, 4> quad{
      {{-1, -1, -0.5f}, {1, -1, -0.5f}, {1, 1, -0.5f}, {-1, 1, -0.5f}}};
  const std::array<u32, 4> colors{0x124878d2, 0xf0d0b090, 0x3456ef78, 0x9abcedff};
  for (auto *fixture : {&software, &hardware}) {
    fixture->floats(0x101a, {320, 264, 1000, 662, 606, 1000});
    fixture->bp(0x21, (981 << 12) | 869);
    fixture->bp(0, 1 << 4);
    fixture->bp(0x28, 0);
    fixture->bp(0x40, 0);
    fixture->bp(0x41, 24);
    fixture->bp(0x43, 0);
    fixture->bp(0xf1, 0);
    fixture->bp(0xf5, 0);
    fixture->bp(0xf3, (7 << 16) | (7 << 19));
    fixture->bp(0xc0, 0xfffa | (1 << 19));
    fixture->bp(0xc1, (7 << 13) | (7 << 10) | (7 << 7) | (5 << 4) | (1 << 19));
  }
  const auto elapsed = [&](Fixture &fixture) {
    const auto started = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 64; ++i)
      fixture.draw(0x80, quad, colors);
    fixture.graphics.fragment_count();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  };
  const double cpu = elapsed(software), gpu = elapsed(hardware);
  CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
  for (unsigned y = 0; y < 528; ++y)
    for (unsigned x = 0; x < 640; ++x) {
      CHECK(software.graphics.color(x, y) == hardware.graphics.color(x, y));
      CHECK(software.graphics.depth(x, y) == hardware.graphics.depth(x, y));
    }
  std::cout << "Verified 64 full EFB quads: CPU seconds=" << cpu << ", OpenGL seconds=" << gpu
            << ", rendering ratio=" << cpu / gpu << '\n';
}
} // namespace

int main(int argc, char **argv) {
  renderer_run_started = std::chrono::steady_clock::now();
  std::cout << std::unitbuf << "[renderer] begin fixture construction\n";
  const auto fixture_started = renderer_stage_started("fixture construction");
  Fixture software, hardware;
  std::cout << "[renderer] returned fixture construction\n";
  renderer_stage_returned("fixture construction", fixture_started);
  bool required = false, measure = false, fog_only = false, texture_cache_only = false,
       xfb_only = false, efb_copy_only = false, bbox_only = false, projection_only = false,
       state_only = false, fixed_lod_only = false, counter_only = false,
       counter_benchmark_only = false, empty_coverage_only = false,
       division_identities_exhaustive = false, division_rounding_only = false,
       program_cache_only = false, skip_program_cache_checks = false;
  for (int i = 1; i < argc; ++i) {
    program_cache_only |= std::strcmp(argv[i], "--program-cache-only") == 0;
    skip_program_cache_checks |= std::strcmp(argv[i], "--skip-program-cache-checks") == 0;
    required |= std::strcmp(argv[i], "--require-device") == 0;
    measure |= std::strcmp(argv[i], "--benchmark") == 0;
    fog_only |= std::strcmp(argv[i], "--fog-only") == 0;
    texture_cache_only |= std::strcmp(argv[i], "--texture-cache-only") == 0;
    xfb_only |= std::strcmp(argv[i], "--xfb-only") == 0;
    efb_copy_only |= std::strcmp(argv[i], "--efb-copy-only") == 0;
    bbox_only |= std::strcmp(argv[i], "--bbox-only") == 0;
    projection_only |= std::strcmp(argv[i], "--projection-only") == 0;
    state_only |= std::strcmp(argv[i], "--state-only") == 0;
    fixed_lod_only |= std::strcmp(argv[i], "--fixed-lod-only") == 0;
    counter_only |= std::strcmp(argv[i], "--counter-only") == 0;
    counter_benchmark_only |= std::strcmp(argv[i], "--counter-benchmark-only") == 0;
    empty_coverage_only |= std::strcmp(argv[i], "--empty-coverage-only") == 0;
    division_identities_exhaustive |= std::strcmp(argv[i], "--division-identities-exhaustive") == 0;
    division_rounding_only |= std::strcmp(argv[i], "--division-rounding-only") == 0;
  }
  try {
    renderer_stage("backend initialization",
                   [&] { hardware.graphics.set_hardware_rendering(true); });
    std::cout << "[renderer] device=" << hardware.graphics.hardware_device() << '\n';
  } catch (const gfx::HardwareUnavailable &error) {
    std::cerr << "Hardware backend unavailable: " << error.what() << '\n';
    return required ? 1 : 77;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return run_tests([&] {
    if (bbox_only) {
      renderer_stage("bounding_box_reset_checks", [&] { bounding_box_reset_checks(); });
      return;
    }
    if (efb_copy_only) {
      renderer_stage("buffer_fragment_fallback_checks", [&] { buffer_fragment_fallback_checks(); });
      renderer_stage("efb_texture_copy_checks", [&] { efb_texture_copy_checks(); });
      if (measure)
        renderer_stage("efb_texture_copy_benchmark", [&] { efb_texture_copy_benchmark(); });
      return;
    }
    if (division_rounding_only) {
      renderer_stage("division_rounding_checks", [&] { division_rounding_checks(); });
      return;
    }
    if (program_cache_only) {
      renderer_stage("zero_program_creation_checks", [&] { zero_program_creation_checks(); });
      renderer_stage("zero_first_use_environment_checks",
                     [&] { zero_first_use_environment_checks(); });
      renderer_stage("zero_family_lifetime_checks", [&] { zero_family_lifetime_checks(); });
      renderer_stage("zero_family_transitions", [&] { zero_family_transitions(); });
      return;
    }
    if (division_identities_exhaustive) {
      renderer_stage("division_identity_checks", [&] { division_identity_checks(true); });
      return;
    }
    if (empty_coverage_only) {
      renderer_stage("logical_sequence_cap_checks", [&] { logical_sequence_cap_checks(); });
      renderer_stage("empty_coverage_checks", [&] { empty_coverage_checks(); });
      return;
    }
    if (counter_benchmark_only) {
      renderer_stage("counter_benchmark", [&] { counter_benchmark(); });
      return;
    }
    if (counter_only) {
      renderer_stage("counter_checks", [&] { counter_checks(); });
      if (measure)
        renderer_stage("counter_benchmark", [&] { counter_benchmark(); });
      return;
    }
    if (state_only) {
      renderer_stage("state_upload_checks", [&] { state_upload_checks(measure); });
      renderer_stage("latch_state_readback_checks", [&] { latch_state_readback_checks(); });
      renderer_stage("bounding_box_reset_checks", [&] { bounding_box_reset_checks(); });
      return;
    }
    if (fixed_lod_only) {
      renderer_stage("projective_mip_checks", [&] { projective_mip_checks(); });
      if (measure)
        renderer_stage("projective_stage_benchmark", [&] { projective_stage_benchmark(true); });
      return;
    }
    if (projection_only) {
      renderer_stage("division_rounding_checks", [&] { division_rounding_checks(); });
      renderer_stage("division_identity_checks", [&] { division_identity_checks(false); });
      renderer_stage("projection_precision_checks", [&] { projection_precision_checks(); });
      renderer_stage("projective_mip_checks", [&] { projective_mip_checks(); });
      renderer_stage("division_fragment_checks", [&] { division_fragment_checks(true, false); });
      if (measure)
        renderer_stage("projective_stage_benchmark", [&] { projective_stage_benchmark(); });
      return;
    }
    if (xfb_only) {
      renderer_stage("xfb_checks", [&] { xfb_checks(measure); });
      renderer_stage("xfb_clear_checks", [&] { xfb_clear_checks(); });
      return;
    }
    if (texture_cache_only) {
      renderer_stage("texture_cache_checks", [&] { texture_cache_checks(measure); });
      renderer_stage("texture_cache_bank_checks", [&] { texture_cache_bank_checks(); });
      renderer_stage("texture_partial_upload_checks", [&] { texture_partial_upload_checks(); });
      return;
    }
    if (!fog_only) {
      renderer_stage("division_rounding_checks", [&] { division_rounding_checks(); });
      renderer_stage("division_identity_checks", [&] { division_identity_checks(false); });
      renderer_stage("empty_coverage_checks", [&] { empty_coverage_checks(); });
      renderer_stage("counter_checks", [&] { counter_checks(); });
      renderer_stage("logical_sequence_cap_checks", [&] { logical_sequence_cap_checks(); });
      renderer_stage("state_upload_checks", [&] { state_upload_checks(false); });
      renderer_stage("projection_precision_checks", [&] { projection_precision_checks(); });
      renderer_stage("projective_mip_checks", [&] { projective_mip_checks(); });
      renderer_stage("checks", [&] { checks(software, hardware); });
      renderer_stage("indirect_checks", [&] { indirect_checks(software, hardware); });
      renderer_stage("indirect_state_checks", [&] { indirect_state_checks(); });
      renderer_stage("latch_state_readback_checks", [&] { latch_state_readback_checks(); });
      renderer_stage("bounding_box_reset_checks", [&] { bounding_box_reset_checks(); });
      if (!skip_program_cache_checks) {
        renderer_stage("zero_program_creation_checks", [&] { zero_program_creation_checks(); });
        renderer_stage("zero_first_use_environment_checks",
                       [&] { zero_first_use_environment_checks(); });
        renderer_stage("zero_family_lifetime_checks", [&] { zero_family_lifetime_checks(); });
        renderer_stage("zero_family_transitions", [&] { zero_family_transitions(); });
      }
      renderer_stage("wide_depth_origin_checks", [&] { wide_depth_origin_checks(); });
      renderer_stage("texture_cache_checks", [&] { texture_cache_checks(false); });
      renderer_stage("texture_cache_bank_checks", [&] { texture_cache_bank_checks(); });
      renderer_stage("texture_partial_upload_checks", [&] { texture_partial_upload_checks(); });
      renderer_stage("xfb_checks", [&] { xfb_checks(false); });
      renderer_stage("buffer_fragment_fallback_checks", [&] { buffer_fragment_fallback_checks(); });
      renderer_stage("efb_texture_copy_checks", [&] { efb_texture_copy_checks(); });
      renderer_stage("xfb_clear_checks", [&] { xfb_clear_checks(); });
    }
    renderer_stage("division_fragment_checks", [&] { division_fragment_checks(!fog_only, true); });
    renderer_stage("fog_checks", [&] { fog_checks(); });
    renderer_stage("fog_bucket_checks", [&] { fog_bucket_checks(); });
    if (measure)
      renderer_stage("benchmark", [&] { benchmark(software, hardware); });
  });
}
