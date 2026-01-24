cmake_minimum_required(VERSION 3.16)
project(chip8 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build SDL2 from submodule
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL2 EXCLUDE_FROM_ALL)

# ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

add_executable(chip8
    src/main.cpp
    src/emulator/chip8.cpp
    src/emulator/debug_ui.cpp
    src/compiler/lexer.cpp
    src/compiler/parser.cpp
    src/compiler/codegen.cpp
    src/compiler/compiler.cpp
    src/compiler/error_handler.cpp
    src/compiler/source_map.cpp
    src/compiler/preprocessor.cpp
    src/compiler/optimizer.cpp
    src/config/runtime_config.cpp
    src/decompiler/decompiler.cpp
    src/disassembler/disassembler.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(chip8 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(chip8 PRIVATE SDL2-static SDL2main)
