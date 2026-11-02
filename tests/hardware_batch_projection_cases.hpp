// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <limits>
#include <string>
#include <vector>

namespace {
namespace hardware_batch_test::projection_cases {
using namespace cupid;
using namespace cupid::gfx;

struct Case {
  std::string name;
  RasterWork work;
  unsigned count;
  bool extra;
};

inline RasterWork seed() {
  RasterWork work{};
  work.left = work.top = 0;
  work.right = work.bottom = 2;
  work.edges.fill({0, 0, 1});
  work.depth = {73, 0, 0, 0, 0, 0, 0};
  work.reciprocal_w = {1, 0, 0, 0, 0, 0, 0};
  for (auto &color : work.colors)
    for (auto &p : color)
      p = {0, 0, 0, 0, 0, 0, 0};
  for (auto &t : work.textures)
    t = {RasterPlane{.25f, 0, 0, 0, 0, 0, 0}, RasterPlane{.125f, 0, 0, 0, 0, 0, 0},
         RasterPlane{1, 0, 0, 0, 0, 0, 0}};
  work.viewport_width = 32;
  return work;
}

inline std::vector<Case> authored() {
  std::vector<Case> out;
  const auto add = [&](std::string name, RasterWork work, unsigned count, bool extra) {
    out.push_back({std::move(name), work, count, extra});
  };
  auto w = seed();
  w.reciprocal_w = {-.5f, 1, 0, 0, 0, 0, 0};
  add("A1_W_cross", w, 1, true);
  w = seed();
  w.textures[0][2] = {-.5f, 1, 0, 0, 0, 0, 0};
  const auto qcross = w;
  add("A2_Q_cross", w, 1, true);
  w = seed();
  w.textures[0][2] = {0, 1, 0, 0, 0, 0, 0};
  add("A3_exact_zero_Q_column", w, 1, true);
  w = qcross;
  w.left = w.top = 1;
  add("A4_odd_partial_quad", w, 1, true);
  w = qcross;
  w.left = 639;
  w.top = 527;
  w.right = 640;
  w.bottom = 528;
  w.textures[0][2].x_origin = 638;
  add("A5_bottom_right", w, 1, true);
  w = qcross;
  w.right = w.bottom = 16;
  add("A6_64quads", w, 1, true);
  w = qcross;
  w.textures[0][0].value = 0x1.fffffep21f;
  add("A7_inside_UV_bound", w, 1, true);
  w = seed();
  w.reciprocal_w = {0, 1, 0, 0, 0, 0, 0};
  w.left = 1;
  w.bottom = 1;
  add("D1_uncovered_W_zero", w, 1, false);
  w = qcross;
  w.textures[1][0].value = 0x1p23f;
  add("D2_unused_texgen_bound", w, 2, false);
  w = qcross;
  w.textures[1][0].value = std::bit_cast<float>(0x7fc00000u);
  add("D2_unused_texgen_NaN", w, 2, false);
  w = qcross;
  add("A8_unused_valid_texgen", w, 2, true);
  w = qcross;
  w.right = 26;
  w.bottom = 10;
  add("D3_65quads", w, 1, false);
  w = qcross;
  w.textures[0][0].value = 0x1p22f;
  add("D4_exact_UV_bound", w, 1, false);
  w = qcross;
  w.textures[0][0] = {0, std::bit_cast<float>(0x007fffffu), 0, 2, 0, 0, 0};
  add("D5_subnormal_coefficient_scaled_normal", w, 1, false);
  w = qcross;
  w.textures[0][0] = {.25f, 0x1p-126f, 0, 0x1p-24f, 0, 0, 0};
  add("D6_minimum_normal_source_margin", w, 1, false);
  w = qcross;
  w.textures[0][0] = {.25f, 0x1p-125f, 0, 0x1p-25f, 0, 0, 0};
  add("D6b_normal_operands_product_rounds_zero", w, 1, false);
  w = seed();
  w.reciprocal_w.value = 0x1p100f;
  w.textures[0][2].value = 0x1p-100f;
  add("D7_nonzero_q_product_rounds_zero", w, 1, false);
  w = qcross;
  w.textures[0][0] = {.25f, 0x1p-126f, 0, 0x1.fffffep-1f, 0, 0, 0};
  add("D8_RN_boundary_counterexample", w, 1, false);
  w = qcross;
  w.textures[0][0] = {.25f, 0x1p-125f, 0, .5f, 0, 0, 0};
  add("D8b_normal_result_below_margin", w, 1, false);
  w = qcross;
  w.textures[0][0] = {1, -1, 0, 1, 0, 0, 0};
  add("A9_exact_add_cancellation", w, 1, true);
  w = qcross;
  w.textures[0][0] = {-0.0f, -0.0f, -0.0f, 0, 0, 0, 0};
  add("A10_signed_zero", w, 1, true);
  w = qcross;
  w.textures[0][0].x_origin = -16777215;
  add("A11_exact_int_delta_limit", w, 1, true);
  w = qcross;
  w.textures[0][0].x_origin = -16777216;
  add("D9_inexact_int_delta", w, 1, false);
  w = qcross;
  w.textures[0][0].x_origin = std::numeric_limits<int>::min();
  add("D10_origin_subtraction_overflow", w, 1, false);
  w = qcross;
  w.left = -1;
  add("D11_invalid_EFB_bounds", w, 1, false);
  w = qcross;
  add("D12_invalid_texgen_count", w, 9, false);
  for (unsigned p = 0; p < 4; ++p)
    for (unsigned coefficient = 0; coefficient < 5; ++coefficient) {
      w = qcross;
      RasterPlane *plane = p == 0 ? &w.reciprocal_w : &w.textures[0][p - 1];
      std::array<float *, 5> fields{&plane->value, &plane->dx, &plane->dy, &plane->x_offset,
                                    &plane->y_offset};
      *fields[coefficient] = std::bit_cast<float>(1u);
      add("Tiny_input_plane" + std::to_string(p) + "_field" + std::to_string(coefficient), w, 1,
          false);
    }
  return out;
}
} // namespace hardware_batch_test::projection_cases
} // namespace
