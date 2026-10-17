# =============================================================================
# Civilization III: Conquests - static recompilation host (32-bit, MSVC x86)
#
#   py -3 run_lift.py        # -> src/recomp/gen/  (needs work/functions.json)
#   build.cmd                # vcvarsall x86 + CMake + Ninja -> build/civ3.exe
#   build\themovies.exe --run --watchdog 60
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(civ3 C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "the host must be 32-bit: use build.cmd (vcvarsall x86), or on Linux build-linux.sh")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../tools" CACHE PATH "pcrecomp checkout")
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen/*.c")
if(NOT GEN_SOURCES)
  message(FATAL_ERROR "src/recomp/gen is empty -- run `py -3 run_lift.py` first")
endif()

# ---- native Linux: the lifted C on runtime/win32hle (cmake/linux-i386.cmake).
# jgl.dll and sound.dll are lifted too (run_lift.py --module); the table that
# finds all three is src/linux/dispatch.c, not the exe's recomp_dispatch.c.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  file(GLOB DLL_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/recomp/gen_jgl/*.c" "${CMAKE_SOURCE_DIR}/src/recomp/gen_sound/*.c")
  if(NOT DLL_SOURCES)
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
  add_executable(civ3 src/linux/main.c src/linux/script.c src/linux/dispatch.c ${HLE_SOURCES} ${GEN_SOURCES} ${DLL_SOURCES})
  target_include_directories(civ3 PRIVATE src/linux ${HLE} ${PCRECOMP}/runtime/recomp32)
  # 64-bit file offsets: a 32-bit stat fails with EOVERFLOW on a disk with
  # 64-bit inode numbers (XFS, Btrfs, WSL's /mnt), and the game's files with it.
  target_compile_definitions(civ3 PRIVATE RECOMP_RETADDR=0u RECOMP_FLAT_MEMORY RECOMP_LOCAL_REGS HLE_WITH_FFMPEG)
  set_source_files_properties(src/linux/main.c src/linux/script.c ${HLE_SOURCES} PROPERTIES COMPILE_DEFINITIONS "_FILE_OFFSET_BITS=64")
  set(GEN_OPT "-O2" CACHE STRING "optimisation for the generated C")
  set_source_files_properties(${GEN_SOURCES} ${DLL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;${GEN_OPT};-fno-strict-aliasing;-fwrapv")
  target_compile_options(civ3 PRIVATE -fno-strict-aliasing -fwrapv)
  target_link_libraries(civ3 PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_TTF PkgConfig::FFMPEG pthread m)
  # Linked at 0x20000000: at the default 0x08048000 the 110 MB of code end
  # near 0x0F000000, and the heap that starts after them (at a random offset)
  # reached 0x10000000, where jgl.dll goes, in some runs.
  target_link_options(civ3 PRIVATE -Wl,-Ttext-segment=0x20000000)
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
