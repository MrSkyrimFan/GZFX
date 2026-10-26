# Current cycle workspace

Current runtime:

```text
0.0.29-hdr-branch-recovery
```

Current phase:

```text
TEST / NO-DRIFT-CLOSED HDR BRANCH-RECOVERY REAL-SKYRIM FIELD VALIDATION
```

The owner-required extra pre-field no-drift cycle is complete and archived in
`../old/`.

That cycle performed:

```text
DATA
-> RESEARCH
-> NEW FINDINGS
-> BLUEPRINT
-> TEST-ONLY IMPLEMENTATION
-> PR / CI
-> MERGE / MERGED-MAIN CI
-> PACKAGE
-> BINARY NO-DRIFT VERIFICATION
```

Final proof:

- current 0.0.29 algorithm independently replayed 7,541/7,541 prior active field frames;
- PR #33 changed no product source;
- PR #33: 37/37 PASS;
- merged main: 37/37 PASS;
- tests now lock equal-size delayed HDR sibling recovery, exact per-active-frame semantic counts, capacity 4/5, depth 3/4, and frame isolation;
- product executable code/data is byte-identical to the original merged 0.0.29 build after normalizing only PE linker/debug timestamps;
- current package remains exactly GZFX.dll, d3d11.dll, and dinput8.dll.

This directory is intentionally clear of active-cycle TXT authority.

The next real-Skyrim telemetry upload starts the next DATA cycle here.

Primary field objective:

- HDRReduction on every active exterior frame;
- HDRReduction on every active interior frame;
- BeforeTonemap on every active scene frame;
- true <=2x2 terminal on every active scene frame;
- BeforeGlobalTAA / AfterGlobalTAA remain 1:1;
- lifecycle, ownership, zero drops, and complete shutdown remain clean.

Do not start 0.0.30 or another graphics feature before that field DATA is
reviewed.

Read root `AGENT.md` and
`GZFXWorkFolder/old/GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.29_NODRIFT.txt`
first.
