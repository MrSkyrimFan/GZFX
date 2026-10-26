# Current cycle workspace

Current runtime:

```text
0.0.29-hdr-branch-recovery
```

Current phase:

```text
DATA COMPLETE / TWO INDEPENDENT 0.0.29 REAL-SKYRIM PASSES
```

Active authority:

```text
GZFX_0.0.29_REAL_SKYRIM_FIELD_DATA_PASS_1.txt
GZFX_0.0.29_REAL_SKYRIM_FIELD_DATA_PASS_2.txt
```

Both complete DATA passes agree.

Primary field result:

- active scene frames: 12,123;
- HDRReduction: 12,123 / 12,123;
- BeforeTonemap: 12,123 / 12,123;
- BeforeGlobalTAA: 12,123 / 12,123;
- AfterGlobalTAA: 12,123 / 12,123;
- true physical 2x2 terminal: 12,123 / 12,123;
- exterior active frames: 8,882, all target stages present;
- interior active frames: 3,241, all target stages present;
- the 0.0.29 derived-HDR sibling path was physically exercised on 2,884 interior frames and remained complete;
- independent raw replay of the current 0.0.29 algorithm reproduced HDRReduction and BeforeTonemap on all 12,123 active frames with zero frame-set mismatch;
- telemetry dropped=0 and shutdown is complete;
- primary startup, resource provenance, command-list ownership, and physical Present remain clean.

Do NOT begin research automatically.

The owner explicitly requested a discussion after both complete DATA passes to
decide the next phase.

No product code has been modified in this DATA cycle.
