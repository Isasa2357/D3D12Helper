cmake_minimum_required(VERSION 3.20)

project(D3D12HelperPackageSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(D3D12Helper CONFIG REQUIRED)

add_executable(d3d12helper_package_smoke main.cpp)
target_link_libraries(d3d12helper_package_smoke PRIVATE D3D12Helper::D3D12Helper)

if(MSVC)
    target_compile_options(d3d12helper_package_smoke PRIVATE /utf-8 /EHsc)
endif()
