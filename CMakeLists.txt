cmake_minimum_required(VERSION 3.16)
project(chip8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2 setup
set(SDL2_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(SDL2_LIB_DIR ${CMAKE_SOURCE_DIR}/lib)

add_executable(chip8
    src/main.cpp
    src/emulator/chip8.cpp
)

target_include_directories(chip8 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIR}
)

target_link_directories(chip8 PRIVATE ${SDL2_LIB_DIR})
target_link_libraries(chip8 SDL2 SDL2main)
