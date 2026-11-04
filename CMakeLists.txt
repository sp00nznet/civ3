# =============================================================================
# Civilization III: Conquests - native Windows, Linux and macOS hosts
#
#   py -3 run_lift.py        # -> src/recomp/gen/  (needs work/functions.json)
#   build.cmd                # vcvarsall x86 + CMake + Ninja -> build/civ3.exe
#   ./build-macos.sh          # Apple Silicon or Intel, 64-bit win32hle
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(civ3 C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(_pcrecomp_default "${CMAKE_SOURCE_DIR}/../tools")
foreach(_candidate "${CMAKE_SOURCE_DIR}/.deps/pcrecomp" "${CMAKE_SOURCE_DIR}/../pcrecomp")
  if(EXISTS "${_candidate}/runtime/win32hle/guest_mem.c")
    set(_pcrecomp_default "${_candidate}")
    break()
  endif()
endforeach()
if(DEFINED ENV{PCRECOMP})
  set(_pcrecomp_default "$ENV{PCRECOMP}")
endif()
set(PCRECOMP "${_pcrecomp_default}" CACHE PATH "pcrecomp checkout (see pcrecomp-revision.txt)")
option(CIV3_BUILD_GAME "Build the game (requires the three generated lifts)" ON)
option(CIV3_BUILD_TESTS "Build synthetic host/runtime tests; no game files needed" OFF)
if(CIV3_BUILD_GAME)
  file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/*.c")
  if(NOT GEN_SOURCES)
    message(FATAL_ERROR "src/recomp/gen is empty -- run setup-macos.sh on macOS, setup.sh on Linux, or Setup.cmd on Windows. For tests only: -DCIV3_BUILD_GAME=OFF -DCIV3_BUILD_TESTS=ON")
  endif()
endif()

# ---- native POSIX: the lifted C on runtime/win32hle.
# jgl.dll and sound.dll are lifted too (run_lift.py --module); the table that
# finds all three is src/linux/dispatch.c, not the exe's recomp_dispatch.c.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
  if(APPLE AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "modern macOS requires a 64-bit arm64 or x86_64 build")
  endif()
  if(NOT EXISTS "${PCRECOMP}/runtime/win32hle/guest_mem.c")
    message(FATAL_ERROR "pcrecomp is missing or too old: use the revision in pcrecomp-revision.txt (setup-macos.sh installs it locally)")
  endif()
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
  set(HLE ${PCRECOMP}/runtime/win32hle)
  file(GLOB HLE_SOURCES CONFIGURE_DEPENDS "${HLE}/*.c")
  list(FILTER HLE_SOURCES EXCLUDE REGEX "_selftest\\.c$")
  add_library(civ3_hle STATIC ${HLE_SOURCES})
  target_include_directories(civ3_hle PUBLIC ${HLE} ${PCRECOMP}/runtime/recomp32)
  target_compile_definitions(civ3_hle PUBLIC RECOMP_RETADDR=0u HLE_WITH_FFMPEG _FILE_OFFSET_BITS=64)
  target_compile_options(civ3_hle PUBLIC -fno-strict-aliasing -fwrapv)
  target_link_libraries(civ3_hle PUBLIC PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG Threads::Threads m)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(civ3_hle PUBLIC RECOMP_FLAT_MEMORY)
    # Keep a flat Linux host and its heap clear of the guest's DLLs.
    target_link_options(civ3_hle INTERFACE -Wl,-Ttext-segment=0x20000000)
  else()
    # The guest stays 32-bit; host pointers are never squeezed into its VAs.
    # A constant base lets the compiler optimise generated memory accesses.
    set(CIV3_GUEST_MEMORY_BASE "0x100000000000ull" CACHE STRING "64-bit host address of guest memory; empty for a dynamic mapping")
    if(CIV3_GUEST_MEMORY_BASE)
      target_compile_definitions(civ3_hle PUBLIC RECOMP_MEM_BASE=${CIV3_GUEST_MEMORY_BASE})
    endif()
  endif()
  if(APPLE)
    # Deep lifted calls use the native main-thread stack (also used by SDL).
    target_link_options(civ3_hle INTERFACE -Wl,-stack_size,0x1000000)
  endif()
  if(CIV3_BUILD_GAME)
    set(DLL_SOURCES)
    foreach(_module jgl sound)
      file(GLOB _module_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen_${_module}/*.c")
      if(NOT _module_sources)
        message(FATAL_ERROR "src/recomp/gen_${_module} is empty -- run run_lift.py --all --module ${_module} after cataloging that DLL")
      endif()
      list(APPEND DLL_SOURCES ${_module_sources})
    endforeach()
    list(FILTER GEN_SOURCES EXCLUDE REGEX "recomp_dispatch\\.c$")
    add_executable(civ3 src/linux/main.c src/linux/script.c src/linux/dispatch.c ${GEN_SOURCES} ${DLL_SOURCES})
    target_link_libraries(civ3 PRIVATE civ3_hle)
    target_compile_definitions(civ3 PRIVATE RECOMP_LOCAL_REGS)
    set(GEN_OPT "-O2" CACHE STRING "optimisation for the generated C")
    set_source_files_properties(${GEN_SOURCES} ${DLL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;${GEN_OPT}")
  endif()
  if(CIV3_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
  return()
endif()

if(NOT CIV3_BUILD_GAME OR CIV3_BUILD_TESTS)
  message(FATAL_ERROR "synthetic host tests use the POSIX host (Linux or macOS)")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "the Windows host must be 32-bit: use build.cmd (vcvarsall x86)")
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
