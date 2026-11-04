find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

# Compile the real Civ3 host against a tiny synthetic guest, never the game.
add_executable(civ3_host_smoke
  ../src/linux/main.c ../src/linux/script.c host_fixture.c)
target_link_libraries(civ3_host_smoke PRIVATE civ3_hle)
add_test(NAME civ3_host_smoke
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/host_smoke.py
          $<TARGET_FILE:civ3_host_smoke>)

# Exercise the shared loader, 64-bit memory/ABI and GDI paths on each host OS.
foreach(_test win32hle pe_loader host gdi)
  add_executable(hle_${_test}_test ${HLE}/${_test}_selftest.c)
  target_link_libraries(hle_${_test}_test PRIVATE civ3_hle)
  add_test(NAME hle_${_test} COMMAND hle_${_test}_test)
endforeach()
set_tests_properties(civ3_host_smoke hle_win32hle hle_pe_loader hle_host hle_gdi
  PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 60)
