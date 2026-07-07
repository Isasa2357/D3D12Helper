# D3D12Helper Test Coverage

This document summarizes the CTest suite coverage as of v1.8.1.

## Core / Foundation

- ModuleHeaders
- FormatUtil
- DxgiUtil
- ThrowIfFailed
- Barrier
- Subresource
- Core
- Fence
- SharedFence
- CommandContext

## GPU / Resource

- DescriptorAllocator
- Resource
- Helpers
- UploadReadback
- Transfer
- UploadRing
- ShaderCompiler
- ComputePipeline
- GraphicsPipeline
- CopyResolveMipmap
- ViewState
- Binding

## Presentation / Diagnostics / Interop

- Presentation
- Diagnostics
- Interop

## Processing

- Processing
- ProcessingBlur
- ProcessingRegionEffect
- ProcessingRegionBlur
- ProcessingColorAdjust
- ProcessingKernelFilter
- ProcessingMask
- ProcessingThreshold
- ProcessingPyramid
- ProcessingPyramidBlur

## Hardening

v1.8.1 adds the Hardening suite for edge cases and invalid input checks across shared handles, state transitions, view descriptors, copy, resolve, and mipmap helpers.
