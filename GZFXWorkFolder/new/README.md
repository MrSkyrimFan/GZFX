# Current cycle workspace

Current runtime:

```text
0.0.32-commonlib-mit-engine-access
```

Current phase:

```text
COMMONLIB MIT ENGINE ACCESS INTEGRATION ACTIVE / FULL-PRODUCT MAJOR-ENGINEERING GATE STILL NOT YET GREEN
```

Active authority:

```text
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_1.txt
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_1.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_CLOSURE_MATRIX.csv
GZFX_0.0.32_COMMONLIB_FINDINGS.txt
GZFX_0.0.32_COMMONLIB_RESEARCH.txt
GZFX_0.0.32_COMMONLIB_BLUEPRINT.txt
```

Corrected project verdict:

- the earlier broad statement that GZFX was ready for the complete major-engineering campaign was premature under the owner's actual target of beating the full ENB + ReShade feature set;
- water/reflections are **not absent** from the map:
  - PRE_WATER_DEPTH is locked;
  - a dynamic 512x512 six-face float16 reflection cubemap is structurally verified;
  - a stable 1280x720 float16 reflection/lighting chain is structurally verified;
  - exterior full-resolution cubemap/reflection apply families recur on every exterior frame;
  - exact water TAA/blend/reflection/refraction/underwater subdomains remain to be correlated and locked;
- shadows are **not absent** from the map:
  - stable 4096/512 shadow depth-array families recur across independent sessions;
  - a stable full-resolution SHADOW_MASK reads MAIN_DEPTH and the large shadow arrays;
  - exact directional sun/moon, local-light, cloud-shadow and filtering/control subdomains remain to be decomposed;
- the exterior volumetric core is strongly mapped and day/night field-proven, but separate sun-ray, moon-ray, cloud and weather controls are not yet mapped;
- sky/cloud/celestial data exists in the historical campaigns, but a dedicated structural-family differential was never performed;
- repeated inn/fire evidence exists, but fire/emissive/smoke/local-light/local-shadow/bloom families were never isolated from the larger interior-only renderer population;
- PostTonemapPreUi remains partial with one unique 1.7.104 pre-UI discovery candidate;
- no new generic field run is justified yet;
- the next phase must exhaust existing evidence into a complete renderer-domain atlas, then request only minimal controlled contrasts for what remains ambiguous.

No product code has been modified during this re-audit.

Broad whole-product engineering is not authorized by the current evidence state.

Closed subsystems may continue under bounded authority, but the complete better-than-ENB + ReShade campaign must wait for the blocked domains in `GZFX_FULL_RENDERER_DOMAIN_CLOSURE_MATRIX.csv` to close.


## 0.0.31 integration status

The bounded SKSE single-DLL migration is COMPLETE and archived under
`GZFXWorkFolder/old/`.

Current controlling integration records:

```text
GZFX_0.0.31_SKSE_SINGLE_DLL_ENGINEERING_RESULT.txt
GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.31.txt
```

Next integration action is a real-Skyrim 1.7.104 + SKSE 2.3.1 field validation.
The renderer-domain re-audit files listed above remain active authority for later
feature engineering and are intentionally still in `new/`.


## 0.0.32 CommonLib integration

Owner-authorized engine-access migration is active on:

`feature/0.0.32-commonlib-mit-engine-access`

Selected dependency:

`eddoursul/CommonLibSSE-GG@2053e94fd1c147b36eae2b4338118552fba407e2`

License: MIT.

The wave adds typed read-only access to Skyrim renderer, Sky/weather and
TESDataHandler state while preserving GZFX's field-proven D3D/semantic
contracts. No blocked renderer domain is promoted by this integration.
