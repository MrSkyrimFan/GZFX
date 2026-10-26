# Ambient Depth Stack

GZFX's ambient-depth pipeline runs in Skyrim's validated pre-UI HDR scene stage.

## Order

1. Full-resolution scene/depth capture
2. Edge-preserving half-resolution depth + HDR color prefilter
3. Half-resolution Horizon Occlusion analysis
4. Half-resolution screen-space Indirect Bounce analysis
5. Horizontal depth-aware denoise
6. Vertical depth-aware denoise
7. Edge-aware full-resolution HDR composite
8. Full-resolution Ambient Contact
9. HDR Local Glow
10. Skyrim's downstream image-space processing and UI

## Quality tiers

- Low: 3 horizon directions, 3 depth steps
- Medium: 4 directions, 3 steps
- High: 6 directions, 4 steps
- Ultra: 8 directions, 5 steps

Each half-resolution pixel receives a deterministic low-discrepancy spatial rotation and step offset. This breaks up the fixed-direction banding seen in the first field implementation without introducing frame-to-frame temporal jitter.

## Artifact controls

The input depth/HDR scene is first reduced to a cache-friendly working set. The closest depth sample in each 2x2 footprint is retained with its corresponding scene color.

After AO/indirect analysis and bilateral denoise, the full-resolution composite performs a four-tap depth-aware upscale using the full-resolution Skyrim depth. This is intended to keep the half-resolution field from bleeding across depth discontinuities or producing visible row/column structure.

## Strength response

Horizon Occlusion uses an exponential visibility response instead of direct linear multiplication. The live strength control is normalized to 0-1 so the top of the slider remains intentionally dramatic without making a normal scene collapse toward flat gray.

AO and SSIL radii use logarithmic sliders so the low/mid range receives substantially more tuning precision while the large-radius experimental end remains available.

## Performance evidence

The runtime records non-blocking D3D11 GPU timestamp queries for:

- depth/color prefilter
- horizon + indirect analysis
- horizontal denoise
- vertical denoise
- full-resolution composite
- total Ambient Depth cost

The latest resolved values are written to `Diagnostics/ambient_depth_stack.txt`.

## Research basis

The implementation is independent GZFX engineering informed by public screen-space AO, horizon-occlusion and indirect-lighting research. It does not use ENB implementation details.

## Fail-soft behavior

If the pre-UI stage is not admitted, GZFX does not mutate the HDR scene at that point. The existing Present-time fallback remains available for the development modes that depend on Ambient Contact.
