GZFX 0.0.32 - SKSE + COMMONLIB ENGINE ACCESS
==============================================

TARGET
------
Skyrim Special Edition / Anniversary Edition (Steam) 1.7.104
SKSE 2.3.1
Address Library for SKSE Plugins (1.7.104 / format 5)

RUNTIME DEPENDENCIES
--------------------
SKSE and Address Library are required and are NOT bundled with GZFX.

Install the official SKSE build for Skyrim 1.7.104.

Install Address Library for SKSE Plugins so the 1.7.104 database is available
under Data\SKSE\Plugins. GZFX's CommonLib engine-access layer expects the
1.7.104 version database (versionlib-1-7-104-0.bin).

PACKAGE CONTENTS
----------------
Exactly one GZFX binary:

  Data\SKSE\Plugins\GZFX.dll

CommonLibSSE-GG and its build dependencies are statically linked into GZFX.dll.

There is no GZFX d3d11.dll.
There is no GZFX dinput8.dll.
There is no separate CommonLib DLL.
There is no SKSE binary inside the GZFX archive.
There is no Address Library binary/database inside the GZFX archive.

INSTALLATION
------------
1. Install Skyrim Steam 1.7.104.

2. Install official SKSE 2.3.1.

3. Install Address Library for SKSE Plugins for Skyrim 1.7.104.

4. Extract the GZFX archive into the Skyrim game folder so the plugin lands at:

     Data\SKSE\Plugins\GZFX.dll

5. Launch Skyrim through SKSE.

EXPECTED EVIDENCE
-----------------
GZFX writes:

  Documents\My Games\Skyrim Special Edition\GZFX\GZFX.log

and diagnostics including:

  Diagnostics\skse_bridge.txt
  Diagnostics\commonlib_engine.txt
  Diagnostics\d3d11_lifecycle.txt
  Diagnostics\renderer_startup.csv

GZFX also writes the embedded CommonLib MIT notice to:

  Documents\My Games\Skyrim Special Edition\GZFX\CommonLibSSE-GG-MIT.txt

The first field test must prove:
- SKSE preload/load reaches GZFX;
- CommonLib identifies Skyrim 1.7.104 as AE;
- Address Library format-5 data is available;
- Renderer / Sky / TESDataHandler bind successfully after DataLoaded;
- CommonLib renderer pointers correlate with GZFX's D3D renderer ownership;
- the internal D3D11 lifecycle front door still attaches correctly;
- locked renderer semantics remain intact;
- clean process-exit telemetry closure still completes.

UNINSTALL
---------
Remove:

  Data\SKSE\Plugins\GZFX.dll

SKSE and Address Library are separate dependencies and are not owned by GZFX.
