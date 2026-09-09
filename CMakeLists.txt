cmake_minimum_required(VERSION 3.15)
project(XRenderDemo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(XRENDER_COMPILE_SHADERS "Compile GLSL shaders to SPIR-V at build time" ON)

if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g)
    else()
        add_compile_options(-O3)
    endif()
endif()

if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HOMEBREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
    endif()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/xrender_engine.cmake)

set(XRENDER_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Directory SPIR-V modules are written to")

add_executable(xrender_demo
    examples/demo.cpp
    ${XRENDER_ENGINE_SOURCES}
    ${XRENDER_ENGINE_HEADERS}
)

xrender_engine_configure(xrender_demo)

set_target_properties(xrender_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${XRENDER_SHADER_OUTPUT_DIR}")

if(XRENDER_COMPILE_SHADERS)
    xrender_compile_shaders(xrender_shaders "${XRENDER_SHADER_OUTPUT_DIR}"
        "${XRENDER_ENGINE_SHADER_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
    )
    add_dependencies(xrender_demo xrender_shaders)
endif()
