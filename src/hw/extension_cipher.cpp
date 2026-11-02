// SPDX-License-Identifier: GPL-2.0-or-later
// Protocol constants: Copyright (C) Hector Martin (2008), GPL-2.0-or-later.
#include "cupid/hw/extension_cipher.hpp"
#include <bit>
#include <string_view>

namespace cupid::hw {
namespace {
using Permutation = std::array<u8, 256>;

consteval unsigned hex_digit(char digit) {
  if (digit >= '0' && digit <= '9')
    return unsigned(digit - '0');
  if (digit >= 'a' && digit <= 'f')
    return unsigned(digit - 'a') + 10;
  throw "Invalid protocol permutation digit";
}

consteval Permutation permutation(std::string_view cycles) {
  Permutation result{};
  std::array<bool, 256> visited{};
  unsigned count = 0;
  std::size_t cursor = 0;
  while (cursor < cycles.size()) {
    unsigned first = 0, previous = 0;
    bool initial = true;
    while (cursor < cycles.size() && cycles[cursor] != '/') {
      if (cursor + 1 >= cycles.size())
        throw "Incomplete protocol permutation byte";
      const unsigned next = (hex_digit(cycles[cursor]) << 4) | hex_digit(cycles[cursor + 1]);
      cursor += 2;
      if (visited[next])
        throw "Repeated protocol permutation byte";
      visited[next] = true;
      ++count;
      if (initial)
        first = next;
      else
        result[previous] = u8(next);
      previous = next;
      initial = false;
    }
    if (initial || cursor == cycles.size())
      throw "Incomplete protocol permutation cycle";
    result[previous] = u8(first);
    ++cursor;
  }
  if (count != 256)
    throw "Incomplete protocol permutation";
  return result;
}

// Fixed protocol permutations, expressed as disjoint cycles.
constexpr auto nonce_substitution =
    permutation("0070/"
                "015191c62c84c074856409ccbb17317f41b357d7c7ff5846a28a25349ccb9083b5a60ef5813bba65"
                "14d5674d07eb944432af8e7d50eaf30f0b8798ce2ae7a9d42711d059bc244a105e2fa7b0533f12f8"
                "04404b667775b992ab62f6180c35fa71b86df19bd3c45de8691f064f54f9e46e43b652ca5a281ec1"
                "b1b70ad195999f7e973cb2020386ee15e23a9e7ad2d9fe42a1/"
                "050dbd33634e8fa85c619d7629daa51d7888d6738caa3879fd2b1afce93623e63dc9375b1b214cc5"
                "edef6fa39abf7213f2083e225519ad30e38dcddfd868dbdd5f6a2e962648892da08260aee0ecde93"
                "be454939c847e1/"
                "166c7c1cc3f720c2b4acf4dc6b7b56f08ba480e5fbcf/");
constexpr std::array<Permutation, 8> selector_substitutions{
    permutation("0001a0affe693e7990c7f01d741326ad70de6c615ed18a50b3ef0fbddd47b9f58df9ace7d93b284a"
                "9aed053f96abcb3794db4b4281ce0d1754c2f6f398111459d22cdf4946781c5be93ce4755a29e57e"
                "e29235a89d553202a995194f431691a284442e6fc08b4060386dfcff0c6603621e93c88ff76abfa5"
                "aab204d6c1d30b15720afac9ecd076/"
                "06857f6e521af8bc89d5d8e87dfdb8b4c5644171e06718/"
                "07a7eb7a0e09d4b5f1fba6512245e6208ce3cf/"
                "08b6c3dc1bb04cae80ba5f318e8327105d9b33cc5c633677be3088a173589c9ee17b538687a3daf4"
                "572dc6d71f99254e7c1234/"
                "21f28224ea972f9f48c44db73ab165bb23cda42aca563d39/"
                "2bee6b68/"),
    permutation("004c57d0d1b771c59e1f292d94f1c14e2fdfe03a5bae0b3d81685fb589e85261bd99ee045a1b43c8"
                "1762556c8378c3cd6dbaac6a2690cbd82bea16dbebe7bce510ffcca4a8a15dfed634d9cf951a7460"
                "3677c7efc408a219e13f220633274809ab9315e96756af7998ad5e3b5cde23e2f7391cdc38847ea7"
                "6b03078d59b43c518e8fd2d4be281e18e49f2c73f84296b2/"
                "014dbb403ea6d39ba3850549b869b34f5075a5e65370ca8802720a4682f37f92659c8bf2c27a3247"
                "7c12f5da2e548cb0e37bfbb1ddce448abfc67df914c031f6d52a1d862037c9fc0f9730256f4b4a45"
                "ec80aa918721f49a58fd3511f0b66e13fa9d664124ed76b9a90c63/"
                "0d/"
                "0ea0d764/"),
    permutation("00b9e0b5a4b234db0d69e414f46439d47e6ad88244181cffecb0453a173751359168e62b5704a2c0"
                "892cf70e237bd211defcf85295ca059c197d84a5/"
                "01da487fa7edef24789be56b5df3e2ddb4aea96cd381c5c3fe4bc162030c1ad580533175c24a7456"
                "4db832a6465b765f8349216542a06dd0f2e1c73c909d2e40cbe91dbde8ea941629b3559ad610c4ee"
                "12/"
                "02389987725e71fb3ece337c3b7aaf22be20cfccf63f63a8c6e3883d8df573bcac9247500bfd0f30"
                "8e4fbadcd78ad99f1558071fc979e754591ec8f1eb5c13d19e7785f96186368b1bbf8ca170962a26"
                "98a3df2fad2dfa25974cbb/"
                "0609b18f93414eb766aa43ab60f0cd27672808/"
                "0ab6/"
                "5a/"
                "6e/"
                "6f/"),
    permutation("00c04d124589437feadcb433916cb25d79d7d58b0dfebc6a4624a782324117804b7423d9c1e74f8d"
                "9c7e3f4252edd2063d1cb54a67f1eee85a59df441f388554c4ac905805495c97f3a13cbe2863eb6b"
                "9fe57216a9cb37217d36e4cfc3846295708660a4d1adecdbd0cea08c929df4e6bbe368575b4c0e2c"
                "deb114ab15488fef7cbfffcd7a1dba1bd31acc0c2fcab8a80ac92a4e87d6fb1904023050e9f9fd3b"
                "27b6942de0f298b30f2bf70710759377296966f5d8f0c8a2566155f85f18fcb7a564bdc6ae03f639"
                "da1ea3712201/"
                "086e092031aa132696fa8eb0af990ba6e1476f5134655373dd9b405e83b935886d81112e78c2257b"
                "d49e8a9ae2c7c5/"
                "3a/"
                "3e/"
                "76/"),
    permutation("007ca6c4465f424dc97e2925b791cfaa082057b3394965dfc16e06bfa14e1e876afbae21f3581454"
                "7186519e0511e1a2f14cf4a0ca83f79df0243cd201e31880ecd712099885c3c504b2dd28a37dd8bb"
                "7fd335d11734eac790926288de16bad4552667a92f47fcafe668adf9a769e5894abe5d0e6ccc0373"
                "1fa4301545821372ceb6c03b4be2dbe7383632e49cbd9bc8eef2b07a27b484fae9abb12b4f449a43"
                "cd56631050cb1c53645c8a5a401d2c076f8e233fdc0281b8d60b7576e0f65e195b7059e8972e1b3e"
                "a5fd8cc2b5ff9f93a866410defc6f522330afe95f8d91aed3a8d8beb2a606dd579778f48949961ac"
                "520c96d0/"
                "0fda31/"
                "2d6b743dbc/"
                "3778b9/"
                "7b/"),
    permutation("00285ec70bd6cfa9f31e0d3ca84c170530a241458cece224cd53d0b1e3ff2fc5fa76c9162e44af51"
                "fe135a788e1a9ed9fd32f10220f68b82ac5d94b673722d29846c03d73b3671bdae5588f04a89bf8a"
                "b44dd3f568c2b34264cce4236b39ef2aad57521d15b8c834c12254b977d4909210e90c/"
                "01b7ba31cebeeae0eba1e518a6dd47c69360de91d23e0a677af96dab9d3540276661a550fbca33aa"
                "1fe10fe74e490709c0da8f59/"
                "04b097e68514f8831b5c7d62d56a81/"
                "06c387639bc45643a3a45b2b/"
                "0819252111f4953dcbe87f1cd812/"
                "0e7edb9a7037864bdca7bb6974753f489f7cedd13afc65f79880df996e965f/"
                "2646b258bc/"
                "2c38b59cf24f796fa0ee/"
                "7b8d/"),
    permutation("00c35894f74403a4319b4b0f6789eaad3ce29385ca08347e784faa416ec17d6b2984866f07374e6c"
                "e971b89d38535714d3029a3f22f4689cd98c172f48a95963478720ebb42543b18ae5c2ff545d9eaf"
                "f890bc018204ba956624cf8e74acbfb569910d51f6/"
                "05818d1cccd68f7f567a79f11a2ab79975c6922c392ece5249d4cd3e36a10b80b6125f/"
                "0660464cd819b921bed764dba7f55aa0daf33361c855fb26b31627c53a77988b7662ee701eef2888"
                "e6/"
                "0935e723bda6a28350181310edbbab45b2d0fac40e65dc0ca81de1f0d5df6a97e44273fef25cb0f9"
                "a53272fd2bdd5ede0afc3dc9e35b11306d7be0/"
                "154a4d/"
                "1bd2c7d1a32d40/"
                "1fae/"
                "3b/"
                "7ce8eccb9f/"
                "96/"
                "c0/"),
    permutation("00965c0b0a0d358a575f2bbd43769fc926c675e38cf1736558e57b83acd4031f848e476d46fee010"
                "64d3bf4b308d682855c1c39c2289b919dd44b49790a216d0aedfefa60781e2a8d86ecaf61d71389d"
                "234ff7b6d712fc397e533b82c46bccba45c204edb5c752f23a7a49d90cdbf93788f44eea604a61ab"
                "3d13785b991e053306e1f8fdcbc5f054510f9abe02daeb36e4e82ce79eb1fb4d7c7086b84cb374f5"
                "01ad9bd58bf314aa93ceb0216a/"
                "086931bb980e77d6807d2dd2a162957909/"
                "11d15a2f723c27502992201767ee943442a491592a6fe6ff32ec87257fa93f1afa481c63a53e41c8"
                "a78fb2/"
                "151b6c/"
                "18a0/"
                "246656de/"
                "2e40af855ea3/"
                "5d/"
                "b7dcc0bce9cdcf/")};

struct ChallengeByte {
  u8 mix, rotate, subtract, final_xor;
};

constexpr std::array<ChallengeByte, 6> challenge_routes{
    {{5, 2, 9, 4}, {1, 0, 5, 7}, {6, 8, 2, 0}, {4, 7, 3, 2}, {1, 6, 3, 4}, {7, 8, 5, 9}}};
constexpr std::array<std::array<u8, 7>, 6> challenge_constants{{{168, 90, 143, 13, 32, 169, 48},
                                                                {119, 53, 183, 103, 118, 38, 126},
                                                                {166, 133, 26, 199, 55, 63, 144},
                                                                {224, 226, 98, 190, 143, 43, 14},
                                                                {247, 114, 135, 79, 104, 16, 133},
                                                                {67, 151, 56, 62, 183, 227, 10}}};

bool matches_selector(const std::array<u8, 10> &nonce, const std::array<u8, 6> &response,
                      unsigned selector) {
  std::array<u8, 10> substituted{};
  for (std::size_t i = 0; i < nonce.size(); ++i)
    substituted[i] = nonce_substitution[nonce[i]];
  for (std::size_t i = 0; i < response.size(); ++i) {
    const auto route = challenge_routes[i];
    const auto rotated = std::rotr(u8(challenge_constants[i][selector] ^ substituted[route.mix]),
                                   int(substituted[route.rotate]));
    const u8 expected = u8(u8(unsigned(rotated) - unsigned(substituted[route.subtract])) ^
                           substituted[route.final_xor]);
    if (response[i] != expected)
      return false;
  }
  return true;
}

ExtensionKey combine(const std::array<u8, 16> &material, unsigned left, unsigned right, u8 adjust) {
  // Material stores six response bytes followed by ten nonce bytes.
  constexpr std::array<u8, 8> subtract_left{4, 2, 5, 0, 1, 3, 6, 7};
  constexpr std::array<u8, 8> subtract_right{9, 11, 13, 8, 10, 15, 12, 14};
  constexpr std::array<u8, 8> xor_left{0, 5, 3, 2, 4, 1, 9, 8};
  constexpr std::array<u8, 8> xor_right{7, 10, 6, 15, 13, 14, 11, 12};
  const auto &a = selector_substitutions[left];
  const auto &b = selector_substitutions[right];
  ExtensionKey result;
  for (std::size_t phase = 0; phase < 8; ++phase) {
    result.subtraction[phase] =
        u8(a[material[subtract_left[phase]]] ^ b[material[subtract_right[phase]]] ^ adjust);
    result.xor_mask[phase] =
        u8(a[material[xor_left[phase]]] ^ b[material[xor_right[phase]]] ^ adjust);
  }
  return result;
}
} // namespace

ExtensionKey make_extension_key(std::span<const u8, 16> register_key_data) {
  std::array<u8, 10> nonce{};
  std::array<u8, 6> response{};
  std::array<u8, 16> material{};
  for (std::size_t i = 0; i < nonce.size(); ++i) {
    nonce[i] = register_key_data[9 - i];
    material[i + 6] = nonce[i];
  }
  for (std::size_t i = 0; i < response.size(); ++i) {
    response[i] = register_key_data[15 - i];
    material[i] = response[i];
  }
  for (unsigned selector = 0; selector < 7; ++selector) {
    if (matches_selector(nonce, response, selector))
      return combine(material, selector, selector + 1, 0);
  }
  // The fallback substitutions reduce to the last/first pair with a common XOR.
  return combine(material, 7, 0, 0x80);
}

void ExtensionKey::encrypt(std::span<u8> data, u32 address) const {
  unsigned phase = address & 7;
  for (auto &byte : data) {
    byte = u8(u8(unsigned(byte) - unsigned(subtraction[phase])) ^ xor_mask[phase]);
    phase = (phase + 1) & 7;
  }
}

void ExtensionKey::decrypt(std::span<u8> data, u32 address) const {
  unsigned phase = address & 7;
  for (auto &byte : data) {
    byte = u8(unsigned(byte ^ xor_mask[phase]) + subtraction[phase]);
    phase = (phase + 1) & 7;
  }
}
} // namespace cupid::hw
