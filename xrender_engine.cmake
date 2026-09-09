get_filename_component(XRENDER_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

set(XRENDER_ENGINE_SOURCES
    "${XRENDER_ENGINE_ROOT}/engine/core/xrendercontext.cpp"
    "${XRENDER_ENGINE_ROOT}/engine/core/xvma_implementation.cpp"
)

set(XRENDER_ENGINE_HEADERS
    "${XRENDER_ENGINE_ROOT}/engine/core/xrendercontext.hpp"
    "${XRENDER_ENGINE_ROOT}/engine/camera/xorbit_camera_controller.hpp"
)

set(XRENDER_ENGINE_INCLUDE_DIRS
    "${XRENDER_ENGINE_ROOT}/engine"
)

set(XRENDER_ENGINE_SYSTEM_INCLUDE_DIRS
    "${XRENDER_ENGINE_ROOT}/third_party/VulkanMemoryAllocator/include"
)

set(XRENDER_ENGINE_SHADER_DIR "${XRENDER_ENGINE_ROOT}/shaders")

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Threads REQUIRED)

list(APPEND XRENDER_ENGINE_SYSTEM_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})

set(XRENDER_ENGINE_LIBRARIES
    Vulkan::Vulkan
    glfw
    glm::glm
    Threads::Threads
)

function(xrender_compile_shaders target output_dir)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
    if(NOT GLSLANG_VALIDATOR)
        message(WARNING "glslangValidator not found, SPIR-V modules will not be built")
        return()
    endif()

    file(MAKE_DIRECTORY "${output_dir}")

    set(patterns "")
    foreach(dir IN LISTS ARGN)
        foreach(stage vert frag comp geom tesc tese)
            list(APPEND patterns "${dir}/*.${stage}")
        endforeach()
    endforeach()

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${patterns})

    set(outputs "")
    foreach(source IN LISTS sources)
        get_filename_component(name "${source}" NAME_WE)
        get_filename_component(stage "${source}" LAST_EXT)
        string(REPLACE "." "" stage "${stage}")

        set(output_abs "${output_dir}/${name}_${stage}.spv")

        add_custom_command(
            OUTPUT "${output_abs}"
            COMMAND "${GLSLANG_VALIDATOR}" -V "${source}" -o "${output_abs}" --target-env vulkan1.2
            DEPENDS "${source}"
            COMMENT "Compiling ${name}.${stage}"
            VERBATIM
        )
        list(APPEND outputs "${output_abs}")
    endforeach()

    list(LENGTH outputs count)
    if(count EQUAL 0)
        message(WARNING "xrender_compile_shaders(${target}): no shaders found")
    endif()

    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()

function(xrender_engine_configure target)
    target_include_directories(${target} PRIVATE ${XRENDER_ENGINE_INCLUDE_DIRS})
    target_include_directories(${target} SYSTEM PRIVATE ${XRENDER_ENGINE_SYSTEM_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE ${XRENDER_ENGINE_LIBRARIES})
endfunction()
