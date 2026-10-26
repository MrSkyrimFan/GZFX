# Native Surface + Temporal Foundation

## Field evidence source

Validated from the stock-render semantic capture produced by GZFX 0.0.15 at 2560x1440.

The main opaque scene repeatedly binds this MRT set together with the already-validated main depth target:

- SV_Target0: R16G16B16A16_FLOAT — main HDR scene color
- SV_Target1: R16G16_FLOAT — native motion vectors
- SV_Target2: R8G8B8A8_UNORM — native normal + TAA/SSR mask target
- SV_Target3: R8G8_UNORM — native auxiliary target; semantics not yet promoted
- DSV: R24G8_TYPELESS — main scene depth

The slot ordering matches public Skyrim shader output structure, and the slot-2 target visually decodes into coherent view-space surface normals across terrain, rocks, trees, and actors.

## Native normal usage

GZFX now consumes the stock R8G8B8A8 normal target directly when it was observed in the current frame.

The RG channels are decoded as the algebraic inverse of Skyrim's vanilla two-channel normal encoding. Depth-derived normals remain only as a fail-soft fallback when the native target is unavailable.

This removes a major instability source from the previous AO/SSIL implementation: four independent depth samples were previously used to estimate the receiver normal every frame.

## Native motion usage

GZFX consumes the stock R16G16_FLOAT motion-vector target when it was observed in the current frame.

Motion vectors reproject the half-resolution AO/SSIL history using:

previousUV = currentUV + motion.xy

This convention matches public Skyrim screen-space temporal implementations.

## Temporal history

Two half-resolution history surfaces are ping-ponged frame to frame.

Each history set stores:

- AO
- indirect lighting
- view-space depth
- view-space normal

History is rejected when:

- reprojected UV leaves the frame
- relative view-depth mismatch exceeds the admission threshold
- normal agreement is too low
- motion is extremely large

Accepted history is neighborhood-clamped around the current AO/SSIL signal before blending. Current-frame weight increases with motion so rapidly moving pixels adapt faster.

## Spatial filtering

Temporal output becomes the input to the existing two-pass bilateral denoise. Native normals are also available to the analysis and temporal stages, while the spatial filter continues to use depth discontinuities as its primary rejection signal.

## Semantic re-capture

The first stable gameplay window is captured stock, then GZFX scene effects automatically resume.

Shift + Numpad 5 requests an additional short stock semantic capture. This allows controlled captures after changing time of day, entering an interior, looking at water, or reaching another renderer state without requiring a dedicated diagnostic-only build.

## Fail-soft behavior

If current-frame native normals are not available:
- GZFX falls back to depth-derived normals.

If current-frame native motion is not available:
- temporal accumulation is disabled for that frame;
- spatial AO/SSIL still runs.

The pre-UI stage admission and Present fallback rules remain unchanged.
