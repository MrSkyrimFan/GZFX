# GZFX Bootstrap Architecture

This document describes the engineering boundary for the first GZFX runtime stages.

## Runtime ownership

```text
SkyrimSE.exe
     |
     v
GZFX bootstrap / native runtime
     |
     v
Direct3D 11 + DXGI
     |
     v
HLSL
     |
     v
GPU
```

GZFX is designed as an independent graphics runtime. SKSE, Address Library, ENB, ReShade, Community Shaders, and Open Shaders are not runtime dependencies.

## Bootstrap sequence

1. Build a reproducible Windows x64 `GZFX.dll`.
2. Package the DLL in a Skyrim-root-ready archive.
3. Establish and validate an SKSE-free loading mechanism.
4. Discover the active D3D11 device, immediate context, and swap chain without modifying rendering.
5. Add bounded renderer instrumentation.
6. Capture evidence before introducing advanced effects.

## Renderer diagnostics

The diagnostics layer will grow into a renderer black-box recorder capable of tracking:

- swap chains;
- D3D11 resources and views;
- render-target and depth-target bindings;
- shader fingerprints and bindings;
- constant-buffer metadata;
- render-state transitions;
- draw, dispatch, copy, resolve, clear, resize, and present ordering;
- bounded frame captures for targeted investigations.

The development rule is:

```text
instrument -> capture -> classify -> verify -> implement
```

## Runtime data location

GZFX will resolve the current Windows Documents known folder rather than hard-coding a username.

Runtime data will live under:

```text
Documents\My Games\Skyrim Special Edition\GZFX\
```

This naturally follows Windows/OneDrive Documents redirection.

## Compatibility principles

- Do not assume GZFX owns `d3d11.dll` or `dxgi.dll`.
- Do not assume only one meaningful swap chain exists.
- Preserve and restore D3D11 state around GZFX work.
- Prefer fail-soft feature disablement over process failure.
- Validate version-sensitive Skyrim knowledge before use.
- Keep generic D3D11 knowledge separate from Skyrim-specific reverse-engineered state.
