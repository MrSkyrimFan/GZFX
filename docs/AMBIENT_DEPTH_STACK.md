# Ambient Depth Stack

GZFX's ambient-depth pipeline runs in Skyrim's validated pre-UI HDR scene stage.

## Order

1. Half-resolution Horizon Occlusion analysis
2. Half-resolution screen-space Indirect Bounce analysis
3. Horizontal depth-aware denoise
4. Vertical depth-aware denoise
5. Full-resolution HDR composite
6. Full-resolution Ambient Contact
7. HDR Local Glow
8. Skyrim's downstream image-space processing and UI

## Quality tiers

- Low: 4 horizon directions, 3 depth steps
- Medium: 6 directions, 4 steps
- High: 8 directions, 4 steps
- Ultra: 12 directions, 6 steps

The expensive horizon and indirect-light search is intentionally half-resolution. The full-resolution depth buffer is sampled for view-space reconstruction and the results are depth-aware filtered before HDR composition.

## Scope

The implementation is independent GZFX engineering informed by public screen-space AO / horizon-occlusion / indirect-lighting research. It does not use ENB implementation details.

## Fail-soft behavior

If the pre-UI stage is not admitted, GZFX does not mutate the HDR scene at that point. The existing Present-time fallback remains available for the development modes that depend on Ambient Contact.
