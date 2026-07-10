#
# D3D12Helper - root CMake
#
# Builds the library body, optional samples, and optional CTest tests.
# The library implementation lives in src/.  Public headers live in
# include/D3D12Helper/.
#
cmake_minimum_required(VERSION 3.20)

project(D3D12Helper
    VERSION 1.13.0
    LANGUAGES CXX
)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(D3D12HELPER_INSTALL_DEFAULT ON)
    set(D3D12HELPER_PACKAGE_SMOKE_DEFAULT ON)
else()
    set(D3D12HELPER_INSTALL_DEFAULT OFF)
    set(D3D12HELPER_PACKAGE_SMOKE_DEFAULT OFF)
endif()

option(D3D12HELPER_BUILD_SAMPLES   "Build D3D12Helper samples"            ON)
option(D3D12HELPER_BUILD_TESTS     "Build D3D12Helper tests"              ON)
option(D3D12HELPER_ENABLE_WARNINGS "Enable compiler warnings for library" ON)
option(D3D12HELPER_INSTALL         "Enable D3D12Helper install rules"     ${D3D12HELPER_INSTALL_DEFAULT})
option(D3D12HELPER_ENABLE_PACKAGE_SMOKE_TESTS "Enable install/find_package smoke tests" ${D3D12HELPER_PACKAGE_SMOKE_DEFAULT})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(WARNING
        "D3D12Helper is a Direct3D 12 helper library and can only be built on Windows. "
        "No targets will be generated on this platform.")
    return()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
        $<BUILD_INTERFACE:${D3D12HELPER_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
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

if(D3D12HELPER_INSTALL)
    set(D3D12HELPER_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/D3D12Helper")

    install(TARGETS D3D12Helper
        EXPORT D3D12HelperTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(DIRECTORY "${D3D12HELPER_INCLUDE_DIR}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shaders/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/D3D12Helper/shaders"
        OPTIONAL
    )

    install(EXPORT D3D12HelperTargets
        FILE D3D12HelperTargets.cmake
        NAMESPACE D3D12Helper::
        DESTINATION "${D3D12HELPER_INSTALL_CMAKEDIR}"
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/D3D12HelperConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/D3D12HelperConfig.cmake"
        INSTALL_DESTINATION "${D3D12HELPER_INSTALL_CMAKEDIR}"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/D3D12HelperConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/D3D12HelperConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/D3D12HelperConfigVersion.cmake"
        DESTINATION "${D3D12HELPER_INSTALL_CMAKEDIR}"
    )
endif()

if(D3D12HELPER_BUILD_SAMPLES)
    add_subdirectory(sample)
endif()

if(D3D12HELPER_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()
