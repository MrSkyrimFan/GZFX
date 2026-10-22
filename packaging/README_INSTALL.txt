GZFX - SKYRIM LOADER TEST
=========================

STATUS
------
This is the first in-game bootstrap proof.

It does NOT yet hook Direct3D 11 or modify Skyrim rendering.

PACKAGE CONTENTS
----------------
  GZFX.dll      - the actual GZFX native runtime
  dinput8.dll   - a thin SKSE-free bootstrap/proxy

INSTALLATION
------------
1. Open the Skyrim Special Edition game root, the folder containing SkyrimSE.exe.

2. Check whether a file named dinput8.dll ALREADY exists there.

   - If NO existing dinput8.dll exists:
       Extract this ZIP directly into the Skyrim root.

   - If an existing dinput8.dll DOES exist:
       DO NOT overwrite it.
       Rename the existing file to:
         dinput8_gzfx_chain.dll
       Then extract this ZIP into the Skyrim root.

3. Launch Skyrim normally.

EXPECTED EVIDENCE
-----------------
On successful initialization GZFX creates:

  Documents\My Games\Skyrim Special Edition\GZFX\GZFX.log

Windows resolves the Documents location, including OneDrive redirection.

The first log records whether d3d11.dll and dxgi.dll were already loaded when
GZFX initialized. That tells us whether this bootstrap fires early enough for
the renderer-watcher stage.

UNINSTALL
---------
Remove:
  GZFX.dll
  dinput8.dll

If you renamed a previous loader to dinput8_gzfx_chain.dll, rename it back to:
  dinput8.dll
