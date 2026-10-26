# Current cycle workspace

Current runtime:

```text
0.0.30-post-tonemap-preui-cartography
```

Current phase:

```text
TEST / POST-TONEMAP PRE-UI CARTOGRAPHY FIELD VALIDATION
```

The complete 0.0.30 implementation cycle is archived in `../old/`.

Controlling continuity:

```text
GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.30.txt
GZFX_NEW_CHAT_TAKEOVER_0.0.30.txt
GZFX_NEW_CHAT_TECHNICAL_APPENDIX_0.0.30.txt
GZFX_0.0.30_POST_TONEMAP_PREUI_CARTOGRAPHY_ENGINEERING_RESULT.txt
```

0.0.30 is an evidence build.

It does **not** promote `PostTonemapPreUi`.

New field evidence expected:

- detailed late pipeline remains active after internal `UiPresent`;
- detailed late pipeline reaches physical `BeforeOriginalPresent`;
- Skyrim 1.7.104 reports discovery-only post-processing candidate counts/RVAs;
- Skyrim 1.7.104 reports discovery-only pre-UI candidate counts/RVAs;
- `engine_profile_available` remains false on 1.7.104;
- no unverified 1.7.104 engine hook is installed.

Locked semantics that must remain 1:1:

- HDRReduction;
- BeforeTonemap;
- BeforeGlobalTAA;
- AfterGlobalTAA.

Still unpromoted:

- PostTonemapPreUi;
- WaterDomain;
- AfterShadowProduction.

Engineering proof:

- PR #35: 39/39 PASS;
- merged main: 39/39 PASS;
- package refresh changed only the Lance ZIP;
- current package exactly matches the merged-main workflow package.

This directory is intentionally clear of active-cycle TXT authority.

The next real-Skyrim telemetry upload starts the next DATA cycle here.

Do not modify product code again before that DATA is reviewed.
