# XRender Engine

Drop-in-source Vulkan rendering engine.

Require Vulkan 1.2, GLFW 3, GLM, C++17, CMake 3.15+ and `glslangValidator`. 

## run

```sh
./build_and_run.sh
```

## include the engine

```cmake
include(path/to/xrender-engine/xrender_engine.cmake)
add_executable(my_app main.cpp ${XRENDER_ENGINE_SOURCES})
xrender_engine_configure(my_app)
xrender_compile_shaders(app_shaders "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
add_dependencies(my_app app_shaders)
```