# GZFX

> **EVERY AGENT: READ `AGENT.md` BEFORE DOING ANYTHING ELSE.**

GZFX is a standalone, SKSE-free Direct3D 11 graphics framework for Skyrim SE/AE.

```text
SkyrimSE.exe
  -> d3d11.dll -> GZFX.dll -> D3D11 / DXGI -> HLSL / GPU
  -> dinput8.dll -> GZFX.dll -> input bridge
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
current runtime: 0.0.28-pre-field-lifecycle-hardening
current phase: TEST / PRE-FIELD LIFECYCLE-HARDENED RENDERER VALIDATION
primary discovery authority: Continuous Renderer Telemetry Listener
graphics front door: standalone GZFX d3d11.dll bootstrap + dinput8 input bridge
stable shader/resource identity: observed at D3D11 creation time
physical DXGI present timeline: explicit present epochs
deferred command lists: owner-tagged with late-detail-independent retained evidence
universal renderer core: runtime-independent D3D11/DXGI architecture
known runtime families: 1.5.97 / common 1.6.x / 1.7.99 / 1.7.104
verified optional engine epoch profile: Steam AE 1.6.1170 only; other runtimes keep full D3D core and fail closed on engine hooks
field-locked late contract: BeforeGlobalTAA / AfterGlobalTAA structural family
field-validation candidates: HDRReduction / BeforeTonemap
still unpromoted: WaterDomain / PostTonemapPreUi / AfterShadowProduction
next graphics wave: NOT YET SELECTED
```

Wave 3 remains field-closed. Wave 4 proved that GZFX can generate its own ambient-visibility signal and replace Skyrim's filtered SAO input before Skyrim's native Ambient Screen-Space Composite. Final artistic tuning remains intentionally deferred until the renderer architecture is sufficiently owned.

The 0.0.26 real-Skyrim field campaign proved the renderer-identity architecture and exposed two narrow remaining issues: the live game is Skyrim 1.7.104 rather than 1.6.1170, and the lazy dinput8 bootstrap can allow long-lived renderer resources to exist before GZFX's creation observers arm. That same field session locked the global TAA family at 9,103 / 9,103 active scene frames and proved the remaining HDR misses are detector-ancestry/terminal logic, not missing renderer structure.

0.0.27 moves renderer ownership to the graphics front door with a tiny standalone `d3d11.dll` bootstrap while retaining `dinput8.dll` for input. The renderer core remains runtime-independent and engine executable hooks are optional profiles: 1.5.97, 1.6.1170 and 1.7.104 are recognized Skyrim runtime families, but only the already-validated 1.6.1170 engine-epoch profile is allowed to patch the executable. Missing engine profiles do not disable GZFX's D3D renderer core. The HDR detector now accepts bounded full-resolution HDR ancestry and commits the observed 2x2 terminal instead of stopping at 3x2.

**Important:** GZFX 0.0.28 is standalone. ENB and ReShade were architecture research references, not runtime dependencies or coexistence targets. The field package contains only GZFX's own `dinput8.dll`, `d3d11.dll`, and `GZFX.dll`. `WaterDomain`, `PostTonemapPreUi`, and `AfterShadowProduction` remain intentionally unclaimed pending real-Skyrim evidence.

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
