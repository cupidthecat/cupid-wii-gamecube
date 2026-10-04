// SPDX-License-Identifier: GPL-2.0-or-later
#include "cupid/gfx/hardware_renderer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#if CUPID_HAVE_SDL
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#endif

namespace cupid::gfx {
#if CUPID_HAVE_SDL
namespace {
constexpr const char *compute_source =
#include "hardware_shader.inc"
    ;

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
} // namespace

struct HardwareRenderer::Impl {
  Memory &memory;
  std::span<const u8> texture_memory;
  std::array<u32, 256> bp{};
  unsigned texture_count = 0;
  unsigned pending_dispatches = 0;
  bool samples_texture = false;
  bool textures_ready = false, supported = false;

  struct CachedTexture {
    bool valid = false;
    u64 hash = 0, revision = 0;
    std::array<u32, 7> registers{};
    std::vector<u8> ram_snapshot, texture_memory_snapshot;
    std::array<u32, 11> offsets{};
    std::vector<u32> texels;
  };

  std::array<CachedTexture, 8> textures;
  std::array<u64, 8> uploaded_revisions{};
  SDL_Window *window = nullptr;
  SDL_GLContext context = nullptr;
  bool initialized = false, dirty = false;
  u64 dispatches = 0, fallbacks = 0;
  std::string device, fallback;
  GLuint program = 0;
  std::array<GLuint, 6> buffers{};
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

  // Construct only after a context is current: the entry-point initializers need it.
  Impl(SDL_Window *host_window, SDL_GLContext host_context, Memory &guest_memory,
       std::span<const u8> guest_texture_memory)
      : memory(guest_memory), texture_memory(guest_texture_memory), window(host_window),
        context(host_context), initialized(true) {
    device = reinterpret_cast<const char *>(GetString(GL_RENDERER));
    const GLuint shader = CreateShader(GL_COMPUTE_SHADER);
    ShaderSource(shader, 1, &compute_source, nullptr);
    CompileShader(shader);
    GLint success = 0;
    GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
      std::array<char, 8192> log{};
      GetShaderInfoLog(shader, GLsizei(log.size()), nullptr, log.data());
      DeleteShader(shader);
      throw std::runtime_error(std::string("Hardware fragment compilation failed: ") + log.data());
    }
    program = CreateProgram();
    AttachShader(program, shader);
    LinkProgram(program);
    DeleteShader(shader);
    GetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
      std::array<char, 8192> log{};
      GetProgramInfoLog(program, GLsizei(log.size()), nullptr, log.data());
      DeleteProgram(program);
      program = 0;
      throw std::runtime_error(std::string("Hardware fragment link failed: ") + log.data());
    }
    GenBuffers(GLsizei(buffers.size()), buffers.data());
    const std::array<std::size_t, 6> sizes{640 * 528 * 2, 288, 6, 14 + 34 * 7, 88, 640 * 528 * 2};
    for (unsigned i = 0; i < buffers.size(); ++i) {
      BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
      BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizes[i] * sizeof(u32)), nullptr,
                 GL_DYNAMIC_DRAW);
      BindBufferBase(GL_SHADER_STORAGE_BUFFER, i, buffers[i]);
    }
    reset_counters();
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
        u64 hash = 14695981039346656037ull;
        std::array<u32, 7> signature{};
        std::span<const u8> ram_bytes, texture_memory_bytes;
        const auto hash_bytes = [&](std::span<const u8> bytes) {
          for (u8 byte : bytes)
            hash = (hash ^ byte) * 1099511628211ull;
        };
        for (unsigned group = 0; group < 7; ++group) {
          const u32 value = bp[address + group * 4];
          signature[group] = value;
          for (unsigned i = 0; i < 4; ++i)
            hash = (hash ^ u8(value >> (i * 8))) * 1099511628211ull;
        }
        if ((bp[address + 12] & (1u << 21)) || format == 8 || format == 9 || format == 10)
          texture_memory_bytes = texture_memory;
        hash_bytes(texture_memory_bytes);
        if (!(bp[address + 12] & (1u << 21))) {
          u64 offset = 0, extent = 0;
          for (unsigned level = 0; level <= levels; ++level) {
            const unsigned w = ((width - 1) >> level) + 1, h = ((height - 1) >> level) + 1;
            extent = std::max(extent, offset + u64((w + tile_w - 1) / tile_w) *
                                                   ((h + tile_h - 1) / tile_h) * tile_bytes);
            offset += u64(std::max(tile_w, width >> level)) * std::max(tile_h, height >> level) *
                      tile_bytes / (tile_w * tile_h);
          }
          ram_bytes = memory.range(bp[address + 20] << 5, static_cast<std::size_t>(extent));
          hash_bytes(ram_bytes);
        }
        auto &cached = textures[unit];
        const auto same_bytes = [](std::span<const u8> bytes, const std::vector<u8> &snapshot) {
          return bytes.size() == snapshot.size() &&
                 std::equal(bytes.begin(), bytes.end(), snapshot.begin());
        };
        if (!cached.valid || cached.hash != hash || cached.registers != signature ||
            !same_bytes(ram_bytes, cached.ram_snapshot) ||
            !same_bytes(texture_memory_bytes, cached.texture_memory_snapshot)) {
          cached.texels.clear();
          cached.valid = false;
          for (unsigned level = 0; level <= levels; ++level) {
            cached.offsets[level] = static_cast<u32>(cached.texels.size());
            const unsigned w = ((width - 1) >> level) + 1, h = ((height - 1) >> level) + 1;
            for (unsigned y = 0; y < h; ++y)
              for (unsigned x = 0; x < w; ++x) {
                const auto color = sampler.texel(unit, x, y, level);
                u32 value = 0;
                for (u8 component : color)
                  value = (value << 8) | component;
                cached.texels.push_back(value);
              }
          }
          cached.hash = hash;
          cached.valid = true;
          ++cached.revision;
          cached.registers = signature;
          cached.ram_snapshot.assign(ram_bytes.begin(), ram_bytes.end());
          cached.texture_memory_snapshot.assign(texture_memory_bytes.begin(),
                                                texture_memory_bytes.end());
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
      std::vector<u32> data(88);
      for (unsigned unit = 0; unit < 8; ++unit) {
        if (!used[unit])
          continue;
        const auto &cached = textures[unit];
        for (unsigned level = 0; level < 11; ++level)
          data[unit * 11 + level] = static_cast<u32>(data.size()) + cached.offsets[level];
        data.insert(data.end(), cached.texels.begin(), cached.texels.end());
      }
      BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[4]);
      BufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(data.size() * sizeof(u32)), data.data(),
                 GL_DYNAMIC_DRAW);
      uploaded_revisions = hashes;
    }
    return true;
  }

  ~Impl() {
    SDL_GL_MakeCurrent(window, context);
    if (buffers[0])
      DeleteBuffers(GLsizei(buffers.size()), buffers.data());
    if (program)
      DeleteProgram(program);
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
    const std::array<u32, 6> counters{0, 0xffffffff, 0, 0xffffffff, 0, 0};
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
  impl_->texture_count = bp[0] & 15;
  impl_->samples_texture = false;
  impl_->textures_ready = false;
  std::copy_n(bp.begin(), 256, impl_->bp.begin());
  if ((bp[0] >> 16) & 7)
    impl_->fallback = "Indirect texture stages require the CPU fragment path";
  else if (((bp[0xf1] >> 21) & 7) >= 4 || (((bp[0xf1] >> 21) & 7) && (bp[0xe8] & 1024)))
    impl_->fallback = "Exponential fog and fog range adjustment require the CPU fragment path";
  else if (((bp[0xf1] >> 21) & 7) &&
           (((!(bp[0xf1] & (1u << 20))) && bp[0xf0] >= 32) ||
            ((((bp[0xee] >> 11) & 255) == 255 || ((bp[0xf1] >> 11) & 255) == 255) &&
             !(((bp[0xee] >> 11) & 255) == 255 && ((bp[0xf1] >> 11) & 255) == 255))))
    impl_->fallback = "Fog parameter range requires CPU diagnostics";
  else {
    bool texture_valid = false;
    for (unsigned stage = 0; stage <= ((bp[0] >> 10) & 15); ++stage) {
      const u32 order = bp[0x28 + stage / 2] >> ((stage & 1) * 12);
      const unsigned channel = (order >> 7) & 7;
      if ((channel >= 2 && channel != 7) || bp[0x10 + stage]) {
        impl_->fallback = "Indirect or retained texture state requires the CPU fragment path";
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
  if (!impl_->supported)
    return false;
  std::array<u32, 288> state{};
  std::copy_n(bp.begin(), 256, state.begin());
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned c = 0; c < 4; ++c) {
      state[256 + i * 4 + c] = static_cast<u32>(colors[i][c]);
      state[272 + i * 4 + c] = static_cast<u32>(konst[i][c]);
    }
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[1]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(state), state.data());
  impl_->UseProgram(impl_->program);
  return true;
}

bool HardwareRenderer::draw(const RasterWork &work) {
  impl_->current();
  if (!impl_->supported) {
    ++impl_->fallbacks;
    return false;
  }
  if (impl_->pending_dispatches == 4094) {
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
    for (unsigned i = 0; i < impl_->texture_count; ++i) {
      const auto q_range = range(work.textures[i][2]);
      const auto denominator = q_range.first == 0 && q_range.second == 0 ? w_range : q_range;
      const float minimum = std::min(std::abs(denominator.first), std::abs(denominator.second));
      safe &= denominator.first * denominator.second > 0;
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
  std::array<u32, 14 + 34 * 7> data{};
  data[0] = work.left;
  data[1] = work.top;
  data[2] = work.right;
  data[3] = work.bottom;
  for (unsigned i = 0; i < 3; ++i) {
    const auto &edge = work.edges[i];
    const s64 base = edge.constant + edge.dx * work.top * 16 - edge.dy * work.left * 16;
    const s64 horizontal = -edge.dy * (work.right - work.left - 1) * 16;
    const s64 vertical = edge.dx * (work.bottom - work.top - 1) * 16;
    const s64 magnitude = std::abs(base) + std::abs(horizontal) + std::abs(vertical);
    if (magnitude > std::numeric_limits<s32>::max() ||
        std::abs(edge.dx * 16) > std::numeric_limits<s32>::max() ||
        std::abs(edge.dy * 16) > std::numeric_limits<s32>::max()) {
      ++impl_->fallbacks;
      impl_->fallback = "Triangle edge range exceeds 32-bit hardware coverage";
      return false;
    }
    data[4 + i * 3] = static_cast<u32>(edge.dx);
    data[5 + i * 3] = static_cast<u32>(edge.dy);
    data[6 + i * 3] = static_cast<u32>(base);
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
  data[index] = impl_->samples_texture ? (impl_->pending_dispatches + 1) * 1048576u : 0;
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[3]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(data), data.data());
  impl_->DispatchCompute((work.right - work.left + 7) / 8, (work.bottom - work.top + 7) / 8, 1);
  impl_->MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  impl_->dirty = true;
  ++impl_->dispatches;
  ++impl_->pending_dispatches;
  return true;
}

void HardwareRenderer::upload(std::span<const u32> colors, std::span<const u32> depths) {
  impl_->current();
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[0]);
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(colors.size_bytes()), colors.data());
  impl_->BufferSubData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(colors.size_bytes()),
                       GLsizeiptr(depths.size_bytes()), depths.data());
}

HardwareStats HardwareRenderer::synchronize(std::span<u32> colors, std::span<u32> depths,
                                            FragmentShader *shader) {
  HardwareStats result;
  result.dispatches = impl_->dispatches;
  result.fallback_rectangles = impl_->fallbacks;
  if (!impl_->dirty)
    return result;
  impl_->current();
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[0]);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(colors.size_bytes()),
                          colors.data());
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(colors.size_bytes()),
                          GLsizeiptr(depths.size_bytes()), depths.data());
  std::array<u32, 6> counters{};
  impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[2]);
  impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(counters), counters.data());
  result.fragments = counters[0];
  result.left = counters[1];
  result.right = counters[2];
  result.top = counters[3];
  result.bottom = counters[4];
  if (counters[5] && shader) {
    const u32 pixel_order = counters[5] & 1048575;
    const u32 block = pixel_order / 4;
    const u32 x = (block % 320) * 2 + (pixel_order & 1);
    const u32 y = (block / 320) * 2 + ((pixel_order >> 1) & 1);
    std::array<u32, 2> texture_values{};
    impl_->BindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffers[5]);
    impl_->GetBufferSubData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr((y * 640 + x) * 2 * sizeof(u32)),
                            sizeof(texture_values), texture_values.data());
    const auto unpack = [](u32 value) {
      return Color{u8(value >> 24), u8(value >> 16), u8(value >> 8), u8(value)};
    };
    shader->restore_texture_state(unpack(texture_values[0]), unpack(texture_values[1]));
  }
  impl_->reset_counters();
  impl_->dirty = false;
  impl_->pending_dispatches = 0;
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
