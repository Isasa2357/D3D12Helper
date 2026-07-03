#
# D3D12Helper サンプル用 CMake
#
#   ライブラリ本体（include/**/*.cpp）を static library にまとめ、
#   各サンプルをそれにリンクする。Windows / MSVC 専用。
#
# ビルド例（"x64 Native Tools Command Prompt for VS" などから）:
#   cd sample
#   cmake -S . -B build -A x64
#   cmake --build build --config Release
#
# 生成物:
#   build/Release/01_HelloDevice.exe
#   build/Release/02_ComputeGrayscale.exe
#
cmake_minimum_required(VERSION 3.20)
project(D3D12HelperSamples LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "D3D12Helper is Windows-only (Direct3D 12).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# リポジトリの include/ （sample/ の 1 つ上）
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(INCLUDE_DIR "${REPO_ROOT}/include")

# ---- ライブラリ本体 ----
file(GLOB_RECURSE D3D12HELPER_SOURCES CONFIGURE_DEPENDS
    "${INCLUDE_DIR}/D3D12Core/*.cpp"
    "${INCLUDE_DIR}/D3D12Framework/*.cpp")

add_library(D3D12Helper STATIC ${D3D12HELPER_SOURCES})

target_include_directories(D3D12Helper PUBLIC
    "${INCLUDE_DIR}"
    "${INCLUDE_DIR}/D3D12Core"
    "${INCLUDE_DIR}/D3D12Framework")

# 必要なシステムライブラリ。
# （ソースの #pragma comment(lib, ...) でも指定されるが、明示しておく）
target_link_libraries(D3D12Helper PUBLIC
    d3d12 dxgi dxguid d3dcompiler dxcompiler)

# UTF-16 / UTF-8 混在ソースの警告(C4819)対策と、例外有効化。
if(MSVC)
    target_compile_options(D3D12Helper PUBLIC /utf-8 /EHsc)
endif()

# ---- サンプル ----
function(add_d3d12_sample name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE D3D12Helper)
endfunction()

add_d3d12_sample(01_HelloDevice        01_HelloDevice/main.cpp)
add_d3d12_sample(02_ComputeGrayscale   02_ComputeGrayscale/main.cpp)
add_d3d12_sample(03_HelloTriangle      03_HelloTriangle/main.cpp)
add_d3d12_sample(04_ParallelCompute    04_ParallelCompute/main.cpp)
add_d3d12_sample(05_BufferCompute      05_BufferCompute/main.cpp)
add_d3d12_sample(06_UploadRingStreaming 06_UploadRingStreaming/main.cpp)

# 03 はウィンドウを作るので user32（CreateWindowEx / MessageBox 等）が必要
target_link_libraries(03_HelloTriangle PRIVATE user32)
