# Retained from the early macOS branch, adapted to the verified POSIX host.
# All fixtures below are synthetic; no commercial executable or game assets.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_library(civ3_fixture_hle STATIC ${HLE_SOURCES})
target_include_directories(civ3_fixture_hle PUBLIC ${HLE} ${PCRECOMP}/runtime/recomp32)
target_compile_definitions(civ3_fixture_hle PUBLIC RECOMP_RETADDR=0u
                           RECOMP_MEM_BASE=0x100000000000ull HLE_WITH_FFMPEG)
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
