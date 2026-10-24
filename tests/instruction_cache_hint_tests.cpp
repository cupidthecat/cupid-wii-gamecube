// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/cpu/cache.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

using namespace cupid;
using namespace cupid::cpu;

namespace {
class QueryBus final : public Bus {
public:
  std::shared_ptr<std::array<u8, 65536>> bytes = std::make_shared<std::array<u8, 65536>>();
  std::vector<std::array<u64, 3>> records;
  u32 key_xor = 0;

  u8 read8(u32 address) override {
    return (*bytes)[address & 65535];
  }

  void write8(u32 address, u8 value) override {
    (*bytes)[address & 65535] = value;
  }

  u32 instruction_cache_key(u32 physical) const override {
    return physical ^ key_xor;
  }

  bool read_instruction_words(u32 address, std::span<u32> words) override {
    records.push_back({0, address, words.size()});
    return Bus::read_instruction_words(address, words);
  }

  bool read_instruction_bytes(u32 address, std::span<u8> output) override {
    records.push_back({1, address, output.size()});
    return Bus::read_instruction_bytes(address, output);
  }
};

constexpr std::array<u32, 4> categories{0x38630001, 0x80640000, 0x4082fff0, 0};
u64 truth_cases = 0, continuation_reads = 0, digest = 1469598103934665603ull;

void line(QueryBus &bus, u32 address, const std::array<u32, 8> &words) {
  for (u32 word : words) {
    bus.write32(address, word);
    address += 4;
  }
}

void truth(const InstructionCache &cache, const QueryBus &bus, u32 offset) {
  const auto records = bus.records.size();
  for (unsigned minimum : {2u, 4u}) {
    const bool before = cache.recent_integer_count(offset) >= minimum ||
                        cache.recent_cache_flush_run(offset) ||
                        cache.register_branch_run_hint(offset) || cache.scalar_run_hint(offset);
    const bool after = cache.ordinary_run_hint(offset, minimum);
    CHECK(before == after);
    CHECK(bus.records.size() == records);
    digest = (digest ^ (u64{offset} << 2) ^ minimum ^ static_cast<u64>(after)) * 1099511628211ull;
    ++truth_cases;
  }
}

void offsets(const InstructionCache &cache, const QueryBus &bus, u32 address) {
  for (unsigned entry = 0; entry < 8; ++entry) {
    truth(cache, bus, address + entry * 4);
    truth(cache, bus, (address ^ 0x80001000u) + entry * 4); // Unrelated effective recent hint.
  }
}

void continuation(InstructionCache &queried, QueryBus &bus, u32 address) {
  // Copy before querying. The untouched cache follows the same real fetches,
  // including all replacement ways and locked/disabled/unaligned accesses.
  InstructionCache untouched = queried;
  QueryBus reference = bus;
  reference.records.clear();
  bus.records.clear();
  offsets(queried, bus, address);
  for (unsigned n = 0; n < 48; ++n) {
    const u32 at = address + (n % 12) * 4096 + ((n * 7) & 31);
    const bool enabled = n % 11 != 0, locked = n % 7 == 0;
    CHECK(queried.read(bus, at, enabled, locked) == untouched.read(reference, at, enabled, locked));
    CHECK(bus.records == reference.records);
    for (unsigned entry = 0; entry < 8; ++entry) {
      const auto key = bus.instruction_cache_key(address + entry * 4);
      const auto a = queried.inspect(key, address + entry * 4);
      const auto b = untouched.inspect(key, address + entry * 4);
      CHECK(a.integer_mask == b.integer_mask && a.scalar_mask == b.scalar_mask &&
            a.branch_mask == b.branch_mask && a.scalar_start_mask == b.scalar_start_mask);
      CHECK(std::equal(a.words.begin(), a.words.end(), b.words.begin(), b.words.end()));
    }
    ++continuation_reads;
  }
}

void exhaustive_line_categories() {
  QueryBus bus;
  for (u32 pattern = 0; pattern < 65536; ++pattern) {
    InstructionCache cache;
    std::array<u32, 8> current{}, previous{}, following{};
    for (unsigned n = 0; n < 8; ++n) {
      current[n] = categories[(pattern >> (n * 2)) & 3];
      previous[n] = categories[((pattern >> ((7 - n) * 2)) + n) & 3];
      following[n] = categories[((pattern >> (n * 2)) + pattern / 8192) & 3];
    }
    line(bus, 0x1000, current);
    line(bus, 0x0fe0, previous);
    line(bus, 0x1020, following);
    (void)cache.read(bus, 0x0fe0, true, false);
    (void)cache.read(bus, 0x1020, true, false);
    (void)cache.read(bus, 0x1000, true, false);
    offsets(cache, bus, 0x1000);
    if (!(pattern & 255))
      continuation(cache, bus, 0x1000);
    bus.records.clear();
  }
}

void boundary_tags_aliases_and_flush() {
  constexpr std::array<u32, 6> bases{0, 32, 0x1000, 0x7fffffe0, 0xffffffe0, 0xffffffc0};
  for (u32 address : bases)
    for (u32 alias : {0u, 4u, 28u, 0x10000u, 0x80000000u, 0xffffffe0u})
      for (unsigned prefix = 0; prefix <= 8; ++prefix)
        for (unsigned present = 0; present < 8; ++present) {
          QueryBus bus;
          bus.key_xor = alias;
          InstructionCache cache;
          std::array<u32, 8> current{}, previous{}, following{};
          current.fill(0x38630001);
          previous.fill(0x80640000);
          following.fill(0);
          if (prefix < 8)
            current[prefix] = 0x4082fff0;
          for (unsigned n = 0; n < prefix; ++n)
            following[n] = 0x38630001;
          if (prefix < 8)
            following[prefix] = 0x41820010;
          previous[6] = previous[7] = 0x38630001;
          line(bus, address, current);
          line(bus, address - 32, previous);
          line(bus, address + 32, following);
          if (present & 1)
            (void)cache.read(bus, address - 32, true, false);
          if (present & 2)
            (void)cache.read(bus, address + 32, true, false);
          if (present & 4)
            (void)cache.read(bus, address, true, false);
          offsets(cache, bus, address);
          InstructionCache copied(cache), assigned;
          assigned = cache;
          offsets(copied, bus, address);
          offsets(assigned, bus, address);
          assigned.invalidate(bus.instruction_cache_key(address));
          offsets(assigned, bus, address);
          copied.reset();
          offsets(copied, bus, address);
          if (present == 7)
            continuation(cache, bus, address);
        }
  for (unsigned flush = 0; flush < 8; ++flush) {
    QueryBus bus;
    InstructionCache cache;
    std::array<u32, 8> words{};
    words.fill(0x38630001);
    words[flush] = 0x7c0018ac;
    if (flush + 2 < words.size())
      words[flush + 2] = 0x4200fff8;
    line(bus, 0x1000, words);
    (void)cache.read(bus, 0x1000, true, false);
    offsets(cache, bus, 0x1000);
    continuation(cache, bus, 0x1000);
  }
}

void pressure_trace() {
  QueryBus bus;
  InstructionCache cache;
  u32 random = 0x7139a5cdu;
  for (unsigned n = 0; n < 4096; ++n) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    const u32 address = ((random & 15) * 4096 + ((random >> 8) & 127) * 32);
    std::array<u32, 8> words{};
    for (unsigned word = 0; word < 8; ++word)
      words[word] = categories[(random >> ((word * 3) % 30)) & 3];
    line(bus, address, words);
    if (!(n % 17))
      cache.invalidate(address);
    (void)cache.read(bus, address + ((random >> 24) & 7) * 4, true, n % 9 == 0);
    if (!(n % 13))
      cache.commit_hit(bus.instruction_cache_key(address - 4096));
    offsets(cache, bus, address ^ 0x1000);
    if (!(n % 127))
      continuation(cache, bus, address);
    bus.records.clear();
  }
}
} // namespace

int main() {
  return run_tests([] {
    InstructionCache empty;
    QueryBus bus;
    offsets(empty, bus, 0);
    exhaustive_line_categories();
    boundary_tags_aliases_and_flush();
    pressure_trace();
    std::cout << "Ordinary hint truth cases=" << truth_cases
              << " continuation fetches=" << continuation_reads << " digest=" << std::hex << digest
              << std::dec << '\n';
  });
}
