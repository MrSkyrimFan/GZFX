# Current cycle workspace

Current runtime:

```text
0.0.29-hdr-branch-recovery
```

Current phase:

```text
HDR / TONEMAP / GLOBAL-TAA IMAGE-SPACE SPINE FIELD-CLOSED
NEXT GRAPHICS WAVE NOT YET SELECTED
```

The complete 0.0.29 real-Skyrim field-closure cycle is archived in `../old/`.

Controlling continuity:

```text
GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.29_FIELD_CLOSED.txt
GZFX_NEW_CHAT_TAKEOVER_0.0.29_FIELD_CLOSED.txt
GZFX_NEW_CHAT_TECHNICAL_APPENDIX_0.0.29_FIELD_CLOSED.txt
GZFX_0.0.29_HDR_TONEMAP_FIELD_CLOSURE_ENGINEERING_RESULT.txt
```

Field-locked on Skyrim 1.7.104:

- HDRReduction;
- BeforeTonemap;
- BeforeGlobalTAA;
- AfterGlobalTAA.

Real-Skyrim closure proof:

- active frames: 12,123;
- HDRReduction: 12,123 / 12,123;
- BeforeTonemap: 12,123 / 12,123;
- BeforeGlobalTAA: 12,123 / 12,123;
- AfterGlobalTAA: 12,123 / 12,123;
- true <=2x2 terminal: 12,123 / 12,123;
- former derived-HDR sibling failure path: 2,884 / 2,884 successful;
- independent raw replay mismatch: 0;
- telemetry drops: 0;
- shutdown: complete.

Runtime scope:

- D3D11/DXGI architecture is runtime-independent;
- Skyrim 1.7.104 is real-field certified by this campaign;
- other runtime families remain architecture-supported but not field-certified by this campaign.

Still intentionally unpromoted:

- PostTonemapPreUi;
- WaterDomain;
- AfterShadowProduction.

PR #34 closed the authority state only. Product source, runtime, tests, and the
successful field-test package did not change.

This directory is intentionally clear of active-cycle TXT authority.

Do not begin another graphics wave until the owner selects the next target.

Research consensus identifies PostTonemapPreUi as the nearest unresolved
late-stack target, but it is NOT authorized merely by this README.
