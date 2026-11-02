# SPDX-License-Identifier: GPL-2.0-or-later

function(cupid_sdl_replace path before after)
  file(READ "${path}" content)
  string(FIND "${content}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "SDL audio patch does not match ${path}")
  endif()
  string(REPLACE "${before}" "${after}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()

function(cupid_prepare_sdl_audio source_dir result)
  # Patch a build-owned copy so an explicitly supplied dependency cache stays intact.
  set(paths src/audio/SDL_sysaudio.h src/audio/SDL_audio.c src/audio/pulseaudio/SDL_pulseaudio.c)
  set(hashes
    bf5fb0a48c9a699fe19d6350b7ed69bed7306d34e42a66984ac459899ec7e1be
    e4ff1836b449f9b00d5eb5d73487fa92ee06afa4df9b2bae76b0f0811e6b7644
    7e78dd2ec5820387252f1301cd8009759e1ca89f30152f6cba3beeaf95bd1b9e)
  foreach(index RANGE 0 2)
    list(GET paths ${index} path)
    list(GET hashes ${index} expected)
    file(SHA256 "${source_dir}/${path}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "SDL audio patch requires the pinned 2.30.12 source: ${path}")
    endif()
  endforeach()
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" patch_hash)
  string(SHA256 identity "${source_dir};${patch_hash};${hashes}")
  string(SUBSTRING "${identity}" 0 12 suffix)
  set(staged "${CMAKE_BINARY_DIR}/_deps/sdl2-audio-${suffix}")
  set(marker "${staged}/cupid-audio-patch.txt")
  if(EXISTS "${marker}")
    file(READ "${marker}" saved_identity)
    if(NOT saved_identity STREQUAL identity)
      message(FATAL_ERROR "SDL audio patch staging identity differs")
    endif()
  else()
    file(COPY "${source_dir}/" DESTINATION "${staged}" PATTERN ".git" EXCLUDE)
    cupid_sdl_replace("${staged}/src/audio/SDL_sysaudio.h"
      "    void (*FlushCapture)(_THIS);\n"
      "    void (*FlushCapture)(_THIS);\n    void (*PrepareToClose)(_THIS); /* Altered for cupid-wii-gamecube: wake before join. */\n")
    cupid_sdl_replace("${staged}/src/audio/SDL_audio.c"
      "    current_audio.impl.UnlockDevice(device);\n\n    if (device->thread != NULL) {"
      "    current_audio.impl.UnlockDevice(device);\n\n    /* Altered for cupid-wii-gamecube: retain callback state until the worker exits. */\n    if (device->hidden != NULL && current_audio.impl.PrepareToClose != NULL) {\n        current_audio.impl.PrepareToClose(device);\n    }\n    if (device->thread != NULL) {")
    cupid_sdl_replace("${staged}/src/audio/pulseaudio/SDL_pulseaudio.c"
      "static void PULSEAUDIO_CloseDevice(_THIS)\n"
      "/* Altered for cupid-wii-gamecube: close disables audio before this wake.\n * The playback and capture predicates hold this same lock when entering a wait,\n * so shutdown cannot miss a wake. No server acknowledgement is required.\n */\nstatic void PULSEAUDIO_PrepareToClose(_THIS)\n{\n    (void)this;\n    PULSEAUDIO_pa_threaded_mainloop_lock(pulseaudio_threaded_mainloop);\n    PULSEAUDIO_pa_threaded_mainloop_signal(pulseaudio_threaded_mainloop, 0);\n    PULSEAUDIO_pa_threaded_mainloop_unlock(pulseaudio_threaded_mainloop);\n}\n\nstatic void PULSEAUDIO_CloseDevice(_THIS)\n")
    cupid_sdl_replace("${staged}/src/audio/pulseaudio/SDL_pulseaudio.c"
      "    impl->GetDeviceBuf = PULSEAUDIO_GetDeviceBuf;\n"
      "    impl->GetDeviceBuf = PULSEAUDIO_GetDeviceBuf;\n    impl->PrepareToClose = PULSEAUDIO_PrepareToClose;\n")
    file(WRITE "${marker}" "${identity}")
  endif()
  set(${result} "${staged}" PARENT_SCOPE)
endfunction()
