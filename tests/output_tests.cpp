// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "test_support.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <vector>

namespace {
using namespace cupid;

SDL_Window *game_window() {
  for (u32 id = 1; id < 32; ++id)
    if (auto *window = SDL_GetWindowFromID(id))
      return window;
  throw std::runtime_error("Output did not create its SDL window");
}

void check_surface(SDL_Window *window, int x, int y, bool white) {
  auto *renderer = SDL_GetRenderer(window);
  CHECK(renderer != nullptr);
  int width = 0, height = 0;
  CHECK(SDL_GetRendererOutputSize(renderer, &width, &height) == 0);
  CHECK(x >= 0 && x < width && y >= 0 && y < height);
  CHECK(SDL_RenderSetLogicalSize(renderer, 0, 0) == 0);
  std::vector<u8> rgba(std::size_t(width) * height * 4);
  CHECK(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, rgba.data(), width * 4) ==
        0);
  const auto offset = (std::size_t(y) * width + x) * 4;
  for (unsigned component = 0; component < 3; ++component)
    CHECK(white ? rgba[offset + component] >= 253 : rgba[offset + component] == 0);
}

void display_geometry(platform::DisplayAspect mode) {
  Memory memory(Console::GameCube);
  hw::Devices devices(Console::GameCube);
  const std::array<u8, 8> white_field{235, 128, 235, 128, 235, 128, 235, 128};
  memory.copy_in(0x1000, white_field);
  platform::Output output({false, false, {}, {}, mode});
  const hw::Xfb field{0x1000, 4, 1, 8, 0, 0};
  output.present(memory, field);
  auto *window = game_window();
  check_surface(window, 320, 240, true);
  check_surface(window, 320, 20, mode == platform::DisplayAspect::Standard);
  check_surface(window, 320, 100, mode != platform::DisplayAspect::Pixels);

  SDL_SetWindowSize(window, 900, 600);
  CHECK(output.poll(devices));
  output.present(memory, field);
  int width = 0, height = 0;
  CHECK(SDL_GetRendererOutputSize(SDL_GetRenderer(window), &width, &height) == 0);
  CHECK(width == 900 && height == 600);
  check_surface(window, 450, 300, true);
  check_surface(window, 450, 20, mode == platform::DisplayAspect::Standard);
  check_surface(window, 20, 300, mode != platform::DisplayAspect::Standard);
  check_surface(window, 450, 100, mode != platform::DisplayAspect::Pixels);

  SDL_WarpMouseInWindow(window, 450, 300);
  CHECK(output.poll(devices));
  CHECK(SDL_GetMouseFocus() == window);
  const auto &points = output.remote_state().infrared;
  CHECK(points[0].x >= 479 && points[0].x <= 481);
  CHECK(points[1].x >= 543 && points[1].x <= 545);
  CHECK(points[0].y >= 383 && points[0].y <= 385);
  SDL_WarpMouseInWindow(window, mode == platform::DisplayAspect::Standard ? 20 : 450,
                        mode == platform::DisplayAspect::Standard ? 300 : 20);
  CHECK(output.poll(devices));
  CHECK(output.remote_state().infrared[0].x == 0xffff);
  CHECK(output.remote_state().infrared[1].x == 0xffff);
}
} // namespace

int main() {
  return run_tests([] {
    using namespace cupid;
    Memory memory(Console::GameCube);
    hw::Devices devices(Console::GameCube);
    const std::array<u8, 16> framebuffer{16,  128, 235, 128, 81,  90,  145, 240,
                                         235, 128, 16,  128, 145, 240, 81,  90};
    memory.copy_in(0x1000, framebuffer);

    {
      platform::Output output({false, true, {}, {}});
      CHECK(output.poll(devices));
      output.present(memory, {0x1000, 2, 2, 4, 0, 0});
      output.present(memory, {0x1000, 4, 2, 8, 0, 0});
      CHECK(output.presented_frames() == 2);
      CHECK(output.last_frame_hash() != 0);

      // A short VI field is displayed with its signal aspect, rather than square pixels.
      const std::array<u8, 8> white_field{235, 128, 235, 128, 235, 128, 235, 128};
      memory.copy_in(0x1000, white_field);
      output.present(memory, {0x1000, 4, 1, 8, 0, 0});
      auto *window = SDL_GetWindowFromID(1);
      CHECK(window != nullptr);
      auto *renderer = SDL_GetRenderer(window);
      CHECK(renderer != nullptr);
      int width = 0, height = 0;
      CHECK(SDL_GetRendererOutputSize(renderer, &width, &height) == 0);
      CHECK(width == 640 && height == 480);
      // Read the complete output surface, including areas outside a logical viewport.
      CHECK(SDL_RenderSetLogicalSize(renderer, 0, 0) == 0);
      std::vector<u8> displayed(std::size_t(width) * height * 4);
      CHECK(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, displayed.data(),
                                 width * 4) == 0);
      const auto offset = (std::size_t(20) * width + 320) * 4;
      CHECK(displayed[offset] >= 253 && displayed[offset + 1] >= 253 &&
            displayed[offset + 2] >= 253);

      const std::array<s16, 8> pcm{0, 0, 32767, -32768, 8192, -8192, 0, 0};
      output.audio(pcm, 32000);
      output.audio(pcm, 48000);
      output.audio(pcm, 32028);
      output.audio(pcm, 48042);
      CHECK(output.audio_samples() == 32);
      CHECK(output.audio_nonzero_samples() == 16);
      CHECK(output.audio_peak() == 32768);
      CHECK(output.audio_hash() == 0xd9168ca25cee7afdULL);
      CHECK_THROWS(output.audio(std::span<const s16>(pcm).first(3), 48000));
      CHECK_THROWS(output.audio(pcm, 20000));
      CHECK(output.audio_samples() == 32 && output.audio_nonzero_samples() == 16);
      CHECK(output.audio_peak() == 32768 && output.audio_hash() == 0xd9168ca25cee7afdULL);
      CHECK(output.poll(devices));
    }
    for (const auto mode : {platform::DisplayAspect::Standard, platform::DisplayAspect::Widescreen,
                            platform::DisplayAspect::Pixels})
      display_geometry(mode);
  });
}
