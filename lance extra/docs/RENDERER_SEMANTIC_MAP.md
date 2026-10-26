# Renderer Semantic Mapping Pass

## Why this pass exists

GZFX has proven independent access to Skyrim's D3D11 device, swap chain, immediate context, main depth, HDR scene color and per-frame camera matrices.

Field testing of depth-only Ambient Visibility / SSIL showed an important limit: access to depth and final HDR color is sufficient for experimentation, but not sufficient evidence that GZFX is using the best native information available for integrated lighting.

This pass temporarily suppresses GZFX scene effects and maps stock Skyrim's renderer.

## Public renderer clues

Public Skyrim renderer work documents stock/native concepts including:

- a NORMAL_TAAMASK_SSRMASK render target used by vanilla SSAO;
- a MOTION_VECTOR render target;
- main scene depth plus later finalized opaque-depth handling;
- a vanilla SSAO chain;
- shadow depth targets;
- image-space post processing and reduction chains.

Modern SSGI implementations additionally depend on explicit surface normals, motion vectors/history, radiance and finalized depth. GZFX must independently locate and validate the corresponding stock resources instead of assuming depth-derived normals are sufficient.

## Evidence targets

The probe records:

- PS shader-resource bindings;
- CS shader-resource bindings;
- CS UAV bindings;
- render-target/depth bindings;
- current pixel/compute shader object;
- draw ordinal and viewport;
- resource size/format/bind flags;
- co-read frequency with the already validated main depth.

It writes:

- Diagnostics/renderer_semantics.txt
- Diagnostics/semantic_bindings.csv
- Diagnostics/semantic_candidate_XX_fYY.bmp

## Candidate hypotheses

These are hypotheses only until a real Skyrim capture confirms them.

- Full-resolution R8G8B8A8_UNORM resources frequently co-read with main depth are candidates for Skyrim's encoded normal/TAA/SSR mask target.
- Full-resolution R16G16_FLOAT resources are candidates for motion vectors.
- Full-resolution R32_FLOAT or R16_FLOAT resources are candidates for scalar depth/image-space intermediates.
- Large 4096x4096 / 512x512 depth-stencil resources are likely shadow resources.
- Half/quarter resolution scalar resources around a pass that reads main depth plus normals are candidates for vanilla SSAO intermediates.
- Small reduction chains may correspond to bloom/exposure/image-space analysis.

## Stock-capture rule

For this diagnostic build, GZFX scene mutation and the Present development overlay are suppressed. The evidence must describe stock Skyrim's renderer, not GZFX observing its own resources.

## Next architectural decision

Only after the native resources are semantically mapped should GZFX decide whether to:

- consume a stock normal/mask target directly;
- use motion vectors and temporal history for stable AO/SSIL;
- consume or replace vanilla SSAO intermediates;
- move AO/GI composition into ambient/diffuse lighting rather than multiplying final HDR color;
- construct missing material/G-buffer data when stock Skyrim does not expose enough information.
