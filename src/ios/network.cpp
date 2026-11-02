// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/ios/network.hpp"
#include "cupid/core/memory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <string>

namespace cupid::ios {
namespace {
constexpr s32 invalid = -4, broken = -14, write_error = -19;
constexpr std::string_view config_path = "/shared2/wc24/nwc24msg.cfg";
constexpr std::string_view backup_path = "/shared2/wc24/nwc24msg.cbk";
constexpr std::string_view settings_path = "/title/00000001/00000002/data/setting.txt";

u32 word(std::span<const u8> data, std::size_t offset) {
  return (u32(data[offset]) << 24) | (u32(data[offset + 1]) << 16) | (u32(data[offset + 2]) << 8) |
         data[offset + 3];
}

void put(std::span<u8> data, std::size_t offset, u32 value) {
  for (unsigned i = 0; i < 4; ++i)
    data[offset + i] = u8(value >> (24 - i * 8));
}

u64 wide(std::span<const u8> data, std::size_t offset) {
  return (u64(word(data, offset)) << 32) | word(data, offset + 4);
}

void put_wide(std::span<u8> data, std::size_t offset, u64 value) {
  put(data, offset, u32(value >> 32));
  put(data, offset + 4, u32(value));
}

u32 checksum(std::span<const u8> data) {
  u32 sum = 0;
  for (std::size_t offset = 0; offset < 0x3fc; offset += 4)
    sum += word(data, offset);
  return sum;
}

std::string setting(std::span<const u8> bytes, std::string_view name) {
  if (bytes.size() != 256)
    return {};
  std::string text;
  u32 key = 0x73b5dbfa;
  for (const auto byte : bytes) {
    const char value = char(byte ^ u8(key));
    if (value != '\r')
      text.push_back(value);
    key = std::rotl(key, 1);
  }
  std::size_t begin = 0;
  while (begin < text.size()) {
    const auto end = text.find('\n', begin);
    const auto line = std::string_view(text).substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (line.starts_with(name) && line.size() > name.size() && line[name.size()] == '=')
      return std::string(line.substr(name.size() + 1));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return {};
}

u8 area_code(std::string_view area) {
  constexpr std::array<std::pair<std::string_view, u8>, 13> areas{{{"JPN", u8(0)},
                                                                   {"USA", u8(1)},
                                                                   {"EUR", u8(2)},
                                                                   {"AUS", u8(2)},
                                                                   {"BRA", u8(1)},
                                                                   {"TWN", u8(3)},
                                                                   {"ROC", u8(3)},
                                                                   {"KOR", u8(4)},
                                                                   {"HKG", u8(5)},
                                                                   {"ASI", u8(5)},
                                                                   {"LTN", u8(1)},
                                                                   {"SAF", u8(2)},
                                                                   {"CHN", u8(6)}}};
  for (const auto &[name, code] : areas)
    if (name == area)
      return code;
  return 7;
}

u8 model_code(std::string_view model) {
  if (model == "RVT" || model == "RVV")
    return 0;
  if (model == "RVL")
    return 1;
  if (model == "RVD")
    return 2;
  return 7;
}
} // namespace

UnsupportedNetwork::UnsupportedNetwork(NetworkDevice kind, u32 code)
    : std::runtime_error("Unsupported IOS network operation: " +
                         std::string(kind == NetworkDevice::Time ? "KD time " : "KD request ") +
                         std::to_string(code)),
      device(kind), operation(code) {
}

NetworkStorageError::NetworkStorageError(s32 code)
    : std::runtime_error("IOS network storage error " + std::to_string(code)), error(code) {
}

u64 make_wc24_id(u32 hardware_id, u16 generation, u8 hardware_model, u8 area) {
  if (generation > 31 || hardware_model > 7 || area > 7)
    throw std::invalid_argument("Invalid WC24 identifier fields");
  const u64 payload = (u64(area) << 50) | (u64(hardware_model) << 47) | (u64(hardware_id) << 15) |
                      (u64(generation) << 10);
  u64 remainder = payload;
  for (unsigned bit = 53; bit-- > 10;)
    if ((remainder & (u64(1) << bit)) != 0)
      remainder ^= u64(0x635) << (bit - 10);
  u64 value = (payload | u32(remainder)) ^ 0xb3b3b3b3b3b3;
  value = (value >> 10) | ((value & 1023) << 43);
  constexpr std::array<u8, 16> nibbles{4, 11, 7, 9, 15, 1, 13, 3, 12, 2, 6, 14, 8, 0, 10, 5};
  for (unsigned i = 0; i < 6; ++i) {
    const u8 byte = u8(value >> (i * 8));
    const u8 replacement = u8((nibbles[byte >> 4] << 4) | nibbles[byte & 15]);
    value = (value & ~(u64(255) << (i * 8))) | (u64(replacement) << (i * 8));
  }
  constexpr std::array<unsigned, 6> destinations{1, 5, 0, 4, 2, 3};
  const u64 substituted = value;
  for (unsigned i = 0; i < destinations.size(); ++i) {
    const unsigned shift = destinations[i] * 8;
    value = (value & ~(u64(255) << shift)) | (u64(u8(substituted >> (i * 8))) << shift);
  }
  value &= 0x1fffffffffffff;
  value = ((value << 1) | (value >> 52)) ^ 0x5e5e5e5e5e5e;
  return value & 0x1fffffffffffff;
}

struct Network::Impl {
  Network &owner;
  Memory &memory;
  u64 bias = 0;
  u32 rtc = 0;
  bool suspended = false, config_loaded = false;
  u32 error_count = 0;
  s32 config_error = 0;
  std::array<u8, 1024> scheduler{}, config{};

  Impl(Network &owner, Memory &memory) : owner(owner), memory(memory) {
    put(scheduler, 4, 0xffffffff);
  }

  bool buffer(u32 address, u32 size, u32 minimum) const {
    if (address == 0 || (address & 3) != 0 || size < minimum)
      return false;
    try {
      (void)memory.range(address, size);
      return true;
    } catch (const BusFault &) {
      return false;
    }
  }

  void log_error(u32 category, s32 code) {
    const u32 recorded = u32(code) - category;
    put(scheduler, (32 + (error_count % 32)) * 4, recorded);
    ++error_count;
    put(scheduler, 20, error_count);
    put(scheduler, 8, recorded);
  }

  bool save(std::span<const u8> bytes, bool backup) {
    if (!owner.write_file)
      return false;
    try {
      if (backup && !owner.write_file(backup_path, bytes))
        return false;
      return owner.write_file(config_path, bytes);
    } catch (const NetworkStorageError &) {
      return false;
    }
  }

  std::optional<std::vector<u8>> read(std::string_view path, s32 &error) {
    if (!owner.read_file) {
      error = -16;
      return std::nullopt;
    }
    try {
      return owner.read_file(path);
    } catch (const NetworkStorageError &failure) {
      error = failure.error;
      return std::nullopt;
    }
  }

  s32 load_config() {
    if (config_loaded)
      return config_error;
    config_loaded = true;
    const auto stored = read(config_path, config_error);
    if (config_error != 0)
      return config_error;
    if (stored) {
      if (stored->size() != config.size())
        return config_error = broken;
      std::copy(stored->begin(), stored->end(), config.begin());
      if (word(config, 0) != 0x57634366 || word(config, 0x3fc) != checksum(config) ||
          word(config, 0x10) > 31 || word(config, 0x14) > 2)
        return config_error = broken;
      if (word(config, 4) != 8)
        return config_error = -27;
      return 0;
    }
    put(config, 0, 0x57634366);
    put(config, 4, 8);
    constexpr std::string_view email = "@wii.com";
    std::copy(email.begin(), email.end(), config.begin() + 0x18);
    constexpr std::array<std::string_view, 5> urls{
        "https://amw.wc24.wii.com/cgi-bin/account.cgi", "http://rcw.wc24.wii.com/cgi-bin/check.cgi",
        "http://mtw.wc24.wii.com/cgi-bin/receive.cgi", "http://mtw.wc24.wii.com/cgi-bin/delete.cgi",
        "http://mtw.wc24.wii.com/cgi-bin/send.cgi"};
    for (unsigned i = 0; i < urls.size(); ++i)
      std::copy(urls[i].begin(), urls[i].end(), config.begin() + 0x9c + i * 128);
    put(config, 0x3fc, checksum(config));
    if (!save(config, false))
      return config_error = write_error;
    return 0;
  }

  s32 time(u32 operation, u32 input, u32 input_size, u32 output, u32 output_size) {
    if (operation < 0x14 || operation > 0x18)
      throw UnsupportedNetwork(NetworkDevice::Time, operation);
    const u32 required = operation == 0x14 || operation == 0x18 ? 12 : 4;
    if (!buffer(output, output_size, required))
      return invalid;
    if ((operation == 0x15 && !buffer(input, input_size, 12)) ||
        (operation == 0x17 && !buffer(input, input_size, 8)))
      return invalid;
    if ((operation == 0x15 && memory.read32(input + 8) != 0) ||
        (operation == 0x17 && memory.read32(input + 4) != 0))
      throw UnsupportedNetwork(NetworkDevice::Time, operation);
    if (operation == 0x14)
      memory.write64(output + 4, owner.adjusted_utc());
    else if (operation == 0x15)
      bias = u64(u32(owner.unix_time())) - memory.read64(input);
    else if (operation == 0x17)
      rtc = memory.read32(input);
    else if (operation == 0x18)
      memory.write64(output + 4, owner.adjusted_utc() - rtc);
    memory.write32(output, 0);
    return operation == 0x16 ? -9 : 0;
  }

  s32 generated_id(u32 output) {
    s32 result = load_config();
    if (result == 0) {
      const u32 stage = word(config, 0x14);
      if (stage == 1)
        result = -35;
      else if (stage == 2)
        result = -36;
      else {
        s32 storage_error = 0;
        const auto settings = read(settings_path, storage_error);
        const auto hardware_id = owner.device_id ? owner.device_id() : std::nullopt;
        const auto area = settings ? setting(*settings, "AREA") : std::string{};
        const auto model = settings ? setting(*settings, "MODEL") : std::string{};
        if (storage_error != 0) {
          result = storage_error;
          log_error(101200, result);
        } else if (!hardware_id || area.empty() || model.empty()) {
          result = -1;
          log_error(101200, -3);
        } else {
          const auto id = make_wc24_id(*hardware_id, u16(word(config, 0x10)), model_code(model),
                                       area_code(area));
          auto updated = config;
          put_wide(updated, 8, id);
          put(updated, 0x10, (word(config, 0x10) + 1) & 31);
          put(updated, 0x14, 1);
          put(updated, 0x3fc, checksum(updated));
          if (!save(updated, true))
            result = write_error;
          else {
            config = updated;
            result = id > 9999999999999999ull ? -1 : 0;
          }
        }
      }
    }
    memory.write32(output, u32(result));
    memory.write64(output + 4, wide(config, 8));
    memory.write32(output + 12, word(config, 0x14));
    return 0;
  }

  s32 request(u32 operation, u32 input, u32 input_size, u32 output, u32 output_size) {
    switch (operation) {
    case 1:
    case 2:
    case 3:
      if (operation != 2 && !buffer(output, output_size, 4))
        return invalid;
      suspended = operation != 3;
      if (operation != 2)
        memory.write32(output, 0);
      return 0;
    case 4:
      if (!buffer(output, output_size, 12))
        return invalid;
      memory.write32(output, 0);
      memory.write32(output + 4, 1);
      memory.write32(output + 8, 2);
      return 0;
    case 0x1e:
      if (!buffer(output, output_size, 16))
        return invalid;
      memory.copy_in(output, std::span<const u8>(scheduler).first(std::min(output_size, 256u)));
      return 0;
    case 0x0a:
      if (!buffer(output, output_size, 4))
        return invalid;
      if (owner.device_id && owner.device_id())
        throw UnsupportedNetwork(NetworkDevice::Request, operation);
      log_error(102200, broken);
      memory.write32(output, u32(broken));
      return 0;
    case 0x0e: {
      if (!buffer(input, input_size, 12) || !buffer(output, output_size, 4))
        return invalid;
      s32 storage_error = 0;
      const auto list = read("/shared2/wc24/nwc24dl.bin", storage_error);
      s32 result = broken;
      constexpr std::size_t list_size = 128 + 120 * 16 + 120 * 512;
      if (list && list->size() == list_size && word(*list, 0) == 0x5763446c &&
          word(*list, 4) == 1) {
        const u32 index = memory.read16(input + 6);
        if (index >= 120)
          result = -3;
        else if (word(*list, 128 + 120 * 16 + index * 512 + 12) == 0)
          result = -13;
        else
          throw UnsupportedNetwork(NetworkDevice::Request, operation);
      }
      log_error(107200, result);
      memory.write32(output, u32(result));
      return 0;
    }
    case 0x0f:
      if (!buffer(output, output_size, 16))
        return invalid;
      return generated_id(output);
    case 0x10: {
      if (!buffer(output, output_size, 8))
        return invalid;
      s32 result = load_config();
      if (result == 0) {
        if (word(config, 0x14) == 2)
          result = -36;
        else if (word(config, 0x14) != 1)
          result = -34;
        else
          throw UnsupportedNetwork(NetworkDevice::Request, operation);
      }
      log_error(101200, result);
      memory.write32(output, u32(result));
      memory.write32(output + 4, 0);
      return 0;
    }
    default:
      throw UnsupportedNetwork(NetworkDevice::Request, operation);
    }
  }
};

Network::Network(Memory &memory) : impl_(std::make_unique<Impl>(*this, memory)) {
  unix_time = [] {
    return u64(std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count());
  };
}

Network::~Network() = default;

s32 Network::ioctl(NetworkDevice device, u32 operation, u32 input, u32 input_size, u32 output,
                   u32 output_size) {
  return device == NetworkDevice::Time
             ? impl_->time(operation, input, input_size, output, output_size)
             : impl_->request(operation, input, input_size, output, output_size);
}

bool Network::scheduler_suspended() const {
  return impl_->suspended;
}

u64 Network::adjusted_utc() const {
  if (!unix_time)
    throw std::runtime_error("IOS network clock is not connected");
  return u64(u32(unix_time())) + impl_->bias;
}

void Network::reset_storage() {
  impl_->config_loaded = false;
  impl_->config_error = 0;
  impl_->config.fill(0);
}
} // namespace cupid::ios
