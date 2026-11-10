# GZFX

> **EVERY AGENT: READ `AGENT.md` BEFORE DOING ANYTHING ELSE.**

GZFX is an SKSE-hosted native Direct3D 11 graphics framework for Skyrim SE/AE.

```text
SkyrimSE.exe
  -> official SKSE
     -> Data/SKSE/Plugins/GZFX.dll
        -> MIT CommonLibSSE-GG -> typed Skyrim engine state
        -> D3D11 / DXGI -> HLSL / GPU
```

The current target is Skyrim Steam 1.7.104 + SKSE 2.3.1 + Address Library for
SKSE Plugins. SKSE and Address Library are installed separately and are not
redistributed by GZFX. GZFX statically links the MIT-licensed
`eddoursul/CommonLibSSE-GG` engine-access layer into the single GZFX DLL.

The long-term product target is a native graphics framework capable of replacing the graphics role of ENB + ReShade with integrated lighting, ambient visibility, indirect radiance, shadows, atmosphere, volumetrics, materials, water, reflections, HDR/post, presets, a live editor, compatibility systems, and a public renderer API.

## Current engineering frontier

```text
foundation baseline: 0.0.20-foundation-epochs-stage-correctness
Wave 1: CLOSED
Wave 2: CLOSED
Wave 3: CLOSED
broad foundation renderer cartography: COMPLETE
Wave 4: renderer-stack ownership architecture PROVEN
current runtime: 0.0.38-calibrated-unified-renderer
current phase: 0.0.38 ENGINEERING CLOSED / COMBINED REAL-SKYRIM VISUAL AND PERFORMANCE FIELD VALIDATION NEXT
primary discovery authority: Continuous Renderer Telemetry Listener
graphics front door: SKSE Preload/Load + internal GZFX D3D11/DXGI lifecycle capture
engine access: pinned MIT CommonLibSSE-GG + Address Library v5 on Skyrim 1.7.104
stable shader/resource identity: observed at D3D11 creation time
physical DXGI present timeline: explicit present epochs
deferred command lists: owner-tagged with late-detail-independent retained evidence
universal renderer core: runtime-independent D3D11/DXGI architecture
known runtime families: 1.5.97 / common 1.6.x / 1.7.99 / 1.7.104
verified optional engine epoch profile: Steam AE 1.6.1170 only; other runtimes keep full D3D core and fail closed on engine hooks
field-locked late contracts: HDRReduction / BeforeTonemap / BeforeGlobalTAA / AfterGlobalTAA
HDRReduction contract: bounded MAIN_HDR ancestry + bounded full-resolution sibling sources + true <=2x2 terminal
BeforeTonemap contract: immediately before Skyrim HDR tonemap/blend/cinematic image-space pass
field certification: Skyrim 1.7.104 proven; other runtime families architecture-supported but not yet field-certified
still unpromoted: WaterDomain / PostTonemapPreUi / AfterShadowProduction
active wave: PostTonemapPreUi evidence/cartography only; no semantic promotion yet
```

Wave 3 remains field-closed. Wave 4 proved that GZFX can generate its own
ambient-visibility signal and replace Skyrim's filtered SAO input before
Skyrim's native Ambient Screen-Space Composite. Final artistic tuning remains
intentionally deferred until the renderer architecture is sufficiently owned.

The 0.0.28 real-Skyrim field session proved the standalone D3D11 front door,
qualified primary selection, pre-resource creation attachment, core Skyrim
resource provenance, physical Present timeline, command-list ownership, clean
natural process exit, and zero-drop telemetry closure on Skyrim 1.7.104. Global
TAA remained exactly 1:1 on all 7,541 active scene frames, and a true 2x2
HDR/exposure terminal physically existed on all 7,541 frames.

That session also isolated the remaining HDR semantic miss. On 2,640 interior
frames, Skyrim emits an earlier plausible MAIN_HDR downsample, then a later
full-resolution RGBA16F sibling descended from MAIN_HDR, then the real reduction
root from that sibling. The old single-source ancestry state stopped learning
new HDR sources after the early root started, so semantic HDRReduction and
BeforeTonemap missed the otherwise-complete physical chain.

0.0.29 keeps a fixed-capacity, depth-bounded set of verified full-resolution HDR
sources for the current frame. The existing single reduction chain may restart
only from canonical MAIN_HDR or one of those verified sources. Coherent descent,
the true <=2x2 terminal, BeforeTonemap dependency, and independent global TAA
contract are preserved.

Before field testing, the owner required another complete no-drift pipeline.
The current 0.0.29 algorithm was independently replayed across all 1,022,351
detailed rows from the prior field archive and recovered 7,541/7,541 active
frames. PR #33 changed only the test harness and CMake, strengthening the exact
field topology and locking capacity, ancestry-depth, frame-isolation, and
one-to-one per-frame semantic coverage. PR and merged-main CI pass 37/37.
Product executable code/data remained byte-identical after normalization of
linker/debug PE timestamps.

The subsequent 0.0.29 real-Skyrim field session then validated the repaired
architecture on 12,123 active scene frames: HDRReduction, BeforeTonemap,
BeforeGlobalTAA, and AfterGlobalTAA each covered 12,123/12,123 frames; the true
<=2x2 HDR terminal covered 12,123/12,123; exterior coverage was 8,882/8,882;
interior coverage was 3,241/3,241; and the exact derived-HDR sibling path that
failed in 0.0.28 was exercised successfully on 2,884 interior frames. An
independent raw replay reproduced HDRReduction and BeforeTonemap with zero
frame-set mismatch.

Accordingly, HDRReduction and BeforeTonemap are now field-locked structural
contracts for Skyrim 1.7.104. BeforeGlobalTAA and AfterGlobalTAA remain locked.
The architecture remains runtime-independent, but other runtime families are
not claimed as real-field certified by this campaign.

0.0.30 begins the next evidence wave without promoting a new semantic. Detailed
late pipeline telemetry now remains active through physical
BeforeOriginalPresent instead of stopping at the internal UiPresent marker.
For Skyrim 1.7.104, GZFX also performs discovery-only masked scans for the
publicly corroborated post-processing and pre-UI opcode skeletons and records
candidate match/target RVAs in normal session evidence. Those candidates are
never hooked by 0.0.30 and engineProfileAvailable remains false until later
field DATA and research independently verify them.

The first real-Skyrim 0.0.32 validation failed before healthy GZFX runtime
output. 0.0.33 repaired that loader boundary and GZFX1 field-closed it. 0.0.34
migrated DirectInput mouse ownership into the single SKSE-loaded GZFX.dll, and
GZFX2 field evidence now closes that mouse path as PASS. GZFX2 also proved the
current effect sliders were disconnected from the active render owners even
though their values persisted. 0.0.35 reconnects live Horizon/HBAO,
AmbientContact, spatial IndirectBounce/SSIL and LocalGlow controls while keeping
AV2 as the current AO owner. Routine broad late-pipeline telemetry is now opt-in
instead of default.

0.0.32 completed the owner-authorized MIT CommonLib engine-access integration.
Its loader path was subsequently repaired by 0.0.33 and field-proven by GZFX1.
CommonLib remains statically linked into the single GZFX DLL and Address Library
remains external.

**Important:** GZFX uses official SKSE as its loader/lifecycle host, the
MIT-licensed CommonLibSSE-GG line as a typed read-only Skyrim engine-access
plane, and retains its own D3D11/DXGI renderer. ENB and ReShade remain architecture
research references, not runtime dependencies or coexistence targets. The GZFX
package contains exactly one binary at `Data/SKSE/Plugins/GZFX.dll`; SKSE is
installed separately. `WaterDomain`, `PostTonemapPreUi`, and
`AfterShadowProduction` remain intentionally unclaimed pending independent
evidence.

## Permanent authority workflow

GZFX authority now lives **inside this repository** under:

```text
GZFXWorkFolder/
  new/
  old/
```

The old manual workflow of downloading TXT files, manually putting them into a folder, and uploading that folder back to ChatGPT is retired permanently.

For every development/test cycle:

```text
field data
  -> full findings TXT goes into GZFXWorkFolder/new/
research
  -> full research TXT goes into GZFXWorkFolder/new/
blueprint
  -> full blueprint TXT goes into GZFXWorkFolder/new/
implementation
  -> agent reads those files directly from the repo
  -> implements the coherent engineering wave
  -> CI / merge / package verification
  -> full engineering-result TXT goes into GZFXWorkFolder/old/
  -> the three completed current-cycle TXT files move from new/ to old/
  -> new/ is cleared for the next field cycle
in-game testing
  -> chat contains only the short status summary and exact test instructions
repeat
```

This loop remains in force through completion and official Nexus release unless the repository owner explicitly changes it.

### Chat rule

Normal chat is deliberately lean:

- short progress/status summaries;
- exact in-game instructions when a build is ready;
- no giant findings/research/blueprint/engineering reports in chat.

All durable technical evidence, research, blueprints, engineering reports, handoffs, CI proof, and implementation records belong in the repository.

## Authority order

For repository truth:

1. current `main`;
2. current real-Skyrim field evidence;
3. `GZFXWorkFolder/new/` during an active cycle;
4. newest applicable records in `GZFXWorkFolder/old/`;
5. current source/docs;
6. older chats.

Newer verified field evidence supersedes older assumptions.

## Build / user package

GitHub Actions builds and tests Windows x64. A successful main build stages the
validated package on the unprotected `lance-package` branch because repository
rules reject Actions-bot writes directly to `main`. During closeout, the agent
verifies that staged blob against the validated artifact and syncs the exact ZIP
into:

```text
LANCE YOUR FILE IS HERE/GZFX-SkyrimSE-x64.zip
```

The test package contains exactly `Data/SKSE/Plugins/GZFX.dll`.

## Hard project rules

- Official SKSE is the loader/lifecycle dependency; never bundle SKSE runtime binaries in the GZFX package.
- Address Library for SKSE Plugins is an external runtime dependency for CommonLib engine access; never bundle it in the GZFX package.
- The CommonLib dependency must stay on an owner-approved permissive/MIT line unless Greenezone explicitly authorizes a licensing-model change.
- Do not copy proprietary ENB implementation.
- Do not restart broad renderer cartography unless a real feature blocker requires a targeted investigation.
- Do not resurrect retired `OPAQUE_FINAL_DEPTH` or retired generic stage names.
- Do not hardcode field-observed draw ordinal gaps.
- Do not call Local Glow GI.
- Do not silently substitute depth-derived normals for verified native normals.
- Diagnostics must not rely on hue alone.
- Fail closed on missing/stale semantic inputs.
- Large coherent engineering passes with embedded targeted probes are preferred over one-probe-per-version churn.

See `AGENT.md` for the mandatory operating contract.


## 0.0.36 unified product renderer

0.0.36 is merged and automated-engineering closed. It replaces disconnected
post-effect expansion with one shared HDR-stage product graph. The graph owns one
scene snapshot, shared luminance/bloom products, exposure history, one ordered
composite and one final MAIN_HDR writeback.

The current live product model exposes 86 rows and includes exposure/adaptation,
bloom/lens dirt, Local Glow, grading/tone shaping, DoF, native-vector motion blur,
depth fog, vignette, grain, chromatic aberration, denoise, sharpening,
letterbox/pillarbox, quality tiers, five built-in looks, three custom profiles and
a product master switch while preserving AV2 HBAO and indirect-only SSIL
ownership.

PR #41 and merged-main CI pass 41/41 plus PE and one-DLL packaging. Real-Skyrim
visual/performance validation of this broad graph is intentionally combined with
the next native-domain semantic-anchor build instead of forcing another tiny
field cycle.


## 0.0.37 combined native-domain anchors

0.0.37 adds one read-only semantic observer over the existing D3D11 evidence.
It correlates Address Library / CommonLib engine identities for shadow, water,
reflection, underwater, volumetric and broad effects intervals with the same
telemetry timeline used by the renderer.

This is intentionally one combined wave. The next real-Skyrim session will
validate the broad 0.0.36 product graph and return native-domain correlations
together instead of testing one feature per DLL.


## 0.0.37 automated-engineering closeout: ready for GZFX4

PRs #42 and #43 are merged. The latest Windows merged-main run #301 (42162962740) passes PE/load, 42/42 tests, runtime smoke and one-DLL packaging. Exact main package: 819826-byte ZIP, SHA-256 `3eaf5536eaef4089faeb1dd1e98105bd5ee0bebe598028cc2888a4074413e875`, Git blob `8e0e8744db35574ed8e8720ed161f9820e2523fe`. Commit `be958535bdc5e939a54491979c90c69969c6fc8d` published that validated blob without rebuilding or rezipping it.

0.0.37 adds a read-only engine-domain observer for shadow-light subtypes, world/water/effects phases, image-space water/reflection/underwater/volumetric identities and late post-processing/global and UI TAA/HDR/tonemap/copy/refraction/pre-UI anchors. It does not yet replace native weather, water, shadows, fire or lighting and does not prove final product visual quality. Shadow/water/PostTonemapPreUi promotions await real-Skyrim field correlation. The next step is one combined GZFX4 field test of the 0.0.36 unified renderer + 0.0.37 observers; HBAO flicker, performance and final tuning stay deferred.

Controlling closeout: `GZFXWorkFolder/old/GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.37.txt` and `GZFXWorkFolder/old/GZFX_0.0.37_NATIVE_DOMAIN_ANCHOR_ENGINEERING_RESULT.txt`.

## 0.0.38 calibrated unified renderer (automated-engineering closed)

The owner-returned `GZFX12423JJ.zip` 0.0.37 session exposed severe over-strong
controls, dark/grey output, misleading preset selection, and an unwanted
five-position Shift+Numpad3 display cycle. PR #44 implements one coherent
0.0.38 wave: 48 restrained ranges, safer exposure and tone behavior, actual
preset-on-selection, RESET DEFAULTS preserving three saved custom profiles,
**exactly two modes (vanilla OFF / full-screen GZFX ON)** and six additional
shared-HDR post/tone features (shadow recovery, highlight rolloff, clarity,
halation, split toning, fine dither). No half-screen or hidden diagnostic
display modes remain. New contract + embedded HLSL tests bring suite to 44.

PR #44 merged at `60d7952152ec652e1597fbc144349efb376213ab`.
PR run #310 and merged-main run #311 pass 44/44, Windows x64 PE and
one-DLL package checks. The validated `821671`-byte archive (SHA-256
`5802701929e8c293c7a252d89890e13d34181e93422174356ec5b50e926e7f8d`)
was staged on `lance-package`, then published exactly unchanged on main in
package-only commit `3331489a3ed31d3c1f03cdbdf9d41c1ec73ee106`.

`LANCE YOUR FILE IS HERE/GZFX-SkyrimSE-x64.zip` contains only
`Data/SKSE/Plugins/GZFX.dll`. SKSE 2.3.1 and Address Library v5 are external.

The real-Skyrim 0.0.38 visual/performance gate is **not** yet closed.
GZFX remains independent of ENB and ReShade but has not achieved full native
feature parity; shadow, weather/sky, water/reflections, fire/emissive,
materials and native lighting ownership still require evidence-based work.
Read the 0.0.38 engineering result and new-chat index in `GZFXWorkFolder/old/`
before proceeding to the combined field session.
