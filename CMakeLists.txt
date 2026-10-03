cmake_minimum_required(VERSION 3.16)
project(eliteplus C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(epcore STATIC core/ep_galaxy.c core/ep_desc.c core/ep_market.c core/ep_render.c core/ep_objects.c core/ep_world.c core/ep_rng.c core/ep_circle.c core/ep_title.c core/ep_commander.c core/ep_tables.c)
target_include_directories(epcore PUBLIC core)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(epcore PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    -Wsign-conversion -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes)
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

# The port: SDL2 frontend over the core. The font is embedded, so the binary has no runtime
# asset paths.
set(FONT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/Exo2.ttf)
set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/font_data.c)
add_custom_command(
  OUTPUT ${FONT_C}
  COMMAND ${CMAKE_COMMAND} -DIN=${FONT_SRC} -DOUT=${FONT_C} -DNAME=font_ttf -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${FONT_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake)
find_package(SDL2 REQUIRED)
add_executable(eliteplus src/main.c src/gfx.c src/font.c third_party/stb_truetype_impl.c ${FONT_C})
target_link_libraries(eliteplus PRIVATE epcore SDL2::SDL2)
if(TARGET SDL2::SDL2main)
  target_link_libraries(eliteplus PRIVATE SDL2::SDL2main)
endif()
if(NOT WIN32)
  target_link_libraries(eliteplus PRIVATE m)
endif()
add_executable(ep_subsys tests/subsys.c tests/statemap.c)
target_link_libraries(ep_subsys PRIVATE epcore)
target_include_directories(ep_subsys PRIVATE tests)
