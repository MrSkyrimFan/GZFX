GZFX - BOOTSTRAP BUILD
======================

STATUS
------
This is an engineering bootstrap artifact. It does not hook or modify Skyrim yet.

INSTALLATION TARGET
-------------------
The final GZFX package is designed to be extracted directly into the Skyrim Special Edition game root, next to SkyrimSE.exe.

Example:
  ...\SteamLibrary\steamapps\common\Skyrim Special Edition\

CURRENT CONTENT
---------------
  GZFX.dll

A Skyrim-side bootstrap/loader will be added only after the build pipeline is proven stable.

RUNTIME DATA (future development builds)
----------------------------------------
GZFX will resolve the Windows Documents directory and write logs/settings/diagnostics under:

  Documents\My Games\Skyrim Special Edition\GZFX\

If Windows redirects Documents through OneDrive, GZFX will use that redirected location automatically.
