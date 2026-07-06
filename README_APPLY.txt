D3D12Helper RegionBlur overlay
==============================

This overlay assumes that the D3D12 Processing infra + Blur patch and the
D3D12 RegionEffect patch have already been applied.

Apply
-----
Copy this overlay into the D3D12Helper repository root and overwrite files.

Then run:

    build_test_and_push_if_passed.cmd

Added
-----
- include/D3D12Helper/D3D12Processing/D3D12RegionBlur.hpp
- src/D3D12RegionBlur.cpp
- shaders/D3D12Processing/RegionBlurBlendRgba.hlsl
- sample/11_ProcessingRegionBlur/main.cpp
- test/test_ProcessingRegionBlur.cpp

Updated
-------
- include/D3D12Helper/D3D12Processing/D3D12Processing.hpp
- sample/CMakeLists.txt
- test/CMakeLists.txt

Commit message
--------------
Add D3D12 Processing region blur
