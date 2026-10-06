cmake_minimum_required(VERSION 3.28)
project(cupid-wii-gamecube VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false;VcpkgApplocalDeps=false")

option(CUPID_HEADLESS_ONLY "Build without video, audio and host-controller output" OFF)
option(CUPID_USE_SYSTEM_DEPS "Use installed dependency libraries instead of pinned sources" OFF)
option(CUPID_BUILD_TESTS "Build public synthetic tests" ON)
option(CUPID_REQUIRE_HARDWARE_TESTS "Fail hardware tests when OpenGL is unavailable" OFF)
if(CUPID_BUILD_TESTS AND CUPID_REQUIRE_HARDWARE_TESTS AND CUPID_HEADLESS_ONLY)
  message(FATAL_ERROR "Required hardware tests need SDL video support")
endif()
option(CUPID_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(CUPID_ENABLE_LTO "Enable supported Release link-time optimization" OFF)
if(CUPID_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT cupid_ipo_supported OUTPUT cupid_ipo_error LANGUAGES C CXX)
  if(NOT cupid_ipo_supported)
    message(FATAL_ERROR "Release link-time optimization is unavailable: ${cupid_ipo_error}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
option(CUPID_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(CUPID_ENABLE_LOCAL_INTEGRATION "Run tests requiring locally supplied private assets" OFF)
set(CUPID_LOCAL_GAME "" CACHE FILEPATH "Local disc image for opt-in bounded integration")
set(CUPID_BEHAVIOR_REFERENCE_ROOT "" CACHE PATH "External pinned behavioral checkout for opt-in differential checks")
set(CUPID_COMMIT "" CACHE STRING "Source revision recorded in the executable")
if(CUPID_COMMIT STREQUAL "")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE CUPID_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  if(CUPID_COMMIT STREQUAL "")
    set(CUPID_COMMIT "unknown")
  endif()
endif()
configure_file(cmake/build_info.hpp.in generated/cupid/build_info.hpp @ONLY)
include(cmake/Dependencies.cmake)

add_library(cupid_options INTERFACE)
target_compile_features(cupid_options INTERFACE cxx_std_20)
if(MSVC)
  target_compile_options(cupid_options INTERFACE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /fp:strict)
  target_compile_definitions(cupid_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(CUPID_WARNINGS_AS_ERRORS)
    target_compile_options(cupid_options INTERFACE /WX)
  endif()
else()
  target_compile_options(cupid_options INTERFACE -Wall -Wextra -Wpedantic -frounding-math -ffp-contract=off)
  if(CUPID_WARNINGS_AS_ERRORS)
    target_compile_options(cupid_options INTERFACE -Werror)
  endif()
endif()
if(CUPID_ENABLE_SANITIZERS)
  if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Address and undefined-behavior sanitizers require GCC or Clang")
  endif()
  target_compile_options(cupid_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cupid_options INTERFACE -fsanitize=address,undefined)
endif()

set(cupid_sources
  src/core/memory.cpp
  src/cpu/interpreter.cpp
  src/cpu/floating.cpp
  src/cpu/estimates.cpp
  src/cpu/fused.cpp
  src/cpu/cache.cpp
  src/cpu/native_integer.cpp
  src/disc/image.cpp
  src/disc/crypto.cpp
  src/disc/filesystem.cpp
  src/hw/devices.cpp
  src/hw/remote.cpp
  src/hw/extension_cipher.cpp
  src/hw/sdp.cpp
  src/ios/ipc.cpp
  src/ios/memory_values.cpp
  src/ios/network.cpp
  src/ios/sysconf.cpp
  src/gfx/command_processor.cpp
  src/gfx/vertex.cpp
  src/gfx/rasterizer.cpp
  src/gfx/hardware_renderer.cpp
  src/gfx/texture.cpp
  src/gfx/texture_decode.cpp
  src/gfx/fragment.cpp
  src/gfx/efb_copy.cpp
  src/gfx/xfb_copy.cpp
  src/runtime/boot.cpp
  src/runtime/elf.cpp
  src/runtime/fonts.cpp
  src/audio/remote_speaker.cpp
  src/audio/host_mixer.cpp
  src/audio/disc_stream.cpp
  src/dsp/engine.cpp
  src/dsp/audio.cpp
  src/dsp/handheld.cpp
  src/dsp/handheld_task.cpp
  src/runtime/machine.cpp
  src/platform/pointer.cpp
  src/platform/output.cpp
  src/platform/input_replay.cpp
  src/platform/input_replay_follow.cpp)
add_library(cupid_core STATIC ${cupid_sources})
target_include_directories(cupid_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(cupid_core PUBLIC cupid_options cupid_dependencies)
add_executable(cupid-wii-gamecube src/main.cpp)
target_link_libraries(cupid-wii-gamecube PRIVATE cupid_core)
include(cmake/DeveloperChecks.cmake)

if(CUPID_BUILD_TESTS)
  enable_testing()
  foreach(suite IN ITEMS cpu native_integer cpu_integer_run core disc disc_stream device event_budget remote remote_speaker host_mixer extension_cipher sdp sdp_transport runtime runtime_group elf fonts dsp handheld ios network sysconf gfx vertex texture fragment rasterizer efb_copy xfb_copy efb_access guest_video input_replay input_replay_follow input_replay_follow_guest audio_stats pointer)
    add_executable(cupid_${suite}_tests tests/${suite}_tests.cpp)
    target_include_directories(cupid_${suite}_tests PRIVATE tests)
    target_link_libraries(cupid_${suite}_tests PRIVATE cupid_core)
    add_test(NAME ${suite} COMMAND cupid_${suite}_tests)
    set_tests_properties(${suite} PROPERTIES TIMEOUT 60 LABELS "public;unit")
  endforeach()
  set_tests_properties(native_integer PROPERTIES SKIP_RETURN_CODE 77)
  # The complete state and ordered-bus fixture needs headroom on a shared host.
  set_tests_properties(cpu_integer_run PROPERTIES TIMEOUT 300 LABELS "public;unit;cpu")
  set_tests_properties(runtime_group PROPERTIES LABELS "public;guest;integration;cpu")
  set_tests_properties(event_budget PROPERTIES LABELS "public;guest;integration;cpu")
  if(CUPID_ENABLE_SANITIZERS)
    # The complete multi-codec sector/hash fixtures take 153 seconds under Clang Debug sanitizers.
    set_tests_properties(disc PROPERTIES TIMEOUT 300)
    # Exhaustive CPU/native state comparisons retain every case under Debug instrumentation.
    set_tests_properties(cpu native_integer PROPERTIES TIMEOUT 300)
  endif()
  set_tests_properties(guest_video PROPERTIES TIMEOUT 180 LABELS "public;guest;integration")
  # The 756-case run/step scope retains full RAM and ordered event comparisons.
  set_tests_properties(runtime_group PROPERTIES TIMEOUT 300)
  set_tests_properties(input_replay PROPERTIES LABELS "public;guest;input")
  set_tests_properties(input_replay_follow PROPERTIES LABELS "public;unit;input")
  set_tests_properties(input_replay_follow_guest PROPERTIES TIMEOUT 180 LABELS "public;guest;integration;usb;input")
  set_tests_properties(sdp_transport PROPERTIES LABELS "public;integration;usb")
  set_tests_properties(efb_access PROPERTIES LABELS "public;guest;integration;graphics")
  foreach(argument IN ITEMS help version self-test)
    add_test(NAME cli_${argument} COMMAND cupid-wii-gamecube --${argument})
    set_tests_properties(cli_${argument} PROPERTIES TIMEOUT 30 LABELS "public;cli")
  endforeach()
  add_test(NAME cli_self_test_native COMMAND cupid-wii-gamecube --self-test --cpu native)
  set_tests_properties(cli_self_test_native PROPERTIES TIMEOUT 30 LABELS "public;cli;cpu"
    SKIP_RETURN_CODE 77)
  if(NOT CUPID_HEADLESS_ONLY)
    add_executable(cupid_hardware_renderer_tests tests/hardware_renderer_tests.cpp)
    target_include_directories(cupid_hardware_renderer_tests PRIVATE tests)
    target_link_libraries(cupid_hardware_renderer_tests PRIVATE cupid_core)
    if(CUPID_REQUIRE_HARDWARE_TESTS)
      add_test(NAME hardware_renderer COMMAND cupid_hardware_renderer_tests --require-device)
    else()
      add_test(NAME hardware_renderer COMMAND cupid_hardware_renderer_tests)
    endif()
    set_tests_properties(hardware_renderer PROPERTIES TIMEOUT 120 LABELS "public;backend;graphics"
      SKIP_RETURN_CODE 77)
    if(CUPID_REQUIRE_HARDWARE_TESTS)
      add_test(NAME guest_video_hardware COMMAND cupid_guest_video_tests --hardware --require-device)
    else()
      add_test(NAME guest_video_hardware COMMAND cupid_guest_video_tests --hardware)
    endif()
    set_tests_properties(guest_video_hardware PROPERTIES TIMEOUT 180
      LABELS "public;backend;guest;integration;graphics" SKIP_RETURN_CODE 77)
    add_executable(cupid_output_tests tests/output_tests.cpp)
    target_include_directories(cupid_output_tests PRIVATE tests)
    target_link_libraries(cupid_output_tests PRIVATE cupid_core)
    add_test(NAME output COMMAND cupid_output_tests)
    set_tests_properties(output PROPERTIES TIMEOUT 30 LABELS "public;backend"
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_executable(cupid_input_tests tests/input_tests.cpp)
    target_include_directories(cupid_input_tests PRIVATE tests)
    target_link_libraries(cupid_input_tests PRIVATE cupid_core)
    add_test(NAME input COMMAND cupid_input_tests)
    set_tests_properties(input PROPERTIES TIMEOUT 60 LABELS "public;backend;input"
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_executable(cupid_host_audio_tests tests/host_audio_tests.cpp)
    target_include_directories(cupid_host_audio_tests PRIVATE tests)
    target_link_libraries(cupid_host_audio_tests PRIVATE cupid_core)
    add_test(NAME host_audio COMMAND cupid_host_audio_tests)
    set_tests_properties(host_audio PROPERTIES TIMEOUT 60 LABELS "public;backend;audio"
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
  else()
    add_test(NAME hardware_renderer COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: hardware rendering is excluded from this headless build")
    set_tests_properties(hardware_renderer PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
      LABELS "public;backend;graphics")
    add_test(NAME guest_video_hardware COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: hardware guest video is excluded from this headless build")
    set_tests_properties(guest_video_hardware PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
      LABELS "public;backend;guest;integration;graphics")
    add_test(NAME output COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: host video/audio backend is excluded from this headless build")
    set_tests_properties(output PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "public;backend")
    add_test(NAME input COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: host input backend is excluded from this headless build")
    set_tests_properties(input PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "public;backend;input")
    add_test(NAME host_audio COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: host audio backend is excluded from this headless build")
    set_tests_properties(host_audio PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "public;backend;audio")
  endif()
  if(CUPID_BEHAVIOR_REFERENCE_ROOT)
    set(cupid_float_reference "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/FloatUtils.cpp")
    if(NOT EXISTS "${cupid_float_reference}")
      message(FATAL_ERROR "The behavioral checkout must contain Source/Core/Common/FloatUtils.cpp")
    endif()
    add_executable(cupid_cpu_reference_checks tests/cpu_reference_checks.cpp "${cupid_float_reference}")
    target_include_directories(cupid_cpu_reference_checks SYSTEM PRIVATE
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core")
    target_link_libraries(cupid_cpu_reference_checks PRIVATE cupid_core)
    add_test(NAME cpu_reference_checks COMMAND cupid_cpu_reference_checks)
    set_tests_properties(cpu_reference_checks PROPERTIES TIMEOUT 60 LABELS "private;differential")
    add_executable(cupid_cpu_fused_reference_checks tests/cpu_fused_reference_checks.cpp)
    set_target_properties(cupid_cpu_fused_reference_checks PROPERTIES CXX_STANDARD 23)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(fmt SYSTEM EXCLUDE_FROM_ALL
      URL https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.tar.gz
      URL_HASH SHA256=5dea48d1fcddc3ec571ce2058e13910a0d4a6bab4cc09a809d8b1dd1c88ae6f2)
    FetchContent_MakeAvailable(fmt)
    target_include_directories(cupid_cpu_fused_reference_checks SYSTEM PRIVATE
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      target_compile_definitions(cupid_cpu_fused_reference_checks PRIVATE _M_X86_64)
    endif()
    target_link_libraries(cupid_cpu_fused_reference_checks PRIVATE cupid_core fmt::fmt-header-only)
    if(WIN32)
      target_compile_definitions(cupid_cpu_fused_reference_checks PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME cpu_fused_reference_checks COMMAND cupid_cpu_fused_reference_checks)
    set_tests_properties(cpu_fused_reference_checks PROPERTIES TIMEOUT 60 LABELS "private;differential")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      set(cupid_ni_reference "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/x64FPURoundMode.cpp")
      if(NOT EXISTS "${cupid_ni_reference}")
        message(FATAL_ERROR "The behavioral checkout must contain Source/Core/Common/x64FPURoundMode.cpp")
      endif()
      add_executable(cupid_cpu_ni_reference_checks tests/cpu_ni_reference_checks.cpp "${cupid_ni_reference}")
      set_target_properties(cupid_cpu_ni_reference_checks PROPERTIES CXX_STANDARD 23)
      target_include_directories(cupid_cpu_ni_reference_checks SYSTEM PRIVATE
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core")
      target_compile_definitions(cupid_cpu_ni_reference_checks PRIVATE _M_X86_64)
      if(WIN32)
        target_compile_definitions(cupid_cpu_ni_reference_checks PRIVATE UNICODE _UNICODE)
      endif()
      target_link_libraries(cupid_cpu_ni_reference_checks PRIVATE cupid_core fmt::fmt-header-only)
      add_test(NAME cpu_ni_reference_checks COMMAND cupid_cpu_ni_reference_checks)
      set_tests_properties(cpu_ni_reference_checks PROPERTIES TIMEOUT 120 LABELS "private;differential")
    else()
      add_test(NAME cpu_ni_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: optional NI arithmetic comparisons require an x86_64 SIMD behavioral target")
      set_tests_properties(cpu_ni_reference_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    endif()
    set(cupid_memory_references
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/MMU.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/MMIO.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_LoadStore.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_LoadStorePaired.cpp")
    foreach(reference IN LISTS cupid_memory_references)
      if(NOT EXISTS "${reference}")
        message(FATAL_ERROR "Required behavioral target file is absent: ${reference}")
      endif()
    endforeach()
    add_executable(cupid_cpu_memory_reference_checks tests/cpu_memory_reference_checks.cpp
      ${cupid_memory_references})
    set_source_files_properties(${cupid_memory_references} PROPERTIES
      COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>;$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
    set_target_properties(cupid_cpu_memory_reference_checks PROPERTIES CXX_STANDARD 23)
    target_include_directories(cupid_cpu_memory_reference_checks SYSTEM PRIVATE
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson")
    target_link_libraries(cupid_cpu_memory_reference_checks PRIVATE cupid_core fmt::fmt)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      target_compile_definitions(cupid_cpu_memory_reference_checks PRIVATE _M_X86_64)
    endif()
    if(WIN32)
      target_compile_definitions(cupid_cpu_memory_reference_checks PRIVATE UNICODE _UNICODE)
    endif()
    if(MSVC)
      target_compile_options(cupid_cpu_memory_reference_checks PRIVATE /Gy /Gw)
      target_link_options(cupid_cpu_memory_reference_checks PRIVATE /OPT:REF)
    else()
      target_compile_options(cupid_cpu_memory_reference_checks PRIVATE -ffunction-sections -fdata-sections)
      target_link_options(cupid_cpu_memory_reference_checks PRIVATE -Wl,--gc-sections)
    endif()
    add_test(NAME cpu_memory_reference_checks COMMAND cupid_cpu_memory_reference_checks)
    set_tests_properties(cpu_memory_reference_checks PROPERTIES TIMEOUT 120 LABELS "private;differential")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      set(cupid_physical_memory_references
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/Memmap.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/MMU.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/PPCCache.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/MMIO.cpp")
      foreach(reference IN LISTS cupid_physical_memory_references)
        if(NOT EXISTS "${reference}")
          message(FATAL_ERROR "Required behavioral target file is absent: ${reference}")
        endif()
      endforeach()
      add_executable(cupid_physical_memory_reference_checks tests/physical_memory_reference_checks.cpp
        ${cupid_physical_memory_references})
      set_source_files_properties(${cupid_physical_memory_references} PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>;$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
      set_target_properties(cupid_physical_memory_reference_checks PROPERTIES CXX_STANDARD 23)
      target_include_directories(cupid_physical_memory_reference_checks SYSTEM PRIVATE
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson")
      target_compile_definitions(cupid_physical_memory_reference_checks PRIVATE _M_X86_64)
      target_link_libraries(cupid_physical_memory_reference_checks PRIVATE cupid_core fmt::fmt)
      if(WIN32)
        target_compile_definitions(cupid_physical_memory_reference_checks PRIVATE UNICODE _UNICODE)
      endif()
      if(MSVC)
        target_compile_options(cupid_physical_memory_reference_checks PRIVATE /Gy /Gw)
        target_link_options(cupid_physical_memory_reference_checks PRIVATE /OPT:REF)
      else()
        target_compile_options(cupid_physical_memory_reference_checks PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(cupid_physical_memory_reference_checks PRIVATE -Wl,--gc-sections)
      endif()
      add_test(NAME physical_memory_reference_checks COMMAND cupid_physical_memory_reference_checks)
      set_tests_properties(physical_memory_reference_checks PROPERTIES TIMEOUT 120 LABELS "private;differential")
      set(cupid_step_references ${cupid_physical_memory_references}
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/PPCTables.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/PowerPC.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/Debugger/Debugger_SymbolMap.cpp")
      foreach(reference IN LISTS cupid_step_references)
        if(NOT EXISTS "${reference}")
          message(FATAL_ERROR "Required behavioral target file is absent: ${reference}")
        endif()
      endforeach()
      add_executable(cupid_cpu_step_reference_checks tests/cpu_step_reference_checks.cpp
        ${cupid_step_references})
      set_source_files_properties(${cupid_step_references} PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>;$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
      set_property(SOURCE "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/PowerPC.cpp"
        APPEND PROPERTY COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=extra>")
      set_target_properties(cupid_cpu_step_reference_checks PROPERTIES CXX_STANDARD 23)
      target_include_directories(cupid_cpu_step_reference_checks SYSTEM PRIVATE
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson")
      target_compile_definitions(cupid_cpu_step_reference_checks PRIVATE _M_X86_64)
      target_link_libraries(cupid_cpu_step_reference_checks PRIVATE cupid_core fmt::fmt)
      if(WIN32)
        target_compile_definitions(cupid_cpu_step_reference_checks PRIVATE UNICODE _UNICODE)
      endif()
      if(MSVC)
        target_compile_options(cupid_cpu_step_reference_checks PRIVATE /Gy /Gw)
        target_link_options(cupid_cpu_step_reference_checks PRIVATE /OPT:REF)
      else()
        target_compile_options(cupid_cpu_step_reference_checks PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(cupid_cpu_step_reference_checks PRIVATE -Wl,--gc-sections)
      endif()
      add_test(NAME cpu_step_reference_checks COMMAND cupid_cpu_step_reference_checks)
      set_tests_properties(cpu_step_reference_checks PROPERTIES TIMEOUT 120 LABELS "private;differential")
      set(cupid_dispatch_references ${cupid_step_references}
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_Tables.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_Integer.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_Branch.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_FloatingPoint.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_Paired.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_LoadStore.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/Interpreter/Interpreter_LoadStorePaired.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/FloatUtils.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/ConditionRegister.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/x64CPUDetect.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/StringUtil.cpp")
      foreach(reference IN LISTS cupid_dispatch_references)
        if(NOT EXISTS "${reference}")
          message(FATAL_ERROR "Required behavioral target file is absent: ${reference}")
        endif()
      endforeach()
      add_executable(cupid_cpu_dispatch_reference_checks tests/cpu_dispatch_reference_checks.cpp
        ${cupid_dispatch_references})
      set_source_files_properties(${cupid_dispatch_references} PROPERTIES
        COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>;$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
      set_property(SOURCE "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/PowerPC/PowerPC.cpp"
        APPEND PROPERTY COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-error=extra>")
      set_target_properties(cupid_cpu_dispatch_reference_checks PROPERTIES CXX_STANDARD 23)
      target_include_directories(cupid_cpu_dispatch_reference_checks SYSTEM PRIVATE
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson")
      target_compile_definitions(cupid_cpu_dispatch_reference_checks PRIVATE _M_X86_64)
      target_link_libraries(cupid_cpu_dispatch_reference_checks PRIVATE cupid_core fmt::fmt)
      if(WIN32)
        target_compile_definitions(cupid_cpu_dispatch_reference_checks PRIVATE UNICODE _UNICODE)
      endif()
      if(MSVC)
        target_compile_options(cupid_cpu_dispatch_reference_checks PRIVATE /Gy /Gw)
        target_link_options(cupid_cpu_dispatch_reference_checks PRIVATE /OPT:REF)
      else()
        target_compile_options(cupid_cpu_dispatch_reference_checks PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(cupid_cpu_dispatch_reference_checks PRIVATE -Wl,--gc-sections)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
          target_compile_definitions(cupid_cpu_dispatch_reference_checks PRIVATE CUPID_OBSERVE_REFERENCE_COPY)
          target_link_options(cupid_cpu_dispatch_reference_checks PRIVATE
            "-Wl,--wrap=_ZNK6Memory13MemoryManager11CopyFromEmuEPvjm"
            "-Wl,--wrap=_ZNK6Memory13MemoryManager8Read_U32Ej")
        endif()
      endif()
      add_test(NAME cpu_dispatch_reference_checks COMMAND cupid_cpu_dispatch_reference_checks)
      set_tests_properties(cpu_dispatch_reference_checks PROPERTIES TIMEOUT 120 LABELS "private;differential")
    else()
      add_test(NAME physical_memory_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: optional physical memory comparisons require an x86_64 behavioral target")
      set_tests_properties(physical_memory_reference_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
      add_test(NAME cpu_step_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: optional CPU stepping comparisons require an x86_64 behavioral target")
      set_tests_properties(cpu_step_reference_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
      add_test(NAME cpu_dispatch_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: optional CPU dispatch comparisons require an x86_64 behavioral target")
      set_tests_properties(cpu_dispatch_reference_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    endif()
    set(cupid_vertex_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/TransformUnit.cpp")
    if(NOT EXISTS "${cupid_vertex_reference}")
      message(FATAL_ERROR "The behavioral checkout must contain Source/Core/VideoBackends/Software/TransformUnit.cpp")
    endif()
    add_executable(cupid_vertex_reference_checks tests/vertex_reference_checks.cpp "${cupid_vertex_reference}")
    # Keep diagnostics from the unmodified comparison source visible without
    # applying the product's warning-as-error policy to that external file.
    set_source_files_properties("${cupid_vertex_reference}" PROPERTIES
      COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>")
    set_target_properties(cupid_vertex_reference_checks PROPERTIES CXX_STANDARD 23)
    target_include_directories(cupid_vertex_reference_checks SYSTEM PRIVATE
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core")
    target_link_libraries(cupid_vertex_reference_checks PRIVATE cupid_core fmt::fmt)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
      target_compile_definitions(cupid_vertex_reference_checks PRIVATE _M_X86_64)
    endif()
    if(WIN32)
      target_compile_definitions(cupid_vertex_reference_checks PRIVATE UNICODE _UNICODE)
    endif()
    add_test(NAME vertex_reference_checks COMMAND cupid_vertex_reference_checks)
    set_tests_properties(vertex_reference_checks PROPERTIES TIMEOUT 60 LABELS "private;differential")
    set(cupid_rasterizer_references
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/Rasterizer.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/Clipper.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoCommon/BPFunctions.cpp")
    set(cupid_texture_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoCommon/TextureDecoder_Common.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/TextureSampler.cpp")
    set(cupid_fragment_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/Tev.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoCommon/BPMemory.cpp")
    set(cupid_sdp_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/IOS/USB/Bluetooth/WiimoteHIDAttr.cpp")
    set(cupid_efb_copy_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/TextureEncoder.cpp")
    if(CUPID_ENABLE_SANITIZERS)
      # Packed pixels have a three-byte stride. Define the external comparison's
      # word loads without changing its algorithms or disabling instrumentation.
      find_package(Python3 REQUIRED COMPONENTS Interpreter)
      set(cupid_packed_encoder_source "${cupid_efb_copy_reference}")
      set(cupid_efb_copy_reference "${CMAKE_CURRENT_BINARY_DIR}/comparisons/packed_encoder.cpp")
      execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/prepare_packed_encoder.py"
        "${cupid_packed_encoder_source}" "${cupid_efb_copy_reference}"
        COMMAND_ERROR_IS_FATAL ANY)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${cupid_packed_encoder_source}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/prepare_packed_encoder.py")
    endif()
    set(cupid_xfb_copy_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/VideoBackends/Software/SWEfbInterface.cpp")
    set(cupid_host_audio_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/AudioCommon/Mixer.cpp")
    set(cupid_pointer_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/WiimoteEmu/Camera.cpp"
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Common/Matrix.cpp")
    set(cupid_disc_stream_reference
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/StreamADPCM.cpp")
    foreach(reference IN LISTS cupid_rasterizer_references cupid_texture_reference cupid_fragment_reference cupid_sdp_reference cupid_efb_copy_reference cupid_xfb_copy_reference cupid_host_audio_reference cupid_pointer_reference cupid_disc_stream_reference)
      if(NOT EXISTS "${reference}")
        message(FATAL_ERROR "Required behavioral target file is absent: ${reference}")
      endif()
    endforeach()
    add_executable(cupid_rasterizer_reference_checks tests/rasterizer_reference_checks.cpp
      ${cupid_rasterizer_references})
    if(NOT EXISTS "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson/picojson.h")
      message(FATAL_ERROR "The optional rasterizer target requires its genuine picojson dependency")
    endif()
    target_include_directories(cupid_rasterizer_reference_checks SYSTEM PRIVATE
      "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Externals/picojson")
    add_executable(cupid_texture_reference_checks tests/texture_reference_checks.cpp
      ${cupid_texture_reference})
    add_executable(cupid_fragment_reference_checks tests/fragment_reference_checks.cpp
      ${cupid_fragment_reference})
    add_executable(cupid_sdp_reference_checks tests/sdp_reference_checks.cpp
      "${cupid_sdp_reference}")
    add_executable(cupid_efb_copy_reference_checks tests/efb_copy_reference_checks.cpp
      "${cupid_efb_copy_reference}")
    add_executable(cupid_xfb_copy_reference_checks tests/xfb_copy_reference_checks.cpp
      "${cupid_xfb_copy_reference}")
    add_executable(cupid_host_audio_reference_checks tests/host_audio_reference_checks.cpp
      "${cupid_host_audio_reference}")
    add_executable(cupid_pointer_reference_checks tests/pointer_reference_checks.cpp
      ${cupid_pointer_reference})
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(cupid_disc_schedule_reference
      "${CMAKE_CURRENT_BINARY_DIR}/generated/disc_stream_reference_functions.inc")
    add_custom_command(OUTPUT "${cupid_disc_schedule_reference}"
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/disc_stream_reference_extract.py"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}" "${cupid_disc_schedule_reference}"
      DEPENDS tests/disc_stream_reference_extract.py
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/DVD/DVDInterface.cpp"
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core/Core/HW/DVD/DVDInterface.h"
      VERBATIM)
    add_executable(cupid_disc_stream_reference_checks tests/disc_stream_reference_checks.cpp
      tests/disc_stream_schedule_reference_checks.cpp "${cupid_disc_schedule_reference}"
      ${cupid_disc_stream_reference})
    target_include_directories(cupid_disc_stream_reference_checks PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set_source_files_properties(${cupid_rasterizer_references} ${cupid_texture_reference} ${cupid_fragment_reference} "${cupid_sdp_reference}" "${cupid_efb_copy_reference}" "${cupid_xfb_copy_reference}" "${cupid_host_audio_reference}" ${cupid_pointer_reference}
      PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/WX->;$<$<CXX_COMPILER_ID:GNU>:-O1>")
    if(CUPID_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      # Match the existing external-source optimization policy while keeping
      # product code and authored fixtures at their selected Debug optimization.
      set(cupid_sanitizer_references ${cupid_memory_references} ${cupid_physical_memory_references}
        ${cupid_step_references} ${cupid_dispatch_references} "${cupid_vertex_reference}"
        ${cupid_rasterizer_references} ${cupid_texture_reference} ${cupid_fragment_reference}
        "${cupid_sdp_reference}" "${cupid_efb_copy_reference}" "${cupid_xfb_copy_reference}"
        "${cupid_host_audio_reference}" ${cupid_pointer_reference})
      list(REMOVE_DUPLICATES cupid_sanitizer_references)
      set_property(SOURCE ${cupid_sanitizer_references} APPEND PROPERTY COMPILE_OPTIONS -O1)
    endif()
    set_property(SOURCE "${cupid_efb_copy_reference}" APPEND PROPERTY COMPILE_OPTIONS
      "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
    set_property(SOURCE "${cupid_xfb_copy_reference}" APPEND PROPERTY COMPILE_OPTIONS
      "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-error=unused-parameter>")
    foreach(comparison IN ITEMS rasterizer texture fragment sdp efb_copy xfb_copy host_audio pointer disc_stream)
      set(target cupid_${comparison}_reference_checks)
      set_target_properties(${target} PROPERTIES CXX_STANDARD 23)
      target_include_directories(${target} SYSTEM PRIVATE
        "${CUPID_BEHAVIOR_REFERENCE_ROOT}/Source/Core")
      target_link_libraries(${target} PRIVATE cupid_core fmt::fmt)
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_compile_definitions(${target} PRIVATE _M_X86_64)
      endif()
      if(WIN32)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
      endif()
      if(MSVC)
        target_compile_options(${target} PRIVATE /Gy /Gw)
        target_link_options(${target} PRIVATE /OPT:REF)
      else()
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
      endif()
      add_test(NAME ${comparison}_reference_checks COMMAND ${target})
      set_tests_properties(${comparison}_reference_checks PROPERTIES TIMEOUT 60 LABELS "private;differential")
    endforeach()
    set_tests_properties(efb_copy_reference_checks xfb_copy_reference_checks PROPERTIES TIMEOUT 180)
  else()
    add_test(NAME cpu_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional differential source checkout is absent; set CUPID_BEHAVIOR_REFERENCE_ROOT")
    set_tests_properties(cpu_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME cpu_fused_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional fused arithmetic comparison requires CUPID_BEHAVIOR_REFERENCE_ROOT and C++23")
    set_tests_properties(cpu_fused_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME cpu_ni_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional NI arithmetic comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and x86_64")
    set_tests_properties(cpu_ni_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME cpu_memory_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional memory comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and C++23")
    set_tests_properties(cpu_memory_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME physical_memory_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional physical memory comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and x86_64")
    set_tests_properties(physical_memory_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME cpu_step_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional CPU stepping comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and x86_64")
    set_tests_properties(cpu_step_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME cpu_dispatch_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional CPU dispatch comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and x86_64")
    set_tests_properties(cpu_dispatch_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    add_test(NAME vertex_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: optional vertex comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and C++23")
    set_tests_properties(vertex_reference_checks PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    foreach(comparison IN ITEMS rasterizer texture fragment sdp efb_copy xfb_copy host_audio pointer disc_stream)
      add_test(NAME ${comparison}_reference_checks COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: optional ${comparison} comparisons require CUPID_BEHAVIOR_REFERENCE_ROOT and C++23")
      set_tests_properties(${comparison}_reference_checks PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;differential")
    endforeach()
  endif()
  if(NOT CUPID_HEADLESS_ONLY AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(CUPID_PULSE_SERVER pulseaudio)
    if(CUPID_PULSE_SERVER)
      add_executable(cupid_host_audio_shutdown_tests tests/host_audio_shutdown_tests.cpp)
      target_include_directories(cupid_host_audio_shutdown_tests PRIVATE tests)
      target_link_libraries(cupid_host_audio_shutdown_tests PRIVATE cupid_core)
      add_test(NAME host_audio_shutdown COMMAND cupid_host_audio_shutdown_tests "${CUPID_PULSE_SERVER}")
      set_tests_properties(host_audio_shutdown PROPERTIES TIMEOUT 20 LABELS "public;backend;audio")
    else()
      add_test(NAME host_audio_shutdown COMMAND "${CMAKE_COMMAND}" -E echo
        "SKIP: owned-server audio shutdown fixture requires an installed Pulse server")
      set_tests_properties(host_audio_shutdown PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:"
        LABELS "public;backend;audio")
    endif()
  endif()
  if(CUPID_ENABLE_LOCAL_INTEGRATION)
    if(NOT EXISTS "${CUPID_LOCAL_GAME}")
      message(FATAL_ERROR "Local integration requires an existing CUPID_LOCAL_GAME path")
    endif()
    add_test(NAME local_disc_integration COMMAND cupid-wii-gamecube
      --game "${CUPID_LOCAL_GAME}" --headless --max-instructions 10000)
    set_tests_properties(local_disc_integration PROPERTIES TIMEOUT 60 LABELS "private;integration")
  else()
    add_test(NAME local_disc_integration COMMAND "${CMAKE_COMMAND}" -E echo
      "SKIP: private-asset integration is disabled; enable CUPID_ENABLE_LOCAL_INTEGRATION with CUPID_LOCAL_GAME")
    set_tests_properties(local_disc_integration PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "private;integration")
  endif()
  if(CUPID_ENABLE_SANITIZERS)
    # Complete RAM hashes and ordered device comparisons take over 230 seconds
    # under Debug instrumentation. Keep every case and allow shared-host headroom.
    set_tests_properties(event_budget runtime_group hardware_renderer
      cpu_memory_reference_checks physical_memory_reference_checks cpu_dispatch_reference_checks
      fragment_reference_checks efb_copy_reference_checks xfb_copy_reference_checks
      host_audio_reference_checks
      PROPERTIES TIMEOUT 600)
    set_tests_properties(cpu_integer_run PROPERTIES TIMEOUT 300)
    # The 252-case Debug run took 1230.15 seconds; this scope has three times as many cases.
    set_tests_properties(runtime_group PROPERTIES TIMEOUT 5400)
    set_tests_properties(cpu_memory_reference_checks PROPERTIES TIMEOUT 1800)
    set_tests_properties(xfb_copy_reference_checks PROPERTIES TIMEOUT 900)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      # SDL retains process-global D-Bus data by default. Its documented debug
      # teardown hint lets owned test processes validate leaks at SDL_Quit.
      get_property(cupid_registered_tests DIRECTORY PROPERTY TESTS)
      foreach(cupid_test IN LISTS cupid_registered_tests)
        set_property(TEST ${cupid_test} APPEND PROPERTY ENVIRONMENT SDL_SHUTDOWN_DBUS_ON_QUIT=1)
      endforeach()
    endif()
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS cupid-wii-gamecube RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/cupid-wii-gamecube" COMPONENT Runtime)
install(DIRECTORY LICENSES/ DESTINATION "${CMAKE_INSTALL_DATADIR}/cupid-wii-gamecube/LICENSES" COMPONENT Runtime)
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DATADIR}/cupid-wii-gamecube/docs" COMPONENT Runtime)
set(CPACK_PACKAGE_NAME cupid-wii-gamecube)
set(CPACK_PACKAGE_VENDOR cupid-wii-gamecube)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_CUPID_COMMIT "${CUPID_COMMIT}")
set(CPACK_PACKAGE_FILE_NAME "cupid-wii-gamecube-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GameCube and Wii command-line runtime")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
endif()
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)
