cmake_minimum_required(VERSION 2.6)
set(CMAKE_BUILD_TYPE "Debug")


INCLUDE(FindPkgConfig)
PKG_SEARCH_MODULE(SDL2 REQUIRED sdl2)
INCLUDE_DIRECTORIES(${SDL2_INCLUDE_DIRS})

set_source_files_properties( mem.cpp PROPERTIES COMPILE_FLAGS " -O0 -UNDEBUG " )
add_executable(genemu genemu.cpp cpu.cpp vdp.cpp mem.cpp gfx.cpp ioports.cpp hw.c Z80/Z80.c m68k/m68kcpu.c m68k/m68kops.c m68k/m68kopac.c m68k/m68kopdm.c m68k/m68kopnz.c m68k/m68kdasm.c ym2612/ym2612.c)
target_link_libraries(genemu ${SDL2_LIBRARIES})
