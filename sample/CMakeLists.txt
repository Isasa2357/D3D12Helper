#
# D3D12Helper sample CMake
#
# Normally this file is entered from the repository root CMakeLists.txt.
# It also supports standalone configuration from sample/.
#
cmake_minimum_required(VERSION 3.20)

if(NOT TARGET D3D12Helper)
    project(D3D12HelperSamples LANGUAGES CXX)

    if(NOT WIN32)
        message(FATAL_ERROR "D3D12Helper samples are Windows-only.")
    endif()

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    get_filename_component(D3D12HELPER_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    list(APPEND CMAKE_MODULE_PATH "${D3D12HELPER_REPO_ROOT}/cmake")
    include(D3D12HelperDxcRuntime)

    set(D3D12HELPER_INCLUDE_DIR        "${D3D12HELPER_REPO_ROOT}/include")
    set(D3D12HELPER_PUBLIC_INCLUDE_DIR "${D3D12HELPER_REPO_ROOT}/include/D3D12Helper")
    set(D3D12HELPER_CORE_INCLUDE_DIR   "${D3D12HELPER_PUBLIC_INCLUDE_DIR}/D3D12Core")
    set(D3D12HELPER_FRAMEWORK_INCLUDE_DIR "${D3D12HELPER_PUBLIC_INCLUDE_DIR}/D3D12Framework")
    set(D3D12HELPER_SOURCE_DIR         "${D3D12HELPER_REPO_ROOT}/src")

    file(GLOB D3D12HELPER_SOURCES CONFIGURE_DEPENDS
        "${D3D12HELPER_SOURCE_DIR}/*.cpp"
    )

    add_library(D3D12Helper STATIC ${D3D12HELPER_SOURCES})
    add_library(D3D12Helper::D3D12Helper ALIAS D3D12Helper)

    target_include_directories(D3D12Helper
        PUBLIC
            "${D3D12HELPER_INCLUDE_DIR}"
    )

    target_compile_definitions(D3D12Helper PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(D3D12Helper PUBLIC d3d12 dxgi dxguid d3dcompiler dxcompiler user32)

    if(MSVC)
        target_compile_options(D3D12Helper PUBLIC /utf-8 /EHsc)
    endif()
else()
    get_filename_component(D3D12HELPER_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

set(D3D12HELPER_SAMPLE_RUNTIME_TARGET D3D12HelperSampleRuntimeAssets)
if(NOT TARGET ${D3D12HELPER_SAMPLE_RUNTIME_TARGET})
    d3d12helper_find_dxc_runtime()

    set(D3D12HELPER_SAMPLE_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

    set(D3D12HELPER_SAMPLE_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${D3D12HELPER_SAMPLE_RUNTIME_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/../shaders"
            "${D3D12HELPER_SAMPLE_RUNTIME_DIR}/shaders"
    )

    if(D3D12HELPER_COPY_DXC_RUNTIME AND D3D12HELPER_DXCOMPILER_DLL)
        list(APPEND D3D12HELPER_SAMPLE_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${D3D12HELPER_DXCOMPILER_DLL}"
                "${D3D12HELPER_SAMPLE_RUNTIME_DIR}"
        )
    endif()

    if(D3D12HELPER_COPY_DXC_RUNTIME AND D3D12HELPER_DXIL_DLL)
        list(APPEND D3D12HELPER_SAMPLE_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${D3D12HELPER_DXIL_DLL}"
                "${D3D12HELPER_SAMPLE_RUNTIME_DIR}"
        )
    endif()

    add_custom_target(${D3D12HELPER_SAMPLE_RUNTIME_TARGET} ALL
        ${D3D12HELPER_SAMPLE_RUNTIME_COMMANDS}
        VERBATIM
    )
endif()

function(add_d3d12helper_sample target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name} PRIVATE D3D12Helper::D3D12Helper)
    target_compile_definitions(${target_name} PRIVATE
        D3D12HELPER_SAMPLE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8 /EHsc)
    endif()

    add_dependencies(${target_name} ${D3D12HELPER_SAMPLE_RUNTIME_TARGET})
endfunction()

add_d3d12helper_sample(D3D12Sample_01_HelloDevice         01_HelloDevice/main.cpp)
add_d3d12helper_sample(D3D12Sample_02_ComputeGrayscale    02_ComputeGrayscale/main.cpp)
add_d3d12helper_sample(D3D12Sample_03_HelloTriangle       03_HelloTriangle/main.cpp)
add_d3d12helper_sample(D3D12Sample_04_ParallelCompute     04_ParallelCompute/main.cpp)
add_d3d12helper_sample(D3D12Sample_05_BufferCompute       05_BufferCompute/main.cpp)
add_d3d12helper_sample(D3D12Sample_06_UploadRingStreaming 06_UploadRingStreaming/main.cpp)
add_d3d12helper_sample(D3D12Sample_07_ProcessingFusedConvertResize 07_ProcessingFusedConvertResize/main.cpp)
add_d3d12helper_sample(D3D12Sample_08_ProcessingP010Rgba16 08_ProcessingP010Rgba16/main.cpp)
add_d3d12helper_sample(D3D12Sample_09_ProcessingBlur 09_ProcessingBlur/main.cpp)
add_d3d12helper_sample(D3D12Sample_10_ProcessingRegionEffect 10_ProcessingRegionEffect/main.cpp)

add_d3d12helper_sample(D3D12Sample_11_ProcessingRegionBlur 11_ProcessingRegionBlur/main.cpp)
