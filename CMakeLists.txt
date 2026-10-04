cmake_minimum_required(VERSION 3.16)
project(witchspace C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(epcore STATIC core/ep_galaxy.c core/ep_desc.c core/ep_market.c core/ep_render.c core/ep_objects.c core/ep_world.c core/ep_flight.c core/ep_dust.c core/ep_combat.c core/ep_trade.c core/ep_ships.c core/ep_travel.c core/ep_chart.c core/ep_frame.c core/ep_commands.c core/ep_dsmap.c core/ep_station.c core/ep_rng.c core/ep_circle.c core/ep_title.c core/ep_boot.c core/ep_sound.c core/ep_adlib.c core/ep_adfx.c core/ep_commander.c core/ep_data.c)
target_include_directories(epcore PUBLIC core)

option(WS_WERROR "Treat warnings as errors (CI)" OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(epcore PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    -Wsign-conversion -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes)
  if(WS_WERROR)
    target_compile_options(epcore PRIVATE -Werror)
  endif()
endif()

# Test tools used by re/emu (differential tests against the original)
add_executable(ep_galdump tests/galdump.c)
target_link_libraries(ep_galdump PRIVATE epcore)
add_executable(ep_descdump tests/descdump.c)
target_link_libraries(ep_descdump PRIVATE epcore)
add_executable(ep_marketdump tests/marketdump.c)
target_link_libraries(ep_marketdump PRIVATE epcore)
add_executable(ep_renderdump tests/renderdump.c)
target_link_libraries(ep_renderdump PRIVATE epcore)
add_executable(ep_objdump tests/objdump.c)
target_link_libraries(ep_objdump PRIVATE epcore)
add_executable(ep_circledump tests/circledump.c)
target_link_libraries(ep_circledump PRIVATE epcore)
add_executable(ep_planetdump tests/planetdump.c)
target_link_libraries(ep_planetdump PRIVATE epcore)
add_executable(ep_titledump tests/titledump.c)
target_link_libraries(ep_titledump PRIVATE epcore)
add_executable(ep_cmdrdump tests/cmdrdump.c)
target_link_libraries(ep_cmdrdump PRIVATE epcore)

# The frontend: SDL2 over the core. Text uses the original's own glyphs (from ELITE.EXE).
# The AdLib's chip: Nuked OPL3 (LGPL-2.1+, third_party/nuked-opl3), for the frontend only
add_library(opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(opl3 PUBLIC third_party/nuked-opl3)
add_executable(witchspace src/main.c src/screen.c src/grf.c src/audio.c src/files.c)
target_link_libraries(witchspace PRIVATE epcore opl3)
if(EMSCRIPTEN)
  # The web build (web/shell.html): the page gets the game's files first, then calls main.
  # ASYNCIFY lets the frontend's waits (SDL_Delay) give the browser its turn; IDBFS keeps
  # the game's files and the commanders in the browser's storage.
  set(SDL_FLAGS "-sUSE_SDL=2")
  target_compile_options(witchspace PRIVATE ${SDL_FLAGS})
  target_link_options(witchspace PRIVATE ${SDL_FLAGS} -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sINVOKE_RUN=0
    -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -lidbfs.js "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','IDBFS']"
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
  set_target_properties(witchspace PROPERTIES SUFFIX ".html")
else()
  find_package(SDL2 REQUIRED)
  target_link_libraries(witchspace PRIVATE SDL2::SDL2)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(witchspace PRIVATE SDL2::SDL2main)
  endif()
  if(NOT WIN32)
    target_link_libraries(witchspace PRIVATE m)
  endif()
endif()
add_executable(ep_musicwav tests/musicwav.c)
target_link_libraries(ep_musicwav PRIVATE epcore opl3)
add_executable(ep_bootdump tests/bootdump.c)
target_link_libraries(ep_bootdump PRIVATE epcore)
add_executable(ep_flowdump tests/flowdump.c)
target_link_libraries(ep_flowdump PRIVATE epcore)
add_executable(ep_datadump tests/datadump.c)
target_link_libraries(ep_datadump PRIVATE epcore)
add_executable(ep_flow tests/flow.c)
target_link_libraries(ep_flow PRIVATE epcore)
enable_testing()
add_test(NAME flow COMMAND ep_flow)

add_executable(ep_subsys tests/subsys.c tests/statemap.c)
target_link_libraries(ep_subsys PRIVATE epcore)
target_include_directories(ep_subsys PRIVATE tests)

# The test tools read the original's tables from your copy (tests/testdata.c; EP_ORIGINAL
# overrides the folder); without it they exit 77, which ctest counts as skipped.
add_library(eptestdata OBJECT tests/testdata.c)
target_link_libraries(eptestdata PRIVATE epcore)
target_compile_definitions(eptestdata PRIVATE EP_SOURCE_ORIGINAL="${CMAKE_CURRENT_SOURCE_DIR}/original")
foreach(t ep_galdump ep_descdump ep_marketdump ep_renderdump ep_objdump ep_circledump ep_planetdump
          ep_titledump ep_cmdrdump ep_datadump ep_musicwav ep_bootdump ep_flowdump ep_flow ep_subsys)
  target_sources(${t} PRIVATE $<TARGET_OBJECTS:eptestdata>)
endforeach()
set_tests_properties(flow PROPERTIES SKIP_RETURN_CODE 77)

# The frontend and the test tools: the usual warnings (the core has stricter ones above)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(t witchspace ep_galdump ep_descdump ep_marketdump ep_renderdump ep_objdump ep_circledump
            ep_planetdump ep_titledump ep_cmdrdump ep_datadump ep_musicwav ep_bootdump ep_flowdump ep_flow
            ep_subsys)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(WS_WERROR)
      target_compile_options(${t} PRIVATE -Werror)
    endif()
  endforeach()
endif()
