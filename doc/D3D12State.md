# D3D12State

D3D12State provides small helpers for resource state classification and transition recording.

## Added in v1.7.0

- Resource state name helper
- Read/write state classification
- Conservative implicit promotion helper
- Tracked transition barrier helper
- Single and batch transition recording helpers

The helpers use the existing single-state `D3D12Resource` tracking model. They do not add full per-subresource state tracking.
