# Building GZFX

GZFX currently targets 64-bit Windows with MSVC and CMake.

## Local configure

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

## Release build

```powershell
cmake --build build --config Release --parallel
```

The DLL is emitted to:

```text
build\bin\GZFX.dll
```

## Package

```powershell
./scripts/package.ps1 -BuildDir build -OutputDir dist
```

The root-ready Skyrim archive is emitted to:

```text
dist\GZFX-SkyrimSE-x64.zip
```

The archive intentionally places `GZFX.dll` at its root so future development packages can be extracted directly into the Skyrim Special Edition game directory.
