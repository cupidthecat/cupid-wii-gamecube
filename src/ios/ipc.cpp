// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/ios/ipc.hpp"
#include "cupid/core/memory.hpp"
#include "cupid/hw/devices.hpp"
#include "cupid/hw/remote.hpp"
#include "cupid/hw/sdp.hpp"
#include "cupid/ios/network.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cupid::ios {
namespace {
constexpr u32 irq_ipc = 0x40000000, irq_pi = 0x4000;
constexpr u64 tb_ratio = 12, fs_overhead = 2700 * tb_ratio;
constexpr s32 invalid = -4, not_found = -6, fs_invalid = -101, fs_access = -102;
constexpr s32 fs_exists = -105, fs_missing = -106, fs_full = -109, fs_busy = -111;
constexpr s32 es_invalid = -1017;
constexpr unsigned remote_ports = 4, bluetooth_peers = 5;

u16 big16(std::span<const u8> bytes, std::size_t offset) {
  return u16((u16(bytes[offset]) << 8) | bytes[offset + 1]);
}

u32 big32(std::span<const u8> bytes, std::size_t offset) {
  u32 result = 0;
  for (unsigned i = 0; i < 4; ++i)
    result = (result << 8) | bytes[offset + i];
  return result;
}

u64 big64(std::span<const u8> bytes, std::size_t offset) {
  return (u64(big32(bytes, offset)) << 32) | big32(bytes, offset + 4);
}

bool valid_tmd(std::span<const u8> data) {
  return data.size() >= 0x1e4 && data.size() <= 0x49e4 && big32(data, 0) == 0x10001 &&
         data.size() >= 0x1e4 + std::size_t(big16(data, 0x1de)) * 36;
}

bool valid_ticket(std::span<const u8> data) {
  return data.size() == 0x2a4 && big32(data, 0) == 0x10001 && data[0x1bc] == 0;
}

std::vector<u8> tmd_view(std::span<const u8> data) {
  std::vector<u8> view(data.begin() + 0x180, data.begin() + 0x1d8);
  view.insert(view.end(), data.begin() + 0x1dc, data.begin() + 0x1e0);
  for (unsigned i = 0; i < big16(data, 0x1de); ++i) {
    const auto entry = data.begin() + 0x1e4 + i * 36;
    view.insert(view.end(), entry, entry + 16);
  }
  return view;
}

std::vector<u8> ticket_view(std::span<const u8> data) {
  std::vector<u8> view(0xd8);
  view[0] = data[0x1bc];
  std::copy(data.begin() + 0x1d0, data.end(), view.begin() + 4);
  return view;
}

std::string hexadecimal(u32 value) {
  std::array<char, 8> buffer{};
  const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
  return std::string(8 - std::size_t(converted.ptr - buffer.data()), '0') +
         std::string(buffer.data(), converted.ptr);
}

std::string title_directory(u64 title) {
  return "/title/" + hexadecimal(u32(title >> 32)) + "/" + hexadecimal(u32(title));
}

void append_number(std::vector<u8> &bytes, u64 value, unsigned width) {
  for (unsigned i = width; i != 0; --i)
    bytes.push_back(u8(value >> ((i - 1) * 8)));
}

u16 little16(std::span<const u8> bytes, std::size_t offset) {
  return u16(u16(bytes[offset]) | (u16(bytes[offset + 1]) << 8));
}

void append_little(std::vector<u8> &bytes, u16 value) {
  bytes.push_back(u8(value));
  bytes.push_back(u8(value >> 8));
}

void replace_storage(const std::filesystem::path &path, std::span<const u8> bytes) {
  auto temporary = path;
  temporary += ".pending";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    output.flush();
    if (!output)
      throw std::runtime_error("Unable to write Wii filesystem metadata");
  }
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::system_error(int(GetLastError()), std::system_category(),
                            "Unable to replace Wii filesystem metadata");
#else
  std::filesystem::rename(temporary, path);
#endif
}

struct Reply {
  s32 result;
  u64 delay = 4000 * tb_ratio;
};

[[noreturn]] void unsupported(u32 address, const std::string &operation) {
  throw BusFault(address, true, "Unsupported IOS operation: " + operation, true);
}
} // namespace

struct Ipc::Impl {
  enum class Kind { Request, Reply, Interrupt, Disc, Bluetooth, Connection };

  struct Event {
    u64 due, sequence;
    Kind kind;
    u32 address = 0, buffer = 0;
    s32 result = 0;
    std::vector<u8> data;
  };

  struct Later {
    bool operator()(const Event &left, const Event &right) const {
      return left.due > right.due || (left.due == right.due && left.sequence > right.sequence);
    }
  };
  enum class Device { File, Filesystem, Disc, ES, StmEvent, StmImmediate, KD, KDTime, Bluetooth };

  struct Handle {
    Device device;
    std::filesystem::path path;
    u32 mode = 0, offset = 0;
    bool flush = false;
    u32 uid = 0;
    u16 gid = 0;
  };

  struct Metadata {
    u32 uid = 0;
    u16 gid = 0;
    std::array<u8, 3> modes{3, 3, 3};
    u8 attribute = 0;
    u64 order = 0;
  };

  Ipc &owner;
  Memory &memory;
  hw::Devices &devices;
  Console console;
  u64 now = 0, serial = 0, last_reply = 0;
  u32 ppc_message = 0, arm_message = 0, flags = 0, mask = irq_ipc, control = 0;
  u32 gpio_out = 0, gpio_dir = 0xffdf3f, resets = 0xffffffff;
  u32 version = 30, disc_error = 0, last_length = 0;
  bool partition_open = false, disc_busy = false;
  std::filesystem::path root;
  std::array<std::optional<Handle>, 24> handles;
  std::priority_queue<Event, std::vector<Event>, Later> events;
  std::deque<u32> requests, replies, disc_requests;
  std::optional<unsigned> cache_fd;
  u32 cache_cluster = 0;
  bool cache_dirty = false;
  std::vector<u8> title_ticket, title_tmd;
  u64 title_id = 0, selected_partition = 0;
  u32 ppc_uid = 0;
  u16 ppc_gid = 0;
  u64 next_node = 1;
  std::map<std::string, Metadata> nodes;
  std::map<u64, u32> title_uids;
  std::function<void()> shutdown_handler;

  struct StmHook {
    u32 request, output;
    unsigned fd;
  };

  std::optional<StmHook> stm_hook;
  // The fifth registered peer is the inactive board profile, not a host input port.
  std::array<hw::Remote, bluetooth_peers> remotes;
  std::function<void(unsigned, bool)> remote_rumble;
  Network network;

  struct UsbEndpoint {
    u32 request, buffer, size;
  };

  std::optional<UsbEndpoint> hci_endpoint;
  std::deque<std::vector<u8>> hci_events;
  std::map<u16, std::vector<u8>> hci_settings;
  u8 scan_enable = 0;
  std::optional<UsbEndpoint> acl_endpoint;
  std::deque<std::vector<u8>> acl_packets;

  struct Channel {
    u16 psm = 0, peer = 0, mtu = 0;
    bool configured = false, configuring = false;
  };

  std::array<std::map<u16, Channel>, bluetooth_peers> channels;
  std::array<bool, bluetooth_peers> request_pending{}, initiating_channels{};
  std::array<unsigned, bluetooth_peers> connection_delay{};
  std::array<u16, bluetooth_peers> received_packets{};
  unsigned bluetooth_opens = 0;
  u32 bluetooth_generation = 0;

  Impl(Ipc &owner, Memory &memory, hw::Devices &devices, Console console)
      : owner(owner), memory(memory), devices(devices), console(console), network(memory) {
    network.unix_time = [&] { return u64(this->devices.rtc_seconds()) + 946684800; };
    network.read_file = [&](std::string_view name) -> std::optional<std::vector<u8>> {
      const std::string path(name);
      const auto target = host_path(path);
      if (!target)
        throw NetworkStorageError(-16);
      std::error_code error;
      const bool exists = std::filesystem::exists(*target, error);
      if (error)
        throw NetworkStorageError(-16);
      if (!exists)
        return std::nullopt;
      if (!permission(path, 12, 12, 1))
        throw NetworkStorageError(-16);
      const auto size = std::filesystem::file_size(*target, error);
      if (error)
        throw NetworkStorageError(-16);
      if (size > 16u * 1024 * 1024)
        throw NetworkStorageError(-18);
      std::ifstream stream(*target, std::ios::binary);
      if (!stream)
        throw NetworkStorageError(-16);
      std::vector<u8> bytes(static_cast<std::size_t>(size));
      stream.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
      if (!stream)
        throw NetworkStorageError(-18);
      return bytes;
    };
    network.write_file = [&](std::string_view name, std::span<const u8> bytes) {
      const std::string path(name);
      const auto target = host_path(path);
      if (!target || bytes.size() > 16u * 1024 * 1024)
        return false;
      const auto parent = path.substr(0, path.find_last_of('/'));
      if (!permission(parent, 12, 12, 2))
        return false;
      std::error_code error;
      std::filesystem::create_directories(target->parent_path(), error);
      if (error || !host_path(path))
        return false;
      const bool exists = std::filesystem::exists(*target, error);
      if (error || (exists && !permission(path, 12, 12, 2)))
        return false;
      try {
        replace_storage(*target, bytes);
      } catch (const std::runtime_error &) {
        throw NetworkStorageError(-19);
      }
      if (!exists)
        nodes[path] = Metadata{12, 12, {3, 3, 0}, 0, next_node++};
      save_metadata();
      return true;
    };
  }

  void schedule(Kind kind, u64 delay, u32 address = 0) {
    events.push(Event{now + delay, serial++, kind, address, 0, 0, {}});
  }

  bool ready() const {
    return (control & 6) == 0 && (flags & irq_ipc) == 0;
  }

  void interrupts() {
    if (((control & 4) && (control & 16)) || ((control & 2) && (control & 32)))
      flags |= irq_ipc;
    devices.set_interrupt(irq_pi, (flags & mask) != 0);
  }

  std::string string(u32 address, u32 maximum) {
    std::string result;
    for (u32 i = 0; i < maximum; ++i) {
      const u8 byte = memory.read8(address + i);
      if (byte == 0)
        return result;
      result.push_back(char(byte));
    }
    throw std::invalid_argument("IOS string is not terminated");
  }

  std::optional<std::filesystem::path> host_path(const std::string &path) const {
    if (path == "/" && !root.empty())
      return root;
    if (root.empty() || path.empty() || path.front() != '/' || path.size() >= 64 ||
        path.back() == '/' || path.find_first_of("\\:") != std::string::npos)
      return std::nullopt;
    std::filesystem::path relative;
    std::size_t begin = 1;
    while (begin < path.size()) {
      const auto end = path.find('/', begin);
      const auto component = path.substr(begin, end == std::string::npos ? end : end - begin);
      if (component.empty() || component == "." || component == ".." || component.size() > 12 ||
          component.back() == '.' || component.back() == ' ')
        return std::nullopt;
      for (unsigned char byte : component)
        if (byte < 32 || byte == '*' || byte == '?' || byte == '<' || byte == '>' || byte == '|' ||
            byte == '"')
          return std::nullopt;
      auto basename = component.substr(0, component.find('.'));
      for (auto &byte : basename)
        if (byte >= 'a' && byte <= 'z')
          byte = char(byte - 'a' + 'A');
      if (basename == "CON" || basename == "PRN" || basename == "AUX" || basename == "NUL" ||
          (basename.size() == 4 && (basename.starts_with("COM") || basename.starts_with("LPT")) &&
           basename[3] >= '1' && basename[3] <= '9'))
        return std::nullopt;
      relative /= component;
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    auto target = root / relative;
    std::error_code error;
    for (auto cursor = root; const auto &component : relative) {
      cursor /= component;
      auto status = std::filesystem::symlink_status(cursor, error);
      if (!error && std::filesystem::is_symlink(status))
        return std::nullopt;
      error.clear();
    }
    const auto resolved = std::filesystem::weakly_canonical(target, error);
    if (error || std::mismatch(root.begin(), root.end(), resolved.begin(), resolved.end()).first !=
                     root.end())
      return std::nullopt;
    return target;
  }

  static std::string parent_path(const std::string &path) {
    const auto end = path.rfind('/');
    return end == 0 ? "/" : path.substr(0, end);
  }

  Metadata &metadata(const std::string &path) {
    auto [entry, inserted] = nodes.try_emplace(path);
    if (inserted)
      entry->second.order = next_node++;
    return entry->second;
  }

  bool permission(const std::string &path, u32 uid, u16 gid, u8 mode) {
    if (uid == 0)
      return true;
    const auto &entry = metadata(path);
    const u8 allowed = entry.modes[entry.uid == uid ? 0 : (entry.gid == gid ? 1 : 2)];
    return (allowed & mode) == mode;
  }

  void save_metadata() {
    if (root.empty())
      return;
    std::ostringstream stream;
    stream << "CFS1\n";
    for (const auto &[path, entry] : nodes)
      stream << std::quoted(path) << ' ' << entry.uid << ' ' << entry.gid << ' '
             << unsigned(entry.modes[0]) << ' ' << unsigned(entry.modes[1]) << ' '
             << unsigned(entry.modes[2]) << ' ' << unsigned(entry.attribute) << ' ' << entry.order
             << '\n';
    const auto bytes = stream.str();
    replace_storage(root.parent_path() / "wii-fs-metadata.txt",
                    {reinterpret_cast<const u8 *>(bytes.data()), bytes.size()});
  }

  void load_metadata() {
    nodes.clear();
    next_node = 1;
    const auto file = root.parent_path() / "wii-fs-metadata.txt";
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
      if (std::filesystem::file_size(file) > 1024 * 1024)
        throw std::runtime_error("Wii filesystem metadata exceeds supported size");
      std::ifstream stream(file);
      std::string line;
      if (!std::getline(stream, line) || line != "CFS1")
        throw std::runtime_error("Invalid Wii filesystem metadata version");
      while (std::getline(stream, line)) {
        std::istringstream record(line);
        std::string path;
        Metadata entry;
        unsigned gid, owner_mode, group_mode, other_mode, attribute;
        if (!(record >> std::quoted(path) >> entry.uid >> gid >> owner_mode >> group_mode >>
              other_mode >> attribute >> entry.order) ||
            !host_path(path) || gid > 0xffff || owner_mode > 3 || group_mode > 3 ||
            other_mode > 3 || attribute > 255 || entry.order == std::numeric_limits<u64>::max())
          throw std::runtime_error("Invalid Wii filesystem metadata record");
        record >> std::ws;
        if (!record.eof() || nodes.contains(path))
          throw std::runtime_error("Invalid Wii filesystem metadata record");
        entry.gid = u16(gid);
        entry.modes = {u8(owner_mode), u8(group_mode), u8(other_mode)};
        entry.attribute = u8(attribute);
        nodes.emplace(path, entry);
        next_node = std::max(next_node, entry.order + 1);
      }
      if (!stream.eof())
        throw std::runtime_error("Unable to read Wii filesystem metadata");
    }
    metadata("/").modes = {0, 1, 1};

    struct Directory {
      const char *path;
      std::array<u8, 3> modes;
      u32 uid = 0;
      u16 gid = 0;
    };

    const std::array<Directory, 9> directories{{{"/sys", {3, 3, 0}},
                                                {"/ticket", {3, 3, 0}},
                                                {"/title", {3, 3, 1}},
                                                {"/shared1", {3, 3, 0}},
                                                {"/shared2", {3, 3, 3}},
                                                {"/tmp", {3, 3, 3}},
                                                {"/import", {3, 3, 0}},
                                                {"/meta", {3, 3, 3}, 0x1000, 1},
                                                {"/wfs", {3, 0, 0}, 19, 19}}};
    for (const auto &directory : directories) {
      const auto target = host_path(directory.path);
      if (!target)
        throw std::runtime_error("Unsafe Wii system directory");
      std::filesystem::create_directories(*target);
      auto &entry = metadata(directory.path);
      entry.uid = directory.uid;
      entry.gid = directory.gid;
      entry.modes = directory.modes;
      entry.attribute = 0;
    }
    save_metadata();
  }

  void load_uids() {
    title_uids.clear();
    const auto bytes = stored_blob("/sys/uid.sys", 1024 * 1024);
    const auto target = host_path("/sys/uid.sys");
    if (!target)
      throw std::runtime_error("Unsafe Wii UID mapping path");
    if (std::filesystem::exists(*target) && bytes.empty() &&
        std::filesystem::file_size(*target) != 0)
      throw std::runtime_error("Unable to read Wii UID mapping");
    if (bytes.size() % 12 != 0)
      throw std::runtime_error("Invalid Wii UID mapping");
    for (std::size_t offset = 0; offset < bytes.size(); offset += 12) {
      const auto title = big64(bytes, offset);
      const auto uid = big32(bytes, offset + 8);
      if (uid < 0x1000 || uid == std::numeric_limits<u32>::max() || title_uids.contains(title) ||
          std::any_of(title_uids.begin(), title_uids.end(),
                      [&](const auto &entry) { return entry.second == uid; }))
        throw std::runtime_error("Invalid Wii UID mapping");
      title_uids.emplace(title, uid);
    }
    if (title_uids.empty())
      title_uids.emplace(0x0000000100000002ull, 0x1000);
  }

  u32 uid_for_title(u64 title) {
    if (title_uids.empty())
      title_uids.emplace(0x0000000100000002ull, 0x1000);
    if (const auto found = title_uids.find(title); found != title_uids.end())
      return found->second;
    u32 next = 0x1000;
    for (const auto &[id, uid] : title_uids) {
      (void)id;
      next = std::max(next, uid + 1);
    }
    title_uids.emplace(title, next);
    return next;
  }

  void save_uids() {
    if (root.empty())
      return;
    std::vector<std::pair<u32, u64>> ordered;
    for (const auto &[title, uid] : title_uids)
      ordered.emplace_back(uid, title);
    std::sort(ordered.begin(), ordered.end());
    std::vector<u8> bytes;
    for (const auto &[uid, title] : ordered) {
      append_number(bytes, title, 8);
      append_number(bytes, uid, 4);
    }
    const auto target = host_path("/sys/uid.sys");
    if (!target)
      throw std::runtime_error("Unsafe Wii UID mapping destination");
    replace_storage(*target, bytes);
    auto &entry = metadata("/sys/uid.sys");
    entry.modes = {3, 3, 0};
    save_metadata();
  }

  u64 superblock_ticks() const {
    return tb_ratio *
           (version == 28 || version == 80 ? 3350000ull : (version < 28 ? 4100000ull : 3170000ull));
  }

  u64 flush_cache() {
    if (!cache_fd || !cache_dirty)
      return 0;
    cache_dirty = false;
    if (handles[*cache_fd])
      handles[*cache_fd]->flush = true;
    return tb_ratio * (version < 28 ? 370000ull : 300000ull);
  }

  u64 file_timing(unsigned fd, u32 count, bool writing, u32 file_size) {
    u64 delay = 0;
    u32 offset = handles[fd]->offset;
    while (count != 0) {
      const bool cached = cache_fd == fd && cache_cluster == offset / 0x4000;
      u32 copied;
      if (!cached && count >= 0x4000 && offset % 0x4000 == 0) {
        delay += tb_ratio * (writing ? (version < 28 ? 370000ull : 300000ull)
                                     : (version == 28 || version == 80
                                            ? 125000ull
                                            : (version < 28 ? 165000ull : 115000ull)));
        copied = 0x4000;
        if (writing)
          handles[fd]->flush = true;
      } else {
        if (!cached) {
          delay += flush_cache();
          if ((offset % 0x4000 != 0 || offset != file_size) && offset < file_size)
            delay += tb_ratio * (version == 28 || version == 80
                                     ? 125000ull
                                     : (version < 28 ? 165000ull : 115000ull));
          cache_fd = fd;
          cache_cluster = offset / 0x4000;
        }
        copied = std::min(count, 0x4000 - offset % 0x4000);
        delay += tb_ratio * (version < 28 ? u64(copied) + 3 : (u64(copied) * 636 / 1000) + 150);
        if (writing)
          delay += 1000 * tb_ratio;
        cache_dirty = writing;
        if (writing && (offset + copied) % 0x4000 == 0)
          delay += flush_cache();
      }
      count -= copied;
      offset += copied;
    }
    return delay;
  }

  Reply open(u32 address) {
    const auto free =
        std::find_if(handles.begin(), handles.end(), [](const auto &handle) { return !handle; });
    if (free == handles.end())
      return {-5, 5000 * tb_ratio};
    const unsigned fd = unsigned(free - handles.begin());
    const auto path = string(memory.read32(address + 12), 256);
    const u32 mode = memory.read32(address + 16) & 3;
    if (path == "/dev/di") {
      handles[fd] = Handle{Device::Disc, {}, mode, 0, false, ppc_uid, ppc_gid};
      return {s32(fd)};
    }
    if (path == "/dev/es") {
      const auto active = std::count_if(handles.begin(), handles.end(), [](const auto &handle) {
        return handle && handle->device == Device::ES;
      });
      if (active == 3)
        return {-1016};
      handles[fd] = Handle{Device::ES, {}, mode, 0, false, ppc_uid, ppc_gid};
      return {s32(fd)};
    }
    if (path == "/dev/stm/eventhook" || path == "/dev/stm/immediate") {
      const Device device = path == "/dev/stm/eventhook" ? Device::StmEvent : Device::StmImmediate;
      handles[fd] = Handle{device, {}, mode, 0, false, ppc_uid, ppc_gid};
      return {s32(fd)};
    }
    if (path == "/dev/net/kd/request" || path == "/dev/net/kd/time") {
      const Device device = path == "/dev/net/kd/request" ? Device::KD : Device::KDTime;
      handles[fd] = Handle{device, {}, mode, 0, false, ppc_uid, ppc_gid};
      return {s32(fd)};
    }
    if (path == "/dev/usb/oh1/57e/305") {
      handles[fd] = Handle{Device::Bluetooth, {}, mode, 0, false, ppc_uid, ppc_gid};
      if (bluetooth_opens++ == 0)
        schedule(Kind::Bluetooth, devices.ticks_per_second() / 200, bluetooth_generation);
      return {s32(fd)};
    }
    if (path.starts_with("/dev/") && path != "/dev/fs")
      unsupported(address, "device open " + path);
    unsigned fs_count = 0;
    for (const auto &handle : handles)
      if (handle && (handle->device == Device::Filesystem || handle->device == Device::File))
        ++fs_count;
    if (fs_count >= 16)
      return {fs_full, fs_overhead};
    if (path == "/dev/fs") {
      handles[fd] = Handle{Device::Filesystem, {}, mode, 0, false, ppc_uid, ppc_gid};
      return {s32(fd), fs_overhead};
    }
    if (path.empty() || path.front() != '/')
      return {not_found, 3700 * tb_ratio};
    const u64 delay = fs_overhead + 680 * tb_ratio * u64(std::count(path.begin(), path.end(), '/'));
    const auto target = host_path(path);
    if (!target)
      return {fs_invalid, delay};
    std::error_code error;
    if (std::filesystem::is_directory(*target, error))
      return {fs_invalid, delay};
    if (!std::filesystem::is_regular_file(*target, error))
      return {fs_missing, delay};
    if (!permission(path, ppc_uid, ppc_gid, u8(mode)))
      return {fs_access, delay};
    std::fstream probe(*target, std::ios::in | std::ios::out | std::ios::binary);
    if (!probe)
      return {fs_access, delay};
    handles[fd] = Handle{Device::File, *target, mode, 0, false, ppc_uid, ppc_gid};
    return {s32(fd), delay};
  }

  Reply file(unsigned fd, u32 address, u32 command) {
    auto &handle = *handles[fd];
    if (handle.device != Device::File)
      return {fs_invalid, fs_overhead};
    std::error_code error;
    const auto host_size = std::filesystem::file_size(handle.path, error);
    if (error || host_size > 0xffffffffull)
      return {fs_access, fs_overhead};
    const u32 size = u32(host_size);
    const u32 parameter = memory.read32(address + 12), amount = memory.read32(address + 16);
    if (command == 5) {
      u32 position = parameter;
      if (amount == 1)
        position += handle.offset;
      else if (amount == 2)
        position += size;
      else if (amount != 0)
        return {fs_invalid, fs_overhead};
      if (position > size)
        return {fs_invalid, fs_overhead};
      handle.offset = position;
      return {s32(position), fs_overhead};
    }
    const bool writing = command == 4;
    const u32 count = writing ? amount : std::min(amount, size - handle.offset);
    if ((handle.mode & (writing ? 2u : 1u)) == 0)
      return {fs_access, fs_overhead};
    if (count > 0x04000000 || count > 0xffffffffu - handle.offset)
      return {fs_invalid, fs_overhead};
    auto bytes = memory.range(parameter, count);
    const u64 delay = fs_overhead + file_timing(fd, count, writing, size);
    std::fstream stream(handle.path, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream)
      return {fs_access, delay};
    if (writing) {
      stream.seekp(handle.offset);
      stream.write(reinterpret_cast<const char *>(bytes.data()), count);
      stream.flush();
    } else {
      stream.seekg(handle.offset);
      stream.read(reinterpret_cast<char *>(bytes.data()), count);
    }
    if (!stream)
      return {fs_access, delay};
    handle.offset += count;
    return {s32(count), delay};
  }

  Reply fs_ioctl(unsigned fd, u32 address) {
    const u32 operation = memory.read32(address + 12), input = memory.read32(address + 16);
    const u32 input_size = memory.read32(address + 20), output = memory.read32(address + 24);
    const u32 output_size = memory.read32(address + 28);
    if (operation == 11) {
      if (handles[fd]->device != Device::File || output_size < 8)
        return {fs_invalid, fs_overhead};
      std::error_code error;
      const auto size = std::filesystem::file_size(handles[fd]->path, error);
      if (error || size > 0xffffffffull)
        return {fs_access, fs_overhead};
      memory.write32(output, u32(size));
      memory.write32(output + 4, handles[fd]->offset);
      return {0, fs_overhead};
    }
    if (handles[fd]->device != Device::Filesystem)
      return {fs_invalid, fs_overhead};
    const auto &handle = *handles[fd];
    if (operation == 5 || operation == 6) {
      if ((operation == 5 && input_size < 74) ||
          (operation == 6 && (input_size < 64 || output_size < 74)))
        return {fs_invalid, fs_overhead};
      const auto path = string(input + (operation == 5 ? 6 : 0), 64);
      const auto target = host_path(path);
      if (!target)
        return {fs_invalid, fs_overhead};
      std::error_code error;
      if (!std::filesystem::exists(*target, error))
        return {fs_missing, fs_overhead};
      auto &entry = metadata(path);
      if (operation == 5) {
        const u32 uid = memory.read32(input);
        const u16 gid = memory.read16(input + 4);
        std::array<u8, 3> modes{};
        for (unsigned i = 0; i < 3; ++i) {
          modes[i] = memory.read8(input + 70 + i);
          if (modes[i] > 3)
            return {fs_invalid, fs_overhead};
        }
        if (handle.uid != 0 && (handle.uid != entry.uid || uid != entry.uid))
          return {fs_access, fs_overhead};
        if (entry.uid != uid && std::filesystem::is_regular_file(*target, error) &&
            std::filesystem::file_size(*target, error) != 0)
          return {-115, fs_overhead};
        entry.uid = uid;
        entry.gid = gid;
        entry.modes = modes;
        entry.attribute = memory.read8(input + 73);
        save_metadata();
        return {0, fs_overhead + superblock_ticks()};
      }
      if (path != "/" && !permission(parent_path(path), handle.uid, handle.gid, 1))
        return {fs_access, fs_overhead};
      auto bytes = memory.range(output, 74);
      std::fill(bytes.begin(), bytes.end(), u8(0));
      memory.write32(output, entry.uid);
      memory.write16(output + 4, entry.gid);
      for (unsigned i = 0; i < 3; ++i)
        memory.write8(output + 70 + i, entry.modes[i]);
      memory.write8(output + 73, entry.attribute);
      return {0, fs_overhead};
    }
    if (operation == 3 || operation == 9) {
      if (input_size < 74)
        return {fs_invalid, fs_overhead};
      const auto path = string(input + 6, 64);
      const auto target = host_path(path);
      if (!target)
        return {fs_invalid, fs_overhead};
      for (u32 i = 0; i < 3; ++i)
        if (memory.read8(input + 70 + i) > 3)
          return {fs_invalid, fs_overhead};
      std::error_code error;
      if (std::filesystem::exists(*target, error))
        return {fs_exists, fs_overhead};
      if (!std::filesystem::is_directory(target->parent_path(), error))
        return {fs_missing, fs_overhead};
      if (!permission(parent_path(path), handle.uid, handle.gid, 2))
        return {fs_access, fs_overhead};
      if (operation == 3 && std::count(path.begin(), path.end(), '/') > 8)
        return {-110, fs_overhead};
      bool created;
      if (operation == 3)
        created = std::filesystem::create_directory(*target, error);
      else {
        std::ofstream stream(*target, std::ios::binary);
        created = bool(stream);
      }
      if (created) {
        auto &entry = metadata(path);
        entry.uid = handle.uid;
        entry.gid = handle.gid;
        entry.attribute = memory.read8(input + 73);
        for (unsigned i = 0; i < 3; ++i)
          entry.modes[i] = memory.read8(input + 70 + i);
        save_metadata();
      }
      return {created ? 0 : fs_access, fs_overhead + (created ? superblock_ticks() : 0)};
    }
    if (operation == 7) {
      if (input_size < 64)
        return {fs_invalid, fs_overhead};
      const auto path = string(input, 64);
      const auto target = host_path(path);
      if (!target || path == "/")
        return {fs_invalid, fs_overhead};
      for (const auto &opened : handles)
        if (opened && opened->device == Device::File &&
            (opened->path == *target ||
             opened->path.generic_string().starts_with(target->generic_string() + "/")))
          return {fs_busy, fs_overhead};
      if (!permission(parent_path(path), handle.uid, handle.gid, 2))
        return {fs_access, fs_overhead};
      std::error_code error;
      if (!std::filesystem::exists(*target, error))
        return {fs_missing, fs_overhead};
      if (std::filesystem::is_directory(*target, error))
        for (const auto &child : std::filesystem::recursive_directory_iterator(*target)) {
          const auto relative = child.path().lexically_relative(root).generic_string();
          if (!host_path("/" + relative))
            return {fs_access, fs_overhead};
        }
      const bool removed = std::filesystem::remove_all(*target, error) != 0 && !error;
      if (removed) {
        std::erase_if(nodes, [&](const auto &entry) {
          return entry.first == path || entry.first.starts_with(path + "/");
        });
        save_metadata();
      }
      return {removed ? 0 : fs_access, fs_overhead + (removed ? superblock_ticks() : 0)};
    }
    if (operation == 8) {
      if (input_size < 128)
        return {fs_invalid, fs_overhead};
      const auto old_path = string(input, 64), new_path = string(input + 64, 64);
      const auto original = host_path(old_path), destination = host_path(new_path);
      if (!original || !destination || old_path == "/" || new_path == "/" ||
          new_path.starts_with(old_path + "/"))
        return {fs_invalid, fs_overhead};
      const u64 delay = fs_overhead + superblock_ticks();
      std::error_code error;
      if (!std::filesystem::exists(*original, error) ||
          !std::filesystem::is_directory(destination->parent_path(), error))
        return {fs_missing, delay};
      if (!permission(parent_path(old_path), handle.uid, handle.gid, 2) ||
          !permission(parent_path(new_path), handle.uid, handle.gid, 2))
        return {fs_access, delay};
      const bool original_file = std::filesystem::is_regular_file(*original, error);
      if (original_file && original->filename() != destination->filename())
        return {fs_invalid, delay};
      for (const auto &opened : handles)
        if (opened && opened->device == Device::File &&
            (opened->path == *original ||
             opened->path.generic_string().starts_with(original->generic_string() + "/")))
          return {fs_busy, delay};
      if (old_path == new_path)
        return {0, delay};
      if (std::filesystem::exists(*destination, error)) {
        const bool destination_file = std::filesystem::is_regular_file(*destination, error);
        if (original_file != destination_file)
          return {fs_invalid, delay};
        for (const auto &opened : handles)
          if (opened && opened->device == Device::File && opened->path == *destination)
            return {fs_busy, delay};
        if (!destination_file)
          for (const auto &child : std::filesystem::recursive_directory_iterator(*destination))
            if (!host_path("/" + child.path().lexically_relative(root).generic_string()))
              return {fs_access, delay};
        std::filesystem::remove_all(*destination, error);
        if (error)
          return {fs_access, delay};
      }
      std::filesystem::rename(*original, *destination, error);
      if (error)
        return {fs_missing, delay};
      std::vector<std::pair<std::string, Metadata>> moved;
      for (const auto &[path, entry] : nodes)
        if (path == old_path || path.starts_with(old_path + "/"))
          moved.emplace_back(new_path + path.substr(old_path.size()), entry);
      std::erase_if(nodes, [&](const auto &entry) {
        return entry.first == old_path || entry.first.starts_with(old_path + "/") ||
               entry.first == new_path || entry.first.starts_with(new_path + "/");
      });
      for (auto &[path, entry] : moved)
        nodes.emplace(path, entry);
      metadata(new_path).order = next_node++;
      save_metadata();
      return {0, delay};
    }
    unsupported(address, "filesystem ioctl " + std::to_string(operation));
  }

  struct Vector {
    u32 address, size;
  };

  struct Vectors {
    std::vector<Vector> input, output;

    bool count(unsigned in, unsigned out) const {
      return input.size() == in && output.size() == out;
    }
  };

  std::optional<Vectors> vectors(u32 address) {
    try {
      const u32 in = memory.read32(address + 16), out = memory.read32(address + 20);
      const u32 base = memory.read32(address + 24);
      if (in > 64 || out > 64 || (in + out != 0 && base == 0))
        return std::nullopt;
      memory.range(base, std::size_t(in + out) * 8);
      Vectors result;
      for (unsigned i = 0; i < in + out; ++i) {
        const Vector vector{memory.read32(base + i * 8), memory.read32(base + i * 8 + 4)};
        if (vector.size != 0 && vector.address == 0)
          return std::nullopt;
        memory.range(vector.address, vector.size);
        (i < in ? result.input : result.output).push_back(vector);
      }
      return result;
    } catch (const BusFault &fault) {
      if (fault.unsupported_reason)
        throw;
      return std::nullopt;
    }
  }

  Reply fs_ioctlv(unsigned fd, u32 address) {
    if (handles[fd]->device != Device::Filesystem)
      return {fs_invalid, fs_overhead};
    const auto parsed = vectors(address);
    if (!parsed)
      return {fs_invalid, fs_overhead};
    const auto &in = parsed->input, &out = parsed->output;
    const auto &handle = *handles[fd];
    const u32 operation = memory.read32(address + 12);
    if (operation != 4)
      unsupported(address, "filesystem ioctlv " + std::to_string(operation));
    if (in.empty() || in.size() > 2 || in.size() != out.size() || in[0].size != 64 ||
        (in.size() == 1 && out[0].size != 4) ||
        (in.size() == 2 && (in[1].size != 4 || out[1].size != 4)))
      return {fs_invalid, fs_overhead};
    const bool listing = in.size() == 2;
    const u32 maximum = listing ? memory.read32(in[1].address) : 0;
    if (listing && (maximum > 0xffffffffu / 13 || out[0].size != maximum * 13))
      return {fs_invalid, fs_overhead};
    const auto path = string(in[0].address, 64);
    const auto target = host_path(path);
    if (!target)
      return {fs_invalid, fs_overhead};
    std::error_code error;
    if (!std::filesystem::exists(*target, error))
      return {fs_missing, fs_overhead};
    if (!std::filesystem::is_directory(*target, error))
      return {fs_invalid, fs_overhead};
    if (!permission(path, handle.uid, handle.gid, 1))
      return {fs_access, fs_overhead};
    std::vector<std::pair<u64, std::string>> names;
    for (const auto &entry : std::filesystem::directory_iterator(*target)) {
      const auto name = entry.path().filename().string();
      const auto guest_path = (path == "/" ? path : path + "/") + name;
      if (!host_path(guest_path) ||
          (path == "/" && !std::filesystem::is_directory(entry.symlink_status())))
        continue;
      const auto found = nodes.find(guest_path);
      names.emplace_back(found == nodes.end() ? 0 : found->second.order, name);
    }
    std::sort(names.begin(), names.end(), std::greater<>());
    if (!listing) {
      memory.write32(out[0].address, u32(names.size()));
      return {0, fs_overhead};
    }
    const u32 count = std::min(maximum, u32(names.size()));
    u32 cursor = out[0].address;
    for (unsigned i = 0; i < count; ++i) {
      const auto &name = names[i].second;
      auto buffer = memory.range(cursor, 13);
      std::fill(buffer.begin(), buffer.end(), u8(0));
      std::copy(name.begin(), name.end(), buffer.begin());
      cursor += u32(name.size()) + 1;
    }
    memory.write32(out[1].address, count);
    return {0, fs_overhead};
  }

  std::vector<u8> stored_blob(const std::string &path, u32 maximum) {
    const auto target = host_path(path);
    if (!target)
      return {};
    std::error_code error;
    const auto size = std::filesystem::file_size(*target, error);
    if (error || size > maximum)
      return {};
    std::vector<u8> data(std::size_t(size), u8{});
    std::ifstream stream(*target, std::ios::binary);
    stream.read(reinterpret_cast<char *>(data.data()), std::streamsize(data.size()));
    return stream && std::size_t(stream.gcount()) == data.size() ? data : std::vector<u8>{};
  }

  std::vector<u8> installed_tmd(u64 title) {
    auto data = stored_blob(title_directory(title) + "/content/title.tmd", 0x49e4);
    return valid_tmd(data) && big64(data, 0x18c) == title ? data : std::vector<u8>{};
  }

  std::vector<u8> installed_ticket(u64 title) {
    auto data = stored_blob(
        "/ticket/" + hexadecimal(u32(title >> 32)) + "/" + hexadecimal(u32(title)) + ".tik", 0x2a4);
    return valid_ticket(data) && big64(data, 0x1dc) == title ? data : std::vector<u8>{};
  }

  std::vector<u64> installed_titles(bool tickets) {
    const auto prefix = std::string(tickets ? "/ticket" : "/title");
    const auto target = host_path(prefix);
    if (!target || !std::filesystem::is_directory(*target))
      return {};
    auto parse_id = [](const std::string &name) -> std::optional<u32> {
      if (name.size() != 8)
        return std::nullopt;
      u32 value;
      const auto parsed = std::from_chars(name.data(), name.data() + name.size(), value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size())
        return std::nullopt;
      return value;
    };
    std::vector<std::pair<u64, std::string>> types;
    for (const auto &entry : std::filesystem::directory_iterator(*target)) {
      const auto name = entry.path().filename().string();
      if (!parse_id(name) || !host_path(prefix + "/" + name) || !entry.is_directory())
        continue;
      const auto found = nodes.find(prefix + "/" + name);
      types.emplace_back(found == nodes.end() ? 0 : found->second.order, name);
    }
    std::sort(types.begin(), types.end(), std::greater<>());
    std::vector<u64> result;
    for (const auto &[order, type] : types) {
      (void)order;
      std::vector<std::pair<u64, std::string>> entries;
      const auto directory = host_path(prefix + "/" + type);
      for (const auto &entry : std::filesystem::directory_iterator(*directory)) {
        const auto name = entry.path().filename().string();
        const auto identifier = tickets ? name.substr(0, 8) : name;
        if (!parse_id(identifier) || !host_path(prefix + "/" + type + "/" + name) ||
            (tickets ? (!entry.is_regular_file() ||
                        (name != identifier + ".tik" && name != identifier + ".tv1"))
                     : !entry.is_directory()))
          continue;
        const auto found = nodes.find(prefix + "/" + type + "/" + name);
        entries.emplace_back(found == nodes.end() ? 0 : found->second.order, identifier);
      }
      std::sort(entries.begin(), entries.end(), std::greater<>());
      for (const auto &[entry_order, identifier] : entries) {
        (void)entry_order;
        result.push_back((u64(*parse_id(type)) << 32) | *parse_id(identifier));
      }
    }
    return result;
  }

  std::vector<u32> stored_contents(std::span<const u8> tmd) {
    const auto shared = stored_blob("/shared1/content.map", 1024 * 1024);
    std::vector<u32> result;
    for (unsigned index = 0; index < big16(tmd, 0x1de); ++index) {
      const auto offset = 0x1e4 + index * 36;
      const u32 id = big32(tmd, offset);
      std::string path =
          title_directory(big64(tmd, 0x18c)) + "/content/" + hexadecimal(id) + ".app";
      if (big16(tmd, offset + 6) & 0x8000) {
        path.clear();
        for (std::size_t record = 0; record + 28 <= shared.size(); record += 28)
          if (std::equal(shared.begin() + record + 8, shared.begin() + record + 28,
                         tmd.begin() + offset + 16)) {
            path = "/shared1/" + std::string(shared.begin() + record, shared.begin() + record + 8) +
                   ".app";
            break;
          }
      }
      const auto target = host_path(path);
      if (target && std::filesystem::is_regular_file(*target))
        result.push_back(id);
    }
    return result;
  }

  void save_title_metadata() {
    if (title_tmd.empty())
      return;
    ppc_uid = uid_for_title(title_id);
    ppc_gid = big16(title_tmd, 0x198);
    if (root.empty())
      return;
    const auto target = host_path(title_directory(title_id) + "/content/title.tmd");
    const auto data = host_path(title_directory(title_id) + "/data");
    if (!target || !data)
      throw std::runtime_error("Unsafe Wii title metadata destination");
    std::filesystem::create_directories(target->parent_path());
    std::filesystem::create_directories(*data);
    const auto base = title_directory(title_id);
    metadata("/title/" + hexadecimal(u32(title_id >> 32))).modes = {3, 3, 1};
    metadata(base).modes = {3, 3, 1};
    metadata(base + "/content").modes = {3, 3, 0};
    metadata(base + "/content/title.tmd").modes = {3, 3, 0};
    auto &data_metadata = metadata(base + "/data");
    data_metadata.uid = ppc_uid;
    data_metadata.gid = ppc_gid;
    data_metadata.modes = {3, 0, 0};
    if (installed_tmd(title_id).empty())
      replace_storage(*target, title_tmd);
    save_uids();
  }

  Reply es_ioctlv(u32 address) {
    const auto parsed = vectors(address);
    if (!parsed)
      return {es_invalid};
    const auto &in = parsed->input, &out = parsed->output;
    const u32 operation = memory.read32(address + 12);
    if (operation == 0x16) {
      if (!parsed->count(1, 2) || in[0].size != 8 || out[1].size != 4 || out[0].size % 8 != 0)
        return {es_invalid};
      const u64 identifier = memory.read64(in[0].address);
      std::vector<u8> ticket;
      if (!title_ticket.empty() && big64(title_ticket, 0x1d0) == identifier)
        ticket = title_ticket;
      else
        for (u64 title : installed_titles(true)) {
          auto installed = installed_ticket(title);
          if (!installed.empty() && big64(installed, 0x1d0) == identifier) {
            ticket = std::move(installed);
            break;
          }
        }
      if (ticket.empty())
        unsupported(address, "ES consumption query without matching ticket");
      for (unsigned record = 0; record < 8; ++record)
        if (big32(ticket, 0x264 + record * 8) != 0)
          unsupported(address, "ES limited-ticket consumption storage");
      memory.write32(out[1].address, 0);
      return {0};
    }
    if (operation >= 0x0c && operation <= 0x0f) {
      const bool count = operation == 0x0c || operation == 0x0e;
      if (!parsed->count(count ? 0 : 1, 1) || (count && out[0].size != 4) ||
          (!count && in[0].size < 4))
        return {es_invalid};
      const auto titles = installed_titles(operation <= 0x0d);
      if (count)
        memory.write32(out[0].address, u32(titles.size()));
      else {
        const u32 amount = std::min(memory.read32(in[0].address), u32(titles.size()));
        if (u64(amount) * 8 > out[0].size)
          return {es_invalid};
        for (unsigned i = 0; i < amount; ++i)
          memory.write64(out[0].address + i * 8, titles[i]);
      }
      return {0};
    }
    if (operation == 0x10 || operation == 0x11 || operation == 0x32) {
      const bool count = operation != 0x11;
      if (!parsed->count(count ? 1 : 2, 1) || (operation != 0x32 && in[0].size != 8) ||
          (count && out[0].size != 4) ||
          (!count && (in[1].size != 4 || u64(memory.read32(in[1].address)) * 4 != out[0].size)))
        return {es_invalid};
      const auto tmd = operation == 0x32
                           ? std::vector<u8>(memory.range(in[0].address, in[0].size).begin(),
                                             memory.range(in[0].address, in[0].size).end())
                           : installed_tmd(memory.read64(in[0].address));
      if (!valid_tmd(tmd))
        return {operation == 0x32 ? es_invalid : fs_missing};
      const auto contents = stored_contents(tmd);
      if (count)
        memory.write32(out[0].address, u32(contents.size()));
      else {
        const u32 amount = std::min(memory.read32(in[1].address), u32(contents.size()));
        for (unsigned i = 0; i < amount; ++i)
          memory.write32(out[0].address + i * 4, contents[i]);
      }
      return {0};
    }
    if (operation == 0x20) {
      if (!parsed->count(0, 1) || out[0].size < 8 || title_tmd.empty())
        return {es_invalid};
      memory.write64(out[0].address, title_id);
      return {0};
    }
    if (operation == 0x1d) {
      if (!parsed->count(1, 1) || in[0].size < 8)
        return {es_invalid};
      const auto path = title_directory(memory.read64(in[0].address)) + "/data";
      if (out[0].size < path.size())
        return {es_invalid};
      memory.copy_in(out[0].address, {reinterpret_cast<const u8 *>(path.data()), path.size()});
      return {0};
    }
    if (operation == 0x39) {
      if (!parsed->count(0, 1) || out[0].size != 4 || title_tmd.empty())
        return {es_invalid};
      memory.write32(out[0].address, u32(title_tmd.size()));
      return {0};
    }
    if (operation == 0x3a) {
      if (!parsed->count(1, 1) || in[0].size != 4 || title_tmd.empty() ||
          memory.read32(in[0].address) != out[0].size || out[0].size < title_tmd.size())
        return {es_invalid};
      memory.copy_in(out[0].address, title_tmd);
      return {0};
    }
    if (operation == 0x19 || operation == 0x1a) {
      if (!parsed->count(operation == 0x19 ? 1 : 2, 1) || in[0].size >= 0x400000 ||
          (operation == 0x19 && out[0].size != 4) ||
          (operation == 0x1a && (in[1].size != 4 || memory.read32(in[1].address) != out[0].size)))
        return {es_invalid};
      const auto bytes = in[0].size == 0 ? std::span<const u8>(title_tmd)
                                         : memory.range(in[0].address, in[0].size);
      if (!valid_tmd(bytes))
        return {es_invalid};
      const auto view = tmd_view(bytes);
      if (operation == 0x19)
        memory.write32(out[0].address, u32(view.size()));
      else {
        if (out[0].size < view.size())
          return {es_invalid};
        memory.copy_in(out[0].address, view);
      }
      return {0};
    }
    if (operation == 0x1b) {
      if (!parsed->count(1, 1) || (in[0].size != 0 && in[0].size != 0x2a4) || out[0].size != 0xd8)
        return {es_invalid};
      const auto bytes = in[0].size == 0 ? std::span<const u8>(title_ticket)
                                         : memory.range(in[0].address, in[0].size);
      if (!valid_ticket(bytes))
        return {es_invalid};
      memory.copy_in(out[0].address, ticket_view(bytes));
      return {0};
    }
    if (operation == 0x14 || operation == 0x15 || operation == 0x34 || operation == 0x35) {
      const bool count = operation == 0x14 || operation == 0x34;
      if (!parsed->count(count ? 1 : 2, 1) || in[0].size != 8 || (count && out[0].size < 4) ||
          (!count &&
           (in[1].size != 4 || (operation == 0x15 && memory.read32(in[1].address) != out[0].size))))
        return {es_invalid};
      const auto bytes = installed_tmd(memory.read64(in[0].address));
      if (bytes.empty())
        return {fs_missing};
      const auto result = operation == 0x14 || operation == 0x15 ? tmd_view(bytes) : bytes;
      if (count)
        memory.write32(out[0].address, u32(result.size()));
      else {
        if ((operation == 0x35 && out[0].size != result.size()) || out[0].size < result.size())
          return {es_invalid};
        memory.copy_in(out[0].address, result);
      }
      return {0};
    }
    if (operation == 0x12 || operation == 0x13) {
      if (!parsed->count(operation == 0x12 ? 1 : 2, 1) || in[0].size < 8 ||
          (operation == 0x12 && out[0].size < 4) || (operation == 0x13 && in[1].size < 4))
        return {es_invalid};
      const auto ticket = installed_ticket(memory.read64(in[0].address));
      if (operation == 0x12)
        memory.write32(out[0].address, ticket.empty() ? 0 : 1);
      else if (!ticket.empty() && memory.read32(in[1].address) != 0) {
        if (out[0].size < 0xd8)
          return {es_invalid};
        memory.copy_in(out[0].address, ticket_view(ticket));
      }
      return {0};
    }
    // The disc verification ioctl is private to IOS and cannot be called by the PPC.
    if (operation == 0x1c)
      return {es_invalid};
    if (operation == 0x3f || operation == 0x41 || operation == 0x42 || operation > 0x45)
      return {invalid};
    unsupported(address, "ES ioctlv " + std::to_string(operation));
  }

  Reply disc_ioctlv(u32 address) {
    const auto parsed = vectors(address);
    if (!parsed || parsed->input.empty() || parsed->input[0].size != 32)
      return {128};
    const u32 operation = memory.read32(address + 12);
    if (operation != 0x8b)
      unsupported(address, "disc ioctlv " + std::to_string(operation));
    if (!parsed->count(3, 2))
      return {128};
    if (parsed->input[1].size != 0 || parsed->input[2].size != 0)
      unsupported(address, "disc partition with caller-supplied ticket or certificates");
    const u64 offset = u64(memory.read32(parsed->input[0].address + 4)) << 2;
    if (title_tmd.empty() || offset != selected_partition)
      unsupported(address, "disc partition selection without matching loaded metadata");
    if (parsed->output[0].size < title_tmd.size() || parsed->output[1].size < 4)
      return {128};
    save_title_metadata();
    partition_open = true;
    memory.copy_in(parsed->output[0].address, title_tmd);
    memory.write32(parsed->output[1].address, 0);
    return {1};
  }

  void enqueue_reply(u32 address, Reply reply, bool ordered = true) {
    const u32 command = memory.read32(address);
    memory.write32(address + 4, u32(reply.result));
    memory.write32(address + 8, command);
    memory.write32(address, 8);
    const u64 due = (ordered ? std::max(now, last_reply) : now) + reply.delay;
    if (ordered)
      last_reply = due;
    events.push(Event{due, serial++, Kind::Reply, address, 0, 0, {}});
  }

  void stm_event(u32 event) {
    if (!stm_hook || !handles[stm_hook->fd] || handles[stm_hook->fd]->device != Device::StmEvent)
      return;
    memory.write32(stm_hook->output, event);
    enqueue_reply(stm_hook->request, Reply{0});
    stm_hook.reset();
  }

  std::optional<Reply> stm_ioctl(unsigned fd, u32 address) {
    const u32 operation = memory.read32(address + 12);
    if (handles[fd]->device == Device::StmEvent) {
      if (operation != 0x1000)
        return Reply{-9};
      if (stm_hook)
        return Reply{-2};
      const u32 output = memory.read32(address + 24);
      if (memory.read32(address + 28) < 4)
        return Reply{invalid};
      memory.range(output, 4);
      stm_hook = StmHook{address, output, fd};
      return std::nullopt;
    }
    if (operation == 0x3002) {
      if (!stm_hook)
        return Reply{not_found};
      memory.write32(stm_hook->output, 0);
      enqueue_reply(stm_hook->request, Reply{0});
      stm_hook.reset();
      return Reply{0};
    }
    if (operation == 0x2003 || operation == 0x2004) {
      if (!shutdown_handler)
        unsupported(address, "STM shutdown without connected host handler");
      shutdown_handler();
      return Reply{0};
    }
    if (operation == 0x2001 || operation == 0x5001 || operation == 0x6002)
      return Reply{0};
    switch (operation) {
    case 0x2002:
    case 0x2005:
    case 0x3001:
    case 0x4001:
    case 0x4002:
    case 0x6001:
    case 0x7001:
    case 0x8001:
      unsupported(address, "STM ioctl " + std::to_string(operation));
    default:
      return Reply{-9};
    }
  }

  Reply kd_ioctl(Device device, u32 address) {
    const u32 operation = memory.read32(address + 12);
    const u32 input = memory.read32(address + 16), input_size = memory.read32(address + 20);
    const u32 output = memory.read32(address + 24), output_size = memory.read32(address + 28);
    try {
      return {network.ioctl(device == Device::KD ? NetworkDevice::Request : NetworkDevice::Time,
                            operation, input, input_size, output, output_size)};
    } catch (const UnsupportedNetwork &) {
      unsupported(address, "KD ioctl " + std::to_string(operation));
    }
  }

  void deliver_hci() {
    if (!hci_endpoint || hci_events.empty())
      return;
    auto packet = std::move(hci_events.front());
    hci_events.pop_front();
    const auto endpoint = *hci_endpoint;
    hci_endpoint.reset();
    if (packet.size() > endpoint.size) {
      enqueue_reply(endpoint.request, Reply{invalid});
      return;
    }
    memory.copy_in(endpoint.buffer, packet);
    enqueue_reply(endpoint.request, Reply{s32(packet.size())});
  }

  static std::array<u8, 6> peer_address(unsigned index) {
    return {0x11, 2, 0x19, 0x79, 0, u8(index)};
  }

  std::optional<unsigned> peer_index(std::span<const u8> address) const {
    if (address.size() < 6 || address[5] >= remotes.size())
      return std::nullopt;
    const auto expected = peer_address(address[5]);
    if (!std::equal(expected.begin(), expected.end(), address.begin()))
      return std::nullopt;
    return address[5];
  }

  void hci_event(u8 operation, std::span<const u8> payload) {
    std::vector<u8> packet{operation, u8(payload.size())};
    packet.insert(packet.end(), payload.begin(), payload.end());
    hci_events.push_back(std::move(packet));
    deliver_hci();
  }

  void command_status(u16 opcode, u8 status = 0) {
    const std::array<u8, 4> data{status, 1, u8(opcode), u8(opcode >> 8)};
    hci_event(0x0f, data);
  }

  void disconnect(unsigned index, u8 reason, bool notify_inactive = false) {
    if (!remotes[index].linked() && !notify_inactive)
      return;
    const std::array<u8, 4> data{0, u8(index), 1, reason};
    hci_event(5, data);
    if (remotes[index].linked())
      remotes[index].set_linked(false);
    channels[index].clear();
    initiating_channels[index] = false;
    request_pending[index] = false;
    connection_delay[index] = 200;
  }

  void connection_complete(std::span<const u8> address, bool accepted, bool initiate) {
    const auto index = peer_index(address);
    const bool success = index && remotes[*index].state().connected && !remotes[*index].linked();
    std::vector<u8> data{u8(success && accepted ? 0 : 8), address[5], 1};
    data.insert(data.end(), address.begin(), address.begin() + 6);
    data.insert(data.end(), {1, 0});
    hci_event(3, data);
    if (success && accepted) {
      remotes[*index].set_linked(true);
      request_pending[*index] = false;
      connection_delay[*index] = 200;
      initiating_channels[*index] = initiate;
      channels[*index].clear();
    }
  }

  void deliver_acl() {
    if (!acl_endpoint || acl_packets.empty() || !hci_events.empty())
      return;
    auto packet = std::move(acl_packets.front());
    acl_packets.pop_front();
    const auto endpoint = *acl_endpoint;
    acl_endpoint.reset();
    if (packet.size() > endpoint.size) {
      enqueue_reply(endpoint.request, Reply{invalid});
      return;
    }
    memory.copy_in(endpoint.buffer, packet);
    enqueue_reply(endpoint.request, Reply{s32(packet.size())});
  }

  void send_acl(unsigned index, u16 cid, std::span<const u8> payload) {
    if (payload.size() > 331)
      unsupported(0, "Bluetooth ACL payload exceeds controller packet size");
    std::vector<u8> packet;
    append_little(packet, u16(0x2100 | index));
    append_little(packet, u16(payload.size() + 4));
    append_little(packet, u16(payload.size()));
    append_little(packet, cid);
    packet.insert(packet.end(), payload.begin(), payload.end());
    acl_packets.push_back(std::move(packet));
    deliver_acl();
  }

  void signal(unsigned index, u8 code, u8 identifier, std::span<const u8> payload) {
    std::vector<u8> packet{code, identifier};
    append_little(packet, u16(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    send_acl(index, 1, packet);
  }

  u16 allocate_channel(unsigned index) const {
    u16 cid = 0x40;
    while (channels[index].contains(cid))
      ++cid;
    return cid;
  }

  void connection_request(unsigned index) {
    if (bluetooth_opens == 0 || !(scan_enable & 2) || !request_pending[index] ||
        !remotes[index].state().connected || remotes[index].linked())
      return;
    const auto address = peer_address(index);
    std::vector<u8> data(address.begin(), address.end());
    data.insert(data.end(), {0, 4, 0x48, 1});
    hci_event(4, data);
    request_pending[index] = false;
    connection_delay[index] = 200;
  }

  void bluetooth_update() {
    deliver_hci();
    deliver_acl();
    for (unsigned index = 0; index < remotes.size(); ++index) {
      auto &remote = remotes[index];
      if (connection_delay[index] != 0)
        --connection_delay[index];
      if (!remote.state().connected) {
        disconnect(index, 0x13);
        request_pending[index] = false;
        continue;
      }
      if (!remote.linked()) {
        if (!request_pending[index] && connection_delay[index] == 0 &&
            remote.state().buttons != 0) {
          request_pending[index] = true;
          connection_delay[index] = 200;
          schedule(Kind::Connection, 0, index);
        }
        continue;
      }
      for (auto &[cid, channel] : channels[index]) {
        if (channel.peer != 0 && !channel.configuring) {
          std::vector<u8> data;
          append_little(data, channel.peer);
          append_little(data, 0);
          data.insert(data.end(), {1, 2, 185, 0});
          signal(index, 4, 4, data);
          channel.configuring = true;
        }
      }
      if (initiating_channels[index]) {
        for (u16 psm : {u16(0x11), u16(0x13)}) {
          const auto found = std::find_if(channels[index].begin(), channels[index].end(),
                                          [&](const auto &item) { return item.second.psm == psm; });
          if (found == channels[index].end()) {
            const u16 cid = allocate_channel(index);
            channels[index][cid].psm = psm;
            std::vector<u8> data;
            append_little(data, psm);
            append_little(data, cid);
            signal(index, 2, 2, data);
            break;
          }
          if (found->second.peer == 0 || found->second.mtu == 0 || !found->second.configured)
            break;
          if (psm == 0x13)
            initiating_channels[index] = false;
        }
      }
      const auto interrupt = std::find_if(channels[index].begin(), channels[index].end(),
                                          [](const auto &item) { return item.second.psm == 0x13; });
      if (interrupt != channels[index].end() && interrupt->second.peer != 0 &&
          interrupt->second.mtu != 0 && interrupt->second.configured) {
        remote.update();
        while (auto report = remote.take_report())
          send_acl(index, interrupt->second.peer, *report);
      }
    }
    if (std::any_of(received_packets.begin(), received_packets.end(),
                    [](u16 count) { return count; })) {
      std::vector<u8> data{u8(remotes.size())};
      for (unsigned index = 0; index < remotes.size(); ++index) {
        append_little(data, u16(0x100 | index));
        append_little(data, received_packets[index]);
      }
      received_packets.fill(u16(0));
      hci_event(0x13, data);
    }
  }

  bool receive_acl(u32 address, std::span<const u8> packet) {
    if (packet.size() < 8 || little16(packet, 2) != packet.size() - 4 ||
        little16(packet, 4) != packet.size() - 8)
      return false;
    const u16 handle = little16(packet, 0);
    if ((handle & 0xf000) != 0x2000 || (handle & 0xff00) != 0x2100)
      return false;
    const unsigned index = handle & 255;
    if (index >= remotes.size() || !remotes[index].linked())
      return false;
    const u16 cid = little16(packet, 6);
    auto payload = packet.subspan(8);
    ++received_packets[index];
    if (cid == 1) {
      while (!payload.empty()) {
        if (payload.size() < 4 || little16(payload, 2) > payload.size() - 4)
          return false;
        const u8 code = payload[0], identifier = payload[1];
        const auto data = payload.subspan(4, little16(payload, 2));
        if (code == 2) {
          if (data.size() != 4)
            return false;
          const u16 psm = little16(data, 0), peer = little16(data, 2);
          if (psm != 1 && psm != 0x11 && psm != 0x13)
            unsupported(address, "Bluetooth L2CAP PSM " + std::to_string(psm));
          const bool duplicate =
              std::any_of(channels[index].begin(), channels[index].end(),
                          [&](const auto &item) { return item.second.psm == psm; });
          const u16 local = duplicate ? 0 : allocate_channel(index);
          if (!duplicate)
            channels[index][local] = Channel{psm, peer, 0, false, false};
          std::vector<u8> response;
          append_little(response, local);
          append_little(response, peer);
          append_little(response, duplicate ? 3 : 0);
          append_little(response, 0);
          signal(index, 3, identifier, response);
        } else if (code == 3) {
          if (data.size() != 8)
            return false;
          const u16 local = little16(data, 2);
          const auto found = channels[index].find(local);
          if (found == channels[index].end())
            return false;
          if (little16(data, 4) != 0) {
            channels[index].erase(found);
            initiating_channels[index] = false;
          } else
            found->second.peer = little16(data, 0);
        } else if (code == 4) {
          if (data.size() < 4 || little16(data, 2) != 0)
            return false;
          const auto found = channels[index].find(little16(data, 0));
          if (found == channels[index].end())
            return false;
          u16 mtu = 672;
          for (std::size_t offset = 4; offset < data.size();) {
            if (data.size() - offset < 2 || data[offset + 1] > data.size() - offset - 2)
              return false;
            const unsigned length = data[offset + 1];
            if (data[offset] != 1 && data[offset] != 2)
              unsupported(address, "Bluetooth L2CAP configuration option");
            if (length != 2)
              return false;
            if (data[offset] == 1)
              mtu = little16(data, offset + 2);
            offset += length + 2;
          }
          found->second.mtu = mtu;
          std::vector<u8> response;
          append_little(response, found->second.peer);
          append_little(response, 0);
          append_little(response, 0);
          response.insert(response.end(), data.begin() + 4, data.end());
          signal(index, 5, identifier, response);
        } else if (code == 5) {
          if (data.size() < 6 || little16(data, 2) != 0)
            return false;
          const auto found = channels[index].find(little16(data, 0));
          if (found == channels[index].end())
            return false;
          if (little16(data, 4) != 0)
            unsupported(address, "Bluetooth L2CAP configuration rejection");
          found->second.configured = true;
        } else if (code == 6) {
          if (data.size() != 4)
            return false;
          const auto found = channels[index].find(little16(data, 0));
          if (found == channels[index].end() || found->second.peer != little16(data, 2))
            return false;
          channels[index].erase(found);
          signal(index, 7, identifier, data);
        } else
          unsupported(address, "Bluetooth L2CAP signal " + std::to_string(code));
        payload = payload.subspan(data.size() + 4);
      }
      return true;
    }
    const auto found = channels[index].find(cid);
    if (found == channels[index].end())
      return false;
    const Channel channel = found->second;
    if (channel.psm == 1) {
      try {
        const auto response = hw::sdp_response(payload);
        send_acl(index, channel.peer, response);
      } catch (const hw::SdpError &error) {
        unsupported(address, "Bluetooth SDP: " + std::string(error.what()));
      }
    } else if (channel.psm == 0x11 || channel.psm == 0x13) {
      if (payload.empty() || payload[0] != (channel.psm == 0x11 ? 0x52 : 0xa2))
        return false;
      if (channel.psm == 0x11) {
        const std::array<u8, 1> handshake{0};
        send_acl(index, channel.peer, handshake);
      }
      remotes[index].receive_output(payload.subspan(1));
      const auto interrupt = std::find_if(channels[index].begin(), channels[index].end(),
                                          [](const auto &item) { return item.second.psm == 0x13; });
      if (interrupt == channels[index].end() || interrupt->second.peer == 0)
        return true;
      while (auto report = remotes[index].take_report())
        send_acl(index, interrupt->second.peer, *report);
    } else
      unsupported(address, "Bluetooth L2CAP channel");
    return true;
  }

  void command_complete(u16 opcode, std::span<const u8> response) {
    std::vector<u8> packet{0x0e, u8(3 + response.size()), 1, u8(opcode), u8(opcode >> 8)};
    packet.insert(packet.end(), response.begin(), response.end());
    hci_events.push_back(std::move(packet));
    deliver_hci();
  }

  void hci_command(u32 address, std::span<const u8> packet) {
    if (packet.size() < 3 || packet.size() != std::size_t(packet[2]) + 3)
      unsupported(address, "malformed Bluetooth HCI command");
    const u16 opcode = u16(u16(packet[0]) | (u16(packet[1]) << 8));
    const auto parameters = packet.subspan(3);
    std::vector<u8> response{0};
    const auto require_size = [&](std::size_t size) {
      if (parameters.size() != size)
        unsupported(address, "Bluetooth HCI parameter size " + std::to_string(opcode));
    };
    switch (opcode) {
    case 0xfc4c:
    case 0xfc4f:
      // These named vendor transports have no side effects in the pinned HCI
      // profile. Patch bytes are not executed; unknown vendor commands still stop.
      break;
    case 0x0401: {
      require_size(5);
      command_status(opcode);
      const auto found = std::find_if(remotes.begin(), remotes.end(), [](const auto &remote) {
        return remote.state().connected && !remote.linked();
      });
      const u8 count = u8(found == remotes.end() ? 0 : 1);
      if (count != 0) {
        const auto peer = peer_address(unsigned(found - remotes.begin()));
        std::vector<u8> data{count};
        data.insert(data.end(), peer.begin(), peer.end());
        data.insert(data.end(), {1, 0, 0, 0, 4, 0x48, 0x18, 0x38});
        hci_event(2, data);
      }
      const std::array<u8, 2> complete{0, count};
      hci_event(1, complete);
      return;
    }
    case 0x0402:
      require_size(0);
      // Inquiry produces its result immediately in this controller profile.
      break;
    case 0x0405:
    case 0x0409: {
      require_size(opcode == 0x0405 ? 13 : 7);
      command_status(opcode);
      const auto index = peer_index(parameters);
      const bool accepting =
          index && remotes[*index].state().connected && !remotes[*index].linked();
      if (opcode == 0x0409 && parameters[6] == 0 && accepting) {
        std::vector<u8> role{0};
        role.insert(role.end(), parameters.begin(), parameters.begin() + 6);
        role.push_back(0);
        hci_event(0x12, role);
      }
      connection_complete(parameters.first(6), true, opcode == 0x0409);
      return;
    }
    case 0x0406: {
      require_size(3);
      const u16 handle = little16(parameters, 0);
      if (handle < 0x100 || handle >= 0x100 + bluetooth_peers)
        unsupported(address, "Bluetooth disconnect for an unknown peer");
      command_status(opcode);
      disconnect(handle - 0x100, parameters[2], true);
      return;
    }
    case 0x040f: {
      require_size(4);
      const u16 handle = little16(parameters, 0);
      if (handle < 0x100 || handle >= 0x100 + bluetooth_peers)
        unsupported(address, "Bluetooth packet type change for an unknown peer");
      command_status(opcode);
      const std::array<u8, 5> changed{0, parameters[0], parameters[1], parameters[2],
                                      parameters[3]};
      hci_event(0x1d, changed);
      return;
    }
    case 0x040b:
    case 0x040c:
      require_size(opcode == 0x040b ? 22 : 6);
      response.insert(response.end(), parameters.begin(), parameters.begin() + 6);
      break;
    case 0x0419: {
      require_size(10);
      command_status(opcode);
      std::vector<u8> data{0};
      data.insert(data.end(), parameters.begin(), parameters.begin() + 6);
      const auto index = peer_index(parameters);
      const std::string name =
          index && *index == remote_ports ? "Nintendo RVL-WBC-01" : "Nintendo RVL-CNT-01";
      data.insert(data.end(), name.begin(), name.end());
      data.resize(255, u8(0));
      if (!index)
        data[0] = 2;
      hci_event(7, data);
      return;
    }
    case 0x041b:
    case 0x041d:
    case 0x041f:
    case 0x0411: {
      require_size(2);
      command_status(opcode);
      const u16 handle = little16(parameters, 0);
      const bool present =
          handle >= 0x100 && handle < 0x100 + bluetooth_peers && remotes[handle - 0x100].linked();
      std::vector<u8> data{u8(present ? 0 : 2), parameters[0], parameters[1]};
      if (opcode == 0x041b) {
        data.insert(data.end(), {0xbc, 2, 4, 0x38, 8, 0, 0, 0});
        hci_event(0x0b, data);
      } else if (opcode == 0x041d) {
        data.insert(data.end(), {2, 0x0f, 0, 0x29, 2});
        hci_event(0x0c, data);
      } else if (opcode == 0x041f) {
        data.insert(data.end(), {0x18, 0x38});
        hci_event(0x1c, data);
      } else
        hci_event(6, data);
      return;
    }
    case 0x0c03:
      require_size(0);
      // The pinned controller profile acknowledges reset without changing links or settings.
      break;
    case 0x0c05:
      // Inquiry is immediate in this profile; its named filter command is acknowledged unused.
      break;
    case 0x0803: {
      require_size(10);
      const u16 handle = little16(parameters, 0);
      if (handle < 0x100 || handle >= 0x100 + bluetooth_peers)
        unsupported(address, "Bluetooth sniff mode for an unknown peer");
      command_status(opcode);
      const std::array<u8, 6> changed{0, parameters[0], parameters[1],
                                      2, parameters[2], parameters[3]};
      hci_event(0x14, changed);
      return;
    }
    case 0x080d:
      require_size(4);
      command_status(opcode);
      return;
    case 0x0c0d: {
      require_size(7);
      if (parameters[6] != 1)
        unsupported(address, "Bluetooth single-address stored key query");
      std::vector<u8> keys{u8(bluetooth_peers)};
      for (unsigned peer = 0; peer < bluetooth_peers; ++peer) {
        const auto identity = peer_address(peer);
        keys.insert(keys.end(), identity.begin(), identity.end());
        keys.insert(keys.end(), 16, u8(0xa0 + peer));
      }
      hci_event(0x15, keys);
      append_little(response, 255);
      append_little(response, bluetooth_peers);
      break;
    }
    case 0x0c12:
      require_size(7);
      if (!peer_index(parameters))
        unsupported(address, "Bluetooth stored key deletion for an unknown peer");
      // The controller profile's preset keys remain registered; none are removed.
      append_little(response, 0);
      break;
    case 0x1001:
      require_size(0);
      response.insert(response.end(), {3, 0xa7, 0x40, 3, 0x0f, 0, 0x0e, 0x43});
      break;
    case 0x1003:
      require_size(0);
      response.insert(response.end(), {0xff, 0xff, 0x8d, 0xfe, 0x9b, 0xf9, 0, 0x80});
      break;
    case 0x1005:
      require_size(0);
      response.insert(response.end(), {0x53, 1, 0x40, 10, 0, 0, 0});
      break;
    case 0x1009:
      require_size(0);
      response.insert(response.end(), {0x11, 2, 0x19, 0x79, 0, 0xff});
      break;
    case 0x0c19:
      require_size(0);
      response.push_back(scan_enable);
      break;
    case 0x0c1a:
      require_size(1);
      if (parameters[0] > 3)
        response[0] = 0x12;
      else
        scan_enable = parameters[0];
      if (scan_enable & 2)
        for (unsigned index = 0; index < remotes.size(); ++index)
          schedule(Kind::Connection, 0, index);
      break;
    case 0x0c13:
      require_size(248);
      hci_settings[opcode] = {parameters.begin(), parameters.end()};
      break;
    case 0x0c0a:
    case 0x0c43:
    case 0x0c45:
    case 0x0c47:
      require_size(1);
      hci_settings[opcode] = {parameters.begin(), parameters.end()};
      break;
    case 0x0c18:
      require_size(2);
      hci_settings[opcode] = {parameters.begin(), parameters.end()};
      break;
    case 0x0c24:
      require_size(3);
      hci_settings[opcode] = {parameters.begin(), parameters.end()};
      break;
    case 0x0c33:
      require_size(7);
      hci_settings[opcode] = {parameters.begin(), parameters.end()};
      break;
    case 0x0c37:
      require_size(4);
      response.insert(response.end(), parameters.begin(), parameters.begin() + 2);
      break;
    default:
      unsupported(address, "Bluetooth HCI command " + std::to_string(opcode));
    }
    command_complete(opcode, response);
  }

  std::optional<Reply> bluetooth_ioctlv(u32 address) {
    const auto parsed = vectors(address);
    if (!parsed)
      return Reply{invalid};
    const u32 operation = memory.read32(address + 12);
    const auto &in = parsed->input, &out = parsed->output;
    if (operation == 0) {
      if (!parsed->count(6, 1) || in[0].size < 1 || in[1].size < 1 || in[2].size < 2 ||
          in[3].size < 2 || in[4].size < 2 || in[5].size < 1)
        return Reply{invalid};
      const u16 encoded = memory.read16(in[4].address);
      const u16 length = u16((encoded >> 8) | (encoded << 8));
      if (length != out[0].size)
        return Reply{invalid};
      if (memory.read8(in[0].address) != 0x20 || memory.read8(in[1].address) != 0)
        unsupported(address, "Bluetooth USB control request");
      hci_command(address, memory.range(out[0].address, length));
      return Reply{length};
    }
    if (operation == 2) {
      if (!parsed->count(2, 1) || in[0].size < 1 || in[1].size < 2 ||
          memory.read16(in[1].address) != out[0].size)
        return Reply{invalid};
      if (memory.read8(in[0].address) != 0x81)
        unsupported(address, "Bluetooth USB interrupt endpoint");
      if (hci_endpoint)
        unsupported(address, "Bluetooth interrupt request replaced while pending");
      hci_endpoint = UsbEndpoint{address, out[0].address, out[0].size};
      deliver_hci();
      return std::nullopt;
    }
    if (operation == 1) {
      if (!parsed->count(2, 1) || in[0].size < 1 || in[1].size < 2 ||
          memory.read16(in[1].address) != out[0].size)
        return Reply{invalid};
      const u8 endpoint = memory.read8(in[0].address);
      if (endpoint == 0x82) {
        if (acl_endpoint)
          unsupported(address, "Bluetooth ACL request replaced while pending");
        acl_endpoint = UsbEndpoint{address, out[0].address, out[0].size};
        deliver_acl();
        return std::nullopt;
      }
      if (endpoint == 2)
        return Reply{receive_acl(address, memory.range(out[0].address, out[0].size)) ? 0 : invalid};
      unsupported(address, "Bluetooth USB bulk endpoint");
    }
    unsupported(address, "Bluetooth USB ioctlv " + std::to_string(operation));
  }

  void execute(u32 address) {
    const u32 command = memory.read32(address), fd = memory.read32(address + 8);
    Reply result{invalid};
    if (command < 1 || command > 7)
      result = {invalid, 978 * tb_ratio};
    else if (command == 1)
      result = open(address);
    else if (fd >= handles.size() || !handles[fd])
      result = {invalid, 550 * tb_ratio};
    else if (command == 2) {
      u64 delay = handles[fd]->device == Device::File || handles[fd]->device == Device::Filesystem
                      ? fs_overhead
                      : 4000 * tb_ratio;
      if (cache_fd == fd) {
        delay += flush_cache();
        cache_fd.reset();
      }
      if (handles[fd]->flush)
        delay += superblock_ticks();
      if (handles[fd]->device == Device::Bluetooth) {
        if (--bluetooth_opens == 0)
          ++bluetooth_generation;
        hci_endpoint.reset();
        acl_endpoint.reset();
        scan_enable = 0;
        received_packets.fill(0);
      }
      handles[fd].reset();
      result = {0, delay};
    } else if (handles[fd]->device == Device::Bluetooth) {
      if (command != 7)
        return enqueue_reply(address, Reply{invalid});
      const auto bluetooth_result = bluetooth_ioctlv(address);
      if (!bluetooth_result)
        return;
      result = *bluetooth_result;
    } else if (handles[fd]->device == Device::KD || handles[fd]->device == Device::KDTime) {
      if (command != 6)
        return enqueue_reply(address, Reply{invalid});
      result = kd_ioctl(handles[fd]->device, address);
    } else if (handles[fd]->device == Device::StmEvent ||
               handles[fd]->device == Device::StmImmediate) {
      if (command != 6)
        return enqueue_reply(address, Reply{invalid});
      const auto stm_result = stm_ioctl(fd, address);
      if (!stm_result)
        return;
      result = *stm_result;
    } else if (handles[fd]->device == Device::ES) {
      if (command != 7)
        return enqueue_reply(address, Reply{invalid});
      result = es_ioctlv(address);
    } else if (handles[fd]->device == Device::Disc) {
      if (command == 7)
        return enqueue_reply(address, disc_ioctlv(address));
      if (command != 6)
        unsupported(address, "disc service command " + std::to_string(command));
      disc_requests.push_back(address);
      if (!disc_busy)
        start_disc();
      return;
    } else if (command == 3 || command == 4 || command == 5)
      result = file(fd, address, command);
    else if (command == 6)
      result = fs_ioctl(fd, address);
    else
      result = fs_ioctlv(fd, address);
    enqueue_reply(address, result);
  }

  void pump() {
    if (!ready())
      return;
    if (!requests.empty()) {
      const u32 address = requests.front();
      requests.pop_front();
      control = (control & ~1u) | 2;
      schedule(Kind::Interrupt, 100 * tb_ratio);
      execute(address);
    } else if (!replies.empty()) {
      arm_message = replies.front();
      replies.pop_front();
      control |= 4;
      schedule(Kind::Interrupt, 100 * tb_ratio);
    }
  }

  void start_disc() {
    const u32 address = disc_requests.front();
    disc_requests.pop_front();
    disc_busy = true;
    const u32 operation = memory.read32(address + 12), input = memory.read32(address + 16);
    const u32 input_size = memory.read32(address + 20), output = memory.read32(address + 24);
    const u32 output_size = memory.read32(address + 28);
    Event event{now + fs_overhead, serial++, Kind::Disc, address, output, 1, {}};
    if (input_size != 32)
      event.result = 32;
    else if (operation == 0x70 || operation == 0x71 || operation == 0x8d || operation == 0x12) {
      const u32 length = operation == 0x70 || operation == 0x12 ? 32 : memory.read32(input + 4);
      const u32 position = operation == 0x70 || operation == 0x12 ? 0 : memory.read32(input + 8);
      if (operation == 0x71 && (!partition_open || !owner.partition_read))
        event.result = 32;
      else if (output_size < length)
        event.result = operation == 0x71 ? 32 : 16;
      else if ((length & 31) != 0 || (output & 31) != 0)
        event.result = 128;
      else if (length > 0x04000000)
        event.result = 128;
      else {
        const u64 end = u64(position) + length / 4;
        const bool system = position <= 0x14000 && end <= 0x14000;
        const bool probe1 = position >= 0x460a0000 && end <= 0x460a0008;
        const bool probe2 = position >= 0x7ed40000 && end <= 0x7ed40008;
        if (operation == 0x8d && !system && !probe1 && !probe2)
          event.result = 32;
        else {
          event.due = now + 300 * (devices.ticks_per_second() / 1000000);
          event.data.resize(length);
          last_length = operation == 0x71 ? position : length;
          try {
            if (operation == 0x12) {
              event.data[3] = 2;
              event.data[4] = 0x20;
              event.data[5] = 6;
              event.data[6] = 5;
              event.data[7] = 0x26;
              event.data[8] = 0x41;
            } else {
              const auto &reader = operation == 0x71 ? owner.partition_read : owner.disc_read;
              if (!reader) {
                event.result = 2;
                disc_error = 0x23a00;
                event.data.clear();
              } else
                reader(u64(position) << 2, event.data);
            }
          } catch (const std::out_of_range &) {
            event.result = 2;
            disc_error = 0x52100;
            event.data.clear();
          } catch (const std::exception &) {
            event.result = 2;
            disc_error = 0x31100;
            event.data.clear();
          }
        }
      }
    } else if (operation == 0x83 || operation == 0x84 || operation == 0x88 || operation == 0xe0 ||
               operation == 0x7a || operation == 0x95 || operation == 0x96) {
      if (output_size < 4)
        event.result = 32;
      else {
        u32 value = 0;
        if (operation == 0x83)
          value = last_length;
        else if (operation == 0x88)
          value = owner.disc_read ? 2 : 1;
        else if (operation == 0xe0) {
          value = disc_error;
          disc_error = 0;
        } else if (operation == 0x7a)
          value = devices.read(0x0c006004, 4);
        else if (operation == 0x95)
          value = devices.read(0x0c006000, 4);
        else if (operation == 0x96)
          value = devices.read(0x0c00601c, 4);
        else
          value = devices.read(0x0c006020, 4);
        event.data = {u8(value >> 24), u8(value >> 16), u8(value >> 8), u8(value)};
      }
    } else if (operation == 0x8c)
      partition_open = false;
    else if (operation == 0x7e || operation == 0x8a) {
      const bool spinup = operation == 0x7e || memory.read32(input + 4) != 0;
      devices.reset_disc(spinup);
      devices.write(0x0c006000, 0x54, 4);
      devices.write(0x0c006004, 4, 4);
      disc_error = 0;
    } else if (operation == 0x85 || operation == 0x86 || operation == 0x89) {
      u32 cover = devices.read(0x0c006004, 4);
      if (operation == 0x85)
        cover &= ~2u;
      if (operation == 0x89)
        cover |= 2;
      devices.write(0x0c006004, cover & (operation == 0x86 ? 6u : 2u), 4);
    } else if (operation == 0x7f)
      event.result = 128;
    else if (operation == 0xa4) {
      event.result = 2;
      disc_error = 0x52000;
    } else if (operation == 0x8b || operation == 0x8e || (operation >= 0x90 && operation <= 0x94))
      event.result = 32;
    else
      unsupported(address, "disc ioctl " + std::to_string(operation));
    events.push(std::move(event));
  }
};

Ipc::Ipc(Memory &memory, hw::Devices &devices, Console console)
    : impl_(std::make_unique<Impl>(*this, memory, devices, console)) {
  if (console == Console::Wii) {
    devices.ipc_read = [this](u32 address) { return read(address); };
    devices.ipc_write = [this](u32 address, u32 value) { write(address, value); };
  }
}

Ipc::~Ipc() {
  if (impl_->console == Console::Wii) {
    impl_->devices.ipc_read = {};
    impl_->devices.ipc_write = {};
  }
}

void Ipc::set_user_directory(const std::filesystem::path &directory) {
  auto target = std::filesystem::absolute(directory / "wii");
  std::filesystem::create_directories(target);
  impl_->root = std::filesystem::canonical(target);
  impl_->load_metadata();
  impl_->load_uids();
  impl_->save_title_metadata();
  impl_->network.reset_storage();
}

void Ipc::set_version(u32 version) {
  impl_->version = version;
}

void Ipc::set_partition_open(bool open) {
  impl_->partition_open = open;
}

void Ipc::set_shutdown_handler(std::function<void()> handler) {
  impl_->shutdown_handler = std::move(handler);
}

void Ipc::press_reset_button() {
  impl_->stm_event(0x20000);
}

void Ipc::press_power_button() {
  impl_->stm_event(0x800);
}

void Ipc::set_remote(unsigned index, const hw::RemoteState &state) {
  if (index >= remote_ports)
    throw std::out_of_range("Wii Remote index exceeds four ports");
  const bool was_available = impl_->remotes[index].state().connected;
  impl_->remotes[index].set_state(state);
  if (!was_available && state.connected) {
    impl_->request_pending[index] = true;
    impl_->connection_delay[index] = 200;
    impl_->schedule(Impl::Kind::Connection, 0, index);
  } else if (was_available && !state.connected) {
    impl_->disconnect(index, 0x13);
    impl_->request_pending[index] = false;
  }
}

void Ipc::set_remote_rumble_handler(std::function<void(unsigned, bool)> handler) {
  impl_->remote_rumble = std::move(handler);
  for (unsigned index = 0; index < impl_->remotes.size(); ++index)
    impl_->remotes[index].rumble = [this, index](bool active) {
      if (impl_->remote_rumble)
        impl_->remote_rumble(index, active);
    };
}

bool Ipc::set_title_context(std::span<const u8> ticket, std::span<const u8> tmd,
                            u64 partition_offset) {
  auto &state = *impl_;
  state.title_ticket.clear();
  state.title_tmd.clear();
  state.title_id = 0;
  if (!valid_ticket(ticket) || !valid_tmd(tmd) || big64(ticket, 0x1dc) != big64(tmd, 0x18c))
    return false;
  state.title_ticket.assign(ticket.begin(), ticket.end());
  state.title_tmd.assign(tmd.begin(), tmd.end());
  state.title_id = big64(tmd, 0x18c);
  state.selected_partition = partition_offset;
  state.save_title_metadata();
  return true;
}

u64 Ipc::cycles() const {
  return impl_->now;
}

u32 Ipc::read(u32 address) {
  auto &state = *impl_;
  switch (address & 0x1ff) {
  case 4:
    return state.control;
  case 8:
    return state.arm_message;
  case 0x64:
    return 0xffffffff;
  case 0xc0:
  case 0xe0:
    return state.gpio_out;
  case 0xc4:
  case 0xe4:
    return state.gpio_dir;
  case 0xc8:
  case 0xe8:
    return disc_read ? 0x80 : 0;
  case 0x194:
    return state.resets;
  case 0x180:
  case 0x1cc:
  case 0x1d0:
    return 0;
  case 0:
  case 0x18:
  case 0x24:
  case 0x30:
  case 0x34:
    throw BusFault(address, false, "Write-only IPC register read");
  default:
    unsupported(address, "IPC register read");
  }
}

void Ipc::write(u32 address, u32 value) {
  auto &state = *impl_;
  constexpr u32 gpio_owner = 0xc3a0;
  switch (address & 0x1ff) {
  case 0:
    state.ppc_message = value;
    break;
  case 4:
    state.control = (state.control & 6 & ~value) | (value & 0x39);
    if (((value & 4) && (state.control & 16)) || ((value & 2) && (state.control & 32)))
      state.flags |= irq_ipc;
    if (state.control & 1)
      state.schedule(Impl::Kind::Request, 500 * tb_ratio, state.ppc_message);
    state.pump();
    state.interrupts();
    break;
  case 0x30:
    state.flags &= ~value;
    state.pump();
    state.interrupts();
    break;
  case 0x34:
    state.mask = value;
    if (value & irq_ipc) {
      state.ppc_message = state.arm_message = state.flags = state.control = 0;
      state.mask = irq_ipc;
      state.gpio_out = 0;
      state.gpio_dir = 0xffdf3f;
      state.resets = 0xffffffff;
    }
    state.pump();
    state.interrupts();
    break;
  case 0xc0:
  case 0xe0:
    if ((value & 0x200) != 0)
      unsupported(address, "disc eject through GPIO");
    state.gpio_out = (address & 0x1ff) == 0xc0
                         ? (value & gpio_owner) | (state.gpio_out & ~gpio_owner)
                         : (value & ~gpio_owner) | (state.gpio_out & gpio_owner);
    break;
  case 0xc4:
  case 0xe4:
    state.gpio_dir = (address & 0x1ff) == 0xc4
                         ? (value & gpio_owner) | (state.gpio_dir & ~gpio_owner)
                         : (value & ~gpio_owner) | (state.gpio_dir & gpio_owner);
    break;
  case 0x194:
    if ((state.resets & 0x400) && !(value & 0x400))
      state.devices.reset_disc(!(state.gpio_out & 16));
    state.resets = value;
    break;
  case 0xc8:
  case 0xe8:
  case 0x18:
  case 0x24:
  case 0x180:
  case 0x1cc:
  case 0x1d0:
    break;
  case 8:
  case 0x64:
    throw BusFault(address, true, "Read-only IPC register write");
  default:
    unsupported(address, "IPC register write");
  }
}

void Ipc::advance(u64 cycles) {
  auto &state = *impl_;
  if (cycles > std::numeric_limits<u64>::max() - state.now)
    throw std::overflow_error("IPC clock overflow");
  const u64 end = state.now + cycles;
  while (!state.events.empty() && state.events.top().due <= end) {
    auto event = state.events.top();
    state.events.pop();
    state.now = event.due;
    if (event.kind == Impl::Kind::Request) {
      state.requests.push_back(event.address);
      state.pump();
    } else if (event.kind == Impl::Kind::Reply) {
      state.replies.push_back(event.address);
      state.pump();
    } else if (event.kind == Impl::Kind::Interrupt)
      state.interrupts();
    else if (event.kind == Impl::Kind::Bluetooth) {
      if (state.bluetooth_opens != 0 && event.address == state.bluetooth_generation) {
        state.bluetooth_update();
        state.schedule(Impl::Kind::Bluetooth, state.devices.ticks_per_second() / 200,
                       state.bluetooth_generation);
      }
    } else if (event.kind == Impl::Kind::Connection) {
      state.connection_request(event.address);
    } else {
      if (!event.data.empty())
        state.memory.copy_in(event.buffer, event.data);
      state.enqueue_reply(event.address, Reply{event.result}, false);
      state.disc_busy = false;
      if (!state.disc_requests.empty())
        state.start_disc();
    }
  }
  state.now = end;
}
} // namespace cupid::ios
