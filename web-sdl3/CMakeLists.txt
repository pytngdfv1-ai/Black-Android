
cmake_minimum_required(VERSION 3.22)
project(BLACK_SDL3_Web LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SDL3_SOURCE_DIR OR
   NOT EXISTS "${SDL3_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "No se encontro SDL3.")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

add_subdirectory(
    "${SDL3_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/SDL3"
    EXCLUDE_FROM_ALL
)

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE INTERNAL "")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

add_executable(black_sdl3_probe main.cpp)

target_link_libraries(
    black_sdl3_probe
    PRIVATE SDL3::SDL3
)

target_link_options(black_sdl3_probe PRIVATE
    "-sALLOW_MEMORY_GROWTH=1"
    "-sUSE_WEBGL2=1"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
    "-sASSERTIONS=1"
    "--shell-file"
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)

set_target_properties(black_sdl3_probe PROPERTIES
    OUTPUT_NAME "index"
)
