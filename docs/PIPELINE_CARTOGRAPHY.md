# GZFX Renderer Pipeline Cartography

## Purpose

This phase deliberately pauses artistic AO/SSIL tuning.

The goal is to reconstruct Skyrim SE/AE's practical D3D11 frame graph from live evidence before deciding where future GZFX lighting systems belong.

The cartographer runs only during the same short stock-only semantic windows already controlled by the semantic probe. It does not keep the heavy pass recorder active during normal gameplay.

## Evidence model

For each captured draw, dispatch, copy, resolve, clear, mip-generation event, or command-list execution, GZFX records:

- frame and execution order;
- VS / HS / DS / GS / PS / CS object identity;
- stable shader bytecode hash when shader creation was observed;
- SRV reads across all six shader stages;
- RTV, DSV, pixel-UAV, and compute-UAV outputs;
- subresource view ranges where D3D11 exposes them;
- constant-buffer bindings across all six shader stages;
- sampler-state fingerprints;
- blend, depth/stencil, and rasterizer-state fingerprints;
- viewport and primitive topology;
- copy / resolve lineage;
- resource descriptor, lifetime identity, and creation owner module where observed.

The state tracker mirrors D3D11 resource-conflict behavior so a resource automatically nulled by the runtime is not falsely reported as an active read.

## Generated evidence

Each semantic capture produces:

- `pipeline_summary*.txt`
- `pipeline_passes*.csv`
- `pipeline_accesses*.csv`
- `pipeline_constant_buffers*.csv`
- `pipeline_samplers*.csv`
- `pipeline_resources*.csv`
- `pipeline_shaders*.csv`
- `pipeline_edges*.csv`

The unsuffixed set belongs to the automatic startup capture. Manual Shift+Numpad5 captures receive the same `_manual_####` suffix as the existing semantic evidence.

## Current known anchors

These are evidence-backed anchors, not guesses:

- main HDR scene-color family;
- main depth family;
- Skyrim motion-vector MRT target;
- Skyrim encoded screen-space/view-space normal MRT target;
- current auxiliary MRT target whose exact semantics remain unresolved;
- validated per-frame camera constant buffer.

The old wording that automatically treated the normal target's remaining channels as TAA/SSR masks is no longer accepted as proven. Channel semantics must be established from field evidence.

## Research-directed target families

Public Skyrim renderer research indicates that the stock renderer has distinct families and stages worth locating in our own traces, including:

- SSAO camera-Z and mip generation;
- raw AO, temporal/previous AO, blur, and composite stages;
- raw indirect / indirect-lighting buffers and history/swap resources;
- finished opaque depth used after the z-prepass;
- motion vectors and screen-space normals;
- directional, spot, and omnidirectional shadow maps;
- water reflections, underwater/water blend, and water temporal history;
- volumetric-lighting half/quarter-resolution chains;
- HDR downsample, luminance, light adaptation, bloom/blur, and tonemap stages;
- TAA and image-space composition.

These names are research leads only. GZFX must promote a semantic only after its own controlled captures establish the writer, consumers, chronology, dimensions/formats, and state dependence.

## Controlled comparison strategy

Use the same location/camera whenever possible and capture discrete states:

1. exterior daylight, still camera;
2. exterior daylight, controlled slow camera motion;
3. exterior night, same view;
4. water/reflection-heavy view;
5. hard directional-shadow view;
6. interior;
7. later: fog/weather and other special image-space states.

The numbered manual captures can then be diffed by pass signature, shader hash, resource descriptors, and producer-consumer graph rather than by pointer address alone.

## Interpretation rule

Do not tune AO, SSIL, temporal weights, or UI controls from this evidence phase.

First answer:

- which pass creates the resource;
- which shader/state owns that pass;
- what consumes it;
- whether it is copied, resolved, downsampled, temporally recycled, or composited;
- whether its behavior changes between controlled states;
- where the stock renderer considers geometry depth final;
- where ambient/directional/indirect/image-space lighting is actually composed.

Only then decide where GZFX effects should replace, augment, or avoid stock behavior.

## Fail-soft notes

The recorder has a bounded pass count and only runs during short semantic windows.

If `ExecuteCommandList` appears, the report flags it because work recorded inside a deferred command list can be opaque to the immediate-context hook. That is evidence of a coverage boundary, not permission to pretend the hidden work did not happen.
