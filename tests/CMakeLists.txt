# Retained from the early macOS branch, adapted to the verified POSIX host.
# All fixtures below are synthetic; no commercial executable or game assets.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_library(civ3_fixture_hle STATIC ${HLE_SOURCES})
target_include_directories(civ3_fixture_hle PRIVATE "${PROJECT_SOURCE_DIR}/src/linux")
target_include_directories(civ3_fixture_hle PUBLIC ${HLE} ${PCRECOMP}/runtime/recomp32)
target_compile_definitions(civ3_fixture_hle PUBLIC RECOMP_RETADDR=0u
                           RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
if(APPLE)
  target_compile_definitions(civ3_fixture_hle PRIVATE CIV3_TEST_AUDIO_SNAPSHOT)
endif()
target_compile_options(civ3_fixture_hle PRIVATE -fno-strict-aliasing -fwrapv)
target_link_libraries(civ3_fixture_hle PUBLIC PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)

add_executable(civ3_host_smoke ../src/linux/main.c ../src/linux/script.c
               ../src/linux/framebuffer.c ../src/linux/record_pipe.c
               ../src/linux/game_lock.c host_fixture.c)
target_include_directories(civ3_host_smoke PRIVATE ../src/linux)
target_compile_definitions(civ3_host_smoke PRIVATE RECOMP_LOCAL_REGS)
target_compile_options(civ3_host_smoke PRIVATE -fno-strict-aliasing -fwrapv)
target_link_libraries(civ3_host_smoke PRIVATE civ3_fixture_hle)
add_test(NAME civ3_host_smoke COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_CURRENT_SOURCE_DIR}/host_smoke.py $<TARGET_FILE:civ3_host_smoke>)

foreach(_test win32hle pe_loader host gdi)
  add_executable(hle_${_test}_test ${HLE}/${_test}_selftest.c)
  target_link_libraries(hle_${_test}_test PRIVATE civ3_fixture_hle)
  add_test(NAME hle_${_test} COMMAND hle_${_test}_test)
endforeach()
set_tests_properties(civ3_host_smoke hle_win32hle hle_pe_loader hle_host hle_gdi
  PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 60 RUN_SERIAL TRUE)

add_executable(civ3_bitmap_test bitmap_safety_selftest.c)
target_include_directories(civ3_bitmap_test PRIVATE ../src/linux)
target_link_libraries(civ3_bitmap_test PRIVATE civ3_fixture_hle)
add_test(NAME macos_bitmap_safety COMMAND civ3_bitmap_test)
set_tests_properties(macos_bitmap_safety PROPERTIES TIMEOUT 30)

add_executable(civ3_pixel_formats_test pixel_formats_selftest.c)
target_link_libraries(civ3_pixel_formats_test PRIVATE civ3_fixture_hle)
add_test(NAME macos_pixel_formats COMMAND civ3_pixel_formats_test)
set_tests_properties(macos_pixel_formats PROPERTIES TIMEOUT 10)

add_executable(civ3_pixel_io_test pixel_io_selftest.c)
target_include_directories(civ3_pixel_io_test PRIVATE ../src/linux)
add_test(NAME macos_pixel_io_bounds COMMAND civ3_pixel_io_test)
set_tests_properties(macos_pixel_io_bounds PROPERTIES TIMEOUT 10)

add_executable(civ3_runtime_state_test runtime_state_selftest.c)
target_include_directories(civ3_runtime_state_test PRIVATE ../src/linux)
target_link_libraries(civ3_runtime_state_test PRIVATE civ3_fixture_hle)
add_test(NAME macos_runtime_state COMMAND civ3_runtime_state_test)
set_tests_properties(macos_runtime_state PROPERTIES TIMEOUT 30)

add_executable(civ3_dispatch_cache_test dispatch_cache_selftest.c)
target_link_libraries(civ3_dispatch_cache_test PRIVATE civ3_fixture_hle)
add_test(NAME macos_dispatch_cache COMMAND civ3_dispatch_cache_test)
set_tests_properties(macos_dispatch_cache PROPERTIES TIMEOUT 30)

# Host file mapping table: 64-view exhaustion, bounds and cleanup.
if(APPLE)
  add_executable(civ3_mapping_safety_test mapping_safety_selftest.c)
  target_link_libraries(civ3_mapping_safety_test PRIVATE civ3_fixture_hle)
  add_test(NAME macos_mapping_safety COMMAND civ3_mapping_safety_test)
  set_tests_properties(macos_mapping_safety PROPERTIES TIMEOUT 20)
endif()

# Sound COM ownership stress: no proprietary game data or audio device.
if(APPLE)
  add_executable(civ3_dsound_lifetime_test dsound_lifetime_selftest.c)
  target_link_libraries(civ3_dsound_lifetime_test PRIVATE civ3_fixture_hle)
  add_test(NAME macos_dsound_lifetime COMMAND civ3_dsound_lifetime_test)
  set_tests_properties(macos_dsound_lifetime PROPERTIES
    TIMEOUT 30 ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy")
endif()

# First-turn software-mixed SFX saturation detector; no audio device or game.
if(APPLE)
  add_executable(civ3_sfx_guard_test sfx_guard_selftest.c)
  target_include_directories(civ3_sfx_guard_test PRIVATE ../src/linux)
  add_test(NAME macos_sfx_runaway_guard COMMAND civ3_sfx_guard_test)
  set_tests_properties(macos_sfx_runaway_guard PROPERTIES TIMEOUT 5)
endif()

# MAC: source-only RIFF/PCM gain validation, fuzzed malformed inputs.
if(APPLE)
  add_executable(civ3_wav_pcm_gain_test wav_pcm_gain_selftest.c)
  target_include_directories(civ3_wav_pcm_gain_test PRIVATE ../src/linux)
  add_test(NAME macos_wav_pcm_gain COMMAND civ3_wav_pcm_gain_test)
  set_tests_properties(macos_wav_pcm_gain PROPERTIES TIMEOUT 10)
endif()

# Native SFX peak limiting, preserving quiet samples.
if(APPLE)
  add_executable(civ3_sfx_softlimit_test sfx_softlimit_selftest.c)
  target_include_directories(civ3_sfx_softlimit_test PRIVATE ../src/linux)
  add_test(NAME macos_sfx_softlimit COMMAND civ3_sfx_softlimit_test)
  set_tests_properties(macos_sfx_softlimit PROPERTIES TIMEOUT 10)
endif()

# Synthetic DirectSound Lock/Unlock snapshot isolation. No assets.
if(APPLE)
  add_executable(civ3_dsound_atomic_snapshot_test dsound_atomic_snapshot_selftest.c)
  target_link_libraries(civ3_dsound_atomic_snapshot_test PRIVATE civ3_fixture_hle)
  add_test(NAME macos_dsound_atomic_snapshot COMMAND civ3_dsound_atomic_snapshot_test)
  set_tests_properties(macos_dsound_atomic_snapshot PROPERTIES TIMEOUT 30
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy")
endif()

if(APPLE)
  add_executable(civ3_winmm_timer_schedule_test winmm_timer_schedule_selftest.c)
  target_include_directories(civ3_winmm_timer_schedule_test PRIVATE ../src/linux)
  add_test(NAME macos_winmm_timer_schedule COMMAND civ3_winmm_timer_schedule_test)
  set_tests_properties(macos_winmm_timer_schedule PROPERTIES TIMEOUT 10)

  add_executable(civ3_winmm_timer_test winmm_timer_selftest.c)
  target_link_libraries(civ3_winmm_timer_test PRIVATE civ3_fixture_hle)
  add_test(NAME macos_winmm_timer COMMAND civ3_winmm_timer_test)
  set_tests_properties(macos_winmm_timer PROPERTIES TIMEOUT 10
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy")
endif()

if(APPLE)
  add_executable(civ3_mmio_buffer_test mmio_buffer_selftest.c)
  target_link_libraries(civ3_mmio_buffer_test PRIVATE civ3_fixture_hle)
  add_test(NAME macos_mmio_buffer COMMAND civ3_mmio_buffer_test)
  set_tests_properties(macos_mmio_buffer PROPERTIES TIMEOUT 10)
endif()
