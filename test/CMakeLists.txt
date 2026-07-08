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
            "${D3D12HELPER_INCLUDE_DIR}"
    )

    target_compile_definitions(D3D12Helper PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(D3D12Helper PUBLIC d3d12 dxgi dxguid d3dcompiler dxcompiler user32)

    if(MSVC)
        target_compile_options(D3D12Helper PUBLIC /utf-8 /EHsc)
    endif()

    include(CTest)
    enable_testing()
else()
    get_filename_component(D3D12HELPER_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
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

set(D3D12HELPER_TEST_RUNTIME_TARGET D3D12HelperTestRuntimeAssets)
if(NOT TARGET ${D3D12HELPER_TEST_RUNTIME_TARGET})
    d3d12helper_find_dxc_runtime()

    set(D3D12HELPER_TEST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

    set(D3D12HELPER_TEST_RUNTIME_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${D3D12HELPER_TEST_RUNTIME_DIR}/D3D12Helper/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/../shaders"
            "${D3D12HELPER_TEST_RUNTIME_DIR}/D3D12Helper/shaders"
    )

    if(D3D12HELPER_COPY_DXC_RUNTIME AND D3D12HELPER_DXCOMPILER_DLL)
        list(APPEND D3D12HELPER_TEST_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${D3D12HELPER_DXCOMPILER_DLL}"
                "${D3D12HELPER_TEST_RUNTIME_DIR}"
        )
    endif()

    if(D3D12HELPER_COPY_DXC_RUNTIME AND D3D12HELPER_DXIL_DLL)
        list(APPEND D3D12HELPER_TEST_RUNTIME_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${D3D12HELPER_DXIL_DLL}"
                "${D3D12HELPER_TEST_RUNTIME_DIR}"
        )
    endif()

    add_custom_target(${D3D12HELPER_TEST_RUNTIME_TARGET} ALL
        ${D3D12HELPER_TEST_RUNTIME_COMMANDS}
        VERBATIM
    )
endif()

add_dependencies(d3d12helper_tests ${D3D12HELPER_TEST_RUNTIME_TARGET})

set(D3D12HELPER_TEST_SUITES
    ModuleHeaders
    FormatUtil
    DxgiUtil
    ThrowIfFailed
    Barrier
    Subresource
    Core
    Fence
    SharedFence
    CommandContext
    DescriptorAllocator
    Resource
    Helpers
    UploadReadback
    Transfer
    UploadRing
    ShaderCompiler
    ShaderReflection
    ComputePipeline
    GraphicsPipeline
    Presentation
    Binding
    Diagnostics
    CopyResolveMipmap
    ViewState
    Interop
    Hardening
    Processing
    ProcessingBlur
    ProcessingRegionEffect
    ProcessingRegionBlur
    ProcessingColorAdjust
    ProcessingKernelFilter
    ProcessingMask
    ProcessingThreshold
    ProcessingPyramid
    ProcessingPyramidBlur
    ProcessingPyramidRegionBlur
    AdvancedProcessing
    CoverageHardening
)

foreach(suite IN LISTS D3D12HELPER_TEST_SUITES)
    add_test(NAME ${suite} COMMAND d3d12helper_tests ${suite})
    set_tests_properties(${suite} PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:d3d12helper_tests>"
    )
endforeach()

option(D3D12HELPER_ENABLE_D3D11_INTEROP_TESTS "Build optional D3D12/D3D11 interop E2E tests" OFF)
set(D3D12HELPER_D3D11HELPER_GIT_REPOSITORY "https://github.com/Isasa2357/D3D11Helper.git" CACHE STRING "D3D11Helper git repository for optional interop tests")
set(D3D12HELPER_D3D11HELPER_GIT_TAG "main" CACHE STRING "D3D11Helper git tag, branch, or commit for optional interop tests")

if(D3D12HELPER_ENABLE_D3D11_INTEROP_TESTS)
    include(FetchContent)

    set(D3D11HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(D3D11HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(D3D11HELPER_ENABLE_D3D12_INTEROP_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        D3D11Helper
        GIT_REPOSITORY "${D3D12HELPER_D3D11HELPER_GIT_REPOSITORY}"
        GIT_TAG        "${D3D12HELPER_D3D11HELPER_GIT_TAG}"
    )
    FetchContent_MakeAvailable(D3D11Helper)

    add_executable(test_interop_d3d11 D3D11InteropE2E.cpp)
    target_include_directories(test_interop_d3d11 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_interop_d3d11 PRIVATE D3D12Helper::D3D12Helper D3D11Helper::D3D11Helper)
    if(MSVC)
        target_compile_options(test_interop_d3d11 PRIVATE /utf-8 /EHsc)
    endif()

    add_test(NAME InteropD3D11 COMMAND test_interop_d3d11)
    set_tests_properties(InteropD3D11 PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:test_interop_d3d11>"
        TIMEOUT 120
    )
endif()

if(D3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS)
    if(NOT D3D12HELPER_INSTALL)
        message(WARNING "D3D12 package smoke tests require D3D12HELPER_INSTALL=ON and will be skipped.")
    else()
        add_test(NAME PackageSmoke COMMAND
            ${CMAKE_COMMAND}
                -DD3D12HELPER_PACKAGE_SMOKE_ROOT_BUILD_DIR=${CMAKE_BINARY_DIR}
                -DD3D12HELPER_PACKAGE_SMOKE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_smoke
                -DD3D12HELPER_PACKAGE_SMOKE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/package_smoke_build/$<CONFIG>
                -DD3D12HELPER_PACKAGE_SMOKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/package_smoke_install/$<CONFIG>
                -DD3D12HELPER_PACKAGE_SMOKE_CONFIG=$<CONFIG>
                -DD3D12HELPER_PACKAGE_SMOKE_GENERATOR=${CMAKE_GENERATOR}
                -DD3D12HELPER_PACKAGE_SMOKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/PackageSmokeTest.cmake
        )
        set_tests_properties(PackageSmoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            TIMEOUT 180
        )
    endif()
endif()
