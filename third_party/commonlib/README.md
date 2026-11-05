# CommonLibSSE-GG pin

GZFX 0.0.32 uses the MIT-licensed CommonLibSSE-GG continuation as a static
engine-access dependency.

Repository:

    eddoursul/CommonLibSSE-GG

Pinned revision:

    2053e94fd1c147b36eae2b4338118552fba407e2

The selected line explicitly supports Skyrim 1.7.99 / 1.7.104 and Address
Library format 5 while remaining on the MIT-licensed CommonLib lineage.

GZFX does not use the later GPL-3.0-or-later CommonLibSSE-NG line.

Runtime dependency:

    Address Library for SKSE Plugins
    Data/SKSE/Plugins/versionlib-1-7-104-0.bin

Address Library is installed separately by the user and is not redistributed
inside the GZFX package.

The CommonLib MIT license text is retained in THIRD_PARTY_NOTICES.md and is
embedded in GZFX.dll. GZFX also writes the embedded notice to its runtime
folder for human-readable inspection.
