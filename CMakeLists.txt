cmake_minimum_required(VERSION 3.16)
project(chip8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2 setup
set(SDL2_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(SDL2_LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

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
    ${CMAKE_SOURCE_DIR}/include/SDL2
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_directories(chip8 PRIVATE ${SDL2_LIB_DIR})
target_link_libraries(chip8 SDL2 SDL2main)
