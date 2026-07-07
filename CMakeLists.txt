#
# D3D12Helper - root CMake
#
# Builds the library body, optional samples, and optional CTest tests.
# The library implementation lives in src/.  Public headers live in
# include/D3D12Helper/.
#
cmake_minimum_required(VERSION 3.20)

project(D3D12Helper
    VERSION 1.3.0
    LANGUAGES CXX
)

option(D3D12HELPER_BUILD_SAMPLES   "Build D3D12Helper samples"            ON)
option(D3D12HELPER_BUILD_TESTS     "Build D3D12Helper tests"              ON)
option(D3D12HELPER_ENABLE_WARNINGS "Enable compiler warnings for library" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(WARNING
        "D3D12Helper is a Direct3D 12 helper library and can only be built on Windows. "
        "No targets will be generated on this platform.")
    return()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(D3D12HelperDxcRuntime)

set(D3D12HELPER_INCLUDE_DIR        "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(D3D12HELPER_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/D3D12Helper")
set(D3D12HELPER_CORE_INCLUDE_DIR   "${D3D12HELPER_PUBLIC_INCLUDE_DIR}/D3D12Core")
set(D3D12HELPER_FRAMEWORK_INCLUDE_DIR "${D3D12HELPER_PUBLIC_INCLUDE_DIR}/D3D12Framework")
set(D3D12HELPER_SOURCE_DIR         "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB D3D12HELPER_SOURCES CONFIGURE_DEPENDS
    "${D3D12HELPER_SOURCE_DIR}/*.cpp"
)

add_library(D3D12Helper STATIC ${D3D12HELPER_SOURCES})
add_library(D3D12Helper::D3D12Helper ALIAS D3D12Helper)

target_include_directories(D3D12Helper
    PUBLIC
        "${D3D12HELPER_INCLUDE_DIR}"
)

target_compile_definitions(D3D12Helper
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(D3D12Helper
    PUBLIC
        d3d12
        dxgi
        dxguid
        d3dcompiler
        dxcompiler
        user32
)

if(MSVC)
    target_compile_options(D3D12Helper PUBLIC /utf-8 /EHsc)
    if(D3D12HELPER_ENABLE_WARNINGS)
        target_compile_options(D3D12Helper PRIVATE /W4)
    endif()
endif()

if(D3D12HELPER_BUILD_SAMPLES)
    add_subdirectory(sample)
endif()

if(D3D12HELPER_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()
