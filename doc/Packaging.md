# D3D12Helper Packaging

D3D12Helper can be consumed by `add_subdirectory`, `FetchContent`, or installed CMake package config files.

## add_subdirectory

```cmake
add_subdirectory(path/to/D3D12Helper)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

## FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    D3D12Helper
    GIT_REPOSITORY https://github.com/Isasa2357/D3D12Helper.git
    GIT_TAG main
)
FetchContent_MakeAvailable(D3D12Helper)

target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

## Install and find_package

```bat
cmake -S . -B out/build/install -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_BUILD_SAMPLES=OFF ^
  -DD3D12HELPER_BUILD_TESTS=OFF ^
  -DD3D12HELPER_INSTALL=ON

cmake --build out/build/install --config Release --parallel
cmake --install out/build/install --config Release --prefix C:\Libraries\D3D12Helper
```

Consumer project:

```cmake
find_package(D3D12Helper CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE D3D12Helper::D3D12Helper)
```

Configure the consumer with:

```bat
cmake -S . -B out/build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Libraries\D3D12Helper
```

## Installed files

- `include/D3D12Helper/...`
- static library under the platform library directory
- CMake package files under `lib/cmake/D3D12Helper`
- processing shaders under `share/D3D12Helper/shaders`
