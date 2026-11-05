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
current runtime: 0.0.33-skse-loader-rescue
current phase: 0.0.33 SKSE LOADER RESCUE / CI + REAL-SKYRIM REVALIDATION
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
output: no GZFX runtime folder and no functional GZFX controls. 0.0.33 is a
bounded loader rescue. It makes SKSE Preload non-invasive, moves renderer arming
to normal SKSE Load, initializes CommonLib through SKSE::Init, and removes
dynamic Microsoft VC runtime dependencies from the shipped DLL. No locked
renderer semantic is being reopened.

0.0.32 completes the owner-authorized MIT CommonLib engine-access integration.
PR #37 and merged-main CI are green at 38/38. CommonLib is statically linked into
the single GZFX DLL; Address Library remains external; the validated package still
contains only `Data/SKSE/Plugins/GZFX.dll`. The next required action is a compact
real-Skyrim 1.7.104 field validation of the combined SKSE + CommonLib path before
broad graphics-feature engineering resumes.

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

GitHub Actions builds and tests Windows x64. A successful push to `main` refreshes:

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
