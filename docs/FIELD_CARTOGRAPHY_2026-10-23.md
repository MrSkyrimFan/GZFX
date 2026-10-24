# GZFX 0.0.17 Field Cartography — 2026-10-23

## Evidence source

Field bundle:

`GZFXfrfreded.zip`

Runtime:

`0.0.17-renderer-pipeline-cartography`

The bundle contains the automatic stock capture and manual semantic/cartography captures `0001` through `0007`.

The first six manual captures are exterior-like renderer states. Manual `0007` is the interior-class capture with very high confidence: it still contains thousands of real scene draws and the normal stock AO/post chain, but exterior volumetric compute disappears.

Do not reinterpret manual `0007` as a menu/loading capture.

Manual `0006` should remain labeled `extra exterior / exact scene unknown` unless the user explicitly supplies a better label.

---

## Confidence vocabulary

- **IDENTIFIED** — live GZFX evidence establishes the resource/pass behavior directly and public Skyrim renderer research independently matches that behavior.
- **STRONG MATCH** — the live dependency graph is coherent and public Skyrim renderer stage names match, but one more visual/targeted proof is desirable before locking an exact engine name.
- **UNKNOWN** — do not promote yet.

---

# 1. Main depth and opaque MRT

## Main depth — IDENTIFIED

Canonical cartography resource:

`0x22CCE6E7AA0`

Descriptor:

- 2560x1440
- `R24G8_TYPELESS`
- DSV view `D24_UNORM_S8_UINT`

The depth-only phase precedes the opaque four-target scene phase.

### Early/pre-opaque copy

At manual-0001 pass `3082`:

`0x22CCE6E7AA0 -> 0x22CCE6E8B20`

This copy occurs **before** the four-target opaque MRT begins.

That is important. Public Skyrim renderer work describes the ordinary z-prepass copy as insufficient for later high-quality screen-space work because alpha-tested rocks/cliffs/road edges may not yet be represented. Community Shaders explicitly republishes a later finished opaque depth for this reason.

### Later depth copy

At manual-0001 pass `4539`:

`0x22CCE6E7AA0 -> 0x22CCE6E82E0`

This is a separate later depth snapshot and must not be conflated with the early pre-opaque copy.

---

## Main four-target MRT — IDENTIFIED

Manual-0001 first observed opaque MRT draw begins at pass `3088`.

| Slot | Resource | Format | Promoted semantic |
| --- | --- | --- | --- |
| RT0 | `0x22CCE6BED50` | R16G16B16A16_FLOAT | main HDR scene color |
| RT1 | `0x22CCE6C7BB0` | R16G16_FLOAT | motion vectors |
| RT2 | `0x22CCE6C4960` / frame-ping-pong partner | R8G8B8A8_UNORM | encoded native normal target |
| RT3 | `0x22CCE6C02F0` | R8G8_UNORM | auxiliary target, exact channel semantics still unknown |

Main depth is bound with this MRT.

The normal target ping-pongs between two resources on successive frames.

Do **not** call RT2 B/A “TAA/SSR masks” without direct proof.

---

# 2. Exterior volumetric-lighting system

## 3D generation — IDENTIFIED

Exterior compute shader:

`0x22D4AA65ED0`

Per frame it reads:

- 4096x4096 R16-typeless depth array, 2 slices
- 512x512 R16-typeless depth array, 2 slices
- 4096x1 R32_FLOAT lookup
- 32x32x32 R8_UNORM 3D texture

and writes two:

- 320x192x90 R16_FLOAT 3D volumes

The shape and register topology independently match public Skyrim volumetric-light generation research:

- shadow-map Texture2DArray inputs
- secondary volumetric shadow-map array
- 1D inverse-repartition lookup
- 3D noise texture
- 3D density UAV

## 3D blur/ping-pong — IDENTIFIED

Compute shader:

`0x22D4AA66060`

It executes 90 slice-level passes per frame and ping-pongs the two 320x192x90 R16_FLOAT volumes.

## 2D resolve / depth-aware blur — STRONG MATCH

Exterior-only later stages include:

- full-resolution R16_FLOAT projection/resolve fed by the 3D volume and main depth;
- two depth-aware compute blur passes:
  - `0x22D4AA9AF40`
  - `0x22D4AA9B580`

These disappear in manual `0007` together with the 3D volumetric generator.

Promote the family as exterior volumetric lighting. Exact individual engine image-space names can be locked after the next visual atlas.

---

# 3. Stock CameraZ / SAO depth hierarchy

## CameraZ resource — IDENTIFIED

Resource:

`0x22CCE6E9BA0`

Descriptor:

- 2560x1440
- R32_FLOAT
- 12 mip levels

Manual-0001 pass `4441` reads the pre-opaque depth copy and writes mip 0.

Passes `4442` through `4452` successively write mips 1 through 11 with viewport dimensions:

2560x1440
→ 1280x720
→ 640x360
→ 320x180
→ 160x90
→ 80x45
→ 40x22
→ 20x11
→ 10x5
→ 5x2
→ 2x1
→ 1x1

This is the stock CameraZ / SAO depth pyramid.

---

# 4. Stock SAO temporal/blur chain

## Raw temporal AO — IDENTIFIED at family level

Pixel shader:

`0x22D4AA63180`

It reads:

- CameraZ hierarchy
- current native-normal target
- previous-frame RGBA8 AO/history target
- motion vectors

and writes an alternate RGBA8 target.

The two AO/history resources ping-pong frame-to-frame:

`0x22CCE6E6760`
↔
`0x22CCE6E90A0`

Example from manual `0001`:

- frame 3590: reads `E90A0`, writes `E6760`
- frame 3591: reads `E6760`, writes `E90A0`
- frame 3592: reads `E90A0`, writes `E6760`

That is direct temporal-history behavior, not a format guess.

This strongly matches Skyrim’s stock `SAO RawAO` temporal stage.

## AO filtering — STRONG MATCH

Immediately after RawAO:

- `0x22D4AAA0210`
- `0x22D4AAA0B70`

form a two-pass RGBA8 filter chain.

Outputs in manual `0001`:

`RawAO -> 0x22CCE6E98E0 -> 0x22CCE6E6CE0`

Treat these as the stock SAO blur/filter pair. Exact H/V ordering should be confirmed by the targeted atlas rather than guessed.

---

# 5. Stock indirect-light / composite block

## Raw indirect — STRONG MATCH

Pixel shader:

`0x22D4AA634A0`

Reads:

- CameraZ
- current HDR scene color
- RT3 auxiliary surface

Writes:

`0x22CCE6C1290`

Format:

`R11G11B10_FLOAT`

The resource shape and placement strongly match Skyrim’s raw screen-space indirect-light/radiance stage.

Do not yet lock the exact engine target enum until the next visual atlas confirms contents.

## Lighting / AO / indirect composite — STRONG MATCH

Pixel shader:

`0x22D4AA63F90`

Reads, together:

- R11G11B10 raw-indirect candidate
- final two-pass AO result
- early/pre-opaque depth copy
- native normal
- RT3 auxiliary
- additional full-resolution mask/lighting surface
- half-resolution HDR lighting/reflection buffer

and writes back to:

`0x22CCE6BED50`

the main HDR scene.

This is a major stock lighting-composition boundary.

The exact label may be SIL composite, lighting composite, or a combined stock image-space composite path. Keep the family promoted; keep the exact name provisional.

---

# 6. Half-resolution reflection / lighting block

Before CameraZ generation, stock Skyrim runs:

1. full-resolution normal + motion → 1280x720 RGBA8
2. that result + HDR scene + depth/history → 1280x720 RGBA16F
3. depth-aware horizontal/vertical compute blur into another 1280x720 RGBA16F

Compute shaders:

- `0x22D4AA9A450`
- `0x22D4AA9DFB0`

The final half-resolution HDR result is later consumed by the main lighting composite.

This is a **STRONG MATCH** for the stock screen-space reflection / reduced-lighting branch. Public Skyrim image-space research exposes reflection ray-tracing, ApplyReflections, and horizontal/vertical reflection blur stages.

The current evidence is insufficient to decide whether every pass in this block belongs exclusively to SSR or whether one resource is shared with another reduced-lighting path.

The next targeted atlas must preview these resources directly.

---

# 7. Stock temporal AA — IDENTIFIED

Compute shader:

`0x22D4AAA1340`

Reads each frame:

- current main HDR scene `0x22CCE6BED50`;
- one full-resolution RGBA16F temporal-history surface;
- motion vectors `0x22CCE6C7BB0`.

Writes the alternate full-resolution RGBA16F temporal-history surface.

History pair:

- `0x22CCE714FD0`
- `0x22CCE715290`

The resources alternate read/write every frame in exterior and interior captures.

Example, manual `0001`:

- frame 3590 reads `714FD0`, writes `715290`;
- frame 3591 reads `715290`, writes `714FD0`;
- frame 3592 reads `714FD0`, writes `715290`.

This is direct stock temporal-AA history behavior: current HDR + previous history + motion → next history.

---

# 8. Shadow-map families

## Exterior directional / volumetric source array — STRONG MATCH

Resource:

`0x22CCE6E9E60`

Descriptor:

- 4096x4096
- R16 typeless depth
- array size 2

It is heavily written outdoors and read directly by the identified exterior volumetric-light generator.

This is a strong match for the directional shadow-map/cascade source used by volumetric lighting.

## Volumetric-lighting shadow array — STRONG MATCH

Resource:

`0x22CCE6E85A0`

Descriptor:

- 512x512
- R16 typeless depth
- array size 2

It is also read directly by the exterior volumetric-light generator.

Public Skyrim renderer research exposes a dedicated `kVOLUMETRIC_LIGHTING_SHADOWMAPS_ESRAM` depth target. Keep the exact enum assignment provisional until one more targeted proof, but the family relationship is strong.

## Local-light shadow array — STRONG MATCH

Resource:

`0x22CCE6E7520`

Descriptor:

- 4096x4096
- R16 typeless depth
- array size 8

Behavior is highly state-dependent:

- exterior manual captures: only ~45 DSV-write observations over three frames;
- interior manual `0007`: ~4,293 DSV-write observations over three frames.

Public Skyrim renderer research uses an array-sliced `kSHADOWMAPS` target for shadowed local lights.

The live interior/exterior behavior makes this a strong local-light shadow-map-array match.

## Remaining 512x512 depth target — UNKNOWN exact role

Resource:

`0x22CCE6E8860`

Descriptor:

- 512x512
- R24G8 typeless depth
- single slice

It is clearly shadow/depth-related but should remain unnamed until its consumers and visual contents are isolated.

---

# 9. HDR reduction / exposure chain

## Luminance/exposure reduction — IDENTIFIED at family level

A later post chain repeatedly downsamples HDR-like resources through:

640x360
→ 160x90
→ 40x23
→ 10x6
→ 3x2
→ 2x2

and feeds the tiny result back into later image-space passes.

This matches Skyrim’s documented HDR downsample / luminance / light-adaptation family.

Exact individual shader names remain to be assigned.

---

# 10. Interior control

Manual `0007`:

- 5,793 draw calls across 3 frames
- 15 compute dispatches
- normal CameraZ/SAO/post chain still present
- exterior volumetric 3D generation absent
- exterior volumetric projection/blur absent

Manual `0001`–`0006` each contain:

- 294 dispatches
- volumetric 3D generation
- 90-slice ping-pong blur per frame

This cleanly separates an interior-class renderer path from exterior states.

---

# 11. Probe defects exposed by this run

These are tooling defects, not Skyrim semantics.

## Known-anchor identity mismatch

`pipeline_summary` stores known anchors using the live interface pointer, while `pipeline_resources` uses canonical COM identity.

Result: known resources are present but `known_*` tags are absent.

Fix: canonicalize anchors when a capture begins.

## Cumulative resource/shader inventories

Manual resource/shader counts accumulate discoveries from earlier captures in the same process.

Result: `resources_seen=1057` in manual 0007 does not mean 1,057 resources were active in the interior capture.

Fix: output capture-local resources/shaders while keeping global metadata internally for stable identity.

## Stale irrelevant shader fields

D3D11 may leave a compute shader bound while graphics draws execute, and leave graphics shaders bound while dispatch executes.

Result: draw rows can show an irrelevant historical CS and dispatch rows an irrelevant historical PS/VS.

Fix: zero irrelevant shader stages in execution records and pass signatures.

## Semantic preview ranking

Interesting AO/indirect resources were discovered but ranked around candidates 28–34 while only the first 14 previews were saved.

Fix:

- reset semantic resource counters per capture;
- broaden candidate selection to reduced-resolution render/UAV intermediates;
- increase preview candidate budget;
- use smaller previews to control evidence size;
- only normal-decode RGBA8 targets that behave like high-write geometry MRT surfaces.

---

# 12. Next evidence priorities

The next field pass should visually confirm the resources already structurally mapped, not repeat broad blind discovery.

Priority atlas targets:

1. RawAO ping-pong pair
2. two-pass AO blur pair
3. R11G11B10 raw-indirect surface
4. half-resolution RGBA8 reflection/reprojection candidate
5. half-resolution RGBA16F reflection/reduced-lighting chain
6. full-resolution R16_FLOAT exterior volumetric projection/blur
7. CameraZ mip 0 and selected coarse mips
8. RT3 R8G8 auxiliary channels
9. current native-normal RGBA8 individual channels without assuming B/A semantics
10. HDR luminance/adaptation chain

Do not resume GZFX artistic AO/SSIL tuning until these semantics and the correct integration boundary are established.
