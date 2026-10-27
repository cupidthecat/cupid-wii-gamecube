// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "hardware_native_program_checks.hpp"

namespace {
namespace hardware_native_raster_test {
using namespace cupid;

void program_mode_proof() {
  CHECK(native_ordered_raster_active());
  Memory memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024);
  std::array<u32, 256> bp{};
  hardware_batch_test::projection::configure(memory, bp, 1, 1);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader retained(memory, bp, colors, konst, tmem);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::vector<u32> pixels(640 * 528), depths(pixels.size());
  renderer.upload(pixels, depths);
  CHECK(renderer.begin(bp, colors, konst));
  auto work = hardware_batch_test::projection_cases::seed();
  work.left = 1;
  work.top = 3;
  work.right = 639;
  work.bottom = 527;
  CHECK(renderer.draw(work));
  const auto stats = renderer.synchronize(pixels, depths, &retained);
  CHECK(stats.dispatches == 1 && stats.fragments != 0 && stats.fallback_rectangles == 0);
  using GetInteger = void(APIENTRY *)(GLenum, GLint *);
  const auto integer = reinterpret_cast<GetInteger>(SDL_GL_GetProcAddress("glGetIntegerv"));
  const auto attached =
      reinterpret_cast<PFNGLGETATTACHEDSHADERSPROC>(SDL_GL_GetProcAddress("glGetAttachedShaders"));
  const auto shader =
      reinterpret_cast<PFNGLGETSHADERIVPROC>(SDL_GL_GetProcAddress("glGetShaderiv"));
  using GetError = GLenum(APIENTRY *)();
  const auto error = reinterpret_cast<GetError>(SDL_GL_GetProcAddress("glGetError"));
  CHECK(integer && attached && shader && error);
  GLint program = 0;
  integer(GL_CURRENT_PROGRAM, &program);
  CHECK(program != 0);
  std::array<GLuint, 4> shaders{};
  GLsizei count = 0;
  attached(GLuint(program), GLsizei(shaders.size()), &count, shaders.data());
  unsigned vertex = 0, fragment = 0, compute = 0;
  for (GLsizei n = 0; n < count; ++n) {
    GLint type = 0;
    shader(shaders[std::size_t(n)], GL_SHADER_TYPE, &type);
    vertex += type == GL_VERTEX_SHADER;
    fragment += type == GL_FRAGMENT_SHADER;
    compute += type == GL_COMPUTE_SHADER;
  }
  CHECK(vertex == 1 && fragment == 1 && compute == 0);
  CHECK(error() == GL_NO_ERROR);
  std::cout << "[native-mode] active program has vertex=1 fragment=1 compute=0; full-size work "
               "submitted\n";
}

void checks() {
  program_mode_proof();
  hardware_native_program_test::checks();
  using hardware_mid_batch_test::Cut;
  using hardware_mid_batch_test::Sequence;
  std::vector<Sequence> sequences{
      {"native_full_extent_pair", {{0, 0, 640, 528}, {0, 0, 640, 528}}, 1},
      {"native_full_extent_budget", std::vector<std::array<int, 4>>(4, {0, 0, 640, 528}), 2},
      {"native_area_exact", std::vector<std::array<int, 4>>(4, {0, 0, 512, 512}), 1},
      {"native_area_plus_one",
       {{0, 0, 512, 512}, {0, 0, 512, 512}, {0, 0, 512, 512}, {0, 0, 512, 512}, {0, 0, 1, 1}},
       2},
      {"native_odd_extent", {{1, 3, 639, 527}, {2, 4, 638, 526}}, 1},
      {"native_disjoint_sides", {{0, 0, 320, 528}, {320, 0, 640, 528}}, 1},
      {"native_area4097", {{0, 0, 17, 241}, {1, 1, 18, 242}}, 1},
      {"native_mixed_area", {{0, 0, 17, 17}, {0, 0, 65, 65}, {0, 0, 640, 528}, {3, 5, 20, 22}}, 1},
  };
  for (unsigned count : {255u, 256u, 257u})
    sequences.push_back({count == 255   ? "native_packet255"
                         : count == 256 ? "native_packet256"
                                        : "native_packet257",
                         std::vector<std::array<int, 4>>(count, {0, 0, 8, 8}),
                         count <= 256 ? 1u : 2u});
  std::vector<std::array<int, 4>> sparse;
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      sparse.push_back({column * 200, row * 170, column * 200 + 17, row * 170 + 17});
  sequences.push_back({"native_sparse_full_extent", sparse, 1});
  for (const auto &[name, cut] : {std::pair{"native_state_cut", Cut::State},
                                  {"native_texture_cut", Cut::Texture},
                                  {"native_family_cut", Cut::Family},
                                  {"native_texture_copy_cut", Cut::TextureCopy},
                                  {"native_clear_copy_cut", Cut::ClearCopy},
                                  {"native_xfb_cut", Cut::Xfb},
                                  {"native_bounds_cut", Cut::Bounding},
                                  {"native_upload_cut", Cut::Upload}})
    sequences.push_back({name, {{1, 3, 129, 131}, {3, 5, 131, 133}}, 2, cut});
  for (unsigned family : {0u, 1u})
    for (const auto &sequence : sequences)
      hardware_mid_batch_test::direct_sequence(sequence, family, true);
  hardware_mid_batch_test::command_processor_blend_checks();
  std::cout << "[native-raster] " << sequences.size() * 2
            << " direct full-EFB/depth/state32/RAM sequences and 16 CP blend/depth/alpha streams "
               "passed\n";
}
} // namespace hardware_native_raster_test
} // namespace
