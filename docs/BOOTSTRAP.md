# Skyrim bootstrap: first in-game proof

## Goal

Load `GZFX.dll` into `SkyrimSE.exe` without SKSE and prove that the native runtime can initialize and write a deterministic log before any renderer hooks are introduced.

## First bootstrap lane

The first test package uses a thin `dinput8.dll` proxy because Skyrim Special Edition imports DirectInput8 during normal startup.

The proxy is intentionally not the GZFX runtime. Its responsibilities are limited to:

1. forward DirectInput8 exports to the real Windows implementation;
2. load `GZFX.dll` from the Skyrim game root;
3. call the exported `GZFX_Initialize` entry point once.

No D3D11 or DXGI interception is performed in this stage.

## Existing dinput8 compatibility

If no `dinput8.dll` exists beside `SkyrimSE.exe`, the root-ready package can be extracted directly.

If another root `dinput8.dll` already exists, **do not overwrite it**. Rename the existing file to:

```text
dinput8_gzfx_chain.dll
```

and then place GZFX's `dinput8.dll` in the root. The GZFX proxy checks for that chain file first and forwards to it; otherwise it forwards to the Windows system `dinput8.dll`.

This chaining path will be validated empirically before being considered public-release behavior.

## Runtime evidence

On successful initialization, GZFX creates:

```text
Documents\My Games\Skyrim Special Edition\GZFX\GZFX.log
```

Windows-known-folder resolution is used, so redirected Documents locations such as OneDrive are followed automatically.

The first log records:

- GZFX version;
- process and thread IDs;
- executable path;
- resolved runtime-data directory;
- whether `d3d11.dll` was already loaded at initialization;
- whether `dxgi.dll` was already loaded at initialization;
- initialization success.

The D3D11/DXGI observations tell us whether this bootstrap fires early enough for the next renderer-observation phase.

## Fail-soft rules

- The proxy continues forwarding DirectInput8 even if `GZFX.dll` cannot be loaded.
- GZFX refuses normal initialization outside `SkyrimSE.exe`.
- Test builds may override the output root through `GZFX_TEST_OUTPUT_ROOT` for CI smoke testing.
- Heavy initialization is never performed from `DllMain`.
