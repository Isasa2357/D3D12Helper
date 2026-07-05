#
# D3D12Helper test CMake
#
# Builds one test executable and registers each suite with CTest.
# The tests are intentionally kept outside src/ so consumers can add only
# include/ and src/ when embedding the library.
#
cmake_minimum_required(VERSION 3.20)

if(NOT TARGET D3D12Helper)
    project(D3D12HelperTests LANGUAGES CXX)

    if(NOT WIN32)
        message(FATAL_ERROR "D3D12Helper tests are Windows-only.")
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
            "${D3D12HELPER_PUBLIC_INCLUDE_DIR}"
            "${D3D12HELPER_INCLUDE_DIR}"
        PRIVATE
            "${D3D12HELPER_CORE_INCLUDE_DIR}"
            "${D3D12HELPER_FRAMEWORK_INCLUDE_DIR}"
    )

    target_compile_definitions(D3D12Helper PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(D3D12Helper PUBLIC d3d12 dxgi dxguid d3dcompiler dxcompiler user32)

    if(MSVC)
        target_compile_options(D3D12Helper PUBLIC /utf-8 /EHsc)
    endif()

    include(CTest)
    enable_testing()
endif()

file(GLOB D3D12HELPER_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
)

add_executable(d3d12helper_tests ${D3D12HELPER_TEST_SOURCES})
target_include_directories(d3d12helper_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(d3d12helper_tests PRIVATE D3D12Helper::D3D12Helper)
target_compile_definitions(d3d12helper_tests PRIVATE
    D3D12HELPER_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(d3d12helper_tests PRIVATE /utf-8 /EHsc)
endif()

d3d12helper_copy_dxc_runtime(d3d12helper_tests)

add_custom_command(TARGET d3d12helper_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../shaders"
        "$<TARGET_FILE_DIR:d3d12helper_tests>/shaders"
)

set(D3D12HELPER_TEST_SUITES
    FormatUtil
    DxgiUtil
    ThrowIfFailed
    Barrier
    Subresource
    Core
    Fence
    CommandContext
    DescriptorAllocator
    Resource
    Helpers
    UploadReadback
    UploadRing
    ShaderCompiler
    ComputePipeline
    GraphicsPipeline
    Processing
)

foreach(suite IN LISTS D3D12HELPER_TEST_SUITES)
    add_test(NAME ${suite} COMMAND d3d12helper_tests ${suite})
    set_tests_properties(${suite} PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:d3d12helper_tests>"
    )
endforeach()
