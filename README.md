# GZFX — NEW AGENT TAKEOVER / CURRENT SOURCE OF TRUTH

> **READ THIS FIRST.**
>
> This README is the direct handoff for the next ChatGPT/GitHub engineering agent.
> It replaces the old bootstrap README.
>
> **Do not restart the project, do not return to baby-step probing, and do not resume stale branches.**
> The current repository already contains a working SKSE-free native D3D11 framework, live in-game settings UI, pre-UI HDR injection, renderer-semantic tracing, native Skyrim motion/normal acquisition, the first temporal AO/SSIL foundation, and the renderer-pipeline cartography system.

---

## 0. Current repository state

Repository:

```text
MrSkyrimFan/GZFX
```

Current authoritative branch:

```text
main
```

Current authoritative source rule:

```text
current main
```

Do not pin future work to the old PR18 package-refresh SHA. The historical PR18 package baseline was:

```text
c9de1705082f7efd740d709c1f916bf1dff2f381
```

PR #19 established the 0.0.17 execution-level renderer cartographer and superseded that historical PR18 package baseline.

Current runtime version:

```text
0.0.18-cartography-semantic-atlas
```

Current engineering phase:

```text
RENDERER CARTOGRAPHY — SEMANTIC ATLAS
pipeline understanding first
effect redesign/tuning later
```

Current cartography advancement:

```text
PR #21 — Promote field cartography and expand semantic atlas
```

PR #19 remains the execution-cartography foundation. PR #21 incorporates the first real 0.0.17 field graph, fixes probe defects exposed by that graph, and expands targeted visual semantic evidence.

Historical PR #18 merge commit:

```text
b8596c0d2dd69dc22663fa6326463e8a8debfa1d
```

Historical PR #18 tested head:

```text
5da67a6fffb1d176cc84e4bba24d34f6ec1b1b04
```

Historical PR #18 CI:

```text
Windows x64 run #80
run id: 39930954463

13 / 13 native tests passed
0 failed
```

Historical PR #18 artifact:

```text
GZFX-SkyrimSE-x64
artifact id: 12491139348
sha256: cf2ff664ba642f4f671731c5b2945143354492ec5df4f7e8b34cf3ed4435adc2
```

Package contents remain exactly:

```text
GZFX.dll
dinput8.dll
```

### IMPORTANT BRANCH RULE

The old branch:

```text
render/native-surface-temporal-foundation
```

is **already merged and stale**.

Do **not** continue work from it.

Always start future work from current `main`.

---

# 1. Project goal

GZFX is an independent native graphics framework for:

```text
The Elder Scrolls V: Skyrim Special Edition / Anniversary Edition
Direct3D 11
Windows x64
```

Long-term goal:

> Replace the functional graphics role of ENB + ReShade with a standalone Skyrim-native graphics framework that does not require SKSE.

Destination includes:

- integrated ambient lighting / AO;
- screen-space indirect illumination;
- advanced shadows;
- sun/moon/cloud lighting;
- atmospheric / volumetric effects;
- materials;
- water;
- bloom / HDR / tone / post processing;
- presets;
- live in-game editor;
- compatibility handling;
- eventual public rendering API.

Architecture:

```text
SkyrimSE.exe
    ↓
dinput8.dll bootstrap/proxy
    ↓
GZFX.dll
    ↓
D3D11 / DXGI renderer integration
    ↓
HLSL
    ↓
GPU
```

No SKSE dependency.

GZFX is built independently from public DirectX documentation, public graphics research, public Skyrim reverse-engineering knowledge, and GZFX runtime evidence.

Do not copy proprietary ENB implementation details.

---

# 2. User workflow — do not fight it

The user's normal field-test workflow is:

```text
download new ZIP
→ install DLLs
→ launch/test Skyrim
→ return to Documents\My Games\Skyrim Special Edition\GZFX
→ CUT that whole GZFX evidence folder to Downloads
→ ZIP it
→ upload ZIP to ChatGPT
→ remove old GZFX DLLs from Skyrim root
→ delete obsolete test ZIP
→ wait for next build
```

This is intentional.

Do not tell the user to preserve old runtime folders indefinitely.

Uploaded evidence ZIPs are the historical field records.

The user prefers **large, high-value engineering passes**, not one-question baby probes.

Use instrumentation aggressively, but batch related renderer questions into one pass.

---

# 3. Installation / packaging

CI automatically publishes:

```text
main
└── LANCE YOUR FILE IS HERE
    └── GZFX-SkyrimSE-x64.zip
```

User extracts the two DLLs directly into Skyrim root:

```text
C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\
├── SkyrimSE.exe
├── GZFX.dll
└── dinput8.dll
```

If an earlier third-party `dinput8.dll` exists, GZFX supports chaining through:

```text
dinput8_gzfx_chain.dll
```

Never tell the user to overwrite/delete that chained DLL.

Runtime data is automatically created under:

```text
C:\Users\lance\OneDrive\Documents\My Games\Skyrim Special Edition\GZFX\
```

Typical contents:

```text
GZFX.log
Settings.ini
Diagnostics\...
```

No INI belongs in the ZIP.

---

# 4. Current hotkeys / UI

Global development controls:

```text
Shift + Numpad 1
Open / close GZFX settings menu

Shift + Numpad 2
Reload Settings.ini from disk

Shift + Numpad 3
Cycle development render mode

Shift + Numpad 4
Capture current rendered frame + exact live settings

Shift + Numpad 5
Request another short STOCK Skyrim semantic-capture window
```

The settings menu is mouse-driven.

When open:

- GZFX captures DirectInput mouse state;
- the custom GZFX cursor moves;
- Skyrim camera input is suppressed;
- sliders are click/drag;
- booleans are click toggles;
- values persist immediately to `Settings.ini`;
- pause-menu operation has already been repaired and validated.

Accessibility rule:

> Diagnostics must never rely on hue alone.

Use monochrome, text, geometry/pattern, or other non-color signals.

User is colorblind.

---

# 5. Proven renderer foundation

The following are FIELD-PROVEN in real Skyrim unless otherwise stated.

## Bootstrap / runtime

- SKSE-free `dinput8.dll` bootstrap works.
- `GZFX.dll` loads inside `SkyrimSE.exe`.
- Documents/OneDrive runtime-path resolution works.
- DirectInput chaining works.
- CI → CMake → DLL → ZIP workflow works.

## D3D11 renderer ownership

GZFX has proven access to:

- live Skyrim swap chain;
- `ID3D11Device`;
- immediate context;
- backbuffer;
- Present;
- ResizeBuffers;
- renderer state changes;
- draw/dispatch activity;
- render target / depth target transitions;
- PS/CS shader resource bindings;
- CS UAV bindings.

Real field environment used during research:

```text
2560×1440
NVIDIA GeForce RTX 4060 Laptop GPU
~8 GB dedicated VRAM
D3D11 feature level 11.0
```

## Main depth

Validated main scene depth:

```text
2560×1440
DXGI_FORMAT_R24G8_TYPELESS
DEPTH_STENCIL + SHADER_RESOURCE
```

It is real Skyrim scene depth.

Diagnostic previews visibly contained:

- player;
- terrain;
- trees;
- grass;
- mountains;
- geometry separation.

## Main HDR scene color

Validated pre-UI HDR scene color family:

```text
2560×1440
DXGI_FORMAT_R16G16B16A16_FLOAT
RTV + SRV
```

The selected texture visibly contains the rendered world before UI.

## Camera buffer

Validated Skyrim per-frame camera constant buffer:

```text
720 bytes
VS slot 12
PS slot 12
```

Structure matches:

- CameraView
- CameraProj
- CameraViewProj
- unjittered/current/previous variants
- CameraViewInverse
- CameraViewProjInverse
- CameraProjInverse
- CameraPosAdjust
- previous camera adjustment
- frame/dynamic-resolution data

Field evidence proved matrix inverse relationships with approximately 1e-7 numerical error.

Camera hunting is DONE.

Do not resume matrix guessing.

---

# 6. Pre-UI injection

GZFX discovered a stable scene-stage transition from actual Skyrim render-target chronology.

The pre-UI stage is admitted from the relationship between:

- repeated HDR+depth world rendering;
- terminal HDR-color-only phase;
- final unbind/transition into later image-space processing.

The architecture is fail-soft:

```text
validated pre-UI transition
→ mutate HDR scene

if transition is not admitted
→ do not blindly mutate
→ Present-time fallback remains available where appropriate
```

This is FIELD-PROVEN.

PR #12 was the first major transition away from “Present-only shader injector” toward an actual scene-pipeline graphics framework.

---

# 7. Live editor / settings foundation

The live GPU-rendered settings UI works.

DirectInput mouse capture works.

Settings persist to:

```text
...\My Games\Skyrim Special Edition\GZFX\Settings.ini
```

Settings migration inserts newly introduced keys without intentionally overwriting existing user values.

Manual evidence capture:

```text
Shift + Numpad 4
```

writes pairs such as:

```text
Diagnostics\manual_capture_0001.bmp
Diagnostics\manual_capture_0001.txt
```

The TXT records exact live effect values.

---

# 8. Effect history — what worked and what failed

This section is CRITICAL.

Do not blindly continue tuning the old AO/SSIL math.

## Ambient Contact

The first visible effect worked.

User field observation:

- GZFX side visibly had stronger local depth/contact;
- rocks and ground appeared more modeled;
- split comparison was clearly non-placeholder.

Ambient Contact established that GZFX could make useful scene-aware visual changes.

## Local Glow

Local Glow works and can create dramatic HDR differences.

But it is not the missing ENB-class depth system.

Strong settings can become excessive.

Do not confuse Local Glow with GI.

## Horizon AO / “HBAO-style” attempts

Several iterations were tried.

### Failure class 1 — coherent banding

Early Horizon AO used fixed screen-space sample directions.

Result:

- obvious horizontal bands;
- especially visible on water and large smooth depth gradients;
- high strength/radius became unusable.

### Failure class 2 — global gray response

AO fields became nonzero over nearly the whole screen.

One field capture showed approximately:

```text
AO mean ~0.21
AO nonzero fraction ~99%
```

At moderate/high strength the image became gray rather than selectively occluded.

### Failure class 3 — flicker / crawling

PR #16 field verdict:

- SSIL extremely weak;
- Horizon/HBAO shadows flickered massively;
- horizontal lines still started around roughly 0.13+ strength;
- no tuning combination looked genuinely good;
- user stopped the long test early after confirming the effects were still aesthetically/stability-wise poor.

This is the most important recent field verdict.

Do not tell the user PR16 still needs testing.

It was tested and failed artistically.

## SSIL failures

Recovered SSIL was technically functional but:

- often too weak;
- higher values could become excessive/global brightness;
- lacked convincing stable local bounce;
- depth-only surface orientation was insufficient;
- lacked temporal history before PR18.

## Performance evidence

One ambient-depth build measured approximately:

```text
analysis                 5.14 ms
horizontal denoise       0.047 ms
vertical denoise         0.047 ms
composite                0.159 ms
total ambient depth      5.40 ms
```

at the user's 2560×1440 field setup.

That is too expensive for the quality obtained.

Do not assume “more samples” is the path forward.

---

# 9. Why PR17 changed the architecture

The user asked an important question:

> Are AO/SSIL failures partly because GZFX still does not understand enough of Skyrim’s other graphics-pipeline functions/resources?

Answer: **yes, very plausibly.**

Depth + HDR color + camera are enough to make screen-space effects.

They are NOT proof that they are the best inputs for a high-quality integrated lighting system.

PR17 pivoted to stock-render semantic mapping.

For that capture GZFX temporarily suppressed its own scene effects so it could observe stock Skyrim.

The field run used the Standing Stones area and the user also cycled day/night.

The original PR17 trace only retained the first short stable gameplay window, so the late day/night chronology was not fully preserved.

PR18 adds manual semantic recapture to fix this.

---

# 10. PR17 semantic breakthrough — VERIFIED stock Skyrim MRT

The stock renderer semantic capture identified the main scene MRT group.

When Skyrim renders the main opaque scene, GZFX observed:

```text
SV_Target0
R16G16B16A16_FLOAT
→ main HDR scene color

SV_Target1
R16G16_FLOAT
→ native Skyrim motion vectors

SV_Target2
R8G8B8A8_UNORM
→ native Skyrim encoded view-space normal target (R/G normal proven; B/A unresolved)

SV_Target3
R8G8_UNORM
→ native full-resolution auxiliary target
→ exact semantics NOT YET PROMOTED

DSV
R24G8_TYPELESS
→ main scene depth
```

### Why motion is considered verified

- exact MRT slot relationship;
- expected full-resolution format;
- public Skyrim renderer knowledge matches this slot/format;
- PR18 synthetic harness reproduces the same structure.

### Why normal/mask target is considered verified

- exact MRT slot relationship;
- RGBA8 full-resolution target;
- visually decodes into coherent scene normals over:
  - terrain;
  - rocks;
  - trees;
  - actors/player;
- ping-pongs across frames;
- public Skyrim renderer work confirms a normal-bearing target in this role, but GZFX field evidence has not yet proven the B/A channel semantics.

THIS IS A MAJOR ARCHITECTURAL RESULT.

Do not go back to treating depth-derived normals as the primary source.

---

# 11. PR18 — CURRENT MAIN — native normals + motion + temporal history

PR #18:

```text
Use Skyrim native normals and motion for temporal AO/SSIL
```

is MERGED.

This is the current source-of-truth implementation.

## Native normal use

Ambient Visibility / SSIL now use Skyrim’s native normal field when the correct current-frame target is available.

RG is decoded as the inverse of Skyrim’s vanilla normal encoding.

Depth-derived normals remain only as a fail-soft fallback.

## Native motion use

GZFX consumes the current-frame:

```text
R16G16_FLOAT
```

motion-vector target.

Temporal reprojection uses:

```text
previousUV = currentUV + motion.xy
```

## Temporal history

GZFX now maintains half-resolution ping-pong history for:

- AO;
- indirect lighting;
- view-space depth;
- view-space normal.

History rejection includes:

- reprojected UV outside frame;
- relative view-depth mismatch;
- insufficient normal agreement;
- extreme motion.

Accepted history is neighborhood-clamped near the current AO/radiance signal.

Current-frame weight rises with motion so fast movement adapts more quickly.

If native motion is unavailable:

- temporal accumulation is disabled for that frame;
- spatial AO/SSIL continues.

If native normal is unavailable:

- depth-derived normal fallback remains.

## GPU timing

The temporal pass is included in GPU timing evidence.

## Semantic capture continuity

At real Skyrim startup:

1. GZFX performs a short stock-only semantic capture.
2. Once that initial window completes, normal GZFX effects automatically resume.

Manual stock capture:

```text
Shift + Numpad 5
```

This is specifically for deliberate:

- day vs night;
- interior vs exterior;
- water;
- weather;
- shadow changes;
- other renderer states.

PR18 writes numbered semantic evidence for manual captures.

---

# 12. PR18 CI acceptance

PR18 passed:

```text
13 / 13 tests
0 failures
```

The new:

```text
native_surface_temporal
```

harness reproduces the field-discovered stock MRT:

- HDR scene;
- R16G16 motion;
- RGBA8 native normal/masks;
- R8G8 auxiliary;
- main depth.

It requires:

- native normal acquisition;
- motion acquisition;
- temporal path enabled;
- valid history after successive frames;
- ACTUAL prior-frame history reuse on a later frame;
- nonzero native resource identities.

The existing positive AO/SSIL tests and flat/uniform neutrality tests also remain green.

This is synthetic/native CI proof.

### PR18 REAL-SKYRIM FIELD VERDICT — COMPLETE

PR18 was field-tested on October 23, 2026.

The important low-level path worked in real Skyrim:

```text
native_normal=true
motion=true
temporal=true
history=true
history_reused=true
```

But the artistic/behavior result was still poor:

- Horizon/AO flicker remained;
- bounce/SSIL was effectively nonfunctional visually;
- raising bounce strength produced almost no meaningful scene response;
- Local Glow still responded strongly, proving the UI/settings/composite plumbing itself was not generally dead.

The SSIL field evidence was nearly empty even at high bounce strength.

The user explicitly does NOT want more AO/SSIL slider tuning yet.

Controlling conclusion:

> GZFX must first map Skyrim's renderer much more completely and determine the correct resource semantics and lighting/integration stages. Only after that cartography is mature should AO, SSIL, temporal behavior, menu controls, and slider ranges be redesigned/tuned.

---

# 13. Current test suite

Current CMake registers 13 tests:

```text
1  runtime_smoke
2  d3d11_lifecycle_probe
3  renderer_forensics
4  resource_intelligence
5  visible_pass
6  menu_only
7  pre_ui_effects
8  ambient_depth_ao
9  ambient_depth_ssil
10 ambient_visibility_flat_ao
11 ambient_visibility_uniform_ssil
12 renderer_semantic_probe
13 native_surface_temporal
```

The merged 0.0.17 cartography baseline passed all 13. The 0.0.18 atlas branch must also pass the full 13-test Windows x64 gate before merge.

---

# 14. Dead / deprioritized paths

Do not repeat these without genuinely new evidence.

## Dead idea: one tiny probe per build

Project has outgrown baby-step iteration.

Use broad forensic/implementation passes with fail-soft subfeatures.

## Dead idea: final Present as permanent effect stage

Present is useful for:

- debug views;
- menu;
- fallback;
- evidence.

Real lighting belongs earlier in the scene pipeline.

## Dead idea: depth-derived normals as primary AO/SSIL normals

PR16 field behavior showed why this is insufficient:

- flicker;
- crawling;
- banding;
- unstable surface orientation.

Native Skyrim normals are now available.

Depth-derived normals are fallback only.

## Dead idea: simply increase AO sample count

One build consumed ~5.4 ms for poor field quality.

More brute force is not automatically better.

## Dead idea: “fix” bad AO by shrinking the slider

The user correctly rejected this.

A slider where 0.13 is the only useful range is evidence of poor field construction, not a good artistic control.

## Dead idea: trust `status=applied` as visual acceptance

Past builds technically executed while AO/SSIL were dead or useless.

Acceptance must prove ownership/effect behavior.

---

# 15. Standard field-test location

Preferred external benchmark:

> The first Guardian Standing Stones area where the player selects Mage / Thief / Warrior.

This area is useful because one small location contains:

- water;
- shoreline;
- mountains;
- rocks;
- grass;
- trees;
- character;
- sky/fog;
- near and far geometry;
- strong depth gradients.

Keep using the same scene when comparing builds.

---

# 16. Current phase — renderer semantic atlas

**DO NOT tune AO/SSIL yet.**

The latest authoritative field bundle is:

```text
GZFXfrfreded.zip
```

It was produced by GZFX 0.0.17 and contains the automatic stock capture plus manual cartography captures `0001` through `0007`.

The field graph promoted several renderer systems from guesses to evidence-backed semantics.

## Identified / promoted from the 0.0.17 field run

### Main depth + depth chronology

Main depth:

```text
2560x1440
R24G8_TYPELESS
```

The graph proves at least two distinct copies:

- an early/pre-opaque depth copy before the four-target opaque MRT;
- a later depth copy after substantially more scene rendering.

Do not treat “the depth buffer” as one semantically timeless input.

### Main opaque MRT

The stock four-target world MRT is confirmed:

```text
RT0  R16G16B16A16_FLOAT  main HDR scene
RT1  R16G16_FLOAT        motion vectors
RT2  R8G8B8A8_UNORM      encoded native normal target
RT3  R8G8_UNORM          auxiliary target, channels still unresolved
DSV  R24G8_TYPELESS      main depth
```

The normal target ping-pongs frame-to-frame.

Do **not** call RT2 B/A “TAA/SSR masks” without direct proof.

### Exterior volumetric lighting

The exterior path is structurally identified:

- 4096x4096 shadow-map array;
- 512x512 secondary shadow-map array;
- 4096x1 R32_FLOAT lookup;
- 32x32x32 R8 3D noise;
- two 320x192x90 R16_FLOAT 3D density volumes;
- 90-slice per-frame ping-pong blur;
- later full-resolution R16_FLOAT projection/depth-aware blur.

This entire compute family disappears in the interior-class capture while ordinary scene rendering/AO/post remains alive.

### Stock CameraZ / SAO depth hierarchy

A full-resolution R32_FLOAT resource with 12 mips is built as:

```text
2560x1440
1280x720
640x360
320x180
160x90
80x45
40x22
20x11
10x5
5x2
2x1
1x1
```

This is the stock CameraZ / SAO depth pyramid.

### Stock temporal RawAO family

The live graph proves:

- CameraZ input;
- current native normal input;
- motion-vector input;
- two full-resolution RGBA8 AO/history resources that alternate read/write every frame.

That is real temporal AO history behavior.

A two-pass RGBA8 filter immediately follows the raw temporal AO result.

### Stock raw-indirect / lighting composition boundary

A full-resolution R11G11B10_FLOAT surface is generated from CameraZ + HDR scene + the RT3 auxiliary surface.

A later full-resolution composite reads that indirect surface together with filtered AO, depth, normal, auxiliary/mask-like inputs, and a half-resolution HDR lighting/reflection surface, then writes back into main HDR.

The family is strongly mapped. Exact individual Bethesda image-space class names remain provisional until the visual atlas confirms contents.

### Half-resolution reflection / reduced-lighting family

A separate half-resolution branch uses normal + motion + HDR + depth/history-like inputs, produces half-resolution HDR intermediates, and runs a depth-aware two-pass compute blur before the main lighting composite consumes it.

This is a strong stock reflection/reduced-lighting match, but exact substage labels remain provisional.

### HDR luminance / exposure family

The graph exposes the later reduction chain down to tiny luminance/adaptation surfaces. This is the stock HDR downsample / luminance / light-adaptation family.

## Interior control

Manual `0007` is the interior-class capture with high confidence:

- thousands of real scene draws still occur;
- CameraZ/SAO/indirect/post remain present;
- the exterior 3D volumetric compute family disappears.

Manual `0006` remains “extra exterior / exact scene unknown” unless the user supplies a more exact label.

The complete pass/resource IDs and confidence levels are preserved in:

```text
docs/FIELD_CARTOGRAPHY_2026-10-23.md
```

---

# 17. Probe defects exposed by the field run and fixed in 0.0.18

The first cartographer was useful enough to expose flaws in itself.

## Known-resource identity mismatch

Known scene/depth/motion/normal/aux pointers were not canonicalized the same way as the resource table.

Effect:

- resources were present;
- `known_*` tags could still be absent.

0.0.18 canonicalizes those anchors and CI explicitly checks all five tags.

## Cumulative resource/shader inventories

The 0.0.17 manual reports accumulated resources discovered earlier in the same process.

Effect:

- later captures could report hundreds of resources that were not active in that specific three-frame window.

0.0.18 outputs capture-local resource/shader inventories while retaining stable global identity internally.

## Stale irrelevant shader attribution

D3D11 can leave a CS bound while graphics draws execute and graphics shaders bound while compute dispatch executes.

That state is legal, but reporting every bound stage as if it executed is misleading.

0.0.18:

- strips CS identity from draw records;
- strips graphics-shader identity from dispatch records;
- strips shader attribution from copy/clear/resolve/mip operations.

CI deliberately binds stale opposite-stage shaders and verifies the report does not lie about execution ownership.

## Semantic-preview coverage

The first run found important AO/indirect surfaces around semantic candidate ranks ~28–34, but only 14 previews were saved.

0.0.18:

- resets semantic counters per capture;
- raises the preview budget;
- includes reduced-resolution RTV/UAV intermediates;
- prioritizes independently known scene/depth/motion/normal/aux anchors;
- uses smaller previews to keep evidence size bounded;
- normal-decodes only the independently identified normal resource;
- motion-visualizes only the independently identified motion resource;
- emits grayscale per-channel planes for the known normal/aux resources and a bounded group of high-value RGBA8 intermediates;
- emits selected mip previews for mipped scalar resources such as CameraZ.

Accessibility remains mandatory: channel evidence is grayscale/textual, never hue-only.

---

# 18. Next field campaign — targeted semantic atlas

The next run is **not** another six-state broad survey.

The broad graph already answered that layer.

The purpose of 0.0.18 is to visually/structurally lock the remaining high-value resources:

1. temporal RawAO ping-pong surfaces;
2. AO two-pass filter surfaces;
3. R11G11B10 raw-indirect surface;
4. half-resolution reflection/reprojection surface;
5. half-resolution HDR reflection/reduced-lighting blur surfaces;
6. exterior volumetric full-resolution R16_FLOAT projection/blur;
7. CameraZ mip 0 plus coarse mips;
8. RT3 R8G8 auxiliary R/G channels;
9. native-normal R/G/B/A channel evidence without assuming B/A semantics;
10. HDR luminance/adaptation reduction surfaces.

Keep captures in **one Skyrim launch/session** so same-session shader/pass identity remains comparable.

Do not alter Horizon, Bounce, Local Glow, temporal weights, or artistic controls during this campaign.

---

# 19. Architectural rule before effect redesign

Do not rebuild GZFX AO/SSIL merely because stock resources now have names.

First determine the correct integration boundary:

- which depth state is appropriate for the effect;
- whether stock AO/indirect should be consumed, replaced, disabled, or intercepted;
- where ambient/directional/indirect lighting is actually combined;
- where temporal history belongs relative to Skyrim TAA;
- which stock resource already carries information GZFX is currently reconstructing badly;
- whether current HDR multiplicative visibility is fundamentally the wrong stage.

Only after that map is locked should GZFX redesign:

- AO;
- indirect/bounce;
- temporal rejection;
- local glow;
- menu controls;
- slider ranges;
- presets.

The current flicker and dead bounce remain intentionally unfixed while the integration architecture is under investigation.

---

# 20. User expectations and communication

User wants ambitious, high-throughput engineering.

Do not patronize.

Do not bury the user in 80 tiny test steps when a short focused field run will answer the question.

The user is explicitly aiming for:

> GZFX eventually making ENB look obsolete / primitive by comparison.

Treat that as a long-term quality target, not a claim that current GZFX already does so.

Current reality:

- renderer access is advanced;
- framework architecture is real;
- visible effects work;
- native semantic understanding is rapidly improving;
- AO/SSIL artistic quality has NOT yet matched ENB;
- PR16 was a real field-quality failure;
- PR18 is the next serious attempt because it replaces guessed depth normals and adds motion-based temporal history.

Be precise about that distinction.

---

# 21. Absolute takeover rule

When this README and older chat history conflict:

1. current `main`;
2. newest uploaded field evidence;
3. this README;
4. merged PR descriptions/docs;
5. older discussion

in that order.

Never tell the user to redo PR16 or PR17 tests.

Never treat the stale `render/native-surface-temporal-foundation` branch as active.

The exact current frontier is:

> **GZFX 0.0.18 semantic-atlas work is the active frontier. The latest field evidence is `GZFXfrfreded.zip`, generated by 0.0.17. It identified the main depth chronology, four-target opaque MRT, exterior volumetric 3D chain, CameraZ mip hierarchy, temporal stock RawAO ping-pong/filter path, raw-indirect/composite boundary, half-resolution reflection/reduced-lighting branch, HDR luminance/adaptation family, and an interior-vs-exterior split. Effect tuning remains intentionally paused. PR #21 fixes cartographer correctness defects and expands targeted grayscale/channel/mip previews so the next field run can lock the remaining resource semantics before AO/SSIL is redesigned.**

That is where the next agent starts.
