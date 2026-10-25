// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/hardware_renderer.hpp"
#include "cupid/gfx/efb_copy.hpp"
#include "cupid/gfx/xfb_copy.hpp"
#include "hardware_batch.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#if CUPID_HAVE_SDL
#include "hardware_program.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#endif

#if defined(_WIN32) && CUPID_HAVE_SDL
// Hybrid graphics drivers inspect these executable exports before creating a
// context. Request the high-performance adapter for guest graphics work.
extern "C" {
__declspec(dllexport) cupid::u32 NvOptimusEnablement = 1;
__declspec(dllexport) cupid::s32 AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace cupid::gfx {
#if CUPID_HAVE_SDL
namespace {
constexpr const char *batch_source =
#include "hardware_batch_shader.inc"
    ;
constexpr const char *compute_source =
#include "hardware_shader.inc"
    ;
constexpr const char *xfb_source =
#include "hardware_xfb_shader.inc"
    ;
constexpr const char *texture_copy_source =
#include "hardware_texture_copy_shader.inc"
    ;
constexpr const char *clear_source =
#include "hardware_clear_shader.inc"
    ;
constexpr const char *sync_source =
#include "hardware_sync_shader.inc"
    ;

constexpr std::string_view fog_math_source(std::string_view shader) {
  const auto first = shader.find("uint limb_quotient(");
  if (first == std::string_view::npos)
    return {};
  const auto last = shader.find("bool compare_value(", first);
  if (last == std::string_view::npos || last == first)
    return {};
  return shader.substr(first, last - first);
}

constexpr std::size_t rounded_division_count(std::string_view shader) {
  constexpr std::string_view division = "rounded_divide(";
  std::size_t count = 0, position = 0;
  while ((position = shader.find(division, position)) != std::string_view::npos) {
    ++count;
    position += division.size();
  }
  return count;
}

static_assert(!fog_math_source(compute_source).empty() && !fog_math_source(batch_source).empty() &&
                  fog_math_source(compute_source) == fog_math_source(batch_source),
              "Raster shaders must use identical rounded division.");

constexpr std::size_t integer_quotient_count(std::string_view shader) {
  constexpr std::string_view quotient = "limb_quotient(";
  std::size_t count = 0, position = 0;
  while ((position = shader.find(quotient, position)) != std::string_view::npos) {
    ++count;
    position += quotient.size();
  }
  return count;
}

static_assert(integer_quotient_count(compute_source) == 2 &&
                  integer_quotient_count(batch_source) == 2,
              "Each raster shader must define and call one integer quotient helper.");

static_assert(rounded_division_count(compute_source) == 7 &&
                  rounded_division_count(batch_source) == 7,
              "Each raster shader must retain one rounded division definition and six uses.");

template <typename T> T load_gl(const char *name) {
  auto function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
  if (!function)
    throw std::runtime_error(std::string("OpenGL function unavailable: ") + name);
  return function;
}

float fog_parameter(u32 value) {
  return std::bit_cast<float>(((value & 0x80000) << 12) | (((value >> 11) & 255) << 23) |
                              ((value & 2047) << 12));
}

const std::array<std::array<u32, 256>, 4> &fog_thresholds() {
  // Each entry is the first representable input whose CPU fog factor reaches
  // its integer bucket. This avoids driver-specific transcendental rounding
  // changing the final eight-bit color at a bucket boundary.
  static const auto tables = [] {
    std::array<std::array<u32, 256>, 4> result{};
    for (unsigned type = 0; type < 4; ++type) {
      const auto factor = [type](u32 bits) {
        return fog_factor(std::bit_cast<float>(bits), type + 4);
      };
      for (u32 bucket = 1; bucket <= 256; ++bucket) {
        u32 lower = 0, upper = std::bit_cast<u32>(1.0f);
        if (factor(upper) < bucket) {
          result[type][bucket - 1] = std::bit_cast<u32>(std::numeric_limits<float>::infinity());
          continue;
        }
        while (lower < upper) {
          const u32 middle = lower + (upper - lower) / 2;
          if (factor(middle) >= bucket)
            upper = middle;
          else
            lower = middle + 1;
        }
        result[type][bucket - 1] = lower;
      }
    }
    return result;
  }();
  return tables;
}
} // namespace

struct HardwareRenderer::Impl {
  Memory &memory;
  std::span<const u8> texture_memory;
  std::array<u32, 256> bp{};
  std::array<u32, 288> uploaded_state{};
  TevRegisters uploaded_colors{}, uploaded_konst{};
  bool state_uploaded = false;
  unsigned texture_count = 0;
  unsigned pending_rectangles = 0;
  unsigned active_family = 0, queued_family = 0;
  std::vector<std::array<u32, 254>> queued;
  detail::BatchExtent queued_extent;

  bool samples_texture = false;
  bool textures_ready = false, supported = false;

  struct CachedTexture {
    bool valid = false;
    u64 revision = 0;
    std::array<u32, 6> registers{};
    std::vector<u8> ram_snapshot;
    std::array<std::vector<u8>, 3> texture_memory_snapshots;
    std::array<u32, 11> offsets{};
    std::vector<u32> texels;
  };

  std::array<CachedTexture, 8> textures;
  std::array<u64, 8> uploaded_revisions{};
  std::array<std::size_t, 8> uploaded_texture_sizes{};
  SDL_Window *window = nullptr;
  SDL_GLContext context = nullptr;
  bool initialized = false, dirty = false;
  u64 dispatches = 0, fallbacks = 0;
  u64 texture_decodes = 0, texture_uploads = 0, texture_snapshot_bytes = 0;
  u64 texture_upload_bytes = 0;
  u64 xfb_copies = 0, xfb_readback_bytes = 0, efb_readbacks = 0, efb_readback_bytes = 0;
  std::vector<u32> xfb_staging;
  std::size_t xfb_capacity = 0;
  std::string device, fallback;

  // Family zero is generic; family one has no indirect prefetch stages.
  std::array<detail::FragmentPrograms, 2> fragment_programs{};
  GLuint xfb_program = 0, clear_program = 0, texture_copy_program = 0;
  GLuint sync_program = 0, sync_buffer = 0;
  bool sync_initialized = false;
  std::array<GLuint, 8> buffers{};
#define CUPID_GL_FUNCTION(type, name) type name = load_gl<type>("gl" #name)
  CUPID_GL_FUNCTION(PFNGLCREATESHADERPROC, CreateShader);
  CUPID_GL_FUNCTION(PFNGLSHADERSOURCEPROC, ShaderSource);
  CUPID_GL_FUNCTION(PFNGLCOMPILESHADERPROC, CompileShader);
  CUPID_GL_FUNCTION(PFNGLGETSHADERIVPROC, GetShaderiv);
  CUPID_GL_FUNCTION(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
  CUPID_GL_FUNCTION(PFNGLDELETESHADERPROC, DeleteShader);
  CUPID_GL_FUNCTION(PFNGLCREATEPROGRAMPROC, CreateProgram);
  CUPID_GL_FUNCTION(PFNGLATTACHSHADERPROC, AttachShader);
  CUPID_GL_FUNCTION(PFNGLLINKPROGRAMPROC, LinkProgram);
  CUPID_GL_FUNCTION(PFNGLGETPROGRAMIVPROC, GetProgramiv);
  CUPID_GL_FUNCTION(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
  CUPID_GL_FUNCTION(PFNGLDELETEPROGRAMPROC, DeleteProgram);
  CUPID_GL_FUNCTION(PFNGLUSEPROGRAMPROC, UseProgram);
  CUPID_GL_FUNCTION(PFNGLGENBUFFERSPROC, GenBuffers);
  CUPID_GL_FUNCTION(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
  CUPID_GL_FUNCTION(PFNGLBINDBUFFERPROC, BindBuffer);
  CUPID_GL_FUNCTION(PFNGLBINDBUFFERBASEPROC, BindBufferBase);
  CUPID_GL_FUNCTION(PFNGLBUFFERDATAPROC, BufferData);
  CUPID_GL_FUNCTION(PFNGLBUFFERSUBDATAPROC, BufferSubData);
  CUPID_GL_FUNCTION(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData);
  CUPID_GL_FUNCTION(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);
  CUPID_GL_FUNCTION(PFNGLMEMORYBARRIERPROC, MemoryBarrier);
#undef CUPID_GL_FUNCTION
  using GetStringType = const GLubyte *(APIENTRY *)(GLenum);
  GetStringType GetString = load_gl<GetStringType>("glGetString");
  using GetIntegerType = void(APIENTRY *)(GLenum, GLint *);
  GetIntegerType GetIntegerv = load_gl<GetIntegerType>("glGetIntegerv");

  // Construct only after a context is current: the entry-point initializers need it.
  Impl(SDL_Window *host_window, SDL_GLContext host_context, Memory &guest_memory,
       std::span<const u8> guest_texture_memory)
      : memory(guest_memory), texture_memory(guest_texture_memory), window(host_window),
        context(host_context), initialized(true) {
    device = reinterpret_cast<const char *>(GetString(GL_RENDERER));
    xfb_program = compile_program(xfb_source, "XFB");
    clear_program = compile_program(clear_source, "clear");
    GenBuffers(GLsizei(buffers.size()), buffers.data());
    const std::array<std::size_t, 8> sizes{640 * 528 * 2, 288,     11, 288, 88,
                                           640 * 528 * 8, 4 * 256, 1};
    for (unsigned i = 0; i < buffers.size(); ++i) {
      BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
      BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizes[i] * sizeof(u32)), nullptr,
                 GL_DYNAMIC_DRAW);
      BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, buffers[i]);
    }
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[6]);
    const auto &thresholds = fog_thresholds();
    BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(thresholds), thresholds.data());
    reset_counters();
  }

  GLuint compile_program(const char *source, const char *kind) {
    return detail::compile_compute_program(*this, source, kind);
  }

  void gather_sync_packet() {
    if (!sync_program)
      sync_program = compile_program(sync_source, "state synchronization");
    if (!sync_buffer) {
      GenBuffers(1, &sync_buffer);
      if (!sync_buffer)
        throw std::runtime_error("Hardware state synchronization buffer creation failed");
      BindBuffer(GL_SHADER_STORAGE_BUFFER, sync_buffer);
      BufferData(GL_SHADER_STORAGE_BUFFER, 19 * sizeof(u32), nullptr, GL_DYNAMIC_DRAW);
    }
    GLint previous_program = 0;
    GetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, sync_buffer);
    UseProgram(sync_program);
    DispatchCompute(1, 1, 1);
    MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, buffers[7]);
    UseProgram(GLuint(previous_program));
  }

  static std::string zero_source(std::string_view source) {
    // Specialize only the prefetch count. Stage wrapping and add-previous
    // coordinate operations remain in both shader families.
    constexpr std::string_view marker = "uint indirect_count=(state[0]>>16u)&7u;";
    const auto pos = source.find(marker);
    if (pos == std::string_view::npos ||
        source.find(marker, pos + marker.size()) != std::string_view::npos)
      throw std::logic_error("Expected one indirect stage declaration");
    std::string result(source);
    result.replace(pos, marker.size(), "uint indirect_count=0u;");
    return result;
  }

  void ensure_family(unsigned family) {
    if (fragment_programs[family].scalar)
      return;
    const auto scalar = family ? zero_source(compute_source) : std::string{};
    const auto batch = family ? zero_source(batch_source) : std::string{};
    fragment_programs[family] = detail::create_fragment_programs(
        [&](bool batched) {
          const char *source = batched ? batch_source : compute_source;
          if (family)
            source = batched ? batch.c_str() : scalar.c_str();
          return compile_program(source, batched ? "batch fragment" : "fragment");
        },
        [&](GLuint program) { DeleteProgram(program); });
  }

  template <typename Dispatch>
  void dispatch_fragment(unsigned family, bool batch, Dispatch dispatch) {
    ensure_family(family);
    detail::dispatch_fragment_program(
        fragment_programs[family], batch, [&](GLuint program) { UseProgram(program); }, dispatch);
  }

  bool prepare_textures() {
    if (textures_ready)
      return true;
    textures_ready = true;
    std::array<bool, 8> used{};
    for (unsigned stage = 0; stage <= ((bp[0] >> 10) & 15); ++stage) {
      const u32 order = bp[0x28 + stage / 2] >> ((stage & 1) * 12);
      if ((order & 64) && texture_count)
        used[order & 7] = true;
    }
    for (unsigned stage = 0; stage < ((bp[0] >> 16) & 7); ++stage)
      used[(bp[0x27] >> (stage * 6)) & 7] = true;
    TextureSampler sampler(memory, bp, texture_memory);
    bool changed = false;
    std::array<u64, 8> hashes{};
    try {
      for (unsigned unit = 0; unit < 8; ++unit) {
        if (!used[unit])
          continue;
        const unsigned address = 0x80 + (unit & 3) + ((unit & 4) << 3);
        const unsigned format = bp[address + 8] >> 20;
        unsigned tile_w = 4, tile_h = 4, tile_bytes = format == 6 ? 64 : 32;
        if (format == 0 || format == 8 || format == 14)
          tile_w = tile_h = 8;
        else if (format == 1 || format == 2 || format == 9) {
          tile_w = 8;
          tile_h = 4;
        }
        const unsigned width = (bp[address + 8] & 1023) + 1;
        const unsigned height = ((bp[address + 8] >> 10) & 1023) + 1;
        const unsigned mip_mode = (bp[address] >> 5) & 3;
        const unsigned maximum = (bp[address + 4] >> 8) & 255;
        unsigned levels = mip_mode ? (maximum + (mip_mode == 1 ? 8 : 15)) / 16 : 0;
        if (levels > 10 || ((bp[address + 12] & (1u << 21)) && levels)) {
          fallback = "Texture mip range requires the CPU fragment path";
          return false;
        }
        const bool manual = (bp[address + 12] & (1u << 21)) != 0;
        const bool indexed = format == 8 || format == 9 || format == 10;
        // Filter, wrap and LOD bias affect sampling, not decoded source pixels.
        // Only the reachable mip count belongs to the decoder's cache key.
        const std::array<u32, 6> signature{levels,
                                           bp[address + 8],
                                           manual ? bp[address + 12] & 0x207fff : 0,
                                           manual && format == 6 ? bp[address + 16] & 0x7fff : 0,
                                           manual ? 0 : bp[address + 20],
                                           indexed ? bp[address + 24] & 0xfff : 0};
        std::span<const u8> ram_bytes;
        std::array<std::span<const u8>, 3> texture_memory_bytes{};
        u64 offset = 0, extent = 0;
        for (unsigned level = 0; level <= levels; ++level) {
          const unsigned w = ((width - 1) >> level) + 1, h = ((height - 1) >> level) + 1;
          extent = std::max(extent, offset + u64((w + tile_w - 1) / tile_w) *
                                                 ((h + tile_h - 1) / tile_h) * tile_bytes);
          offset += u64(std::max(tile_w, width >> level)) * std::max(tile_h, height >> level) *
                    tile_bytes / (tile_w * tile_h);
        }
        const auto tmem_range = [&](std::size_t start, std::size_t length) {
          if (start > texture_memory.size() || length > texture_memory.size() - start)
            throw VertexError("Texture-memory cache range is out of bounds");
          return texture_memory.subspan(start, length);
        };
        if (manual) {
          const auto bank_bytes = static_cast<std::size_t>(format == 6 ? extent / 2 : extent);
          texture_memory_bytes[0] = tmem_range((bp[address + 12] & 0x7fff) * 32, bank_bytes);
          if (format == 6)
            texture_memory_bytes[1] = tmem_range((bp[address + 16] & 0x7fff) * 32, bank_bytes);
        } else {
          ram_bytes = memory.range(bp[address + 20] << 5, static_cast<std::size_t>(extent));
        }
        if (indexed) {
          const unsigned palette_bytes = format == 8 ? 32 : format == 9 ? 512 : 32768;
          texture_memory_bytes[2] = tmem_range((bp[address + 24] & 1023) * 512, palette_bytes);
        }
        auto &cached = textures[unit];
        const auto same_bytes = [&](std::span<const u8> bytes, const std::vector<u8> &snapshot) {
          texture_snapshot_bytes += bytes.size();
          return bytes.size() == snapshot.size() &&
                 std::equal(bytes.begin(), bytes.end(), snapshot.begin());
        };
        bool same = cached.valid && cached.registers == signature;
        if (same) {
          same = same_bytes(ram_bytes, cached.ram_snapshot);
          for (unsigned bank = 0; bank < texture_memory_bytes.size(); ++bank)
            same &= same_bytes(texture_memory_bytes[bank], cached.texture_memory_snapshots[bank]);
        }
        if (!same) {
          cached.texels.clear();
          cached.valid = false;
          for (unsigned level = 0; level <= levels; ++level) {
            cached.offsets[level] = static_cast<u32>(cached.texels.size());
            const unsigned w = ((width - 1) >> level) + 1, h = ((height - 1) >> level) + 1;
            const auto start = cached.texels.size();
            cached.texels.resize(start + std::size_t(w) * h);
            sampler.decode(unit, level, ram_bytes, std::span(cached.texels).subspan(start));
          }
          cached.valid = true;
          ++cached.revision;
          ++texture_decodes;
          cached.registers = signature;
          cached.ram_snapshot.assign(ram_bytes.begin(), ram_bytes.end());
          for (unsigned bank = 0; bank < texture_memory_bytes.size(); ++bank)
            cached.texture_memory_snapshots[bank].assign(texture_memory_bytes[bank].begin(),
                                                         texture_memory_bytes[bank].end());
        }
        hashes[unit] = cached.revision;
        changed |= cached.revision != uploaded_revisions[unit];
      }
    } catch (const BusFault &) {
      fallback = "Texture prefetch exceeds mapped guest memory; using demand CPU sampling";
      return false;
    } catch (const VertexError &) {
      fallback = "Texture decoding requires the CPU fragment path";
      return false;
    }
    changed |= hashes != uploaded_revisions;
    if (changed) {
      flush_batch();
      std::array<std::size_t, 8> sizes{};
      std::array<u32, 88> offsets{};
      std::size_t words = offsets.size();
      for (unsigned unit = 0; unit < 8; ++unit) {
        if (!used[unit])
          continue;
        const auto &cached = textures[unit];
        for (unsigned level = 0; level < 11; ++level)
          offsets[unit * 11 + level] = static_cast<u32>(words) + cached.offsets[level];
        sizes[unit] = cached.texels.size();
        words += sizes[unit];
      }
      BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[4]);
      if (sizes == uploaded_texture_sizes) {
        // Stable unit ranges keep unrelated textures resident. Refresh the mip
        // table too: a new decode can change its offsets without changing size.
        BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(offsets), offsets.data());
        texture_upload_bytes += sizeof(offsets);
        std::size_t start = offsets.size();
        for (unsigned unit = 0; unit < 8; ++unit) {
          if (sizes[unit] && hashes[unit] != uploaded_revisions[unit]) {
            const auto bytes = sizes[unit] * sizeof(u32);
            BufferSubData(GL_SHADER_STORAGE_BUFFER, GLintptr(start * sizeof(u32)),
                          GLsizeiptr(bytes), textures[unit].texels.data());
            texture_upload_bytes += bytes;
          }
          start += sizes[unit];
        }
      } else {
        // A changed layout moves subsequent units, so rebuild the packed image.
        std::vector<u32> data;
        data.reserve(words);
        data.insert(data.end(), offsets.begin(), offsets.end());
        for (unsigned unit = 0; unit < 8; ++unit)
          if (used[unit])
            data.insert(data.end(), textures[unit].texels.begin(), textures[unit].texels.end());
        BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(data.size() * sizeof(u32)), data.data(),
                   GL_DYNAMIC_DRAW);
        texture_upload_bytes += data.size() * sizeof(u32);
        uploaded_texture_sizes = sizes;
      }
      uploaded_revisions = hashes;
      ++texture_uploads;
    }
    return true;
  }

  // Each invocation owns one EFB pixel and applies queued rectangles in order.
  void flush_batch() {
    if (queued.empty())
      return;
    int left = 640, top = 528, right = 0, bottom = 0;
    for (const auto &rectangle : queued) {
      left = std::min(left, int(rectangle[0]));
      top = std::min(top, int(rectangle[1]));
      right = std::max(right, int(rectangle[2]));
      bottom = std::max(bottom, int(rectangle[3]));
    }
    std::vector<u32> data(std::max<std::size_t>(288, 5 + queued.size() * 254));
    data[0] = u32(queued.size());
    data[1] = u32(left);
    data[2] = u32(top);
    data[3] = u32(right);
    data[4] = u32(bottom);
    for (std::size_t i = 0; i < queued.size(); ++i)
      std::copy(queued[i].begin(), queued[i].end(), data.begin() + 5 + i * 254);
    dispatch_fragment(queued_family, true, [&] {
      BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
      BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(data.size() * sizeof(u32)), data.data(),
                 GL_DYNAMIC_DRAW);
      DispatchCompute((right - left + 7) / 8, (bottom - top + 7) / 8, 1);
      MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    });
    ++dispatches;
    queued.clear();
    queued_extent = {};
  }

  ~Impl() {
    SDL_GL_MakeCurrent(window, context);
    if (buffers[0])
      DeleteBuffers(GLsizei(buffers.size()), buffers.data());
    if (sync_buffer)
      DeleteBuffers(1, &sync_buffer);
    for (const auto &family : fragment_programs) {
      if (family.scalar)
        DeleteProgram(family.scalar);
      if (family.batch)
        DeleteProgram(family.batch);
    }
    if (xfb_program)
      DeleteProgram(xfb_program);
    if (clear_program)
      DeleteProgram(clear_program);
    if (texture_copy_program)
      DeleteProgram(texture_copy_program);
    if (sync_program)
      DeleteProgram(sync_program);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    if (initialized)
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }

  void current() {
    if (SDL_GL_GetCurrentContext() != context && SDL_GL_MakeCurrent(window, context) != 0)
      throw std::runtime_error(std::string("Hardware context failed: ") + SDL_GetError());
  }

  void reset_counters() {
    const std::array<u32, 11> counters{0, 0xffffffff, 0, 0xffffffff, 0, 0, 0, 0, 0, 0, 0};
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
    BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters), counters.data());
  }
};

HardwareRenderer::HardwareRenderer(Memory &memory, std::span<const u8> texture_memory) {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    throw HardwareUnavailable(std::string("Hardware video initialization failed: ") +
                              SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
  SDL_Window *window =
      SDL_CreateWindow("cupid hardware renderer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    throw HardwareUnavailable(std::string("OpenGL 4.3 window creation failed: ") + SDL_GetError());
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context) {
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    throw HardwareUnavailable(std::string("OpenGL 4.3 context creation failed: ") + SDL_GetError());
  }
  try {
    impl_ = std::make_unique<Impl>(window, context, memory, texture_memory);
  } catch (...) {
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    throw;
  }
}

HardwareRenderer::~HardwareRenderer() = default;

bool HardwareRenderer::begin(std::span<const u32> bp, const TevRegisters &colors,
                             const TevRegisters &konst) {
  impl_->current();
  impl_->fallback.clear();
  impl_->textures_ready = false;
  // An identical BP array has the same eligibility and enabled texture stages.
  // Source bytes still need checking on the first rectangle of this draw.
  const bool same_bp =
      impl_->supported && std::equal(bp.begin(), bp.begin() + 256, impl_->bp.begin());
  if (!(same_bp && impl_->state_uploaded && colors == impl_->uploaded_colors &&
        konst == impl_->uploaded_konst))
    impl_->flush_batch();
  if (same_bp && impl_->state_uploaded && colors == impl_->uploaded_colors &&
      konst == impl_->uploaded_konst) {
    return true;
  }
  if (!same_bp) {
    impl_->texture_count = bp[0] & 15;
    impl_->samples_texture = false;
    std::copy_n(bp.begin(), 256, impl_->bp.begin());
    if (((bp[0xf1] >> 21) & 7) &&
        (((!(bp[0xf1] & (1u << 20))) && bp[0xf0] >= 32) ||
         ((((bp[0xee] >> 11) & 255) == 255 || ((bp[0xf1] >> 11) & 255) == 255) &&
          !(((bp[0xee] >> 11) & 255) == 255 && ((bp[0xf1] >> 11) & 255) == 255))))
      impl_->fallback = "Fog parameter range requires CPU diagnostics";
    else {
      bool texture_valid = false;
      for (unsigned stage = 0; stage <= ((bp[0] >> 10) & 15); ++stage) {
        const u32 order = bp[0x28 + stage / 2] >> ((stage & 1) * 12);
        const unsigned channel = (order >> 7) & 7;
        const u32 indirect = bp[0x10 + stage];
        const unsigned matrix = (indirect >> 9) & 3, matrix_id = (indirect >> 11) & 3;
        if ((channel >= 2 && channel != 5 && channel != 6 && channel != 7) ||
            ((!matrix && matrix_id) || matrix_id == 3) || ((indirect >> 13) & 7) == 7 ||
            ((indirect >> 16) & 7) == 7) {
          impl_->fallback = "Reserved indirect control requires CPU diagnostics";
          break;
        }
        if (stage == 0 && (indirect & (1 << 20))) {
          impl_->fallback = "Cross-fragment indirect accumulation requires the CPU fragment path";
          break;
        }
        if ((matrix || ((indirect >> 7) & 3)) && (indirect & 3) >= ((bp[0] >> 16) & 7)) {
          impl_->fallback = "Retained indirect map requires the CPU fragment path";
          break;
        }
        texture_valid |= (order & 64) != 0;
        impl_->samples_texture |= (order & 64) != 0;
        for (unsigned operand = 0; operand < 4; ++operand) {
          const unsigned color = (bp[0xc0 + stage * 2] >> (12 - operand * 4)) & 15;
          const unsigned alpha = (bp[0xc1 + stage * 2] >> (13 - operand * 3)) & 7;
          if (!texture_valid && (color == 8 || color == 9 || alpha == 4)) {
            impl_->fallback = "Retained TEV texture operands require the CPU fragment path";
            break;
          }
        }
      }
      if (((bp[0xf5] >> 2) & 3) && !texture_valid)
        impl_->fallback = "Retained depth-texture state requires the CPU fragment path";
    }
    impl_->supported = impl_->fallback.empty();
  }
  if (!impl_->supported)
    return false;
  impl_->active_family = ((bp[0] >> 16) & 7u) == 0 ? 1u : 0u;
  std::array<u32, 288> state{};
  std::copy_n(bp.begin(), 256, state.begin());
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned c = 0; c < 4; ++c) {
      state[256 + i * 4 + c] = static_cast<u32>(colors[i][c]);
      state[272 + i * 4 + c] = static_cast<u32>(konst[i][c]);
    }
  if (!impl_->state_uploaded || state != impl_->uploaded_state) {
    impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[1]);
    impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(state), state.data());
    impl_->uploaded_state = state;
    impl_->uploaded_colors = colors;
    impl_->uploaded_konst = konst;
    impl_->state_uploaded = true;
  }
  return true;
}

bool HardwareRenderer::draw(const RasterWork &work) {
  impl_->current();
  if (!impl_->supported) {
    ++impl_->fallbacks;
    return false;
  }
  // Rasterizer scissors produce nonempty rectangles inside the EFB. Check
  // direct backend inputs before coordinate subtraction or edge products.
  if (work.left < 0 || work.top < 0 || work.right > 640 || work.bottom > 528 ||
      work.left >= work.right || work.top >= work.bottom) {
    ++impl_->fallbacks;
    impl_->fallback = "Raster rectangle bounds require CPU validation";
    return false;
  }
  if (impl_->pending_rectangles == 4094) {
    ++impl_->fallbacks;
    impl_->fallback = "Hardware texture-state synchronization batch is full";
    return false;
  }
  if (!impl_->prepare_textures()) {
    impl_->textures_ready = false;
    ++impl_->fallbacks;
    return false;
  }
  const auto finite_plane = [&](const RasterPlane &plane) {
    if (!std::isfinite(plane.value) || !std::isfinite(plane.dx) || !std::isfinite(plane.dy) ||
        !std::isfinite(plane.x_offset) || !std::isfinite(plane.y_offset))
      return false;
    for (int x : {work.left, work.right - 1})
      for (int y : {work.top, work.bottom - 1})
        if (!std::isfinite(plane.at(x, y)))
          return false;
    return true;
  };
  bool finite = finite_plane(work.depth);
  for (unsigned i = 0; i < ((impl_->bp[0] >> 4) & 7); ++i)
    for (const auto &plane : work.colors[i])
      finite &= finite_plane(plane);
  if (!finite) {
    ++impl_->fallbacks;
    impl_->fallback = "Non-finite interpolation requires CPU diagnostics";
    return false;
  }
  if (((impl_->bp[0xf1] >> 21) & 7) && (impl_->bp[0xe8] & 1024)) {
    bool safe = std::isfinite(work.viewport_width) && work.viewport_width != 0;
    for (int x : {work.left, work.right - 1}) {
      const float offset =
          (float(x) - float(int(impl_->bp[0xe8] & 1023) - 342)) / work.viewport_width;
      safe &= std::isfinite(offset * offset);
    }
    // A zero coefficient can yield an undefined range factor. Retain the CPU
    // diagnostic instead of letting GLSL silently propagate a NaN.
    for (unsigned i = 0; i < 10; ++i)
      safe &= ((impl_->bp[0xe9 + i / 2] >> ((i & 1) ? 0 : 12)) & 4095) != 0;
    if (!safe) {
      ++impl_->fallbacks;
      impl_->fallback = "Fog range parameters require CPU diagnostics";
      return false;
    }
  }
  if (((impl_->bp[0xf1] >> 21) & 7) && !(impl_->bp[0xf1] & (1u << 20)) &&
      (fog_parameter(impl_->bp[0xee]) == 0 ||
       (((impl_->bp[0xee] >> 11) & 255) == 255 && ((impl_->bp[0xf1] >> 11) & 255) == 255))) {
    float minimum = 16777215.0f, maximum = 0;
    for (int x : {work.left, work.right - 1})
      for (int y : {work.top, work.bottom - 1}) {
        const float value = std::clamp(work.depth.at(x, y), 0.0f, 16777215.0f);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
      }
    if ((impl_->bp[0xf5] >> 2) & 3) {
      minimum = 0;
      maximum = 16777215.0f;
    }
    const u32 shift = impl_->bp[0xf0], magnitude = impl_->bp[0xef];
    if ((static_cast<u32>(minimum) >> shift) <= magnitude &&
        (static_cast<u32>(maximum) >> shift) >= magnitude) {
      ++impl_->fallbacks;
      impl_->fallback = "Undefined fog division requires CPU diagnostics";
      return false;
    }
  }
  std::array<u32, 16 + 34 * 7> data{};
  data[0] = work.left;
  data[1] = work.top;
  data[2] = work.right;
  data[3] = work.bottom;
  std::array<s64, 3> coverage_base{}, coverage_dx{}, coverage_dy{};
  bool outside_edge = false;
  constexpr s64 edge_limit = std::numeric_limits<s32>::max();
  constexpr s64 coefficient_limit = edge_limit / 16;
  for (unsigned i = 0; i < 3; ++i) {
    const auto &edge = work.edges[i];
    // Bound the steps before multiplication. Bounded EFB coordinates then
    // keep the origin and extent products inside signed 64-bit arithmetic.
    if (edge.dx < -coefficient_limit || edge.dx > coefficient_limit ||
        edge.dy < -coefficient_limit || edge.dy > coefficient_limit) {
      ++impl_->fallbacks;
      impl_->fallback = "Triangle edge range exceeds 32-bit hardware coverage";
      return false;
    }
    const s64 origin = edge.dx * s64(work.top) * 16 - edge.dy * s64(work.left) * 16;
    // Preserve cancellation from a large constant into a valid 32-bit base.
    // This comparison precedes both addition and abs, including INT64_MIN.
    if (edge.constant < -edge_limit - origin || edge.constant > edge_limit - origin) {
      ++impl_->fallbacks;
      impl_->fallback = "Triangle edge range exceeds 32-bit hardware coverage";
      return false;
    }
    const s64 base = edge.constant + origin;
    const s64 horizontal = -edge.dy * (work.right - work.left - 1) * 16;
    const s64 vertical = edge.dx * (work.bottom - work.top - 1) * 16;
    const s64 magnitude = std::abs(base) + std::abs(horizontal) + std::abs(vertical);
    if (magnitude > edge_limit) {
      ++impl_->fallbacks;
      impl_->fallback = "Triangle edge range exceeds 32-bit hardware coverage";
      return false;
    }
    data[4 + i * 3] = static_cast<u32>(edge.dx);
    data[5 + i * 3] = static_cast<u32>(edge.dy);
    data[6 + i * 3] = static_cast<u32>(base);
    coverage_base[i] = base;
    coverage_dx[i] = -edge.dy * 16;
    coverage_dy[i] = edge.dx * 16;
    outside_edge |= base + std::max(horizontal, s64(0)) + std::max(vertical, s64(0)) <= 0;
  }
  // Empty coverage cannot modify the EFB, counters or retained texture state.
  if (outside_edge)
    return true;
  const int width = work.right - work.left, height = work.bottom - work.top;
  if (width > 0 && width <= 256 && height > 0 && height <= 256 && width * height <= 256) {
    bool covered = false;
    auto row = coverage_base;
    for (int y = 0; y < height && !covered; ++y) {
      auto sample = row;
      for (int x = 0; x < width && !covered; ++x) {
        covered = sample[0] > 0 && sample[1] > 0 && sample[2] > 0;
        for (unsigned i = 0; i < 3; ++i)
          sample[i] += coverage_dx[i];
      }
      for (unsigned i = 0; i < 3; ++i)
        row[i] += coverage_dy[i];
    }
    if (!covered)
      return true;
  }
  if (impl_->texture_count) {
    // Rational interpolation can have a pole between corner samples. Retain CPU
    // diagnostics for those rectangles and for coordinates beyond integer range.
    const auto range = [&](const RasterPlane &plane) {
      std::array<float, 4> values{plane.at(work.left & ~1, work.top & ~1),
                                  plane.at((work.right - 1) | 1, work.top & ~1),
                                  plane.at(work.left & ~1, (work.bottom - 1) | 1),
                                  plane.at((work.right - 1) | 1, (work.bottom - 1) | 1)};
      return std::pair{*std::min_element(values.begin(), values.end()),
                       *std::max_element(values.begin(), values.end())};
    };
    const auto w_range = range(work.reciprocal_w);
    bool safe = finite_plane(work.reciprocal_w) && w_range.first * w_range.second > 0;
    // Projection evaluates 1/W, Q*(1/W), and then (1/W)/(Q*(1/W)).
    // A small final coordinate does not rule out a subnormal intermediate.
    // Leave extreme staged ranges on the CPU, with a normal-value margin;
    // integer exponent comparisons do not create another underflowing product.
    const auto exponent = [](float value) { return (std::bit_cast<u32>(value) >> 23) & 255u; };
    const unsigned w_exponent = std::max(exponent(w_range.first), exponent(w_range.second));
    safe &= w_exponent < 252;
    for (unsigned i = 0; i < impl_->texture_count; ++i) {
      const auto q_range = range(work.textures[i][2]);
      const auto denominator = q_range.first == 0 && q_range.second == 0 ? w_range : q_range;
      const float minimum = std::min(std::abs(denominator.first), std::abs(denominator.second));
      safe &= denominator.first * denominator.second > 0;
      if (q_range.first != 0 || q_range.second != 0) {
        const unsigned q_min = std::min(exponent(q_range.first), exponent(q_range.second));
        const unsigned q_max = std::max(exponent(q_range.first), exponent(q_range.second));
        safe &= q_max < 252 && q_min + 124 >= w_exponent;
      }
      for (unsigned c = 0; c < 3; ++c)
        safe &= finite_plane(work.textures[i][c]);
      for (unsigned c = 0; c < 2; ++c) {
        const auto numerator = range(work.textures[i][c]);
        safe &=
            std::max(std::abs(numerator.first), std::abs(numerator.second)) / minimum < 8388608.0f;
      }
    }
    if (!safe) {
      ++impl_->fallbacks;
      impl_->fallback = "Projected texture coordinate range requires the CPU fragment path";
      return false;
    }
  }
  unsigned index = 13;
  const auto append = [&](const RasterPlane &plane) {
    for (float value : {plane.value, plane.dx, plane.dy, plane.x_offset, plane.y_offset})
      data[index++] = std::bit_cast<u32>(value);
    data[index++] = static_cast<u32>(plane.x_origin);
    data[index++] = static_cast<u32>(plane.y_origin);
  };
  append(work.depth);
  append(work.reciprocal_w);
  for (const auto &color : work.colors)
    for (const auto &plane : color)
      append(plane);
  for (const auto &texture : work.textures)
    for (const auto &plane : texture)
      append(plane);
  data[index++] = (impl_->pending_rectangles + 1) * 1048576u;
  data[index++] = impl_->samples_texture ? 1 : 0;
  data[index] = std::bit_cast<u32>(work.viewport_width);
  const u64 area = u64(work.right - work.left) * u64(work.bottom - work.top);
  if (area > 256) {
    impl_->flush_batch();
    impl_->dispatch_fragment(impl_->active_family, false, [&] {
      impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[3]);
      impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(data), data.data());
      impl_->DispatchCompute((work.right - work.left + 7) / 8, (work.bottom - work.top + 7) / 8, 1);
      impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    });
    ++impl_->dispatches;
  } else {
    auto extent = impl_->queued_extent.including(work.left, work.top, work.right, work.bottom);
    if (!extent.admitted()) {
      impl_->flush_batch();
      extent = impl_->queued_extent.including(work.left, work.top, work.right, work.bottom);
    }
    impl_->queued_extent = extent;
    if (impl_->queued.empty())
      impl_->queued_family = impl_->active_family;
    impl_->queued.push_back(data);
    if (impl_->queued.size() == 16)
      impl_->flush_batch();
  }
  impl_->dirty = true;
  ++impl_->pending_rectangles;
  return true;
}

void HardwareRenderer::upload(std::span<const u32> colors, std::span<const u32> depths) {
  impl_->current();
  impl_->flush_batch();
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[0]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(colors.size_bytes()), colors.data());
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(colors.size_bytes()),
                       GLsizeiptr(depths.size_bytes()), depths.data());
}

bool HardwareRenderer::copy_xfb(std::span<const u32> bp) {
  impl_->current();
  impl_->flush_batch();
  const auto plan = plan_efb_xfb(bp);
  const auto &layout = plan.layout;
  const u64 extent = u64(layout.height - 1) * layout.stride + layout.copied_row_bytes;
  auto destination = impl_->memory.range(layout.destination, static_cast<std::size_t>(extent));
  const unsigned words_per_row = layout.copied_row_bytes / sizeof(u32);
  const std::size_t words = std::size_t(layout.height) * words_per_row;
  impl_->xfb_staging.resize(words);
  impl_->current();
  std::array<u32, 288> parameters{};
  parameters[0] = static_cast<u32>(words);
  parameters[1] = layout.stride;
  parameters[2] = words_per_row;
  parameters[3] = plan.top;
  parameters[4] = plan.source_height;
  parameters[5] = layout.width;
  parameters[7] = bp[0x43] & 7;
  parameters[8] = bp[0x52] & 1;
  parameters[9] = bp[0x52] & 2;
  std::copy(plan.weights.begin(), plan.weights.end(), parameters.begin() + 10);
  const u64 ratio = std::bit_cast<u64>(plan.row_ratio);
  parameters[13] = static_cast<u32>(ratio);
  parameters[14] = static_cast<u32>(ratio >> 32);
  parameters[16] = plan.top;
  parameters[17] = layout.width;
  parameters[18] = plan.top + plan.source_height;
  parameters[19] = ((bp[0x4f] & 255) << 24) | ((bp[0x50] & 0xffff) << 8) | ((bp[0x4f] >> 8) & 255);
  parameters[20] = bp[0x41];
  parameters[21] = bp[0x40] & 16;
  parameters[22] = bp[0x51];
  const unsigned groups = static_cast<unsigned>((words + 63) / 64);
  const unsigned horizontal = std::min(groups, 1024u);
  parameters[23] = horizontal * 64;
  const auto &gamma = xfb_gamma_tables()[plan.gamma];
  std::copy(gamma.begin(), gamma.end(), parameters.begin() + 32);
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[3]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(parameters), parameters.data());
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[7]);
  const auto bytes = words * sizeof(u32);
  if (bytes > impl_->xfb_capacity) {
    impl_->BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(bytes), nullptr, GL_DYNAMIC_DRAW);
    impl_->xfb_capacity = bytes;
  }
  impl_->UseProgram(impl_->xfb_program);
  impl_->DispatchCompute(horizontal, (groups + horizontal - 1) / horizontal, 1);
  impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(bytes),
                          impl_->xfb_staging.data());
  for (unsigned row = 0; row < layout.height; ++row)
    for (unsigned column = 0; column < words_per_row; ++column) {
      const u32 value = impl_->xfb_staging[std::size_t(row) * words_per_row + column];
      const auto offset = std::size_t(row) * layout.stride + column * sizeof(u32);
      for (unsigned byte = 0; byte < 4; ++byte)
        destination[offset + byte] = static_cast<u8>(value >> (byte * 8));
    }
  ++impl_->xfb_copies;
  impl_->xfb_readback_bytes += bytes;
  if (bp[0x52] & (1 << 11)) {
    impl_->UseProgram(impl_->clear_program);
    impl_->DispatchCompute((layout.width + 7) / 8, (plan.source_height + 7) / 8, 1);
    impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    impl_->dirty = true;
  }
  const auto &family = impl_->fragment_programs[impl_->active_family];
  // A batch-first family has not initialized its scalar program yet.
  impl_->UseProgram(family.scalar_initialized ? family.scalar : 0);
  return true;
}

bool HardwareRenderer::copy_texture(std::span<const u32> bp) {
  const auto plan = plan_efb_texture(bp);
  const auto &layout = plan.layout;
  const u64 extent = u64(layout.block_rows - 1) * layout.stride + layout.row_bytes;
  auto destination = impl_->memory.range(layout.destination, static_cast<std::size_t>(extent));
  const auto bytes = std::size_t(layout.block_rows) * layout.row_bytes;
  const auto words = bytes / sizeof(u32);
  impl_->xfb_staging.resize(words);
  impl_->current();
  impl_->flush_batch();
  std::array<u32, 288> parameters{};
  parameters[0] = static_cast<u32>(words);
  parameters[1] = plan.columns;
  parameters[2] = plan.block_bytes;
  parameters[3] = plan.block_width;
  parameters[4] = plan.block_height;
  parameters[5] = plan.scale;
  parameters[6] = plan.copy_format;
  parameters[7] = plan.source_format;
  parameters[8] = plan.convert_intensity;
  parameters[9] = plan.source_format == 3 && plan.scale == 2;
  parameters[15] = plan.left;
  parameters[16] = plan.top;
  const auto clear_width = std::min((bp[0x4a] & 1023) + 1, 640 - plan.left);
  const auto clear_height = std::min(((bp[0x4a] >> 10) & 1023) + 1, 528 - plan.top);
  parameters[17] = plan.left + clear_width;
  parameters[18] = plan.top + clear_height;
  parameters[19] = ((bp[0x4f] & 255) << 24) | ((bp[0x50] & 0xffff) << 8) | ((bp[0x4f] >> 8) & 255);
  parameters[20] = bp[0x41];
  parameters[21] = bp[0x40] & 16;
  parameters[22] = bp[0x51];
  const unsigned groups = static_cast<unsigned>((words + 63) / 64);
  const unsigned horizontal = std::min(groups, 1024u);
  parameters[23] = horizontal * 64;
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[3]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(parameters), parameters.data());
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[7]);
  if (bytes > impl_->xfb_capacity) {
    impl_->BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(bytes), nullptr, GL_DYNAMIC_DRAW);
    impl_->xfb_capacity = bytes;
  }
  if (!impl_->texture_copy_program)
    impl_->texture_copy_program = impl_->compile_program(texture_copy_source, "texture copy");
  impl_->UseProgram(impl_->texture_copy_program);
  impl_->DispatchCompute(horizontal, (groups + horizontal - 1) / horizontal, 1);
  impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(bytes),
                          impl_->xfb_staging.data());
  // Compact staging keeps every encoded row intact. Applying rows in order also
  // retains the defined last-writer behavior when the guest stride overlaps.
  for (unsigned row = 0; row < layout.block_rows; ++row) {
    const auto source = std::span<const u32>(impl_->xfb_staging)
                            .subspan(std::size_t(row) * layout.row_bytes / sizeof(u32),
                                     layout.row_bytes / sizeof(u32));
    auto target = destination.subspan(std::size_t(row) * layout.stride, layout.row_bytes);
    if constexpr (std::endian::native == std::endian::little)
      std::memcpy(target.data(), source.data(), source.size_bytes());
    else
      for (std::size_t word = 0; word < source.size(); ++word)
        for (unsigned byte = 0; byte < sizeof(u32); ++byte)
          target[word * sizeof(u32) + byte] = static_cast<u8>(source[word] >> (byte * 8));
  }
  if (bp[0x52] & (1 << 11)) {
    impl_->UseProgram(impl_->clear_program);
    impl_->DispatchCompute((clear_width + 7) / 8, (clear_height + 7) / 8, 1);
    impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    impl_->dirty = true;
  }
  const auto &family = impl_->fragment_programs[impl_->active_family];
  impl_->UseProgram(family.scalar_initialized ? family.scalar : 0);
  return true;
}

void HardwareRenderer::reset_bounding_box(bool vertical) {
  if (!impl_->dirty)
    return;
  impl_->current();
  impl_->flush_batch();
  // Earlier fragments still contribute to counts, the other axis and retained
  // shader state. Only bounds after this reset may extend the new CPU pair.
  const std::array<u32, 2> empty{0xffffffffu, 0};
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[2]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, (vertical ? 3 : 1) * sizeof(u32), sizeof(empty),
                       empty.data());
}

HardwareStats HardwareRenderer::synchronize(std::span<u32> colors, std::span<u32> depths,
                                            FragmentShader *shader) {
  HardwareStats result;
  result.dispatches = impl_->dispatches;
  result.fallback_rectangles = impl_->fallbacks;
  result.texture_decodes = impl_->texture_decodes;
  result.texture_uploads = impl_->texture_uploads;
  result.texture_snapshot_bytes = impl_->texture_snapshot_bytes;
  result.texture_upload_bytes = impl_->texture_upload_bytes;
  result.xfb_copies = impl_->xfb_copies;
  result.xfb_readback_bytes = impl_->xfb_readback_bytes;
  result.efb_readbacks = impl_->efb_readbacks;
  result.efb_readback_bytes = impl_->efb_readback_bytes;
  if (!impl_->dirty)
    return result;
  const HostFloatEnvironment synchronization_environment;
  impl_->current();
  impl_->flush_batch();
  result.dispatches = impl_->dispatches;
  const bool gather_state = shader && impl_->pending_rectangles != 0;
  // Deferred driver compilation can extend through the first completed read.
  std::optional<HostFloatEnvironment> initialization_environment;
  if (shader && !impl_->sync_initialized)
    initialization_environment.emplace();
  if (gather_state)
    impl_->gather_sync_packet();
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[0]);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(colors.size_bytes()),
                          colors.data());
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(colors.size_bytes()),
                          GLsizeiptr(depths.size_bytes()), depths.data());
  result.efb_readbacks = ++impl_->efb_readbacks;
  impl_->efb_readback_bytes += colors.size_bytes() + depths.size_bytes();
  result.efb_readback_bytes = impl_->efb_readback_bytes;
  std::array<u32, 19> counters{};
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER,
                    gather_state ? impl_->sync_buffer : impl_->buffers[2]);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                          GLsizeiptr(gather_state ? sizeof(counters) : 11 * sizeof(u32)),
                          counters.data());
  if (gather_state)
    impl_->sync_initialized = true;
  result.fragments = counters[0];
  result.left = counters[1];
  result.right = counters[2];
  result.top = counters[3];
  result.bottom = counters[4];

  const auto unpack = [](u32 value) {
    return Color{u8(value >> 24), u8(value >> 16), u8(value >> 8), u8(value)};
  };
  if (counters[5] && gather_state) {
    shader->restore_texture_state(unpack(counters[11]), unpack(counters[12]));
  }
  if (counters[6] && gather_state) {
    shader->restore_indirect_coordinates(
        {static_cast<s32>(counters[13]), static_cast<s32>(counters[14])});
  }
  for (unsigned stage = 0; stage < 4; ++stage)
    if (counters[7 + stage] && gather_state) {
      shader->restore_indirect_texture(stage, unpack(counters[15 + stage]));
    }
  impl_->reset_counters();
  impl_->dirty = false;
  impl_->pending_rectangles = 0;
  return result;
}

const std::string &HardwareRenderer::device() const {
  return impl_->device;
}

const std::string &HardwareRenderer::fallback_reason() const {
  return impl_->fallback;
}
#else
struct HardwareRenderer::Impl {
  std::string device, fallback;
};

HardwareRenderer::HardwareRenderer(Memory &, std::span<const u8>) {
  throw HardwareUnavailable("Hardware rendering requires a build with SDL video support");
}

HardwareRenderer::~HardwareRenderer() = default;

bool HardwareRenderer::begin(std::span<const u32>, const TevRegisters &, const TevRegisters &) {
  return false;
}

bool HardwareRenderer::draw(const RasterWork &) {
  return false;
}

void HardwareRenderer::upload(std::span<const u32>, std::span<const u32>) {
}

bool HardwareRenderer::copy_texture(std::span<const u32>) {
  return false;
}

bool HardwareRenderer::copy_xfb(std::span<const u32>) {
  return false;
}

void HardwareRenderer::reset_bounding_box(bool) {
}

HardwareStats HardwareRenderer::synchronize(std::span<u32>, std::span<u32>, FragmentShader *) {
  return {};
}

const std::string &HardwareRenderer::device() const {
  return impl_->device;
}

const std::string &HardwareRenderer::fallback_reason() const {
  return impl_->fallback;
}
#endif
} // namespace cupid::gfx
