cmake_minimum_required(VERSION 3.20)

function(d3d12helper_package_smoke_require var_name)
    if(NOT DEFINED ${var_name} OR "${${var_name}}" STREQUAL "")
        message(FATAL_ERROR "${var_name} is required")
    endif()
endfunction()

function(d3d12helper_package_smoke_run)
    message(STATUS "Running: ${ARGN}")
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed with exit code ${result}: ${ARGN}")
    endif()
endfunction()

d3d12helper_package_smoke_require(D3D12HELPER_PACKAGE_SMOKE_ROOT_BUILD_DIR)
d3d12helper_package_smoke_require(D3D12HELPER_PACKAGE_SMOKE_SOURCE_DIR)
d3d12helper_package_smoke_require(D3D12HELPER_PACKAGE_SMOKE_BINARY_DIR)
d3d12helper_package_smoke_require(D3D12HELPER_PACKAGE_SMOKE_INSTALL_PREFIX)
d3d12helper_package_smoke_require(D3D12HELPER_PACKAGE_SMOKE_CONFIG)

file(REMOVE_RECURSE "${D3D12HELPER_PACKAGE_SMOKE_INSTALL_PREFIX}")
file(REMOVE_RECURSE "${D3D12HELPER_PACKAGE_SMOKE_BINARY_DIR}")

d3d12helper_package_smoke_run(
    "${CMAKE_COMMAND}"
    --install "${D3D12HELPER_PACKAGE_SMOKE_ROOT_BUILD_DIR}"
    --config "${D3D12HELPER_PACKAGE_SMOKE_CONFIG}"
    --prefix "${D3D12HELPER_PACKAGE_SMOKE_INSTALL_PREFIX}"
)

set(configure_args
    -S "${D3D12HELPER_PACKAGE_SMOKE_SOURCE_DIR}"
    -B "${D3D12HELPER_PACKAGE_SMOKE_BINARY_DIR}"
    -D "CMAKE_PREFIX_PATH=${D3D12HELPER_PACKAGE_SMOKE_INSTALL_PREFIX}"
)

if(DEFINED D3D12HELPER_PACKAGE_SMOKE_GENERATOR AND NOT "${D3D12HELPER_PACKAGE_SMOKE_GENERATOR}" STREQUAL "")
    list(APPEND configure_args -G "${D3D12HELPER_PACKAGE_SMOKE_GENERATOR}")
endif()

if(DEFINED D3D12HELPER_PACKAGE_SMOKE_GENERATOR_PLATFORM AND NOT "${D3D12HELPER_PACKAGE_SMOKE_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND configure_args -A "${D3D12HELPER_PACKAGE_SMOKE_GENERATOR_PLATFORM}")
endif()

d3d12helper_package_smoke_run("${CMAKE_COMMAND}" ${configure_args})

d3d12helper_package_smoke_run(
    "${CMAKE_COMMAND}"
    --build "${D3D12HELPER_PACKAGE_SMOKE_BINARY_DIR}"
    --config "${D3D12HELPER_PACKAGE_SMOKE_CONFIG}"
    --parallel
)
