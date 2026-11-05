# SKSE bootstrap: single-DLL front door

## Goal

Load GZFX as a normal SKSE native plugin while preserving the existing D3D11/DXGI renderer and removing GZFX-owned `d3d11.dll` and `dinput8.dll` proxy files from the Skyrim root.

Target for the first migration field cycle:

```text
Skyrim Steam runtime: 1.7.104
SKSE:                 2.3.1
GZFX:                 0.0.31-skse-single-dll-frontdoor
```

## Runtime shape

```text
SkyrimSE.exe
  -> official SKSE
     -> Data/SKSE/Plugins/GZFX.dll
        -> SKSEPlugin_Preload
        -> SKSEPlugin_Load
        -> existing GZFX D3D11/DXGI renderer
```

GZFX does not redistribute SKSE binaries.

## Preload

`SKSEPlugin_Preload` performs exact runtime gating and arms GZFX's existing executable D3D11/DXGI import lifecycle probe.

The plugin remains resident on the supported runtime after the early front-door attempt. Native import hooks must never point into an unloaded plugin.

## Load

`SKSEPlugin_Load`:

1. acquires the SKSE plugin handle;
2. acquires Messaging;
3. acquires Trampoline;
4. registers for SKSE lifecycle messages;
5. initializes the existing GZFX renderer/framework;
6. records deterministic bridge status.

SKSE lifecycle events supplement GZFX state. They do not replace physical D3D11/DXGI frame-stage evidence.

## Renderer attachment

The old local `d3d11.dll` proxy is retired.

Primary renderer capture now uses the D3D11 lifecycle logic already present inside GZFX.dll. Renderer-forensics hooks remain GZFX-owned and are armed before the real Skyrim device is accepted.

No locked renderer semantic is changed by this bootstrap migration.

## Input

The old `dinput8.dll` proxy is retired.

The GZFX settings UI retains its existing Win32/raw-input path:
- window procedure subclass;
- `WM_INPUT`;
- mouse window messages;
- keyboard polling.

SKSE `InputLoaded` is observed as lifecycle evidence, but the first migration does not rewrite the menu around SKSE input internals.

## Process exit

The proven ExitProcess-driven telemetry shutdown formerly owned by the D3D11 proxy now lives inside GZFX.dll.

Shutdown remains outside `DllMain` and is idempotent.

## Package

The public/test archive contains exactly:

```text
Data/SKSE/Plugins/GZFX.dll
```

Nothing else is required from GZFX.
