// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace {
namespace hardware_mid_batch_test {
using namespace cupid;

enum class Cut { None, State, Texture, Family, TextureCopy, ClearCopy, Xfb, Bounding, Upload };

struct Sequence {
  const char *name;
  std::vector<std::array<int, 4>> bounds;
  u64 dispatches;
  Cut cut = Cut::None;
};

void direct_sequence(const Sequence &sequence, unsigned family) {
  const HostFloatEnvironment environment;
  Memory memory{Console::GameCube}, reference_memory{Console::GameCube};
  std::vector<u8> tmem(1024 * 1024), reference_tmem(tmem.size());
  std::array<u32, 256> bp{}, reference_bp{};
  hardware_batch_test::projection::configure(memory, bp, 1, family);
  hardware_batch_test::projection::configure(reference_memory, reference_bp, 1, family);
  hardware_batch_test::projection::patterned_tile(memory);
  hardware_batch_test::projection::patterned_tile(reference_memory);
  gfx::TevRegisters colors{}, konst{};
  gfx::FragmentShader reference(reference_memory, bp, colors, konst, reference_tmem);
  gfx::FragmentShader retained(memory, bp, colors, konst, tmem);
  hardware_batch_test::projection::seed(reference);
  hardware_batch_test::projection::seed(retained);
  gfx::HardwareRenderer renderer(memory, tmem);
  std::vector<u32> expected(640 * 528, 0x315a87), expected_depths(expected.size(), 0x54321);
  auto actual = expected, actual_depths = expected_depths;
  renderer.upload(actual, actual_depths);
  u64 fragments = 0;
  const auto compare_ram = [&] {
    const auto first = memory.range(0x160000 - 64, 65536 + 128);
    const auto second = reference_memory.range(0x160000 - 64, 65536 + 128);
    CHECK(std::equal(first.begin(), first.end(), second.begin()));
  };
  for (unsigned index = 0; index < sequence.bounds.size(); ++index) {
    if (index == 1) {
      if (sequence.cut == Cut::State)
        konst[0][0] ^= 1;
      if (sequence.cut == Cut::Family)
        bp[0] ^= 1u << 16;
      if (sequence.cut == Cut::Texture) {
        std::array<u8, 64> tile{};
        for (unsigned pixel = 0; pixel < 16; ++pixel) {
          tile[pixel * 2] = 255;
          tile[pixel * 2 + 1] = 211;
          tile[32 + pixel * 2] = 73;
          tile[33 + pixel * 2] = 19;
        }
        memory.copy_in(0x30000, tile);
        reference_memory.copy_in(0x30000, tile);
      }
      if (sequence.cut == Cut::TextureCopy || sequence.cut == Cut::ClearCopy) {
        auto copy = bp;
        copy[0x43] = copy[0x49] = 0;
        copy[0x4a] = 3 | (3 << 10);
        copy[0x4b] = 0x160000 >> 5;
        copy[0x4d] = 2;
        copy[0x40] = 16;
        copy[0x41] = 8;
        copy[0x4f] = 0x71f4;
        copy[0x50] = 0x38d2;
        copy[0x51] = 0x815392;
        copy[0x52] = (6 << 4) | (sequence.cut == Cut::ClearCopy ? 1 << 11 : 0);
        gfx::copy_efb_texture(
            reference_memory, copy,
            {[&](unsigned x, unsigned y) { return (expected[y * 640 + x] << 8) | 255u; },
             [&](unsigned x, unsigned y) { return expected_depths[y * 640 + x]; }});
        CHECK(renderer.copy_texture(copy));
        compare_ram();
        if (sequence.cut == Cut::ClearCopy)
          for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x) {
              expected[y * 640 + x] = 0xf438d2;
              expected_depths[y * 640 + x] = 0x815392;
            }
      }
      if (sequence.cut == Cut::Xfb) {
        auto copy = bp;
        copy[0x49] = 0;
        copy[0x4a] = 15 | (15 << 10);
        copy[0x4b] = 0x160000 >> 5;
        copy[0x4d] = 1;
        copy[0x4e] = 256;
        copy[0x52] = (1 << 14) | 3;
        copy[0x53] = (1 << 12) | (63 << 18);
        gfx::copy_efb_xfb(reference_memory, copy, [&](unsigned x, unsigned y) {
          return (expected[y * 640 + x] << 8) | 255u;
        });
        CHECK(renderer.copy_xfb(copy));
        compare_ram();
      }
      if (sequence.cut == Cut::Bounding)
        renderer.reset_bounding_box(false);
      if (sequence.cut == Cut::Upload)
        renderer.upload(expected, expected_depths);
    }
    auto work = hardware_batch_test::projection_cases::seed();
    const auto &bounds = sequence.bounds[index];
    work.left = bounds[0];
    work.top = bounds[1];
    work.right = bounds[2];
    work.bottom = bounds[3];
    work.depth.value = float(73 + index);
    work.textures[0][0] = {float(index) * .375f, .125f, .25f, 0, 0, 0, 0};
    work.textures[0][1] = {float(index) * -.125f, .25f, -.125f, 0, 0, 0, 0};
    CHECK(renderer.begin(bp, colors, konst));
    CHECK(renderer.draw(work));
    fragments +=
        hardware_batch_test::projection::shade(work, 1, reference, expected, expected_depths);
  }
  const auto stats = renderer.synchronize(actual, actual_depths, &retained);
  if (stats.dispatches != sequence.dispatches || actual != expected ||
      actual_depths != expected_depths || retained.texture_state() != reference.texture_state())
    throw std::runtime_error(std::string("Mid-batch case ") + sequence.name +
                             " family=" + std::to_string(family) +
                             " dispatches=" + std::to_string(stats.dispatches) +
                             " wanted=" + std::to_string(sequence.dispatches));
  CHECK(stats.fragments == fragments && stats.fallback_rectangles == 0);
  compare_ram();
  std::cout << "[mid-batch] " << sequence.name << " family=" << family
            << " rectangles=" << sequence.bounds.size() << " dispatches=" << stats.dispatches
            << " fragments=" << stats.fragments << " full_EFB_depth_state32_RAM_equal=1\n";
}

void command_processor_blend_checks() {
  unsigned cases = 0;
  for (unsigned family : {0u, 1u})
    for (unsigned mode = 0; mode < 8; ++mode) {
      Fixture software, hardware;
      hardware.graphics.set_hardware_rendering(true);
      for (Fixture *fixture : {&software, &hardware}) {
        fixture->floats(0x101a, {64, 64, 1000, 406, 406, 1000});
        fixture->bp(0x21, (469 << 12) | 469);
        fixture->bp(0, (1 << 4) | (family << 16));
        constexpr u32 blends[]{24,
                               25 | (4 << 8) | (5 << 5),
                               25 | (1 << 8) | (1 << 5),
                               25 | 2048,
                               26 | (6 << 12),
                               16,
                               28,
                               24};
        fixture->bp(0x41, blends[mode]);
        fixture->bp(0x43, mode == 6 ? 1 : (mode & 1 ? 64 : 0));
        fixture->bp(0x40, mode == 7 ? 1 | (1 << 1) | 16 : 1 | (7 << 1) | 16);
        if (mode == 7)
          fixture->bp(0xf3, 137 | (205 << 8) | (4 << 16) | (7 << 19));
      }
      const auto draw = [&](int left, int top, int extent, u32 color, float z) {
        const auto vertex = [&](int x, int y) {
          return std::array<float, 3>{(float(x) - 64) / 64, (float(y) - 64) / 64, z};
        };
        const std::array<std::array<float, 3>, 4> vertices{
            vertex(left, top), vertex(left + extent, top), vertex(left, top + extent),
            vertex(left + extent, top + extent)};
        const std::array<u32, 1> colors{color};
        software.draw(0x98, vertices, colors);
        hardware.draw(0x98, vertices, colors);
      };
      // The original 17-square area now batches. A 65-square control remains scalar.
      draw(3, 5, 17, 0xc13b7180, -.75f);
      draw(7, 9, 32, 0x39c751b0, -.5f);
      draw(20, 25, 64, 0x8b4dc9d0, -.625f);
      draw(25, 20, 65, 0x5397bd90, -.875f);
      draw(9, 7, 17, 0xe17f33a0, -.25f);
      CHECK(software.graphics.fragment_count() == hardware.graphics.fragment_count());
      for (unsigned y = 0; y < 528; ++y)
        for (unsigned x = 0; x < 640; ++x) {
          CHECK(software.graphics.color(x, y) == hardware.graphics.color(x, y));
          CHECK(software.graphics.depth(x, y) == hardware.graphics.depth(x, y));
        }
      for (u32 address = 0x0c001010; address <= 0x0c001016; address += 2)
        CHECK(software.graphics.read(address, 2) == hardware.graphics.read(address, 2));
      CHECK(hardware.graphics.hardware_stats().fallback_rectangles == 0);
      ++cases;
    }
  std::cout << "[mid-batch] " << cases
            << " overlapping mixed scalar/batch command streams: full EFB/depth/bounds equal\n";
}

void checks() {
  std::vector<Sequence> sequences{
      {"area256", {{0, 0, 16, 16}, {0, 0, 16, 16}}, 1},
      {"area257", {{0, 0, 257, 1}, {0, 0, 257, 1}}, 1},
      {"area4096", {{0, 0, 64, 64}, {0, 0, 64, 64}}, 1},
      {"area4097", {{0, 0, 17, 241}, {0, 0, 17, 241}}, 2},
      {"scalar65", {{0, 0, 65, 65}, {0, 0, 65, 65}}, 2},
      {"partial_overlap", {{0, 0, 32, 32}, {16, 0, 48, 32}}, 1},
      {"disjoint", {{0, 0, 32, 32}, {32, 0, 64, 32}}, 1},
      {"union4096", {{0, 0, 64, 32}, {0, 32, 64, 64}}, 1},
      {"union_exhaustion", {{0, 0, 64, 64}, {1, 0, 65, 64}}, 2},
      {"ratio4", {{0, 0, 17, 17}, {119, 0, 136, 17}}, 1},
      {"ratio_exhaustion", {{0, 0, 17, 17}, {120, 0, 137, 17}}, 2},
      {"mixed_dispatch", {{0, 0, 17, 17}, {0, 0, 65, 65}, {0, 0, 17, 17}}, 3},
  };
  for (unsigned count : {16u, 17u})
    sequences.push_back({count == 16 ? "packet16" : "packet17",
                         std::vector<std::array<int, 4>>(count, {0, 0, 17, 17}),
                         count == 16 ? 1u : 2u});
  for (const auto &[name, cut] : {std::pair{"state_cut", Cut::State},
                                  {"texture_cut", Cut::Texture},
                                  {"family_cut", Cut::Family},
                                  {"texture_copy_cut", Cut::TextureCopy},
                                  {"copy_clear_cut", Cut::ClearCopy},
                                  {"xfb_cut", Cut::Xfb},
                                  {"bounding_cut", Cut::Bounding},
                                  {"upload_cut", Cut::Upload}})
    sequences.push_back({name, {{0, 0, 17, 17}, {1, 1, 18, 18}}, 2, cut});
  for (unsigned family : {0u, 1u})
    for (const auto &sequence : sequences)
      direct_sequence(sequence, family);
  command_processor_blend_checks();
  std::cout << "[mid-batch] " << sequences.size() * 2
            << " direct boundary/order/retained/copy sequences passed\n";
}
} // namespace hardware_mid_batch_test
} // namespace
