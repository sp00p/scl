cmake_minimum_required(VERSION 3.16)
project(scl LANGUAGES C CXX)

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

add_executable(scl
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

target_include_directories(scl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(scl PRIVATE SDL2-static SDL2main)

# Copy assets to build directory
add_custom_command(TARGET scl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:scl>/assets
)

# Tests
enable_testing()

# GoogleTest
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

add_executable(test_lexer tests/test_lexer.cpp src/compiler/lexer.cpp src/compiler/error_handler.cpp)
target_include_directories(test_lexer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_lexer PRIVATE gtest gtest_main)
add_test(NAME test_lexer COMMAND test_lexer)

add_executable(test_parser tests/test_parser.cpp src/compiler/lexer.cpp src/compiler/parser.cpp src/compiler/error_handler.cpp)
target_include_directories(test_parser PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_parser PRIVATE gtest gtest_main)
add_test(NAME test_parser COMMAND test_parser)
