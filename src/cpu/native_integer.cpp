// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/native_integer.hpp"
#include "cupid/cpu/interpreter.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#define CUPID_NATIVE_X64 1
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>
#endif
#else
#define CUPID_NATIVE_X64 0
#endif

namespace cupid::cpu {
namespace {
bool eligible(u32 opcode) {
  const unsigned primary = opcode >> 26;
  return primary == 14 || primary == 15 || primary == 20 || primary == 21 || primary == 23 ||
         (primary >= 24 && primary <= 29);
}

#if CUPID_NATIVE_X64
static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>);
using Operation = void (*)(State *);

class Encoder {
public:
  void bytes(std::initializer_list<u8> values) {
    for (u8 value : values)
      byte(value);
  }

  void byte(u8 value) {
    if (size_ == storage_.size())
      throw std::logic_error("Native integer operation exceeds its encoding bound");
    storage_[size_++] = value;
  }

  void word(u32 value) {
    for (unsigned n = 0; n < 4; ++n)
      byte(static_cast<u8>(value >> (n * 8)));
  }

  void memory(u8 instruction, unsigned reg, std::size_t displacement) {
    // R10 holds State; all emitted scratch registers are volatile in both ABIs.
    bytes({0x41, instruction});
    if (displacement <= 127) {
      byte(static_cast<u8>(0x42 | (reg << 3)));
      byte(static_cast<u8>(displacement));
    } else {
      byte(static_cast<u8>(0x82 | (reg << 3)));
      word(static_cast<u32>(displacement));
    }
  }

  void load(unsigned reg, unsigned guest) {
    memory(0x8b, reg, offsetof(State, gpr) + guest * sizeof(u32));
  }

  void store(unsigned guest) {
    memory(0x89, 0, offsetof(State, gpr) + guest * sizeof(u32));
  }

  void accumulator(u8 instruction, u32 immediate) {
    byte(instruction);
    word(immediate);
  }

  void record() {
    // The destination has already been stored. TEST classifies the full signed
    // result; SO is read independently from XER, and CR1 through CR7 are retained.
    bytes({0x31, 0xd2, 0x45, 0x31, 0xc0, 0x85, 0xc0}); // clear EDX/R8D; test EAX
    bytes({0x0f, 0x94, 0xc2, 0x41, 0x0f, 0x98, 0xc0}); // EQ in DL; LT in R8B
    bytes({0x0f, 0x9f, 0xc0, 0x0f, 0xb6, 0xc0});       // GT in EAX
    bytes({0xc1, 0xe0, 2, 0xc1, 0xe2, 1, 0x41, 0xc1, 0xe0, 3});
    bytes({0x09, 0xc2, 0x44, 0x09, 0xc2});
    memory(0x8b, 0, offsetof(State, spr) + spr::xer * sizeof(u32));
    bytes({0xc1, 0xe8, 31, 0x09, 0xc2, 0xc1, 0xe2, 28});
    memory(0x8b, 0, offsetof(State, cr));
    accumulator(0x25, 0x0fffffff);
    bytes({0x09, 0xd0});
    memory(0x89, 0, offsetof(State, cr));
  }

  std::span<const u8> code() const {
    return std::span(storage_).first(size_);
  }

private:
  std::array<u8, 128> storage_{};
  std::size_t size_ = 0;
};

Encoder encode(u32 opcode) {
  Encoder code;
  code.bytes({0xf3, 0x0f, 0x1e, 0xfa}); // ENDBR64 also permits indirect entry with CET.
#ifdef _WIN32
  code.bytes({0x49, 0x89, 0xca}); // RCX -> R10
#else
  code.bytes({0x49, 0x89, 0xfa}); // RDI -> R10
#endif
  const unsigned primary = opcode >> 26, source = (opcode >> 21) & 31;
  const unsigned address = (opcode >> 16) & 31, count = (opcode >> 11) & 31;
  bool record = false;
  if (primary == 14 || primary == 15) {
    const u32 immediate = static_cast<u32>(static_cast<s32>(static_cast<s16>(opcode)))
                          << (primary == 15 ? 16 : 0);
    if (address == 0)
      code.accumulator(0xb8, immediate);
    else {
      code.load(0, address);
      code.accumulator(0x05, immediate);
    }
    code.store(source);
  } else if (primary >= 24 && primary <= 29) {
    const u32 immediate = (opcode & 0xffff) << ((primary & 1) ? 16 : 0);
    code.load(0, source);
    code.accumulator(primary < 26 ? 0x0d : primary < 28 ? 0x35 : 0x25, immediate);
    code.store(address);
    record = primary >= 28;
  } else {
    const unsigned begin = (opcode >> 6) & 31, end = (opcode >> 1) & 31;
    const u32 from_begin = 0xffffffffu >> begin, through_end = 0xffffffffu << (31 - end);
    const u32 mask = begin <= end ? from_begin & through_end : from_begin | through_end;
    code.load(0, source);
    if (primary == 23) {
      code.load(1, count);
      code.bytes({0xd3, 0xc0}); // ROL EAX, CL masks its count to five bits.
    } else if (count != 0) {
      code.bytes({0xc1, 0xc0, static_cast<u8>(count)});
    }
    code.accumulator(0x25, mask);
    if (primary == 20) {
      code.load(2, address);
      code.bytes({0x81, 0xe2});
      code.word(~mask);
      code.bytes({0x09, 0xd0});
    }
    code.store(address);
    record = (opcode & 1) != 0;
  }
  if (record)
    code.record();
  code.byte(0xc3);
  return code;
}

class ExecutablePage {
public:
  explicit ExecutablePage(std::size_t size) : size_(size) {
#ifdef _WIN32
    memory_ =
        static_cast<u8 *>(VirtualAlloc(nullptr, size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory_)
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native code allocation failed");
#else
    void *mapping =
        mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
      throw std::system_error(errno, std::generic_category(), "Native code allocation failed");
    memory_ = static_cast<u8 *>(mapping);
#endif
  }

  ~ExecutablePage() {
#ifdef _WIN32
    (void)VirtualFree(memory_, 0, MEM_RELEASE);
#else
    (void)munmap(memory_, size_);
#endif
  }

  ExecutablePage(const ExecutablePage &) = delete;
  ExecutablePage &operator=(const ExecutablePage &) = delete;

  bool fits(std::size_t bytes) const {
    return bytes <= size_ - used_;
  }

  Operation append(std::span<const u8> code) {
    if (!fits(code.size()))
      throw std::logic_error("Native code page capacity exceeded");
    // Emitted operations make no calls. Compilation runs on the owning CPU
    // thread between operations, so an existing page is never executing here.
    if (sealed_)
      protect(false);
    std::memcpy(memory_ + used_, code.data(), code.size());
    protect(true);
#ifdef _WIN32
    if (!FlushInstructionCache(GetCurrentProcess(), memory_ + used_, code.size()))
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native instruction-cache flush failed");
#else
    __builtin___clear_cache(reinterpret_cast<char *>(memory_ + used_),
                            reinterpret_cast<char *>(memory_ + used_ + code.size()));
#endif
    const auto operation = reinterpret_cast<Operation>(memory_ + used_);
    used_ += code.size();
    return operation;
  }

private:
  void protect(bool executable) {
#ifdef _WIN32
    DWORD previous = 0;
    if (!VirtualProtect(memory_, size_, executable ? PAGE_EXECUTE_READ : PAGE_READWRITE, &previous))
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                              "Native code page protection failed");
#else
    if (mprotect(memory_, size_, PROT_READ | (executable ? PROT_EXEC : PROT_WRITE)) != 0)
      throw std::system_error(errno, std::generic_category(), "Native code page protection failed");
#endif
    sealed_ = executable;
  }

  u8 *memory_ = nullptr;
  std::size_t size_ = 0, used_ = 0;
  bool sealed_ = false;
};

std::size_t page_size() {
#ifdef _WIN32
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  const long size = sysconf(_SC_PAGESIZE);
  if (size <= 0)
    throw std::runtime_error("Native code page size is unavailable");
  return static_cast<std::size_t>(size);
#endif
}
#endif
} // namespace

struct NativeInteger::Impl {
  explicit Impl(unsigned limit) : capacity(limit) {
#if CUPID_NATIVE_X64
    operations.reserve(capacity);
#endif
  }

  unsigned capacity;
#if CUPID_NATIVE_X64
  std::unordered_map<u32, Operation> operations;
  std::vector<std::unique_ptr<ExecutablePage>> pages;
#endif
};

bool NativeInteger::supported() noexcept {
  return CUPID_NATIVE_X64 != 0;
}

NativeInteger::NativeInteger(unsigned capacity) {
  if (!supported())
    throw std::runtime_error("Native integer execution requires an x86-64 host");
  if (capacity == 0 || capacity > 4096)
    throw std::invalid_argument("Native integer cache capacity must be between 1 and 4096");
  impl_ = std::make_unique<Impl>(capacity);
}

NativeInteger::~NativeInteger() = default;

bool NativeInteger::execute_other(State &state, u32 opcode) {
  if (!eligible(opcode)) {
    ++statistics_.fallback;
    return false;
  }
#if CUPID_NATIVE_X64
  auto &hint = hints_[(opcode ^ (opcode >> 11) ^ (opcode >> 21)) & 2047];
  Operation operation = nullptr;
  if (const auto found = impl_->operations.find(opcode); found != impl_->operations.end()) {
    operation = found->second;
    ++statistics_.cache_hits;
  } else {
    if (impl_->operations.size() == impl_->capacity) {
      ++statistics_.capacity_fallbacks;
      ++statistics_.fallback;
      return false;
    }
    const auto code = encode(opcode);
    const std::size_t page_bytes = page_size();
    if (impl_->pages.empty() || !impl_->pages.back()->fits(code.code().size()))
      impl_->pages.push_back(std::make_unique<ExecutablePage>(page_bytes));
    operation = impl_->pages.back()->append(code.code());
    impl_->operations.emplace(opcode, operation);
    ++statistics_.compiled_operations;
    statistics_.cache_entries = impl_->operations.size();
    statistics_.executable_bytes = impl_->pages.size() * page_bytes;
  }
  hint = {opcode, operation};
  invoke_operation(state, operation);
  ++statistics_.executed;
  return true;
#else
  (void)state;
  ++statistics_.fallback;
  return false;
#endif
}

void NativeInteger::reset() {
  impl_ = std::make_unique<Impl>(impl_->capacity);
  hints_ = {};
  statistics_ = {};
}

NativeIntegerStatistics NativeInteger::statistics() const noexcept {
  return statistics_;
}
} // namespace cupid::cpu
