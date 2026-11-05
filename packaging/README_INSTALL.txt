GZFX 0.0.31 - SKSE SINGLE-DLL FRONT DOOR
==============================================

TARGET
------
Skyrim Special Edition / Anniversary Edition (Steam) 1.7.104
SKSE 2.3.1

SKSE is a required runtime dependency and is NOT bundled with GZFX.
Install the official SKSE build that matches Skyrim 1.7.104 separately.

PACKAGE CONTENTS
----------------
Exactly one GZFX binary:

  Data\SKSE\Plugins\GZFX.dll

There is no GZFX d3d11.dll.
There is no GZFX dinput8.dll.
There is no SKSE binary inside the GZFX archive.

INSTALLATION
------------
1. Install Skyrim Steam 1.7.104.

2. Install official SKSE 2.3.1 for Skyrim 1.7.104.

3. Extract the GZFX archive into the Skyrim game folder so the plugin lands at:

     Data\SKSE\Plugins\GZFX.dll

4. Launch Skyrim through SKSE.

EXPECTED EVIDENCE
-----------------
GZFX writes:

  Documents\My Games\Skyrim Special Edition\GZFX\GZFX.log

and diagnostics including:

  Diagnostics\skse_bridge.txt
  Diagnostics\d3d11_lifecycle.txt
  Diagnostics\renderer_startup.csv

The first field test must prove:
- SKSE preload/load reaches GZFX;
- the internal D3D11 lifecycle front door arms before Skyrim renderer creation;
- the qualified primary device/context/swapchain attaches;
- the locked renderer semantics remain intact;
- clean process-exit telemetry closure still completes.

UNINSTALL
---------
Remove:

  Data\SKSE\Plugins\GZFX.dll

No Windows-system-name proxy DLL restoration is required.
