cmake_minimum_required(VERSION 3.16)
project(eliteplus C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(epcore STATIC core/ep_galaxy.c core/ep_desc.c core/ep_market.c core/ep_tables.c)
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
