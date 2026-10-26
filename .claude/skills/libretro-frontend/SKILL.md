---
name: libretro-frontend
description: How to build the project's libretro front-end (load a core .so, run frames, video/audio/input via SDL, save states) and expose emulated memory to the dev tools and rcheevos. Use when working on core loading, the main loop, rendering, input, save states or memory access.
---

# libretro front-end

Reference: `libretro.h` from `libretro/libretro-common` (vendored) and the libretro docs.
Existing minimal front-ends worth reading: `nanoarch`, `RALibretro` (Windows, but the closest
to what we build), RetroArch `runloop.c` for edge cases.

## Loading a core

1. `dlopen()` the core `.so`, `dlsym()` every `retro_*` function.
2. Call setters **before** `retro_init()`: `retro_set_environment`, `retro_set_video_refresh`,
   `retro_set_audio_sample`, `retro_set_audio_sample_batch`, `retro_set_input_poll`,
   `retro_set_input_state`.
3. `retro_init()` → `retro_get_system_info()` (check `need_fullpath`) → `retro_load_game()` →
   `retro_get_system_av_info()` (resolution, fps, sample rate).
4. Main loop: `retro_run()` once per frame, then evaluate achievements for that frame.
5. Shutdown: `retro_unload_game()`, `retro_deinit()`, `dlclose()`.

## Environment callback — commands to handle first

`GET_SYSTEM_DIRECTORY` (BIOS location — the user's own files), `GET_SAVE_DIRECTORY`,
`SET_PIXEL_FORMAT`, `GET_VARIABLE` / `SET_VARIABLES` / `SET_CORE_OPTIONS_V2` (core options),
`GET_LOG_INTERFACE`, `SET_MEMORY_MAPS`, `SET_HW_RENDER` (only once HW rendering is supported —
start with software cores/renderers).

## Memory access (the important part)

- Simple: `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` + `retro_get_memory_size(...)`.
- Correct: many cores describe memory through `RETRO_ENVIRONMENT_SET_MEMORY_MAPS`. rcheevos
  ships **`rc_libretro.h` / `rc_libretro.c`** that maps libretro memory (maps or
  `get_memory_data`) to RA addresses per console — **use it** instead of writing our own mapping.
- Read memory only between frames (after `retro_run()` returns).

## Save states

`retro_serialize_size()` → `retro_serialize(buf, size)` / `retro_unserialize(buf, size)`.
After loading a state, reset rcheevos runtime state appropriately (see rcheevos docs on
`rc_runtime_reset` / deserializing progress) so hit counts don't carry over incorrectly.

## SDL integration

- Video: upload the frame from `video_refresh` into an SDL texture (handle the pixel format
  set by the core and `pitch`), let ImGui draw on top.
- Audio: queue samples from `audio_sample_batch` to an SDL audio stream; sync the loop to audio
  or to the core's fps.
- Input: map SDL gamepad/keyboard to `RETRO_DEVICE_JOYPAD` ids in `input_state`.

## Recommended PS1 cores

Beetle PSX (`mednafen_psx_libretro.so`, software renderer) and SwanStation. Both need the
user's own BIOS in the system directory. Do not ship cores or BIOS.
