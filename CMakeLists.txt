# =============================================================================
# Civilization III: Conquests - static recompilation host (Win32/Linux i386 and native macOS 64-bit)
#
#   py -3 run_lift.py        # -> src/recomp/gen/  (needs work/functions.json)
#   build.cmd                # vcvarsall x86 + CMake + Ninja -> build/civ3.exe
#   build\civ3.exe --run --watchdog 60
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(civ3 C)

option(CIV3_RUNTIME_TESTS_ONLY "Build synthetic macOS runtime tests without game data or generated C" OFF)

if(NOT APPLE AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Windows/Linux hosts must be 32-bit: use build.cmd or build-linux.sh. macOS uses a native 64-bit host.")
endif()
if(APPLE AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "macOS requires a native 64-bit toolchain (arm64 or x86_64)")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../tools" CACHE PATH "pcrecomp checkout")
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/*.c")
if(NOT GEN_SOURCES AND NOT CIV3_RUNTIME_TESTS_ONLY)
  message(FATAL_ERROR "src/recomp/gen is empty -- run `py -3 run_lift.py` first")
endif()

# ---- native Linux: the lifted C on runtime/win32hle (cmake/linux-i386.cmake).
# jgl.dll and sound.dll are lifted too (run_lift.py --module); the table that
# finds all three is src/linux/dispatch.c, not the exe's recomp_dispatch.c.
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  file(GLOB DLL_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen_jgl/*.c" "${CMAKE_SOURCE_DIR}/src/recomp/gen_sound/*.c")
  if(NOT DLL_SOURCES AND NOT CIV3_RUNTIME_TESTS_ONLY)
    message(FATAL_ERROR "src/recomp/gen_jgl is empty -- run `run_lift.py --all --module jgl` and `--module sound` (README, Linux)")
  endif()
  list(FILTER GEN_SOURCES EXCLUDE REGEX "recomp_dispatch\.c$")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
  set(HLE ${PCRECOMP}/runtime/win32hle)
  file(GLOB HLE_SOURCES CONFIGURE_DEPENDS "${HLE}/*.c")
  list(FILTER HLE_SOURCES EXCLUDE REGEX "_selftest\.c$")
  if(APPLE)
    # Patch only a build-directory copy. The pinned toolkit stays untouched.
    find_program(PATCH_EXECUTABLE patch REQUIRED)
    set(MACOS_HLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pcrecomp-macos")
    file(MAKE_DIRECTORY "${MACOS_HLE_DIR}" "${MACOS_HLE_DIR}/staging")
    foreach(PATCH_SPEC IN ITEMS "mss.c|pcrecomp-mss-guest-memory.patch"
                                "kernel32_ext.c|pcrecomp-profile-updates.patch"
                                "screen.c|pcrecomp-screen-thread.patch"
                                "gdidc.c|pcrecomp-gdi-performance.patch"
                                "host_lite.c|pcrecomp-machine-snapshot.patch"
                                "gdi32.c|pcrecomp-bitmap-allocation.patch")
      string(REPLACE "|" ";" PATCH_FIELDS "${PATCH_SPEC}")
      list(GET PATCH_FIELDS 0 PATCH_SOURCE)
      list(GET PATCH_FIELDS 1 PATCH_FILE)
      configure_file("${HLE}/${PATCH_SOURCE}" "${MACOS_HLE_DIR}/staging/${PATCH_SOURCE}" COPYONLY)
      if(PATCH_SOURCE STREQUAL "gdidc.c")
        execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${MACOS_HLE_DIR}/staging/gdidc.c"
                        "${CMAKE_SOURCE_DIR}/patches/pcrecomp-window-dc-lifetime.patch"
                        RESULT_VARIABLE DC_LIFETIME_RESULT)
        if(NOT DC_LIFETIME_RESULT EQUAL 0)
          message(FATAL_ERROR "Saved-window-DC lifetime patch failed; use the pinned pcrecomp revision")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${CMAKE_SOURCE_DIR}/patches/pcrecomp-window-dc-lifetime.patch")
      endif()
      execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${MACOS_HLE_DIR}/staging/${PATCH_SOURCE}"
                      "${CMAKE_SOURCE_DIR}/patches/${PATCH_FILE}" RESULT_VARIABLE PATCH_RESULT)
      if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "${PATCH_FILE} failed; use the pinned pcrecomp revision")
      endif()
      if(PATCH_SOURCE STREQUAL "kernel32_ext.c")
        execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${MACOS_HLE_DIR}/staging/kernel32_ext.c"
                        "${CMAKE_SOURCE_DIR}/patches/pcrecomp-file-sharing.patch"
                        RESULT_VARIABLE SHARING_PATCH_RESULT)
        if(NOT SHARING_PATCH_RESULT EQUAL 0)
          message(FATAL_ERROR "File-sharing patch failed; use the pinned pcrecomp revision")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${CMAKE_SOURCE_DIR}/patches/pcrecomp-file-sharing.patch")
      endif()
      if(PATCH_SOURCE STREQUAL "gdidc.c")
        execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${MACOS_HLE_DIR}/staging/gdidc.c"
                        "${CMAKE_SOURCE_DIR}/patches/pcrecomp-bitmap-objects.patch"
                        RESULT_VARIABLE BITMAP_OBJECT_RESULT)
        if(NOT BITMAP_OBJECT_RESULT EQUAL 0)
          message(FATAL_ERROR "Bitmap object safety patch failed; use the pinned runtime")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${CMAKE_SOURCE_DIR}/patches/pcrecomp-bitmap-objects.patch")
        execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${MACOS_HLE_DIR}/staging/gdidc.c"
                        "${CMAKE_SOURCE_DIR}/patches/pcrecomp-pixel-formats.patch"
                        RESULT_VARIABLE PIXEL_FORMAT_RESULT)
        if(NOT PIXEL_FORMAT_RESULT EQUAL 0)
          message(FATAL_ERROR "Pixel format repair failed; use the pinned runtime")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${CMAKE_SOURCE_DIR}/patches/pcrecomp-pixel-formats.patch")
      endif()
      # Reapplying a patch must not rebuild/relink the game on every launch.
      execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                      "${MACOS_HLE_DIR}/staging/${PATCH_SOURCE}" "${MACOS_HLE_DIR}/${PATCH_SOURCE}"
                      RESULT_VARIABLE COPY_RESULT)
      if(NOT COPY_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not stage patched ${PATCH_SOURCE}")
      endif()
      list(REMOVE_ITEM HLE_SOURCES "${HLE}/${PATCH_SOURCE}")
      list(APPEND HLE_SOURCES "${MACOS_HLE_DIR}/${PATCH_SOURCE}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                   "${CMAKE_SOURCE_DIR}/patches/${PATCH_FILE}")
    endforeach()
  endif()
  if(NOT APPLE)
    # main.c is shared with Linux. It must not free an old framebuffer while
    # an upstream SaveDC snapshot still references it. No Mac-only pacing or
    # optimized blit implementation is enabled by this compatibility repair.
    find_program(PATCH_EXECUTABLE patch REQUIRED)
    set(POSIX_HLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pcrecomp-posix")
    file(MAKE_DIRECTORY "${POSIX_HLE_DIR}/staging")
    configure_file("${HLE}/gdidc.c" "${POSIX_HLE_DIR}/staging/gdidc.c" COPYONLY)
    execute_process(COMMAND "${PATCH_EXECUTABLE}" -s "${POSIX_HLE_DIR}/staging/gdidc.c"
                    "${CMAKE_SOURCE_DIR}/patches/pcrecomp-window-dc-lifetime.patch"
                    RESULT_VARIABLE DC_LIFETIME_RESULT)
    if(NOT DC_LIFETIME_RESULT EQUAL 0)
      message(FATAL_ERROR "Saved-window-DC lifetime patch failed; use the pinned pcrecomp revision")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${POSIX_HLE_DIR}/staging/gdidc.c" "${POSIX_HLE_DIR}/gdidc.c"
                    RESULT_VARIABLE DC_COPY_RESULT)
    if(NOT DC_COPY_RESULT EQUAL 0)
      message(FATAL_ERROR "Could not stage saved-window-DC lifetime repair")
    endif()
    list(REMOVE_ITEM HLE_SOURCES "${HLE}/gdidc.c")
    list(APPEND HLE_SOURCES "${POSIX_HLE_DIR}/gdidc.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/patches/pcrecomp-window-dc-lifetime.patch")
  endif()
  if(APPLE)
    set_source_files_properties("${MACOS_HLE_DIR}/gdidc.c" "${MACOS_HLE_DIR}/gdi32.c"
                                PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/linux")
    # Optimize only the profiled pixel-transfer layer; the lifted game and
    # thread scheduler retain their previously tested compiler settings.
    set_source_files_properties("${MACOS_HLE_DIR}/gdidc.c" PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-fwrapv")
  endif()
  option(CIV3_CULTURE_START_FIX "Repair pending-race lookups during culture-linked starts on macOS" ON)
  if(APPLE AND NOT CIV3_RUNTIME_TESTS_ONLY AND CIV3_CULTURE_START_FIX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(CULTURE_OVERLAY "${CMAKE_CURRENT_BINARY_DIR}/culture-overlay")
    # Regeneration of any original chunk must rerun the checked transformation.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GEN_SOURCES}
                 "${CMAKE_SOURCE_DIR}/tools/macos_culture_overlay.py")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/macos_culture_overlay.py"
                    --source "${CMAKE_SOURCE_DIR}/src/recomp/gen" --output "${CULTURE_OVERLAY}"
                    --exe "${CMAKE_SOURCE_DIR}/work/Civ3Conquests.exe"
                    RESULT_VARIABLE CULTURE_RESULT)
    if(NOT CULTURE_RESULT EQUAL 0)
      message(FATAL_ERROR "Culture-start overlay failed; use the supported full lift, or disable CIV3_CULTURE_START_FIX")
    endif()
    include("${CULTURE_OVERLAY}/sources.cmake")
  endif()
  if(NOT CIV3_RUNTIME_TESTS_ONLY)
    add_executable(civ3 src/linux/main.c src/linux/framebuffer.c src/linux/record_pipe.c src/linux/game_lock.c src/linux/script.c src/linux/dispatch.c ${HLE_SOURCES} ${GEN_SOURCES} ${DLL_SOURCES})
    target_include_directories(civ3 PRIVATE src/linux ${HLE} ${PCRECOMP}/runtime/recomp32)
    # 64-bit file offsets: a 32-bit stat fails with EOVERFLOW on a disk with
    # 64-bit inode numbers (XFS, Btrfs, WSL's /mnt), and the game's files with it.
    target_compile_definitions(civ3 PRIVATE RECOMP_RETADDR=0u RECOMP_LOCAL_REGS HLE_WITH_FFMPEG)
    if(APPLE)
      if(NOT EXISTS "${HLE}/guest_mem.c")
        message(FATAL_ERROR "pcrecomp is too old: macOS needs the based guest-memory runtime")
      endif()
      # Guest addresses remain 32-bit offsets; do not truncate native pointers.
      target_compile_definitions(civ3 PRIVATE RECOMP_MEM_BASE=0x100000000000ull)
    else()
      target_compile_definitions(civ3 PRIVATE RECOMP_FLAT_MEMORY)
    endif()
    set_source_files_properties(src/linux/main.c src/linux/script.c ${HLE_SOURCES} PROPERTIES COMPILE_DEFINITIONS "_FILE_OFFSET_BITS=64")
    set(GEN_OPT "-O2" CACHE STRING "optimisation for the generated C")
    set_source_files_properties(${GEN_SOURCES} ${DLL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;${GEN_OPT};-fno-strict-aliasing;-fwrapv")
    target_compile_options(civ3 PRIVATE -fno-strict-aliasing -fwrapv)
    target_link_libraries(civ3 PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
    # Linked at 0x20000000: at the default 0x08048000 the 110 MB of code end
    # near 0x0F000000, and the heap that starts after them (at a random offset)
    # reached 0x10000000, where jgl.dll goes, in some runs.
    if(NOT APPLE)
      target_link_options(civ3 PRIVATE -Wl,-Ttext-segment=0x20000000)
    endif()
  endif()
  if(APPLE)
    include(CTest)
    if(BUILD_TESTING)
      find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
      add_subdirectory(tests)
      add_executable(civ3_host_safety_test tests/host_safety_selftest.c src/linux/game_lock.c)
      target_include_directories(civ3_host_safety_test PRIVATE src/linux)
      add_test(NAME macos_host_safety COMMAND civ3_host_safety_test)
      add_executable(civ3_record_pipe_test tests/record_pipe_selftest.c src/linux/record_pipe.c)
      target_include_directories(civ3_record_pipe_test PRIVATE src/linux)
      add_test(NAME macos_record_pipe COMMAND civ3_record_pipe_test)
      set_tests_properties(macos_record_pipe PROPERTIES TIMEOUT 10)

      add_executable(civ3_culture_test tests/culture_start_selftest.c)
      target_include_directories(civ3_culture_test PRIVATE src/linux)
      add_test(NAME macos_culture_pending_race COMMAND civ3_culture_test)
      add_executable(civ3_framebuffer_test tests/framebuffer_selftest.c src/linux/framebuffer.c)
      target_include_directories(civ3_framebuffer_test PRIVATE src/linux)
      target_link_libraries(civ3_framebuffer_test PRIVATE pthread)
      add_test(NAME macos_framebuffer_lifetime COMMAND civ3_framebuffer_test)
      add_executable(civ3_snapshot_test tests/machine_snapshot_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_snapshot_test PRIVATE src/linux ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_snapshot_test PRIVATE RECOMP_RETADDR=0u RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_snapshot_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_snapshot_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_test(NAME macos_machine_snapshots COMMAND civ3_snapshot_test)
      set_tests_properties(macos_machine_snapshots PROPERTIES TIMEOUT 5)
      add_executable(civ3_gdi_perf_test tests/gdi_performance_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_gdi_perf_test PRIVATE ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_gdi_perf_test PRIVATE RECOMP_RETADDR=0u RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_gdi_perf_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_gdi_perf_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_test(NAME macos_graphics_pixels COMMAND civ3_gdi_perf_test)
      add_executable(civ3_mss_test tests/mss_guest_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_mss_test PRIVATE ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_mss_test PRIVATE RECOMP_RETADDR=0u
                                 RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_mss_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_mss_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_executable(civ3_profile_test tests/profile_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_profile_test PRIVATE ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_profile_test PRIVATE RECOMP_RETADDR=0u
                                 RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_profile_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_profile_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_executable(civ3_screen_test tests/screen_thread_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_screen_test PRIVATE ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_screen_test PRIVATE RECOMP_RETADDR=0u
                                 RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_screen_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_screen_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_executable(civ3_sharing_test tests/file_sharing_selftest.c ${HLE_SOURCES})
      target_include_directories(civ3_sharing_test PRIVATE ${HLE} ${PCRECOMP}/runtime/recomp32)
      target_compile_definitions(civ3_sharing_test PRIVATE RECOMP_RETADDR=0u
                                 RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
      target_compile_options(civ3_sharing_test PRIVATE -fno-strict-aliasing -fwrapv)
      target_link_libraries(civ3_sharing_test PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
      add_test(NAME macos_file_sharing COMMAND civ3_sharing_test)
      add_test(NAME macos_screen_thread COMMAND civ3_screen_test)
      set_tests_properties(macos_screen_thread PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
      add_test(NAME macos_profile_updates COMMAND civ3_profile_test)
      add_test(NAME macos_miles_guest_memory
               COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_mss_guest_test.sh"
                       $<TARGET_FILE:civ3_mss_test> "${FFMPEG_EXECUTABLE}")
    endif()
  endif()
  return()
endif()

if(NOT MSVC)
  message(FATAL_ERROR "runtime/native32 is MSVC x86 inline assembly")
endif()

add_executable(civ3
  src/runtime/host.c
  src/runtime/record.c
  src/runtime/input.c
  src/runtime/oracle.c
  src/runtime/present.c
  ${PCRECOMP}/runtime/native32/native32.c
  ${PCRECOMP}/runtime/recomp32/image_loader.c
  ${PCRECOMP}/runtime/recomp32/recomp_trace.c
  ${GEN_SOURCES}
)
target_include_directories(civ3 PRIVATE
  src/runtime src/recomp/gen ${PCRECOMP}/runtime/native32 ${PCRECOMP}/runtime/recomp32)
# RECOMP_RETADDR=0u: a stack imbalance during bring-up leaks a harmless 0, not
# 0xDEAD0000 into a size argument (forcecommander).
target_compile_definitions(civ3 PRIVATE RECOMP_RETADDR=0u _CRT_SECURE_NO_WARNINGS
                                             RECOMP_FLAT_MEMORY RECOMP_LOCAL_REGS)
# Linked high, so the guest's 0x00400000..0x00CF7000 is free.
target_link_options(civ3 PRIVATE /BASE:0x60000000 /FIXED:NO /DYNAMICBASE:NO
                                      /NXCOMPAT /LARGEADDRESSAWARE /STACK:0x1000000
                                      /MANIFESTUAC:NO)
set(GEN_OPT "/O2" CACHE STRING "optimisation for the generated C")
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "/w;${GEN_OPT}")
set_source_files_properties(src/runtime/host.c src/runtime/record.c src/runtime/input.c src/runtime/oracle.c src/runtime/present.c PROPERTIES COMPILE_OPTIONS "/W3")
target_link_libraries(civ3 PRIVATE user32 gdi32 kernel32 d3d11 dxgi dxguid)

option(CIV3_TRACE "record a ring buffer of lifted function entries" OFF)
if(CIV3_TRACE)
  target_compile_definitions(civ3 PRIVATE RECOMP_TRACE=1)
endif()
