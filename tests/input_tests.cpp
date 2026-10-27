// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/platform/output.hpp"
#include "test_support.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <memory>

namespace {
struct Vibration {
  unsigned calls = 0;
  Uint16 low = 0, high = 0;
};

int SDLCALL vibrate(void *userdata, Uint16 low, Uint16 high) {
  auto &state = *static_cast<Vibration *>(userdata);
  ++state.calls;
  state.low = low;
  state.high = high;
  return 0;
}

class Controller {
public:
  Controller() {
    SDL_VirtualJoystickDesc description{};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    description.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    description.naxes = 6;
    description.nbuttons = 15;
    description.axis_mask = (1u << 6) - 1;
    description.button_mask = (1u << 15) - 1;
    description.name = "Synthetic input fixture";
    description.userdata = &vibration;
    description.Rumble = vibrate;
    const int index = SDL_JoystickAttachVirtualEx(&description);
    CHECK(index >= 0);
    CHECK(SDL_IsGameController(index));
    joystick = SDL_JoystickOpen(index);
    CHECK(joystick != nullptr);
    id = SDL_JoystickInstanceID(joystick);
  }

  ~Controller() {
    if (joystick)
      SDL_JoystickClose(joystick);
    const int index = device_index();
    if (index >= 0)
      SDL_JoystickDetachVirtual(index);
  }

  void axis(SDL_GameControllerAxis axis, Sint16 value) {
    CHECK(SDL_JoystickSetVirtualAxis(joystick, axis, value) == 0);
  }

  void button(SDL_GameControllerButton button, bool pressed) {
    CHECK(SDL_JoystickSetVirtualButton(joystick, button, pressed ? 1 : 0) == 0);
  }

  void detach() {
    CHECK(SDL_JoystickDetachVirtual(device_index()) == 0);
  }

  Vibration vibration;

private:
  int device_index() const {
    for (int index = 0; index < SDL_NumJoysticks(); ++index)
      if (SDL_JoystickGetDeviceInstanceID(index) == id)
        return index;
    return -1;
  }

  SDL_Joystick *joystick = nullptr;
  SDL_JoystickID id = -1;
};

std::array<cupid::u32, 2> read_pad(cupid::hw::Devices &devices, unsigned port) {
  devices.poll_controllers();
  return {devices.read(0x0c006404 + port * 12, 4), devices.read(0x0c006408 + port * 12, 4)};
}
} // namespace

int main() {
  return run_tests([] {
    using namespace cupid;
    // SDL updates controllers even when the synthetic video driver has no focus.
    CHECK(SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1") == SDL_TRUE);
    platform::Output output({false, false, {}, {}});
    hw::Devices devices(Console::GameCube);
    devices.rumble = [&](unsigned port, bool enabled) { CHECK(output.rumble(port, enabled)); };
    CHECK(output.poll(devices));
    CHECK(read_pad(devices, 0)[0] == 0x00808080);
    CHECK((read_pad(devices, 1)[0] & 0xc0000000) == 0xc0000000);
    CHECK(!output.rumble(1, true));

    std::array<std::unique_ptr<Controller>, 4> controllers;
    for (auto &controller : controllers)
      controller = std::make_unique<Controller>();
    CHECK(output.poll(devices));
    for (unsigned port = 0; port < 4; ++port) {
      // Select full-byte C-stick and analog-trigger report mode through SI.
      devices.write(0x0c006400 + port * 12, 0x00400300, 4);
    }
    devices.write(0x0c006438, 0x80000000, 4);
    for (unsigned port = 0; port < 4; ++port) {
      const auto actual = read_pad(devices, port);
      const u32 latched_error = port ? 0x40000000 : 0;
      CHECK(actual == (std::array<u32, 2>{latched_error | 0x00808080, 0x80800000}));
      // SI preserves ERRLATCH after a missing-controller poll. The guest clears INHI.
      devices.write(0x0c006404 + port * 12, 0, 4);
      CHECK(read_pad(devices, port) == (std::array<u32, 2>{0x00808080, 0x80800000}));
    }

    auto &first = *controllers[0];
    first.button(SDL_CONTROLLER_BUTTON_A, true);
    first.button(SDL_CONTROLLER_BUTTON_START, true);
    first.button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
    first.axis(SDL_CONTROLLER_AXIS_LEFTX, -32768);
    first.axis(SDL_CONTROLLER_AXIS_LEFTY, -32768);
    first.axis(SDL_CONTROLLER_AXIS_RIGHTX, 16384);
    first.axis(SDL_CONTROLLER_AXIS_RIGHTY, 16384);
    first.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
    first.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
    controllers[1]->button(SDL_CONTROLLER_BUTTON_B, true);
    controllers[2]->button(SDL_CONTROLLER_BUTTON_X, true);
    controllers[3]->button(SDL_CONTROLLER_BUTTON_Y, true);
    CHECK(output.poll(devices));
    CHECK(read_pad(devices, 0) == (std::array<u32, 2>{0x11c101ff, 0xbf41ff7f}));
    CHECK((read_pad(devices, 1)[0] & 0x0f000000) == 0x02000000);
    CHECK((read_pad(devices, 2)[0] & 0x0f000000) == 0x04000000);
    CHECK((read_pad(devices, 3)[0] & 0x0f000000) == 0x08000000);
    const auto &remote = output.remote_state();
    CHECK(remote.connected && remote.nunchuk.attached);
    CHECK(remote.buttons == (hw::RemoteA | hw::RemotePlus | hw::RemoteLeft));
    CHECK(remote.nunchuk.stick_x == 1 && remote.nunchuk.stick_y == 255);
    CHECK(remote.nunchuk.c && !remote.nunchuk.z);

    // A guest SI motor command reaches the SDL controller driver.
    devices.write(0x0c006400, 0x00400301, 4);
    devices.write(0x0c006438, 0x80000000, 4);
    CHECK(first.vibration.calls >= 1);
    CHECK(first.vibration.low == 32768 && first.vibration.high == 32768);
    devices.write(0x0c006400, 0x00400300, 4);
    devices.write(0x0c006438, 0x80000000, 4);
    CHECK(first.vibration.low == 0 && first.vibration.high == 0);

    first.button(SDL_CONTROLLER_BUTTON_A, false);
    first.button(SDL_CONTROLLER_BUTTON_START, false);
    first.button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
    first.axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
    first.axis(SDL_CONTROLLER_AXIS_LEFTY, 0);
    first.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    first.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    CHECK(output.poll(devices));
    CHECK(read_pad(devices, 0)[0] == 0x00808080);
    CHECK(output.remote_state().buttons == 0);
    CHECK(!output.remote_state().nunchuk.c && !output.remote_state().nunchuk.z);

    controllers[2]->detach();
    CHECK(output.poll(devices));
    CHECK((read_pad(devices, 2)[0] & 0xc0000000) == 0xc0000000);
    CHECK(!output.rumble(2, true));
    auto replacement = std::make_unique<Controller>();
    replacement->button(SDL_CONTROLLER_BUTTON_B, true);
    CHECK(output.poll(devices));
    CHECK((read_pad(devices, 2)[0] & 0xc0000000) == 0x40000000);
    devices.write(0x0c00641c, 0, 4);
    CHECK((read_pad(devices, 2)[0] & 0x0f000000) == 0x02000000);
    CHECK((read_pad(devices, 3)[0] & 0x0f000000) == 0x08000000);
  });
}
