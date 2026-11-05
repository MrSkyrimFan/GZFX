# SKSE integration pin

GZFX does not vendor or redistribute SKSE runtime binaries.

The 0.0.31 migration implements the subset of the official native plugin ABI it
uses directly in `include/gzfx/skse_api.hpp`.

Pinned first-field target:

```text
SKSE release:          2.3.1
SKSE release index:    75
Skyrim Steam runtime:  1.7.104
Packed runtime value:  0x01070680
```

Verified against the official SKSE source/API on 2026-11-04.

The GZFX package remains one file:

```text
Data/SKSE/Plugins/GZFX.dll
```

Users obtain SKSE separately from its official distribution.

## CommonLibSSE-NG

Not linked in 0.0.31.

Current 15.x CommonLibSSE-NG licensing is GPL-3.0-or-later. GZFX does not
silently accept that licensing consequence. Engine integration remains on the
official SKSE ABI plus GZFX-owned renderer/signature machinery unless the owner
later authorizes a different licensing architecture.
