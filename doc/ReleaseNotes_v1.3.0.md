# D3D12Helper v1.3.0 Release Notes

## Summary

v1.3.0 adds presentation-layer wrappers.

## Added

- D3D12RenderTarget
- D3D12SwapChain
- Presentation CTest suite

## Scope

`D3D12RenderTarget` covers offscreen RTV / optional DSV creation, binding, clearing, viewport, and scissor helpers.

`D3D12SwapChain` covers HWND swap-chain wrapping, RTV management, resize, present, current backbuffer access, binding, clearing, viewport/scissor, and state transition helpers.

Existing low-level swap-chain helper APIs remain available.
