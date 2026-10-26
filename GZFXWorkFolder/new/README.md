# Current cycle workspace

Current runtime:

```text
0.0.30-post-tonemap-preui-cartography
```

Current phase:

```text
DATA COMPLETE / TWO INDEPENDENT 0.0.30 REAL-SKYRIM PASSES
```

Active authority:

```text
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_1.txt
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_2.txt
```

Double-DATA consensus:

- telemetry session is healthy and complete;
- 15,137 active scene frames;
- HDRReduction 15,137 / 15,137;
- BeforeTonemap 15,137 / 15,137;
- BeforeGlobalTAA 15,137 / 15,137;
- AfterGlobalTAA 15,137 / 15,137;
- true physical 2x2 HDR terminal 15,137 / 15,137;
- independent raw HDR/tonemap replay mismatch = 0;
- 4,142 active interior frames exercise one derived full-resolution HDR source;
- late detail spans the full AfterGlobalTAA -> physical BeforeOriginalPresent interval on all active frames;
- 1.7.104 discovery-only mode is functioning fail-closed;
- PRE-UI discovery returns exactly one candidate:
  - match RVA 0x116ADF2;
  - target RVA 0x100A050;
- post-processing discovery returns two candidates and remains ambiguous;
- engineProfileAvailable remains false;
- no 1.7.104 engine hook is armed;
- UiPresent is confirmed to be a Present-time internal landmark, not proof of Skyrim engine UI start;
- PostTonemapPreUi remains unpromoted;
- dropped=0 and shutdown complete.

No product code has been modified during this DATA cycle.

The next phase, if authorized, should be targeted research of the unique
1.7.104 PRE-UI candidate and its ABI/control-flow identity.

Do not begin implementation from the candidate RVAs alone.
