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
broad renderer foundation cartography: COMPLETE
active engineering wave: Wave 4
current runtime: 0.0.23-ambient-visibility-v2
current phase: TEST
Wave 4 field status: OPEN — code/CI/package PASS; real-Skyrim quality gate pending
```

Wave 3 is field-closed: the renderer execution service, Native Input Broker, post-opaque R32 hierarchy, native normals/motion, temporal geometry, disocclusion, confidence history, stage integration, and owned command-list isolation are all production foundation. Wave 4 is the first production graphics-quality feature: full-resolution GTAO-family Ambient Visibility v2 is computed from the Wave 3 hierarchy and injected into Skyrim's final filtered SAO surface before Skyrim's own Ambient Screen-Space Composite. Wave 4 now awaits real-Skyrim visual/performance acceptance.

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
