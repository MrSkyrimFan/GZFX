# GZFX

> **EVERY AGENT: READ `AGENT.md` BEFORE DOING ANYTHING ELSE.**

GZFX is a standalone, SKSE-free Direct3D 11 graphics framework for Skyrim SE/AE.

```text
SkyrimSE.exe
  -> dinput8.dll
  -> GZFX.dll
  -> D3D11 / DXGI
  -> HLSL / GPU
```

The long-term product target is a native graphics framework capable of replacing the graphics role of ENB + ReShade with integrated lighting, ambient visibility, indirect radiance, shadows, atmosphere, volumetrics, materials, water, reflections, HDR/post, presets, a live editor, compatibility systems, and a public renderer API.

## Current engineering frontier

```text
foundation baseline: 0.0.20-foundation-epochs-stage-correctness
Wave 1: CLOSED
Wave 2: CLOSED
Wave 3: CLOSED
broad foundation renderer cartography: COMPLETE
Wave 4: renderer-stack ownership architecture PROVEN
current runtime: 0.0.26-renderer-identity-engine-epochs
current phase: TEST / RENDERER-IDENTITY + ENGINE-EPOCH FIELD VALIDATION
primary discovery authority: Continuous Renderer Telemetry Listener
stable shader/resource identity: observed at D3D11 creation time
physical DXGI present timeline: explicit present epochs
deferred command lists: owner-tagged with late-detail-independent retained evidence
verified Skyrim engine epoch profile: Steam AE 1.6.1170 post-processing + pre-UI, fail-closed on other runtimes
field-validation candidates: HDRReduction / BeforeTonemap / BeforeGlobalTAA / AfterGlobalTAA
still unpromoted: WaterDomain / PostTonemapPreUi / AfterShadowProduction
next graphics wave: NOT YET SELECTED
```

Wave 3 remains field-closed. Wave 4 proved that GZFX can generate its own ambient-visibility signal and replace Skyrim's filtered SAO input before Skyrim's native Ambient Screen-Space Composite. Final artistic tuning remains intentionally deferred until the renderer architecture is sufficiently owned.

The 0.0.25 field campaign proved that the remaining late-stack problem was primarily identity/timeline correctness, not missing renderer structure: the HDR/TAA resource spine remained present, the greedy HDR detector could poison itself on an earlier downsample, the old `UiPresent` marker was not the physical DXGI Present boundary, and most of the supposedly "Skyrim" deferred-list population was GZFX's own untagged work.

0.0.26 consolidates that evidence into a stable renderer-identity layer. Shader content hashes and process-local object identities are now separate; resource/shader provenance is captured at creation; every known GZFX command-list producer is explicitly owned; deferred evidence survives outside the immediate late-detail window; physical Present phases are recorded; and a verified, fail-closed Steam AE 1.6.1170 profile observes Skyrim post-processing and pre-UI engine epochs without adding SKSE or Address Library.

**Important:** 0.0.26 remains a forensic field-validation build. Its FPS is not release-performance evidence. `WaterDomain`, `PostTonemapPreUi`, and `AfterShadowProduction` remain intentionally unclaimed until the next real-Skyrim DATA pass correlates stable shader/resource identity, engine epochs, HDR/TAA structure, and the physical Present timeline. The next graphics feature is not selected yet.

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

The test package contains the root-ready DLLs.

## Hard project rules

- No SKSE dependency.
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
