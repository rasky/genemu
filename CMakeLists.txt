cmake_minimum_required(VERSION 2.6)
set(CMAKE_BUILD_TYPE "Debug")

include(FindSDL)
find_package(SDL REQUIRED)
include_directories(${SDL_INCLUDE_DIR})
add_executable(genemu genemu.cpp vdp.cpp hw.c m68k/m68kcpu.c m68k/m68kops.c m68k/m68kopac.c m68k/m68kopdm.c m68k/m68kopnz.c)
target_link_libraries(genemu ${SDL_LIBRARY} SDL_gfx)
