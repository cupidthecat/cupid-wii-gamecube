// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/handheld_task.hpp"
#include "cupid/dsp/handheld.hpp"
#include <stdexcept>
#include <utility>

namespace cupid::dsp {
HandheldTask::HandheldTask(Memory &memory, std::function<void(u32)> send,
                           std::function<bool()> pending, std::function<void()> interrupt)
    : memory_(memory), send_(std::move(send)), pending_(std::move(pending)),
      interrupt_(std::move(interrupt)) {
}

void HandheldTask::initialize() {
  send_(0xdcd10000);
}

void HandheldTask::update() {
  if (pending_() && interrupt_)
    interrupt_();
}

std::optional<HandheldTask::Transition> HandheldTask::handle_mail(u32 value) {
  switch (phase_) {
  case Phase::Request:
    if (value == 0xabba0000)
      phase_ = Phase::Address;
    break;
  case Phase::Address:
    process_joyboot_request(memory_, value & 0x0fffffff);
    send_(0xdcd10003);
    phase_ = Phase::NextTask;
    break;
  case Phase::NextTask:
    if (value == 0xcdd10002)
      return Transition{0, {}, false};
    if (value == 0xcdd10001) {
      phase_ = Phase::Upload;
      next_word_ = 0;
    }
    break;
  case Phase::Upload:
    descriptor_[next_word_++] = value;
    if (next_word_ != descriptor_.size())
      break;
    phase_ = Phase::NextTask;
    next_word_ = 0;
    if ((descriptor_[1] & 0xffff) || (descriptor_[8] & 0xffff))
      throw std::runtime_error("DSP task data-memory upload/download is not implemented");
    const BootDescriptor boot{descriptor_[3], u16(descriptor_[4]), u16(descriptor_[5]),
                              u16(descriptor_[8]), u16(descriptor_[6])};
    const auto code = memory_.range(Memory::boot_physical(boot.source), boot.length);
    return Transition{firmware_hash(code), boot, true};
  }
  return std::nullopt;
}
} // namespace cupid::dsp
