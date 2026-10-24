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

After PR #19 merges, its 0.0.17 renderer-cartography source and the subsequent automatic package refresh on `main` supersede that historical hash.

Current runtime version:

```text
0.0.18-renderer-semantic-atlas
```

Current engineering phase:

```text
RENDERER SEMANTIC ATLAS
pipeline understanding first
effect tuning later
```

PR #19 established execution-level renderer cartography.

PR #20:
```text
Build renderer semantic atlas from field cartography
```

PR #20 is the current engineering frontier. PR18 remains the historical native-normal/motion/temporal foundation; PR19 remains the first renderer-cartography foundation.

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
→ native Skyrim encoded view-space normal + TAA/SSR masks

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
- public Skyrim renderer knowledge matches a normal/TAA/SSR-mask target in this role.

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

PR18 head passed all 13.

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

The first 0.0.17 cartography campaign is complete and preserved in:

```text
docs/FIELD_CARTOGRAPHY_001.md
```

It established an evidence-backed stock frame skeleton.

## Hard / promoted findings

- stock four-target opaque MRT + main depth;
- early and late main-depth copy chronology;
- exterior 3D volumetric-light generation and slice blur;
- stock Camera-Z / depth-pyramid family;
- AO / indirect / shadow / reflection composite boundary writing into main HDR;
- clean exterior-vs-interior volumetric split.

## Strong findings awaiting one more direct surface proof

- stock shadow-mask target;
- reflection mask/color/blur chain;
- temporal RawAO;
- AO blur chain;
- raw indirect light;
- exterior volumetric temporal screen resolve / blur;
- HDR luminance/exposure/light-adaptation reduction family.

The most important architecture result is:

> Skyrim already combines AO, indirect light, depth, normals, shadow information and blurred reflections at a dedicated full-resolution HDR lighting-composite boundary before later image-space/HDR stages.

That means the old mental model — “depth + normals + final HDR is enough; bolt GZFX lighting on later” — is incomplete.

This supports the user's pipeline-first hypothesis. It does **not** yet prove every current GZFX shader equation is wrong.

## 0.0.18 diagnostic upgrades

0.0.18 does not change effect behavior.

It adds:

- COM-canonical resource identities so semantic/cartography evidence crosswalks;
- capture-local semantic counters;
- capture-local resource/shader reporting;
- structural landmark classification independent of one-launch pointer constants;
- `pipeline_landmarks*.csv`;
- direct surface snapshots for important 2D landmarks;
- raw R/G/B/A views for mask-like surfaces;
- known normal B/A channel evidence;
- known auxiliary R/G channel evidence;
- objective channel means/nonzero/min/max;
- constant-buffer use correlated with latest CPU update hash;
- corrected real-device attachment evidence.

The experimental ID3D11Device creation-interception lane remains disabled in field builds.

---

# 17. Immediate 0.0.18 field evidence campaign

Keep all captures in **one Skyrim process/session**.

Use Shift+Numpad5 in this order:

1. exterior daylight, still;
2. exterior daylight, slow camera movement;
3. exterior night, approximately same view;
4. water/reflection-heavy view;
5. strong direct-sun shadow view;
6. normal interior cell.

Do not tune Horizon, Bounce, Local Glow, temporal weights, or other artistic controls.

The next analysis should prioritize:

- normal B/A semantics;
- auxiliary MRT R/G semantics;
- shadow-mask channels;
- RawAO channels and temporal ping-pong behavior;
- AO blur contents;
- raw-indirect radiance contents;
- reflection mask/color/blur contents;
- exact relationship between early/late depth copies and later consumers;
- volumetric temporal inputs/history;
- HDR reduction/exposure chain;
- constant-buffer parameter clustering by landmark pass;
- water-specific additions/removals relative to the same-session baseline.

Only after this atlas is sufficiently mature should GZFX choose where to rebuild AO/GI and how the final UI/slider model should behave.

---

# 18. Renderer semantics still NOT finished

PR17 was a major breakthrough, not the end.

Still unresolved / worth probing:

- exact semantics of `SV_Target3 R8G8_UNORM`;
- exact B/A semantics of the native-normal RGBA8 target — do NOT keep calling them TAA/SSR masks without field proof;
- exact RawAO channel semantics and history encoding;
- exact early-vs-late depth-copy Bethesda semantics;
- shadow-mask channel semantics and shadow-map taxonomy;
- water/reflection resource semantics beyond the now-mapped reflection blur chain;
- exact exposure / bloom reduction subpass semantics;
- exact ambient/sky lighting integration;
- material/albedo/specular/roughness-like semantics available from stock forward passes.

The user’s intuition was correct:

> Some AO/SSIL quality problems may come from not understanding enough of the rest of Skyrim’s graphics pipeline.

Treat that as an active architecture concern.

Do not assume AO/GI can reach ENB-class integration by operating only on final depth + scene color.

---

# 19. Strategic direction after PR18

If PR18 real-field stability is materially improved:

Next serious renderer phase should consider:

1. Native normal/mask semantics:
   - understand B/A mask channels;
   - exploit TAA/SSR-related information if useful.

2. Temporal AO/SSIL refinement:
   - stronger disocclusion rejection;
   - neighborhood clipping;
   - history confidence;
   - per-effect temporal response.

3. Map vanilla SSAO:
   - identify raw SSAO output;
   - identify its exact normal/depth inputs;
   - determine whether GZFX should consume, replace, or inject near that stage.

4. Lighting integration:
   - avoid treating AO only as final HDR multiplication;
   - investigate ambient/sky/diffuse composition stage.

5. Better indirect lighting:
   - source and receiver normals;
   - radiance confidence;
   - temporal accumulation;
   - screen-space visibility;
   - multi-scale / hierarchical depth;
   - eventually bent-normal / specular-occlusion concepts.

6. Continue semantic mapping across controlled states using `Shift+Numpad5`:
   - day;
   - night;
   - interior;
   - water;
   - weather;
   - strong shadows.

If PR18 field stability is still poor:

Do not keep adding artistic controls.

Use the native semantic evidence to identify whether the remaining instability originates in:

- motion-vector decode/convention;
- normal target ping-pong timing;
- mask usage;
- history rejection;
- injection timing;
- half-res reconstruction;
- underlying visibility formulation.

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

> **GZFX 0.0.18 renderer-semantic-atlas is the active frontier. The first 0.0.17 cartography field run mapped Skyrim's stock Camera-Z/RawAO/AO-blur/raw-indirect/HDR-composite structure, exterior volumetric generation/temporal blur, depth-copy chronology and reflection chain. Effect tuning remains intentionally paused. PR20 converts those findings into structural landmark classification, per-channel surface evidence, canonical resource IDs and constant-buffer update correlation for the next controlled field campaign.**

That is where the next agent starts.
