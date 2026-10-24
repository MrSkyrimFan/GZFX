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
- decoded sampler-state descriptors;
- decoded blend, depth/stencil, and rasterizer-state descriptors;
- viewport and primitive topology;
- copy / resolve lineage;
- DSV read-only depth/stencil flags and effective depth/stencil write classification;
- fingerprints of CPU-visible small-buffer updates from Map/Unmap and UpdateSubresource;
- resource descriptor, lifetime identity, and D3D debug name when present.

The state tracker mirrors D3D11 resource-conflict behavior so a resource automatically nulled by the runtime is not falsely reported as an active read.

## Generated evidence

Each semantic capture produces:

- `pipeline_summary*.txt`
- `pipeline_passes*.csv`
- `pipeline_accesses*.csv`
- `pipeline_constant_buffers*.csv`
- `pipeline_samplers*.csv`
- `pipeline_sampler_states*.csv`
- `pipeline_blend_states*.csv`
- `pipeline_depth_states*.csv`
- `pipeline_raster_states*.csv`
- `pipeline_buffer_updates*.csv`
- `pipeline_resources*.csv`
- `pipeline_shaders*.csv`
- `pipeline_edges*.csv`
- `pipeline_signatures*.csv`
- `pipeline_landmarks*.csv`
- `known_surfaces*.txt` plus known MRT/depth BMP previews
- `atlas_surfaces*.txt` plus structural landmark BMP previews

The unsuffixed set belongs to the automatic startup capture. Manual Shift+Numpad5 captures receive the same `_manual_####` suffix as the existing semantic evidence.

## Safe field mode

The field build intentionally does **not** enable the experimental device-creation vtable interception lane.

An early CI campaign showed that creation interception could destabilize ordinary D3D11 WARP harnesses even though the execution cartographer itself was sound. The risky lane is therefore gated behind `GZFX_EXPERIMENTAL_DEVICE_CREATION_HOOKS` and must remain off for user field tests.

Safe mode still captures the execution-level frame graph, live resource/view descriptors, debug names when present, current shader object identities, producer/consumer lineage, render states, copies/resolves, and CPU buffer update fingerprints.

Pass signatures in safe mode are intended for comparisons between the numbered captures from the **same Skyrim process/session**. Do not compare those signatures across separate launches as if pointer identities were globally stable.

`ExecuteCommandList` remains the practical coverage boundary detector. If command-list playback is observed, the report explicitly says immediate-context cartography may not include the work originally recorded on a deferred context.

## Field Cartography 001 result

The first controlled 0.0.17 Skyrim campaign is preserved in:

`docs/FIELD_CARTOGRAPHY_001.md`

The run promoted or strongly constrained:

- the stock four-target opaque MRT;
- Camera-Z/depth-pyramid construction;
- temporal RawAO and AO blur structure;
- raw indirect lighting;
- the full-resolution AO/indirect/shadow/reflection -> HDR lighting composite;
- exterior 3D volumetric generation and temporal screen resolve;
- reflection blur structure;
- main-depth copy chronology;
- the exterior/interior volumetric split.

0.0.18 therefore extends cartography into a **semantic atlas** rather than adding more effect tuning.

### 0.0.18 evidence corrections

- semantic and cartography resource IDs use canonical COM identity;
- semantic counters reset per capture;
- cartography reports capture-local resources/shaders separately from session-known totals;
- constant-buffer uses are correlated with the latest CPU update fingerprint;
- structural landmarks are identified by resource/dependency/chronology patterns, not one-launch pointer constants;
- mask-like landmarks and known MRT surfaces receive channel-level previews/statistics.

Normal-target B/A channels and the auxiliary MRT remain unproven until this channel evidence is analyzed.

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

## Public research target catalog

These names come from current public Skyrim renderer research and are **landmarks only**. GZFX does not claim a live resource/pass matches one until its own capture graph proves the chronology and dependencies.

High-value stage families to look for in the captured signatures:

### Geometry / shadows

- directional-light shadowmaps;
- spot-light shadowmaps;
- omnidirectional-light shadowmaps;
- main depth;
- shadowmasks;
- finished opaque depth after alpha-tested geometry.

### Stock ambient occlusion

- SSAO CameraZ;
- SSAO CameraZ + mip generation compute;
- SSAO RawAO;
- SSAO RawAO NoTemporal;
- SSAO RawAO compute;
- horizontal/vertical AO blur, graphics and compute variants;
- SAO composite;
- fog/SAO composite variants.

### Stock screen-space indirect lighting

- SIL RawInd;
- SIL Composite.

### Temporal

- TemporalAA;
- TemporalAA UI;
- TemporalAA Water.

### Water

- water effects;
- water displacement clear/offset/ripples;
- wading/rain heightmaps;
- heightmap blend/smooth;
- displacement normals;
- water blend/flow;
- reflection blur and water temporal-history behavior.

### Volumetric lighting

- volumetric-lighting generation;
- apply volumetric lighting;
- horizontal/vertical volumetric blur;
- volumetric composite;
- lens-flare + volumetric composite;
- shadow-map arrays, inverse-repartition 1D lookup, 3D noise, and 3D density UAV resources.

### Lighting / image space

- LightingComposite;
- LightingCompositeNoDirectionalLight;
- LightingCompositeMenu;
- ApplyReflections;
- HDR downsample 16/4;
- HDR luminance downsample;
- luminance clamp;
- light adaptation;
- bright-pass blur;
- general blur/minify/downsample families;
- tonemap/cinematic blend;
- final copy/composite stages.

This catalog exists so the field graph can be compared against a finite set of known renderer behaviors rather than guessed from texture format alone.

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

The current leading integration question is whether future GZFX AO/GI should enter at or around the stock full-resolution lighting-composite boundary that consumes blurred AO, raw indirect light, depth, normals, shadow information and reflections before later HDR/image-space processing. This is an architecture target to prove, not yet an implementation decision.

## Fail-soft notes

The recorder has a bounded pass count and only runs during short semantic windows.

If `ExecuteCommandList` appears, the report flags it because work recorded inside a deferred command list can be opaque to the immediate-context hook. That is evidence of a coverage boundary, not permission to pretend the hidden work did not happen.
