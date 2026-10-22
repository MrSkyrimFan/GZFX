# GZFX

**Standalone native graphics framework for The Elder Scrolls V: Skyrim Special Edition / Anniversary Edition.**

GZFX is an independent Direct3D 11 graphics framework intended to provide a modern, extensible rendering layer for Skyrim without requiring SKSE, Address Library, ENB, ReShade, Community Shaders, or Open Shaders.

> **Status:** Early bootstrap / renderer research. No public release yet.

## Project goals

- Native C++ runtime with HLSL GPU effects.
- SKSE-free runtime architecture.
- Direct3D 11 / DXGI integration with careful GPU-state ownership and restoration.
- Runtime renderer diagnostics for discovering Skyrim render stages and resources from evidence instead of guesswork.
- Advanced lighting, ambient contact, indirect lighting, shadows, atmosphere, volumetrics, materials, water, post-processing, presets, and an in-game editor.
- Compatibility-minded architecture for other graphics middleware, overlays, and upscalers.
- Fail-soft behavior when a feature cannot safely validate its required runtime state.

## Architecture

```text
SkyrimSE.exe
     |
     v
GZFX native runtime (C++)
     |
     v
Direct3D 11 / DXGI
     |
     v
HLSL shaders
     |
     v
GPU
```

Python is reserved for offline build, validation, shader, packaging, and diagnostic tooling. Additional languages are only introduced when they provide a clear bounded benefit.

## Current engineering stage

The first milestone is deliberately boring:

1. Produce a minimal Windows x64 `GZFX.dll`.
2. Build it reproducibly with CMake and GitHub Actions.
3. Package a root-ready `GZFX-SkyrimSE-x64.zip`.
4. Establish a safe SKSE-free Skyrim loading path.
5. Add passive D3D11 renderer instrumentation.
6. Use captured evidence to map Skyrim's render pipeline before implementing advanced effects.

The development rule is simple: **instrument, capture, classify, verify, implement. Do not guess.**

## Runtime data

GZFX development builds will write configuration, logs, and diagnostic captures under the user's Skyrim documents directory:

```text
Documents\My Games\Skyrim Special Edition\GZFX\
```

On systems where Windows redirects Documents through OneDrive, the operating system-resolved redirected path is used automatically.

## Installation

There is no installable build yet.

When the bootstrap milestone is complete, CI will produce one root-ready archive:

```text
GZFX-SkyrimSE-x64.zip
```

The intended user workflow is simply to extract that archive into the Skyrim Special Edition game root next to `SkyrimSE.exe`.

## Independence

GZFX is being developed independently using public DirectX documentation, public graphics research, publicly available Skyrim reverse-engineering knowledge where appropriate, and GZFX's own runtime instrumentation and validation.

The project does not depend on proprietary ENB internals or require another post-processing framework to function.

---

**Current branch of active work:** `bootstrap/initial-runtime`
