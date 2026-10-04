// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "cupid/audio/host_mixer.hpp"
#include "cupid/platform/pointer.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#if CUPID_HAS_SDL2
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

namespace cupid::platform {
namespace {
u8 component(int value) {
  return static_cast<u8>(std::clamp(value, 0, 255));
}

void pixel(u8 *dest, int y, int u, int v) {
  const float luminance = 1.164f * static_cast<float>(y - 16);
  const float blue_difference = static_cast<float>(u - 128),
              red_difference = static_cast<float>(v - 128);
  dest[0] = component(static_cast<int>(luminance + 1.596f * red_difference));
  dest[1] =
      component(static_cast<int>(luminance - 0.392f * blue_difference - 0.813f * red_difference));
  dest[2] = component(static_cast<int>(luminance + 2.017f * blue_difference));
  dest[3] = 255;
}
} // namespace

Frame decode_xfb(const Memory &memory, const hw::Xfb &xfb) {
  if (!xfb.width || !xfb.height || (xfb.width & 1) || xfb.width > 720 || xfb.height > 576 ||
      xfb.stride_bytes < xfb.width * 2)
    throw std::runtime_error("Invalid VI framebuffer geometry");
  Frame result{xfb.width, xfb.height, std::vector<u8>(std::size_t(xfb.width) * xfb.height * 4)};
  for (u32 y = 0; y < xfb.height; ++y) {
    const u64 row = u64(xfb.address) + u64(y) * xfb.stride_bytes;
    if (row > 0xffffffff)
      throw std::runtime_error("VI framebuffer address wraps");
    auto source = memory.range(static_cast<u32>(row), xfb.width * 2);
    auto *dest = result.rgba.data() + std::size_t(y) * xfb.width * 4;
    for (u32 x = 0; x < xfb.width; x += 2) {
      pixel(dest + x * 4, source[x * 2], source[x * 2 + 1], source[x * 2 + 3]);
      pixel(dest + x * 4 + 4, source[x * 2 + 2], source[x * 2 + 1], source[x * 2 + 3]);
    }
  }
  return result;
}

u64 frame_hash(std::span<const u8> data) {
  u64 result = 14695981039346656037ULL;
  for (u8 byte : data)
    result = (result ^ byte) * 1099511628211ULL;
  return result;
}

struct Output::Impl {
  OutputOptions options;
  Frame last;
  u64 frames = 0, hash = 0, samples = 0;
  u64 nonzero_samples = 0, pcm_hash = 14695981039346656037ULL;
  u32 peak = 0;
  DiscAudioStats disc_stats;
  hw::RemoteState remote;
#if CUPID_HAS_SDL2
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  SDL_AudioDeviceID audio = 0;
  std::mutex audio_mutex;
  std::exception_ptr audio_error;
  std::atomic<bool> audio_lock_failure{false};
  std::unique_ptr<audio::SampleFifo> dma;
  std::unique_ptr<audio::SampleFifo> disc;
  std::array<std::unique_ptr<audio::SampleFifo>, 4> speakers;
  HostAudioStats host_stats;

  void check_audio_error_locked() const {
    if (audio_lock_failure.load())
      throw std::runtime_error("Cannot synchronize host audio callback");
    if (audio_error)
      std::rethrow_exception(audio_error);
  }

  void check_audio_error() {
    const std::lock_guard lock(audio_mutex);
    check_audio_error_locked();
  }

  static void SDLCALL render_audio(void *userdata, Uint8 *bytes, int length) noexcept {
    auto &self = *static_cast<Impl *>(userdata);
    if (length > 0)
      std::fill_n(bytes, static_cast<std::size_t>(length), Uint8{0});
    std::unique_lock<std::mutex> lock;
    try {
      lock = std::unique_lock(self.audio_mutex);
      self.check_audio_error_locked();
      if (length < 0 || (length & 3))
        throw std::runtime_error("Host audio callback has an incomplete stereo frame");
      std::span<s16> destination{reinterpret_cast<s16 *>(bytes),
                                 static_cast<std::size_t>(length) / sizeof(s16)};
      const audio::MixerOptions mixing;
      self.dma->mix(destination, mixing);
      self.disc->mix(destination, mixing);
      for (auto &speaker : self.speakers)
        speaker->mix(destination, mixing);
      self.host_stats.mixed_frames += destination.size() / 2;
      for (std::size_t n = 0; n < destination.size(); n += 2) {
        self.host_stats.left_nonzero += destination[n] != 0;
        self.host_stats.right_nonzero += destination[n + 1] != 0;
        self.host_stats.nonzero_samples += (destination[n] != 0) + (destination[n + 1] != 0);
      }
    } catch (...) {
      if (lock.owns_lock()) {
        if (!self.audio_error)
          self.audio_error = std::current_exception();
      } else {
        self.audio_lock_failure.store(true);
      }
      if (length > 0)
        std::fill_n(bytes, static_cast<std::size_t>(length), Uint8{0});
    }
  }

  float display_aspect = 4.0f / 3.0f;
  std::array<SDL_GameController *, 4> controllers{};
  std::map<std::string, SDL_Scancode> keys{{"a", SDL_SCANCODE_Z},
                                           {"b", SDL_SCANCODE_X},
                                           {"x", SDL_SCANCODE_A},
                                           {"y", SDL_SCANCODE_S},
                                           {"start", SDL_SCANCODE_RETURN},
                                           {"l", SDL_SCANCODE_Q},
                                           {"r", SDL_SCANCODE_W},
                                           {"z", SDL_SCANCODE_E},
                                           {"up", SDL_SCANCODE_UP},
                                           {"down", SDL_SCANCODE_DOWN},
                                           {"left", SDL_SCANCODE_LEFT},
                                           {"right", SDL_SCANCODE_RIGHT},
                                           {"c-up", SDL_SCANCODE_I},
                                           {"c-down", SDL_SCANCODE_K},
                                           {"c-left", SDL_SCANCODE_J},
                                           {"c-right", SDL_SCANCODE_L},
                                           {"remote-a", SDL_SCANCODE_Z},
                                           {"remote-b", SDL_SCANCODE_X},
                                           {"remote-plus", SDL_SCANCODE_RETURN},
                                           {"remote-minus", SDL_SCANCODE_BACKSPACE},
                                           {"remote-home", SDL_SCANCODE_HOME},
                                           {"remote-one", SDL_SCANCODE_1},
                                           {"remote-two", SDL_SCANCODE_2},
                                           {"remote-up", SDL_SCANCODE_UP},
                                           {"remote-down", SDL_SCANCODE_DOWN},
                                           {"remote-left", SDL_SCANCODE_LEFT},
                                           {"remote-right", SDL_SCANCODE_RIGHT},
                                           {"remote-shake", SDL_SCANCODE_SPACE},
                                           {"remote-tilt-left", SDL_SCANCODE_J},
                                           {"remote-tilt-right", SDL_SCANCODE_L},
                                           {"remote-tilt-forward", SDL_SCANCODE_I},
                                           {"remote-tilt-back", SDL_SCANCODE_K},
                                           {"nunchuk-up", SDL_SCANCODE_W},
                                           {"nunchuk-down", SDL_SCANCODE_S},
                                           {"nunchuk-left", SDL_SCANCODE_A},
                                           {"nunchuk-right", SDL_SCANCODE_D},
                                           {"nunchuk-c", SDL_SCANCODE_C},
                                           {"nunchuk-z", SDL_SCANCODE_LSHIFT},
                                           {"nunchuk-shake", SDL_SCANCODE_V}};

  SDL_FRect display_area() const {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0)
      throw std::runtime_error(std::string("Cannot read game-output dimensions: ") +
                               SDL_GetError());
    if (width <= 0 || height <= 0)
      return {};
    const float draw_width = std::min(static_cast<float>(width), height * display_aspect);
    const float draw_height = draw_width / display_aspect;
    return {(width - draw_width) * 0.5f, (height - draw_height) * 0.5f, draw_width, draw_height};
  }

  void open_controller(int index) {
    if (!SDL_IsGameController(index))
      return;
    const auto id = SDL_JoystickGetDeviceInstanceID(index);
    for (auto *controller : controllers)
      if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == id)
        return;
    const auto slot = std::find(controllers.begin(), controllers.end(), nullptr);
    if (slot == controllers.end())
      return;
    *slot = SDL_GameControllerOpen(index);
    if (!*slot)
      throw std::runtime_error(std::string("Cannot open host controller: ") + SDL_GetError());
  }

  ~Impl() {
    if (audio)
      SDL_CloseAudioDevice(audio);
    for (auto *controller : controllers)
      if (controller)
        SDL_GameControllerClose(controller);
    if (texture)
      SDL_DestroyTexture(texture);
    if (renderer)
      SDL_DestroyRenderer(renderer);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }
#endif
};

Output::Output(const OutputOptions &options) : impl_(std::make_unique<Impl>()) {
  if (options.remote_speakers && !options.audio)
    throw std::invalid_argument("Remote speakers require host audio output");
  impl_->options = options;
#if CUPID_HAS_SDL2
  SDL_SetMainReady();
  if (SDL_Init((options.headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) |
               (options.audio ? SDL_INIT_AUDIO : 0)) != 0)
    throw std::runtime_error(std::string("Cannot initialize output: ") + SDL_GetError());
  if (!options.headless) {
    impl_->window = SDL_CreateWindow("cupid-wii-gamecube", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_RESIZABLE);
    if (!impl_->window)
      throw std::runtime_error(std::string("Cannot create game window: ") + SDL_GetError());
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1, SDL_RENDERER_SOFTWARE);
    if (!impl_->renderer)
      throw std::runtime_error(std::string("Cannot create presentation renderer: ") +
                               SDL_GetError());
    for (int index = 0; index < SDL_NumJoysticks(); ++index)
      impl_->open_controller(index);
  }
  if (!options.input_config.empty()) {
    std::ifstream input(options.input_config);
    if (!input)
      throw std::runtime_error("Cannot open input mapping file");
    std::string line;
    unsigned number = 0;
    while (std::getline(input, line)) {
      ++number;
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty() || line[0] == '#')
        continue;
      auto split = line.find('=');
      if (split == std::string::npos)
        throw std::runtime_error("Invalid input mapping at line " + std::to_string(number));
      auto action = line.substr(0, split), name = line.substr(split + 1);
      auto key = SDL_GetScancodeFromName(name.c_str());
      if (!impl_->keys.contains(action) || key == SDL_SCANCODE_UNKNOWN)
        throw std::runtime_error("Unknown input action or key at line " + std::to_string(number));
      impl_->keys[action] = key;
    }
  }
  if (options.audio) {
    impl_->dma = std::make_unique<audio::SampleFifo>();
    impl_->disc = std::make_unique<audio::SampleFifo>(2250);
    impl_->disc->set_volume(0, 0);
    for (auto &speaker : impl_->speakers)
      speaker = std::make_unique<audio::SampleFifo>(36000);
    SDL_AudioSpec desired{}, obtained{};
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = &Impl::render_audio;
    desired.userdata = impl_.get();
    impl_->audio = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (!impl_->audio)
      throw std::runtime_error(std::string("Cannot open audio output: ") + SDL_GetError());
    if (obtained.freq != 48000 || obtained.format != AUDIO_S16SYS || obtained.channels != 2)
      throw std::runtime_error("Host audio did not retain the requested stereo format");
    SDL_PauseAudioDevice(impl_->audio, 0);
  }
#else
  if (!options.headless || options.audio || !options.input_config.empty())
    throw std::runtime_error(
        "This build has headless output only; rebuild with SDL2 for video, audio and input");
#endif
}

Output::~Output() = default;

bool Output::poll(hw::Devices &devices, bool sample_input) {
#if CUPID_HAS_SDL2
  impl_->check_audio_error();
  if (impl_->options.headless)
    return true;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT)
      return false;
    if (event.type == SDL_CONTROLLERDEVICEADDED)
      impl_->open_controller(event.cdevice.which);
    if (event.type == SDL_CONTROLLERDEVICEREMOVED)
      for (auto &controller : impl_->controllers)
        if (controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) ==
                              event.cdevice.which) {
          SDL_GameControllerClose(controller);
          controller = nullptr;
        }
  }
  const auto *state = SDL_GetKeyboardState(nullptr);
  if (state[SDL_SCANCODE_ESCAPE])
    return false;
  if (!sample_input)
    return true;
  auto pressed = [&](const std::string &action) { return state[impl_->keys.at(action)] != 0; };
  hw::PadState pad;
  pad.connected = true;
  const std::array<std::pair<const char *, u16>, 8> buttons{{{"a", 0x100},
                                                             {"b", 0x200},
                                                             {"x", 0x400},
                                                             {"y", 0x800},
                                                             {"start", 0x1000},
                                                             {"l", 0x40},
                                                             {"r", 0x20},
                                                             {"z", 0x10}}};
  for (auto [name, mask] : buttons)
    if (pressed(name))
      pad.buttons |= mask;
  pad.stick_x = static_cast<u8>(128 + 100 * (int(pressed("right")) - int(pressed("left"))));
  pad.stick_y = static_cast<u8>(128 + 100 * (int(pressed("up")) - int(pressed("down"))));
  pad.cstick_x = static_cast<u8>(128 + 100 * (int(pressed("c-right")) - int(pressed("c-left"))));
  pad.cstick_y = static_cast<u8>(128 + 100 * (int(pressed("c-up")) - int(pressed("c-down"))));
  pad.left_trigger = pressed("l") ? 255 : 0;
  pad.right_trigger = pressed("r") ? 255 : 0;
  pad.analog_a = pressed("a") ? 255 : 0;
  pad.analog_b = pressed("b") ? 255 : 0;
  const auto controller_pad = [](SDL_GameController *controller) {
    hw::PadState result;
    result.connected = controller && SDL_GameControllerGetAttached(controller);
    if (!result.connected)
      return result;
    const std::array<std::pair<SDL_GameControllerButton, u16>, 10> buttons{
        {{SDL_CONTROLLER_BUTTON_A, 0x100},
         {SDL_CONTROLLER_BUTTON_B, 0x200},
         {SDL_CONTROLLER_BUTTON_X, 0x400},
         {SDL_CONTROLLER_BUTTON_Y, 0x800},
         {SDL_CONTROLLER_BUTTON_START, 0x1000},
         {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x10},
         {SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1},
         {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 2},
         {SDL_CONTROLLER_BUTTON_DPAD_DOWN, 4},
         {SDL_CONTROLLER_BUTTON_DPAD_UP, 8}}};
    for (auto [button, mask] : buttons)
      if (SDL_GameControllerGetButton(controller, button))
        result.buttons |= mask;
    const auto stick = [&](SDL_GameControllerAxis axis, bool inverted) {
      const int value = SDL_GameControllerGetAxis(controller, axis);
      return static_cast<u8>(std::clamp(128 + (inverted ? -value : value) * 127 / 32768, 0, 255));
    };
    result.stick_x = stick(SDL_CONTROLLER_AXIS_LEFTX, false);
    result.stick_y = stick(SDL_CONTROLLER_AXIS_LEFTY, true);
    result.cstick_x = stick(SDL_CONTROLLER_AXIS_RIGHTX, false);
    result.cstick_y = stick(SDL_CONTROLLER_AXIS_RIGHTY, true);
    result.left_trigger = static_cast<u8>(
        std::max(0, int(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT))) *
        255 / 32767);
    result.right_trigger = static_cast<u8>(
        std::max(0, int(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT))) *
        255 / 32767);
    if (result.left_trigger >= 230 ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
      result.buttons |= 0x40;
    if (result.right_trigger >= 230)
      result.buttons |= 0x20;
    result.analog_a = result.buttons & 0x100 ? 255 : 0;
    result.analog_b = result.buttons & 0x200 ? 255 : 0;
    return result;
  };
  const auto physical = controller_pad(impl_->controllers[0]);
  if (physical.connected) {
    pad.buttons |= physical.buttons;
    if (pad.stick_x == 128)
      pad.stick_x = physical.stick_x;
    if (pad.stick_y == 128)
      pad.stick_y = physical.stick_y;
    if (pad.cstick_x == 128)
      pad.cstick_x = physical.cstick_x;
    if (pad.cstick_y == 128)
      pad.cstick_y = physical.cstick_y;
    pad.left_trigger = std::max(pad.left_trigger, physical.left_trigger);
    pad.right_trigger = std::max(pad.right_trigger, physical.right_trigger);
    pad.analog_a = std::max(pad.analog_a, physical.analog_a);
    pad.analog_b = std::max(pad.analog_b, physical.analog_b);
  }
  devices.set_pad(0, pad);
  for (unsigned port = 1; port < impl_->controllers.size(); ++port)
    devices.set_pad(port, controller_pad(impl_->controllers[port]));
  auto &remote = impl_->remote;
  remote = {};
  remote.connected = true;
  const std::array<std::pair<const char *, u16>, 11> remote_buttons{
      {{"remote-a", hw::RemoteA},
       {"remote-b", hw::RemoteB},
       {"remote-plus", hw::RemotePlus},
       {"remote-minus", hw::RemoteMinus},
       {"remote-home", hw::RemoteHome},
       {"remote-one", hw::RemoteOne},
       {"remote-two", hw::RemoteTwo},
       {"remote-up", hw::RemoteUp},
       {"remote-down", hw::RemoteDown},
       {"remote-left", hw::RemoteLeft},
       {"remote-right", hw::RemoteRight}}};
  for (auto [name, mask] : remote_buttons)
    if (pressed(name))
      remote.buttons |= mask;
  remote.nunchuk.attached = true;
  remote.nunchuk.stick_x =
      static_cast<u8>(128 + 100 * (int(pressed("nunchuk-right")) - int(pressed("nunchuk-left"))));
  remote.nunchuk.stick_y =
      static_cast<u8>(128 + 100 * (int(pressed("nunchuk-up")) - int(pressed("nunchuk-down"))));
  remote.nunchuk.c = pressed("nunchuk-c");
  remote.nunchuk.z = pressed("nunchuk-z");
  remote.acceleration[0] = static_cast<u16>(
      512 + 104 * (int(pressed("remote-tilt-right")) - int(pressed("remote-tilt-left"))));
  remote.acceleration[1] = static_cast<u16>(
      512 + 104 * (int(pressed("remote-tilt-forward")) - int(pressed("remote-tilt-back"))));
  const u16 shake = (SDL_GetTicks64() / 33) & 1 ? 768 : 256;
  if (pressed("remote-shake"))
    remote.acceleration[0] = shake;
  if (pressed("nunchuk-shake"))
    remote.nunchuk.acceleration[0] = shake;
  if (physical.connected) {
    const std::array<std::pair<u16, u16>, 9> mapped_buttons{{{0x100, hw::RemoteA},
                                                             {0x200, hw::RemoteB},
                                                             {0x400, hw::RemoteOne},
                                                             {0x800, hw::RemoteTwo},
                                                             {0x1000, hw::RemotePlus},
                                                             {1, hw::RemoteLeft},
                                                             {2, hw::RemoteRight},
                                                             {4, hw::RemoteDown},
                                                             {8, hw::RemoteUp}}};
    for (auto [mask, mapped] : mapped_buttons)
      if (physical.buttons & mask)
        remote.buttons |= mapped;
    if (SDL_GameControllerGetButton(impl_->controllers[0], SDL_CONTROLLER_BUTTON_BACK))
      remote.buttons |= hw::RemoteMinus;
    if (remote.nunchuk.stick_x == 128)
      remote.nunchuk.stick_x = physical.stick_x;
    if (remote.nunchuk.stick_y == 128)
      remote.nunchuk.stick_y = physical.stick_y;
    remote.nunchuk.c |= physical.left_trigger >= 230 || (physical.buttons & 0x40);
    remote.nunchuk.z |= physical.right_trigger >= 230;
    if (physical.buttons & 0x10)
      remote.acceleration[0] = shake;
  }
  int mouse_x = 0, mouse_y = 0;
  const auto mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
  if (mouse_buttons & SDL_BUTTON_LMASK)
    remote.buttons |= hw::RemoteA;
  if (mouse_buttons & SDL_BUTTON_RMASK)
    remote.buttons |= hw::RemoteB;
  float output_x = 0, output_y = 0;
  SDL_RenderWindowToLogical(impl_->renderer, mouse_x, mouse_y, &output_x, &output_y);
  const auto area = impl_->display_area();
  if (SDL_GetMouseFocus() == impl_->window && area.w > 1 && area.h > 1 && output_x >= area.x &&
      output_y >= area.y && output_x < area.x + area.w && output_y < area.y + area.h) {
    remote.infrared = pointer_camera((output_x - area.x) * 2.0 / area.w - 1,
                                     1 - (output_y - area.y) * 2.0 / area.h);
  }
#else
  (void)devices;
  (void)sample_input;
#endif
  return true;
}

const hw::RemoteState &Output::remote_state() const {
  return impl_->remote;
}

bool Output::rumble(unsigned port, bool enabled) {
#if CUPID_HAS_SDL2
  if (port >= impl_->controllers.size())
    return false;
  auto *controller = impl_->controllers[port];
  if (!controller || !SDL_GameControllerHasRumble(controller))
    return false;
  return SDL_GameControllerRumble(controller, enabled ? 32768 : 0, enabled ? 32768 : 0,
                                  enabled ? 1000 : 0) == 0;
#else
  (void)port;
  (void)enabled;
  return false;
#endif
}

void Output::present(const Memory &memory, const hw::Xfb &xfb) {
  impl_->last = decode_xfb(memory, xfb);
  impl_->hash = frame_hash(impl_->last.rgba);
  ++impl_->frames;
#if CUPID_HAS_SDL2
  if (!impl_->options.headless) {
    impl_->display_aspect = impl_->options.display_aspect == DisplayAspect::Pixels
                                ? static_cast<float>(xfb.width) / xfb.height
                                : xfb.aspect_ratio;
    if (impl_->options.display_aspect == DisplayAspect::Widescreen)
      impl_->display_aspect *= 4.0f / 3.0f;
    if (!std::isfinite(impl_->display_aspect) || impl_->display_aspect <= 0)
      throw std::runtime_error("Invalid VI display aspect ratio");
    int width = 0, height = 0;
    if (impl_->texture)
      SDL_QueryTexture(impl_->texture, nullptr, nullptr, &width, &height);
    if (!impl_->texture || width != int(xfb.width) || height != int(xfb.height)) {
      if (impl_->texture)
        SDL_DestroyTexture(impl_->texture);
      impl_->texture =
          SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                            static_cast<int>(xfb.width), static_cast<int>(xfb.height));
      if (!impl_->texture)
        throw std::runtime_error(std::string("Cannot create game texture: ") + SDL_GetError());
    }
    if (SDL_UpdateTexture(impl_->texture, nullptr, impl_->last.rgba.data(),
                          static_cast<int>(xfb.width * 4)) != 0)
      throw std::runtime_error(std::string("Cannot upload game frame: ") + SDL_GetError());
    if (SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(impl_->renderer) != 0)
      throw std::runtime_error(std::string("Cannot clear game-output surface: ") + SDL_GetError());
    const auto area = impl_->display_area();
    if (area.w > 0 && area.h > 0 &&
        SDL_RenderCopyF(impl_->renderer, impl_->texture, nullptr, &area) != 0)
      throw std::runtime_error(std::string("Cannot present game frame: ") + SDL_GetError());
    SDL_RenderPresent(impl_->renderer);
  }
#endif
}

void Output::audio(std::span<const s16> samples, u32 sample_rate) {
  if (samples.size() & 1)
    throw std::runtime_error("Audio DMA produced incomplete stereo samples");
  if (sample_rate != 32000 && sample_rate != 48000 && sample_rate != 32028 && sample_rate != 48042)
    throw std::runtime_error("Unsupported guest audio sample rate");
  impl_->samples += samples.size();
  for (s16 sample : samples) {
    impl_->nonzero_samples += sample != 0;
    impl_->peak = std::max(impl_->peak, static_cast<u32>(sample < 0 ? -s32(sample) : s32(sample)));
    const auto bits = static_cast<u16>(sample);
    impl_->pcm_hash = (impl_->pcm_hash ^ (bits >> 8)) * 1099511628211ULL;
    impl_->pcm_hash = (impl_->pcm_hash ^ (bits & 255)) * 1099511628211ULL;
  }
#if CUPID_HAS_SDL2
  if (!impl_->options.audio)
    return;
  const u32 divisor = sample_rate == 32000   ? 3375u
                      : sample_rate == 48000 ? 2250u
                      : sample_rate == 32028 ? 3372u
                                             : 2248u;
  const std::lock_guard lock(impl_->audio_mutex);
  impl_->check_audio_error_locked();
  impl_->dma->set_rate_divisor(divisor);
  // Device DMA values are already host endian, in the guest's right/left order.
  std::array<s16, 256> ordered{};
  while (!samples.empty()) {
    const auto count = std::min(samples.size(), ordered.size());
    for (std::size_t n = 0; n < count; n += 2) {
      ordered[n] = samples[n + 1];
      ordered[n + 1] = samples[n];
    }
    impl_->dma->push_stereo(std::span<const s16>(ordered).first(count));
    samples = samples.subspan(count);
  }
#else
  (void)sample_rate;
#endif
}

void Output::remote_speaker(unsigned port, const audio::RemoteSpeakerPacket &packet) {
  if (port >= 4 || !impl_->options.remote_speakers || !packet.count)
    return;
  if (packet.count > packet.mono.size() || !packet.rate_divisor)
    throw std::invalid_argument("Invalid Remote speaker packet");
#if CUPID_HAS_SDL2
  const std::lock_guard lock(impl_->audio_mutex);
  impl_->check_audio_error_locked();
  auto &speaker = *impl_->speakers[port];
  speaker.set_rate_divisor(packet.rate_divisor);
  speaker.set_volume(packet.left_volume, packet.right_volume);
  speaker.push_mono(std::span<const s16>(packet.mono).first(packet.count));
#endif
}

void Output::disc_audio_control(u32 divisor, u8 left, u8 right) {
  if (divisor != 2248 && divisor != 2250 && divisor != 3372 && divisor != 3375)
    throw std::invalid_argument("Unsupported disc audio rate divisor");
#if CUPID_HAS_SDL2
  if (impl_->options.audio) {
    const std::lock_guard lock(impl_->audio_mutex);
    impl_->check_audio_error_locked();
    impl_->disc->set_rate_divisor(divisor);
    impl_->disc->set_volume(left, right);
  }
#else
  (void)left;
  (void)right;
#endif
}

void Output::disc_audio(std::span<const s16> samples, u32 divisor, u8 left, u8 right) {
  if (samples.size() & 1)
    throw std::invalid_argument("Disc audio has an incomplete stereo frame");
  disc_audio_control(divisor, left, right);
  auto &stats = impl_->disc_stats;
  stats.samples += samples.size();
  for (s16 sample : samples) {
    stats.nonzero_samples += sample != 0;
    stats.peak = std::max(stats.peak, static_cast<u32>(sample < 0 ? -s32(sample) : s32(sample)));
    const auto bits = static_cast<u16>(sample);
    stats.hash = (stats.hash ^ (bits >> 8)) * 1099511628211ULL;
    stats.hash = (stats.hash ^ (bits & 255)) * 1099511628211ULL;
  }
#if CUPID_HAS_SDL2
  if (impl_->options.audio) {
    const std::lock_guard lock(impl_->audio_mutex);
    impl_->check_audio_error_locked();
    // The decoded pair follows the same right/left convention as audio DMA.
    std::array<s16, 336> ordered{};
    while (!samples.empty()) {
      const auto count = std::min(samples.size(), ordered.size());
      for (std::size_t n = 0; n < count; n += 2) {
        ordered[n] = samples[n + 1];
        ordered[n + 1] = samples[n];
      }
      impl_->disc->push_stereo(std::span<const s16>(ordered).first(count));
      samples = samples.subspan(count);
    }
  }
#endif
}

DiscAudioStats Output::disc_audio_stats() const {
  return impl_->disc_stats;
}

bool Output::remote_speaker_processing() const {
  return impl_->options.remote_speakers;
}

HostAudioStats Output::host_audio_stats() const {
#if CUPID_HAS_SDL2
  const std::lock_guard lock(impl_->audio_mutex);
  impl_->check_audio_error_locked();
  auto stats = impl_->host_stats;
  if (impl_->dma) {
    stats.dropped_granules = impl_->dma->dropped_granules();
    stats.dropped_granules += impl_->disc->dropped_granules();
    for (const auto &speaker : impl_->speakers)
      stats.dropped_granules += speaker->dropped_granules();
  }
  return stats;
#else
  return {};
#endif
}

u64 Output::presented_frames() const {
  return impl_->frames;
}

u64 Output::last_frame_hash() const {
  return impl_->hash;
}

u64 Output::audio_samples() const {
  return impl_->samples;
}

u64 Output::audio_nonzero_samples() const {
  return impl_->nonzero_samples;
}

u32 Output::audio_peak() const {
  return impl_->peak;
}

u64 Output::audio_hash() const {
  return impl_->pcm_hash;
}

void Output::save_frame() const {
  if (impl_->options.frame_dump.empty())
    return;
  if (!impl_->frames)
    throw std::runtime_error("No guest framebuffer was presented; cannot write frame dump");
  auto parent = impl_->options.frame_dump.parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  std::ofstream file(impl_->options.frame_dump, std::ios::binary);
  file << "P6\n" << impl_->last.width << ' ' << impl_->last.height << "\n255\n";
  for (std::size_t pixel = 0; pixel < impl_->last.rgba.size(); pixel += 4)
    file.write(reinterpret_cast<const char *>(impl_->last.rgba.data() + pixel), 3);
  if (!file)
    throw std::runtime_error("Cannot write frame dump");
}
} // namespace cupid::platform
