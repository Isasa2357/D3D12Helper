cmake_minimum_required(VERSION 3.20)

project(D3D12HelperPackageSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(D3D12Helper CONFIG REQUIRED)

if(NOT D3D12Helper_SHADER_DIR MATCHES "D3D12Helper")
    message(FATAL_ERROR "D3D12Helper_SHADER_DIR must be namespaced: ${D3D12Helper_SHADER_DIR}")
endif()

if(NOT EXISTS "${D3D12Helper_PROCESSING_SHADER_DIR}")
    message(FATAL_ERROR "D3D12Helper_PROCESSING_SHADER_DIR does not exist: ${D3D12Helper_PROCESSING_SHADER_DIR}")
endif()

add_executable(d3d12helper_package_smoke main.cpp)
target_link_libraries(d3d12helper_package_smoke PRIVATE D3D12Helper::D3D12Helper)

if(MSVC)
    target_compile_options(d3d12helper_package_smoke PRIVATE /utf-8 /EHsc)
endif()
