# GZFX RENDERER BLUEPRINT v1.0
## Stage-Aware Native Skyrim Rendering Architecture

**Blueprint date:** October 23, 2026  
**Authority status:** Proposed controlling implementation blueprint  
**Repository modification during blueprint phase:** NONE  
**Primary evidence base:** `GZFX77676.zip`  
**Primary supporting documents:**  
1. `GZFX 0.0.18 Field Forensic Review`  
2. `GZFX Renderer Research Pass 1 — Stock Skyrim Image-Space Architecture and Crosswalk to GZFX Field Evidence`

---

# 0. PURPOSE

This blueprint converts the latest GZFX field evidence and renderer research into an implementation architecture.

It is not another research memo.

It defines:

- what GZFX is building;
- what the current renderer evidence allows us to treat as fact;
- which old approaches are frozen or retired;
- where future AO, indirect lighting, reflections, volumetrics, water, temporal systems, and HDR work belong;
- which semantic resources may be trusted;
- which resources remain unresolved and therefore may not become production dependencies;
- how GZFX identifies renderer stages without relying on fragile one-session pointer values;
- how GZFX manages temporal history;
- how new rendering systems are admitted;
- the ordered implementation waves;
- the evidence and acceptance gates required before moving from one wave to the next.

The objective is no longer merely:

> make a visible Skyrim post-process.

The objective is:

> build a native, renderer-integrated Skyrim graphics framework whose effects operate at the correct semantic stages with the correct scene data, temporal ownership, and composition model.

The long-term quality target remains ENB-class or better integration and visual quality, while remaining an independently engineered GZFX architecture.

---

# 1. AUTHORITY AND SOURCE-OF-TRUTH ORDER

When implementation decisions conflict, use the following order:

1. current verified GZFX field evidence;
2. this blueprint once approved;
3. current GZFX `main`;
4. public Skyrim renderer reverse-engineering that directly matches field evidence;
5. prior GZFX design documents;
6. old chat assumptions and prototype behavior.

Public renderer names are useful evidence but are not automatically authoritative over GZFX's own runtime observations.

A target called `RAWINDIRECT`, `NORMAL_TAAMASK_SSRMASK`, or similar is not enough by itself to establish exact GZFX semantics.

GZFX promotes semantics only when the public identity agrees with live runtime chronology, producer/consumer behavior, dimensions/formats, and controlled field evidence.

---

# 2. NON-NEGOTIABLE ENGINEERING RULES

## 2.1 Stage correctness before shader quality

No production effect may be admitted simply because it looks good in a screenshot.

Every effect must declare:

- which renderer stage owns it;
- which scene state exists at that stage;
- which depth state it consumes;
- which normal/motion/history inputs it consumes;
- whether it runs before or after stock AO, indirect, reflection, volumetric, TAA, HDR and UI stages;
- how it composes with Skyrim lighting.

A visually impressive effect at the wrong stage is considered architecturally incorrect.

---

## 2.2 “We have depth” is not a valid design statement

Depth must always be identified by semantic chronology.

At minimum GZFX distinguishes:

- main depth during early/prepass state;
- early depth copy;
- finished opaque/final post-z-prepass depth;
- later water/post-water depth where relevant;
- CameraZ / linearized hierarchical screen-space depth.

Any effect that needs geometry-complete scene depth must explicitly reject an early depth source.

---

## 2.3 Format does not establish semantics

No production dependency may be selected because:

- it is R8G8;
- it is RGBA8;
- it is half resolution;
- it looks mask-like;
- it is black indoors;
- it resembles AO in a preview.

Formats, dimensions, and appearance are evidence only when combined with chronology and producer/consumer behavior.

---

## 2.4 Pointer values are session evidence, not permanent renderer identities

One-process COM pointers and shader object pointers are useful for field analysis.

They are not stable architectural identities.

Production stage recognition must use structural evidence such as:

- shader stage ownership;
- shader bytecode hash when safely available;
- input/output resource descriptors;
- MRT layout;
- SRV/UAV/RTV/DSV topology;
- constant-buffer binding shape;
- sampler/state fingerprints;
- viewport;
- primitive topology;
- order relative to known landmarks;
- read/write lineage.

No production system may rely on `0x278...` style addresses surviving another launch.

---

## 2.5 Temporal domains remain separate

GZFX must not treat “history” as one shared concept.

Separate domains include:

- AO history;
- indirect-radiance history;
- reflection history;
- global scene TAA history;
- water history;
- volumetric history;
- exposure/adaptation history.

A history resource owned by one domain may not be reused by another without explicit semantic proof.

---

## 2.6 Exterior and interior are first-class renderer cases

The field evidence proves a major architectural split.

Exterior captures repeatedly show the large 294-dispatch compute topology, exterior shadow families, and 3D volumetric generation.

Interior captures collapse to approximately 15 dispatches while retaining CameraZ, AO, indirect, HDR/post and local-light shadow activity.

GZFX architecture must therefore support:

- shared core scene semantics;
- explicit exterior-only systems;
- explicit interior lighting behavior;
- alternate lighting-composite signatures such as directional versus no-directional-light variants.

Interior must never be treated as “exterior with the sun intensity set to zero.”

---

# 3. CURRENT VERIFIED SKYRIM RENDERER MAP

This section defines what GZFX may currently treat as stable architectural knowledge.

## 3.1 Main opaque world MRT

The stock world MRT is field identified as:

| Slot | Format | Current GZFX semantic status |
|---|---|---|
| RT0 | R16G16B16A16_FLOAT | main HDR scene color |
| RT1 | R16G16_FLOAT | motion vectors |
| RT2 | R8G8B8A8_UNORM | native normal + additional masks/data |
| RT3 | R8G8_UNORM | exterior/environment-sensitive auxiliary data |
| DSV | R24G8_TYPELESS | main depth |

RT2 rules:

- R/G: trusted native encoded normal.
- A: strong SSR-related mask/eligibility evidence, but production use still requires exact consumer crosswalk.
- B: unresolved/path-dependent. Do not consume as TAA or SSR mask by assumption.

RT3 rules:

- real data outdoors;
- completely inactive in tested interiors;
- consumed by later screen-space lighting;
- exact R/G semantics unresolved;
- may not become a production dependency until producer/consumer identity is locked.

---

## 3.2 Depth chronology

GZFX has identified:

1. early/pre-opaque depth state;
2. early depth copy;
3. later/final opaque depth copy after additional geometry;
4. CameraZ hierarchy generated from the appropriate screen-space depth domain.

The production architecture must include an explicit `DepthSemantic` concept rather than one generic `depthTexture`.

Suggested logical identities:

- `MainDepthCurrent`
- `DepthEarlyCopy`
- `DepthOpaqueFinal`
- `DepthPostWater`
- `CameraZ`

Exact implementation naming is flexible.

---

## 3.3 CameraZ

CameraZ is a field-identified full-resolution R32_FLOAT surface with 12 mips.

It represents a stock screen-space analysis hierarchy and should be considered a preferred multiscale depth source.

Future AO/GI work must choose explicitly between:

- consuming stock CameraZ;
- constructing a GZFX-owned equivalent at the same semantically correct stage.

It may not return to brute-force flat-depth-only analysis without a documented reason.

---

## 3.4 Stock temporal AO

The stock AO architecture is field identified at family level:

`CameraZ + native normal + motion + previous AO/history -> new RawAO/history -> two-pass spatial filter`

The RawAO history pair visibly ping-pongs frame to frame.

Field differences correlate strongly with measured motion activity.

This establishes that high-quality GZFX AO must be designed as a temporal system from the beginning.

---

## 3.5 Indirect-light family

The renderer contains a real screen-space indirect family.

GZFX field evidence identifies:

- a full-resolution HDR-like raw-radiance/indirect candidate;
- later consumption by the lighting/AO/indirect composition region;
- writeback into main HDR.

Public renderer research independently exposes raw indirect, previous indirect, swap and composite families.

Production GI must therefore be represented as radiance, not “local glow.”

---

## 3.6 Shadow architecture

GZFX field evidence supports three major shadow domains:

### Exterior directional family
Large array shadow resources active heavily outdoors and effectively inactive indoors.

### Exterior volumetric shadow family
Secondary array consumed directly by exterior volumetric generation.

### Interior/local-light shadow family
Large multi-slice array with thousands of writes indoors and far less activity outdoors.

A full-resolution stock shadowmask is also strongly established and participates in later lighting.

Future shadow enhancements must preserve these separate domains.

---

## 3.7 Exterior volumetric lighting

The exterior volumetric subsystem is considered architecturally identified.

Its dataflow includes:

- exterior shadow arrays;
- 1D inverse-repartition-style lookup;
- 3D noise;
- 320x192x90 scalar 3D density volumes;
- slice-level generation/ping-pong/raymarch behavior;
- later 2D full-resolution depth-aware processing/composition.

This subsystem disappears from the interior renderer path.

GZFX should not spend additional work proving this subsystem exists.

Future work should focus on how to augment, replace, or expose it.

---

## 3.8 Reflection / reduced-lighting branch

A persistent half-resolution HDR branch exists across exterior and interior scenes.

It includes:

- reduced-resolution production;
- depth-aware horizontal/vertical filtering;
- later consumption by the full-resolution lighting/composite region.

It is a strong match for reflection/reduced-lighting infrastructure.

Exact stage ownership remains a blueprint implementation target rather than a current fact.

---

## 3.9 Unresolved quarter-resolution temporal lighting/history

A 640x360 RGBA16F temporal pair:

- consumes current HDR;
- consumes previous history;
- consumes motion;
- ping-pongs;
- persists in exterior and interior captures.

It therefore cannot be classified as exclusively exterior volumetric history.

It also has not demonstrated the expected full global-TAA signature.

Current architectural label:

`ReducedTemporalLightingHistory` — provisional logical name only.

Production code must not call it `TAA` or `VolumetricHistory` until exact stage identity is established.

---

## 3.10 Global TAA and water TAA

Public renderer evidence proves Skyrim has distinct global scene TAA, UI TAA, TAA mask, and water temporal targets.

GZFX has not yet cleanly mapped the global scene TAA execution stage in its own field graph.

Therefore:

- global TAA remains an unresolved integration landmark;
- water temporal history remains a separate domain;
- neither may be conflated with AO, indirect or reduced-light history.

---

## 3.11 HDR / exposure / bloom / tonemap

A real multistage HDR reduction/adaptation chain is field identified at family level.

This domain occurs after core lighting construction and should remain architecturally separate from AO/GI.

GZFX should eventually integrate HDR, bloom, exposure and tonemapping as a post-lighting subsystem rather than allowing those concerns to leak into visibility/radiance generation.

---

# 4. ARCHITECTURAL PIVOT

The old GZFX prototype model was approximately:

`find HDR + find depth -> inject effect -> tune shader -> add more inputs when artifacts appear`

That model proved renderer access but is retired as the production design.

The production model becomes:

`observe renderer -> identify semantic stage -> acquire verified semantic inputs -> execute domain-specific rendering -> maintain domain-specific temporal history -> compose at the correct lighting boundary -> leave later Skyrim domains intact unless deliberately replaced`

This is the central blueprint decision.

---

# 5. OLD EFFECT STACK STATUS

The following systems remain useful as historical diagnostics and regression references:

- Ambient Contact;
- Horizon/HBAO prototype;
- old Indirect Bounce prototype;
- Local Glow;
- split comparison mode;
- depth debug view;
- pre-UI color debug view.

They are **not** the architectural foundation for production AO/GI.

## 5.1 Ambient Contact

Retain as:

- proof that GZFX can perform scene-aware pre-UI mutation;
- simple debug/reference mode.

Do not extend into production AO.

---

## 5.2 Horizon/HBAO prototype

Freeze.

Known failure classes:

- coherent banding;
- broad gray/global response;
- flicker/crawling;
- poor field quality even when technically functional.

Do not spend more time tuning its existing parameters.

Any reusable math must be consciously transplanted into the new architecture, not incrementally patched in place.

---

## 5.3 Old Indirect Bounce

Freeze as failed prototype.

The measured field output was effectively negligible despite large strength changes.

It must not become the basis of production GI.

---

## 5.4 Local Glow

Retain as a post-lighting artistic/debug effect if useful.

It is not GI.

It may eventually become a separate bloom/local-emissive enhancement domain, but must never be used as evidence that indirect illumination is solved.

---

# 6. NEW GZFX LOGICAL ARCHITECTURE

Exact filenames are implementation details. The following are required logical subsystems.

---

## 6.1 Renderer Semantic Registry

Purpose:

Maintain the current frame's verified Skyrim semantic resources and stage identities.

Responsibilities:

- canonical COM identity;
- descriptor tracking;
- semantic confidence state;
- current/previous frame ownership;
- scene-resolution metadata;
- exterior/interior classification;
- stable structural landmark matching;
- resource lifetime validation;
- detection of ping-pong partners;
- rejection of stale semantic anchors.

Core logical outputs should include objects equivalent to:

- `SceneColorHDR`
- `MotionVectors`
- `NativeNormalMask`
- `AuxiliaryRT3`
- `MainDepth`
- `OpaqueFinalDepth`
- `CameraZ`
- `ShadowMask`
- `RawAOCurrent`
- `RawAOPrevious`
- `AOFiltered`
- `RawIndirect`
- `ReducedReflectionLighting`
- `VolumetricLighting`
- known TAA/history landmarks once proven.

Each semantic record carries a confidence level.

Suggested states:

- `Verified`
- `Strong`
- `Provisional`
- `Unknown`

Only `Verified` and explicitly approved `Strong` resources may be production inputs.

---

## 6.2 Renderer Stage Scheduler

Purpose:

Replace the concept of one generic “pre-UI injection point” with explicit renderer-stage ownership.

The scheduler recognizes structural landmarks and executes registered GZFX work only at the correct boundary.

Required logical stage families:

1. `AfterShadowProduction`
2. `AfterDepthPrepass`
3. `AfterOpaqueGeometryFinalDepth`
4. `AfterCameraZ`
5. `AfterStockAO`
6. `BeforeIndirectLightingComposite`
7. `AtLightingComposite`
8. `AfterLightingComposite`
9. `BeforeVolumetricComposite`
10. `AfterVolumetricComposite`
11. `WaterDomain`
12. `BeforeGlobalTAA`
13. `AfterGlobalTAA`
14. `HDRReduction`
15. `BeforeTonemap`
16. `PostTonemapPreUI`
17. `UI/Present`

Not all stages need implementation immediately.

The architecture must make them expressible.

---

## 6.3 Native Semantic Broker

Purpose:

Provide effect code with typed, validated semantic inputs instead of raw D3D resources.

Example logical request:

`AmbientVisibilityInputs`

returns:

- final/appropriate depth;
- CameraZ;
- native decoded normal;
- motion;
- current frame index;
- current/previous camera data;
- dynamic-resolution metadata.

An effect must never independently “find some depth texture.”

This broker becomes the single authority for which Skyrim surface is safe for each domain.

---

## 6.4 Temporal History Manager

Purpose:

Own all GZFX-created temporal resources.

Required behavior:

- per-domain history allocation;
- ping-pong ownership;
- resize invalidation;
- device-reset invalidation;
- camera-cut invalidation;
- world-space/disocclusion validation;
- exterior/interior transition invalidation when necessary;
- frame-gap invalidation;
- dynamic-resolution changes;
- per-history validity flags;
- debug reporting.

Separate histories at minimum:

- AO;
- indirect diffuse radiance;
- optional indirect specular/radiance;
- reflection if GZFX later owns it;
- volumetric if GZFX later owns it.

Do not reuse Skyrim's stock histories merely because they exist unless the blueprint is explicitly revised to authorize it.

---

## 6.5 Lighting Composition Interface

Purpose:

Provide a deliberate place where visibility and radiance contributions interact with Skyrim lighting.

This is the architectural replacement for the current broad HDR multiply/add approach.

Rules:

### AO is visibility

AO should primarily reduce ambient/indirect visibility.

It should not indiscriminately darken:

- direct sun/light;
- emissive surfaces;
- specular highlights;
- sky;
- volumetric light;
- UI;
- final tonemapped output.

The composition model must expose AO as a visibility term, not final-image darkness.

### GI is radiance

Indirect lighting is additive radiance.

It should enter lighting composition as:

`direct/stock lighting + indirect radiance`

subject to energy/clamping rules.

GI must not be implemented as a generic bloom/glow spread.

---

# 7. PRODUCTION AO ARCHITECTURE

Working name:

`Ambient Visibility v2`

The exact algorithm can evolve, but the architecture is fixed.

## 7.1 Required inputs

Production AO requires:

- finished opaque or otherwise stage-correct depth;
- CameraZ or GZFX hierarchical equivalent;
- native normal RG;
- motion vectors;
- camera matrices/current and previous frame data;
- dynamic resolution;
- GZFX AO history.

Optional inputs after proof:

- shadow information;
- material/SSR masks;
- unresolved auxiliary data only after semantic lock.

RT3 and RT2.B are not required for initial production AO.

---

## 7.2 Analysis model

The first production implementation should be hierarchical and horizon/visibility based rather than the old fixed-direction brute-force neighborhood pattern.

Candidate techniques may include:

- GTAO-style horizon visibility;
- horizon search with multiscale depth;
- depth-pyramid accelerated occluder search.

The blueprint does not require one published algorithm verbatim.

It requires these properties:

- screen-space directional visibility;
- depth-scale awareness;
- native surface orientation;
- spatially varied sampling;
- stable multiscale radius behavior;
- predictable physical/artistic controls.

---

## 7.3 Temporal AO

Temporal reconstruction is mandatory, not a later optional polish layer.

Required elements:

- motion reprojection using the verified Skyrim convention;
- previous depth/history validation;
- normal rejection;
- disocclusion rejection;
- camera-cut reset;
- bounded accumulation;
- neighborhood or confidence clipping;
- motion-dependent history confidence.

The exact motion sign/scale must be explicitly validated against Skyrim's public motion convention and GZFX field behavior before production admission.

---

## 7.4 Spatial filtering

Spatial filtering must be edge-aware.

Inputs may include:

- linear depth/CameraZ;
- native normal;
- AO confidence;
- temporal confidence.

Avoid broad blur that spreads occlusion across geometry discontinuities.

---

## 7.5 AO output

AO output should represent a normalized visibility factor.

Recommended logical form:

`visibility = 1.0 = unoccluded`

not:

`ao = arbitrary darkness strength`

The artistic UI may later expose intuitive intensity controls, but internal representation should remain physically meaningful enough for proper composition.

---

# 8. PRODUCTION INDIRECT LIGHTING / GI ARCHITECTURE

Working name:

`Indirect Radiance v1`

## 8.1 Required principle

Indirect lighting is a radiance field.

No production path may be accepted if changing “bounce strength” merely brightens the image without coherent source-to-receiver propagation.

---

## 8.2 Required inputs

Initial GI should use only verified semantics:

- stage-appropriate HDR radiance source;
- final depth / CameraZ;
- native normals;
- motion;
- GZFX indirect-radiance history.

Additional material/SSR/aux masks may be integrated later after proof.

---

## 8.3 Radiance gather

The initial implementation should be reduced resolution for performance.

Likely architecture:

- half or quarter-resolution analysis;
- hierarchical depth-guided screen-space ray/horizon sampling;
- source radiance gathered from an appropriate pre-composite or lighting-stage HDR source;
- geometry rejection using depth/normal;
- energy-limited diffuse transfer;
- optional chroma preservation.

The output is **indirect radiance**, not final HDR.

---

## 8.4 Temporal radiance accumulation

GI history is independent from AO history.

Required:

- motion reprojection;
- depth and normal disocclusion;
- luminance/radiance clamping;
- neighborhood clipping;
- camera-cut reset;
- history confidence;
- rejection of stale off-screen information.

Later multi-bounce behavior may use temporally accumulated prior indirect radiance if stable.

---

## 8.5 Denoise/reconstruction

GI should use:

- temporal accumulation as primary stability mechanism;
- edge-aware spatial reconstruction;
- full-resolution upsample guided by depth/normal.

Brute-force full-resolution sampling is not the default plan.

---

## 8.6 GI composition

GI enters at the lighting composition boundary as additive radiance.

It does not become:

- Local Glow;
- Bloom;
- final-color additive haze;
- ambient multiplier.

---

# 9. AO + GI INTERACTION

AO and GI are separate products but cooperate.

Recommended internal model:

1. compute geometric visibility;
2. compute indirect incident radiance;
3. optionally use visibility to modulate incoming indirect contribution;
4. compose indirect radiance into scene lighting;
5. avoid double-darkening if stock AO remains active.

A major implementation decision will be whether GZFX initially:

- coexists with stock Skyrim AO;
- replaces stock AO;
- neutralizes stock AO while retaining its resources;
- selectively consumes stock AO only as a reference.

The blueprint does **not** authorize silently stacking a strong GZFX AO over stock AO.

That creates double occlusion and invalidates tuning.

A deliberate stock-AO policy is required before field-quality evaluation.

---

# 10. STOCK-SYSTEM INTEROP POLICY

For each Skyrim subsystem GZFX eventually touches, one of four explicit modes must be chosen:

- `Observe`
- `Consume`
- `Augment`
- `Replace`

No subsystem may accidentally sit in an undefined fifth state.

Initial policy:

| Skyrim subsystem | Initial GZFX policy |
|---|---|
| Main HDR | Consume |
| Final opaque depth | Consume |
| CameraZ | Consume if field-stable; otherwise rebuild equivalent |
| Motion vectors | Consume |
| Native normal RG | Consume |
| RT2 A | Observe until exact integration need |
| RT2 B | Observe |
| RT3 R/G | Observe |
| Stock AO | Observe initially; replacement policy decided during AO integration |
| Stock indirect/SIL | Observe initially; replacement/augmentation decision before GI composite |
| Shadow maps | Consume for later shadow/volumetric work, not required for AO v2 |
| Shadowmask | Observe/consume where semantically safe |
| Reflections | Observe initially |
| Volumetrics | Observe initially |
| Global TAA | Observe until exact stage mapped |
| Water temporal | Observe |
| HDR/exposure | Observe initially |

---

# 11. INTERIOR / EXTERIOR POLICY

The renderer scheduler must carry a current environment class.

Minimum:

- `Exterior`
- `Interior`
- `UnknownTransition`

Night is **not** a separate renderer architecture class by itself.

The Riverwood nighttime captures prove exterior architecture survives darkness.

AO and GI should therefore share the same core architecture between day and night, while scene radiance naturally changes.

Exterior-only systems:

- 3D volumetric generation;
- directional/exterior shadow domains;
- sun/cloud-related systems.

Interior-specific concerns:

- local-light shadow dominance;
- no-directional-light lighting composite variant;
- different exposure/radiance distributions.

Transition handling:

Entering/leaving interiors should invalidate histories when semantic continuity cannot be guaranteed.

---

# 12. DIAGNOSTICS ARCHITECTURE v3

The next diagnostics work should stop producing anonymous “candidate 08” style evidence as the primary interface.

## 12.1 Structural landmark names

Once a resource is promoted, output should use semantic identifiers such as:

- `MAIN_HDR`
- `MAIN_DEPTH`
- `OPAQUE_FINAL_DEPTH`
- `CAMERAZ`
- `MOTION`
- `NORMAL_MASK_CURRENT`
- `NORMAL_MASK_PREVIOUS`
- `SAO_RAWAO_CURRENT`
- `SAO_RAWAO_PREVIOUS`
- `SAO_FILTERED`
- `RAW_INDIRECT`
- `SHADOW_MASK`
- `REDUCED_LIGHTING`
- `VOLUMETRIC_DENSITY_A`
- `VOLUMETRIC_DENSITY_B`

Unknowns keep neutral names.

---

## 12.2 Cross-interface COM identity

The remaining semantic-preview identity seam must be removed.

All evidence systems should report:

- canonical `IUnknown` identity;
- original interface pointer if useful;
- semantic registry ID;
- resource birth/generation.

This makes pipeline CSVs, semantic previews, and screenshots directly joinable.

---

## 12.3 Capture manifest

Each capture should produce a small manifest containing:

- GZFX version;
- frame range;
- capture number;
- runtime development mode;
- resolution;
- exterior/interior class;
- semantic anchors available;
- history validity;
- device status;
- opaque command-list warning status;
- preview success/failure counts.

Optional later enhancement:

manual capture labels for field campaigns.

---

# 13. IMPLEMENTATION WAVES

These waves are sequential architectural gates.

They are not arbitrary PR-sized tasks.
A wave can contain multiple commits/PRs if needed.

---

## WAVE 1 — Semantic Authority Layer

### Objective

Turn the current cartography discoveries into runtime semantic infrastructure.

### Required work

- implement or formalize Semantic Registry;
- structural landmark matching;
- typed semantic resources;
- capture-local generation IDs;
- COM identity crosswalk;
- exterior/interior classification;
- stable final-depth identity;
- CameraZ identity;
- normal/motion identity;
- RawAO family identity;
- initial indirect family identity;
- diagnostics use semantic names.

### Visual changes

None required.

### Gate

PASS only if repeated field capture across:

- daylight exterior;
- nighttime exterior;
- interior

shows the same structural semantics correctly reacquired without pointer assumptions.

---

## WAVE 2 — Stage Scheduler / No-Op Integration

### Objective

Prove GZFX can execute a harmless marker/no-op workload at specific semantic boundaries.

### Required stages initially

- after final opaque depth;
- after CameraZ;
- around stock AO completion;
- before/at indirect-light composite;
- after lighting composite;
- before global TAA once identified;
- before HDR/tonemap.

### Visual changes

None.

### Gate

PASS requires exact execution order evidence with no stock-render mutation and no state leakage.

---

## WAVE 3 — Native Input Broker + Temporal Core

### Objective

Create production-quality input access and GZFX history management before implementing new effects.

### Required work

- typed depth access;
- CameraZ access;
- native normal decode;
- verified motion reprojection convention;
- temporal history manager;
- resize/cut/interior-transition invalidation;
- disocclusion utilities;
- full diagnostics.

### Visual changes

Debug views only.

### Gate

Motion-reprojected test history must remain stable during:

- still camera;
- pan;
- walk;
- strafe;
- exterior day;
- exterior night;
- interior;
- load/transition.

---

## WAVE 4 — Ambient Visibility v2

### Objective

Replace the old Horizon/Ambient Contact production concept with stage-correct temporal AO.

### Required work

- hierarchical visibility analysis;
- CameraZ/final-depth selection;
- native normals;
- temporal reprojection;
- disocclusion;
- edge-aware filtering;
- visibility output;
- performance instrumentation.

### Stock AO policy

Must be explicitly chosen before final visual acceptance.

### Gate

Field acceptance must demonstrate:

- no horizontal banding;
- no broad gray wash;
- no crawling/flicker during ordinary movement;
- stable contact at geometry intersections;
- stable mid-scale occlusion;
- acceptable vegetation/alpha-test behavior;
- exterior/interior stability;
- day/night stability.

No slider tuning can rescue a fundamentally unstable field.

---

## WAVE 5 — Indirect Radiance v1

### Objective

Build actual screen-space bounce lighting.

### Required work

- reduced-resolution radiance gather;
- depth/normal-guided hit validation;
- temporal radiance history;
- disocclusion;
- radiance clamp;
- denoise/reconstruction;
- debug radiance-only view.

### Gate

A controlled source must produce coherent spatially localized bounce onto plausible receivers.

Changing indirect strength must scale a real radiance field rather than merely raising global brightness.

---

## WAVE 6 — Lighting Composition

### Objective

Move GZFX AO/GI from “post-process effects” into deliberate lighting composition.

### Required work

- combine AO visibility with ambient/indirect contribution;
- add GZFX indirect radiance;
- preserve direct light/emissive/specular;
- prevent double AO;
- handle interior/no-directional-light path;
- field-debug decomposition views.

### Gate

Comparative captures must prove:

- direct lighting is not globally darkened;
- emissive sources remain emissive;
- GI color is spatially coherent;
- AO and GI do not fight each other;
- interior and exterior both compose correctly.

This is the first wave where GZFX begins to behave like a true renderer integration rather than a shader injector.

---

## WAVE 7 — Reflection / Temporal-Light Mapping

### Objective

Resolve and integrate the remaining half/quarter-resolution lighting families.

### Required work

- exact 1280x720 reflection/reduced-light chain identity;
- exact 640x360 temporal history identity;
- exact global TAA stage;
- decide whether GZFX reflection enhancement augments or replaces stock SSR.

### Gate

No production replacement until exact stage and history ownership are proven.

---

## WAVE 8 — Volumetric Integration

### Objective

Expose or enhance the already-understood exterior volumetric subsystem.

Potential future work:

- density/scattering controls;
- shadow integration;
- weather-aware volumetric response;
- higher-quality temporal filtering;
- interior/exterior handoff.

This wave must not destabilize AO/GI architecture.

---

## WAVE 9 — Water Domain

### Objective

Map and enhance the separate water temporal/reflection/refraction system.

Required first:

- identify water history pair;
- water mask;
- post-water depth;
- reflection/refraction inputs;
- TAA-water boundary.

Water history remains isolated from scene/GI histories.

---

## WAVE 10 — HDR / Exposure / Bloom / Tonemap

### Objective

Turn GZFX into a complete post-lighting presentation framework.

Potential scope:

- light adaptation;
- exposure;
- bloom;
- tonemap;
- color grading;
- HDR display mapping;
- cinematic controls.

These occur after core lighting architecture is stable.

---

# 14. PERFORMANCE POLICY

Old evidence showed approximately 5.4 ms for an unsatisfactory AO/SSIL attempt at 2560x1440.

That is not an acceptable quality/cost reference.

The new architecture must be designed for scalable cost.

Guidelines:

- hierarchical depth instead of huge flat-depth search;
- reduced-resolution GI;
- temporal accumulation to reduce per-frame sample demand;
- compute where it materially improves locality/throughput;
- full-resolution work only where quality requires it;
- explicit per-pass GPU timing;
- quality levels alter meaningful work, not arbitrary blur strength.

Provisional engineering target on the RTX 4060 Laptop 1440p reference system:

- AO alone should aim materially below the old 5.4 ms stack;
- AO + first-pass GI should target a practical combined cost, preferably around a few milliseconds rather than consuming an entire frame budget.

These are engineering targets, not acceptance lies.

Visual correctness comes first, then optimization without architectural regression.

---

# 15. FIELD-TEST DOCTRINE

GZFX field testing now follows XForge-style evidence discipline.

## Before implementation

- evidence review;
- research;
- blueprint;
- then code.

## After each major wave

Use controlled campaigns rather than one screenshot.

Minimum comparison classes:

- exterior daylight;
- exterior nighttime;
- interior;
- still camera;
- motion;
- water when relevant.

Field evidence must contain both:

- visual evidence;
- machine-readable renderer/resource/timing evidence.

No effect is accepted solely from `status=applied`.

---

# 16. CI / HARNESS DOCTRINE

Synthetic tests remain mandatory but do not substitute for Skyrim.

CI should prove:

- lifecycle correctness;
- state restoration;
- semantic registry behavior;
- structural classification;
- history invalidation;
- resource hazard safety;
- deterministic pass scheduling;
- output neutrality for no-op stages;
- known-anchor acquisition;
- algorithm functional sanity.

Real Skyrim field tests prove:

- semantic identity;
- integration stage;
- visual stability;
- actual performance;
- exterior/interior behavior;
- real temporal behavior.

A feature is production-accepted only when both relevant CI and field gates pass.

---

# 17. HARD DEAD PATHS

Do not return to these without genuinely new evidence.

- permanent Present-only lighting architecture;
- one tiny probe per build;
- raw format guessing;
- depth-derived normals as primary production normals;
- flat-depth brute force when CameraZ/hierarchical depth is available;
- sample-count escalation as first response to bad AO;
- shrinking sliders to hide unstable algorithms;
- calling Local Glow “GI”;
- multiplying final HDR to simulate physically meaningful AO;
- treating all temporal history as interchangeable;
- relying on one-session shader/resource pointers;
- calling the 640x360 temporal pair global TAA without proof;
- calling RT2.B a TAA mask without proof;
- consuming RT3 before its semantics are locked;
- assuming one injection point is suitable for every future effect.

---

# 18. FIRST IMPLEMENTATION AUTHORIZATION AFTER BLUEPRINT APPROVAL

When implementation begins, the first authorized engineering objective is **not AO**.

It is:

> **Wave 1 + Wave 2 foundation: Semantic Authority Layer and Stage Scheduler.**

Why:

The renderer map is mature enough to know that correct stage ownership is the missing foundation.

If AO is rewritten before this layer exists, GZFX risks creating another technically sophisticated effect attached to the wrong place.

The foundation pass should therefore:

1. promote verified field semantics into runtime typed identities;
2. fix the remaining diagnostic crosswalk seam;
3. identify structural stage boundaries;
4. execute no-op stage callbacks;
5. prove identical acquisition across daylight exterior, nighttime exterior, and interior.

Only then is `Ambient Visibility v2` authorized.

---

# 19. BLUEPRINT ACCEPTANCE CHECKLIST

This blueprint is considered ready to govern implementation when the project accepts the following statements:

- GZFX is now a stage-aware renderer-integration project, not a post-process injector project.
- The old AO/SSIL stack is frozen as prototype history.
- Final opaque depth chronology matters and must be explicit.
- CameraZ/hierarchical depth is first-class.
- Native normal RG and motion vectors are first-class inputs.
- RT2.B and RT3 remain unresolved and non-required.
- AO is visibility.
- GI is radiance.
- AO and GI require independent temporal histories.
- AO/GI composition belongs at the lighting boundary, not arbitrary final HDR.
- exterior/interior paths must be treated explicitly.
- global TAA, water history, volumetric history, AO history, and GI history are different domains.
- structural stage recognition replaces pointer-based assumptions.
- the next repository work is Semantic Registry + Stage Scheduler before visual-effect redesign.
- every major architecture wave requires CI plus real-Skyrim evidence before credit.

---

# 20. CURRENT FRONTIER

The project frontier after this blueprint is:

> **GZFX has completed the first mature renderer-cartography and research phase. Field evidence now identifies the main HDR/depth/MRT structure, final-depth chronology, CameraZ hierarchy, native motion/normals, temporal stock AO, raw-indirect/lighting-composite region, shadow families, exterior volumetrics, HDR reduction, and the major exterior/interior split. The remaining unknowns are narrow semantic and stage questions rather than basic renderer discovery. Production AO/GI redesign is intentionally paused until the runtime Semantic Authority Layer and Stage Scheduler exist.**

The next implementation phase, once authorized, is:

## FOUNDATION PHASE
### Wave 1 — Semantic Authority Layer
### Wave 2 — Stage Scheduler / No-Op Integration
### Wave 3 — Native Input Broker + Temporal Core

Only after those gates pass:

## LIGHTING PHASE
### Wave 4 — Ambient Visibility v2
### Wave 5 — Indirect Radiance v1
### Wave 6 — Lighting Composition

Everything after that builds on the same renderer-integrated foundation.

---

# 21. FINAL BLUEPRINT RULE

GZFX is no longer allowed to ask:

> “Where can we inject this effect?”

The correct question is:

> **“Which Skyrim renderer domain owns this information, what semantic scene state exists there, and how should GZFX participate in that domain without corrupting the stages before or after it?”**

That question governs all future graphics architecture.

**Blueprint v1.0 — END**