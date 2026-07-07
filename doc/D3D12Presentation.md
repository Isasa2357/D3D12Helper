# D3D12Presentation

D3D12Presentation is the canonical module for render-target and swap-chain helpers.

## Added in v1.3.0

- D3D12RenderTarget
- D3D12SwapChain

`D3D12RenderTarget` provides an offscreen render target with RTV / optional DSV descriptors, viewport/scissor helpers, bind, and clear helpers.

`D3D12SwapChain` wraps the existing low-level swap-chain helper and adds backbuffer RTV management, resize, present, current backbuffer access, bind, clear, viewport/scissor, and state transition helpers.

Existing `D3D12Framework/D3D12SwapChainHelper.hpp` paths remain valid for v1.x compatibility.
