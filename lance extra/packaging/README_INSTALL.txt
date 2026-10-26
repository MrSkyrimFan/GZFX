GZFX - D3D11 LIFECYCLE PROBE
============================

STATUS
------
This build adds the first passive Direct3D 11 lifecycle diagnostic.

It does NOT modify Skyrim rendering.

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

3. Launch Skyrim normally and load into the game.

EXPECTED EVIDENCE
-----------------
GZFX writes:

  Documents\My Games\Skyrim Special Edition\GZFX\GZFX.log

and:

  Documents\My Games\Skyrim Special Edition\GZFX\Diagnostics\d3d11_lifecycle.txt

The lifecycle file records which D3D11/DXGI creation imports exist in
SkyrimSE.exe and captures any device, context, swap-chain, or factory creation
that happens after GZFX initializes.

UNINSTALL
---------
Remove:
  GZFX.dll
  dinput8.dll

If you renamed a previous loader to dinput8_gzfx_chain.dll, rename it back to:
  dinput8.dll
