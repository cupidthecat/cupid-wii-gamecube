// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/dsp/engine.hpp"
#include "cupid/dsp/audio.hpp"
#include <bit>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cupid::dsp {
namespace {

std::string unsupported_message(u32 hash, const BootDescriptor &descriptor) {
  std::ostringstream message;
  message << "Unsupported DSP program hash=0x" << std::hex << std::setw(8) << std::setfill('0')
          << hash << " source=0x" << std::setw(8) << descriptor.source << " length=0x"
          << descriptor.length << " IMEM=0x" << descriptor.instruction_address << " DMEM-length=0x"
          << descriptor.data_length << " entry=0x" << descriptor.entry;
  return message.str();
}

} // namespace

void MailQueue::push(u32 value, bool request_interrupt) {
  if (request_interrupt) {
    if (queue_.empty()) {
      if (interrupt)
        interrupt();
    } else {
      queue_.front().interrupt_on_consumption = true;
    }
  }
  queue_.push_back({value, false});
}

u16 MailQueue::read_high() {
  if (!halted_ && !queue_.empty())
    last_ = queue_.front().value;
  return static_cast<u16>(last_ >> 16);
}

u16 MailQueue::read_low() {
  if (!halted_ && !queue_.empty()) {
    last_ = queue_.front().value;
    const bool notify = queue_.front().interrupt_on_consumption;
    queue_.pop_front();
    if (notify && interrupt)
      interrupt();
  }
  last_ &= 0x7fffffff;
  return static_cast<u16>(last_);
}

void MailQueue::halt(bool value) {
  halted_ = value;
}

void MailQueue::clear() {
  queue_.clear();
}

bool MailQueue::pending() const {
  return !queue_.empty();
}

u32 firmware_hash(std::span<const u8> bytes) {
  u32 value = 0;
  for (const u8 byte : bytes)
    value = std::rotl(value ^ byte, 3);
  return value;
}

UnsupportedMicrocode::UnsupportedMicrocode(u32 value, const BootDescriptor &info)
    : std::runtime_error(unsupported_message(value, info)), hash(value), descriptor(info) {
}

Engine::Engine(Memory &memory, Console console)
    : memory_(memory), console_(console),
      update_interval_(console == Console::Wii ? 729000 : 486000) {
  mails_.interrupt = [this] { request_interrupt(); };
  select_program(0);
  mails_.halt(true);
}

Engine::~Engine() = default;

void Engine::request_interrupt() {
  if (interrupt)
    interrupt();
}

void Engine::select_program(u32 hash) {
  Program next;
  switch (hash) {
  case 0:
    next = Program::Rom;
    break;
  case 1:
    next = Program::Initialization;
    break;
  case 0x65d6cc6f:
    next = Program::Card;
    break;
  default:
    if (!AudioProgram::supports(hash))
      throw UnsupportedMicrocode(hash, boot_);
    next = Program::Audio;
    break;
  }
  mails_.clear();
  program_ = next;
  hash_ = hash;
  parameter_ = 0;
  boot_ = {};
  audio_.reset();
  if (next == Program::Audio) {
    audio_ = std::make_unique<AudioProgram>(
        memory_, console_, hash, [this](u32 value, bool irq) { mails_.push(value, irq); },
        [this](u32 address, std::span<u8> bytes) {
          if (!aram_read)
            throw std::runtime_error("DSP audio requires connected ARAM storage");
          aram_read(address, bytes);
        },
        [this] { request_interrupt(); });
    audio_->initialize();
    return;
  }
  mails_.push(next == Program::Rom              ? 0x8071feed
              : next == Program::Initialization ? 0x80544348
                                                : 0xdcd10000);
}

void Engine::swap_program(u32 hash) {
  if (suspended_audio_ && suspended_audio_->hash() == hash) {
    mails_.clear();
    audio_ = std::move(suspended_audio_);
    hash_ = hash;
    program_ = Program::Audio;
    parameter_ = 0;
    boot_ = {};
    return;
  }
  if (hash != 0 && hash != 1 && hash != 0x65d6cc6f && !AudioProgram::supports(hash))
    throw UnsupportedMicrocode(hash, boot_);
  if (audio_ && !suspended_audio_)
    suspended_audio_ = std::move(audio_);
  select_program(hash);
}

void Engine::handle_mail(u32 value) {
  if (program_ == Program::Audio) {
    if (const auto action = audio_->handle_mail(value)) {
      boot_ = action->boot;
      if (action->preserve)
        swap_program(action->hash);
      else
        select_program(action->hash);
    }
    return;
  }
  if (program_ == Program::Initialization)
    return;
  if (program_ == Program::Card) {
    mails_.push(0xdcd10003);
    select_program(0);
    return;
  }
  if (!parameter_) {
    if ((value & 0xffff0000) == 0x80f30000)
      parameter_ = value;
    else
      mails_.push(0xfeee0000 | (value & 0xffff));
    return;
  }
  const u32 command = parameter_;
  parameter_ = 0;
  switch (command) {
  case 0x80f3a001:
    boot_.source = value;
    break;
  case 0x80f3a002:
    boot_.length = static_cast<u16>(value);
    break;
  case 0x80f3b002:
    boot_.data_length = static_cast<u16>(value);
    break;
  case 0x80f3c002:
    boot_.instruction_address = static_cast<u16>(value);
    break;
  case 0x80f3d001: {
    boot_.entry = static_cast<u16>(value);
    const auto code = memory_.range(Memory::boot_physical(boot_.source), boot_.length);
    select_program(firmware_hash(code));
    break;
  }
  default:
    break;
  }
}

u16 Engine::read16(u32 offset) {
  switch (offset) {
  case 0:
    return static_cast<u16>(cpu_mail_ >> 16);
  case 2:
    return static_cast<u16>(cpu_mail_);
  case 4:
    return mails_.read_high();
  case 6:
    return mails_.read_low();
  case 0x0a:
    if ((control_ & 0x400) && cycles_ / 12 >= init_clear_time_)
      control_ &= static_cast<u16>(~0x400);
    return control_;
  default:
    throw std::runtime_error("Invalid DSP mailbox/control register read");
  }
}

void Engine::write16(u32 offset, u16 value) {
  switch (offset) {
  case 0:
    cpu_mail_ = (cpu_mail_ & 0xffff) | (u32(value) << 16);
    return;
  case 2:
    cpu_mail_ = (cpu_mail_ & 0xffff0000) | value;
    handle_mail(cpu_mail_);
    cpu_mail_ &= 0x7fffffff;
    return;
  case 0x0a: {
    u16 next = value & 0xc07;
    mails_.halt((next & 4) != 0);
    if (next & 1) {
      select_program(0);
      next &= static_cast<u16>(~1);
    }
    if ((control_ & 0x800) && !(next & 0x800)) {
      select_program(1);
      next |= 0x400;
      init_clear_time_ = cycles_ / 12 + 130;
    }
    control_ = next;
    return;
  }
  default:
    throw std::runtime_error("Invalid or read-only DSP mailbox/control register write");
  }
}

void Engine::advance(u64 cpu_cycles) {
  if (cpu_cycles > std::numeric_limits<u64>::max() - cycles_)
    throw std::overflow_error("DSP cycle counter overflow");
  cycles_ += cpu_cycles;
  if (cpu_cycles < update_interval_ - update_cycles_) {
    update_cycles_ += cpu_cycles;
    return;
  }
  update_cycles_ = (update_cycles_ + cpu_cycles % update_interval_) % update_interval_;
  if (program_ == Program::Card && mails_.pending())
    request_interrupt();
  if (audio_)
    audio_->update();
}

u32 Engine::program_hash() const {
  return hash_;
}

const BootDescriptor &Engine::boot_descriptor() const {
  return boot_;
}

} // namespace cupid::dsp
