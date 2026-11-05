# Current cycle workspace

Current runtime:

```text
0.0.30-post-tonemap-preui-cartography
```

Current phase:

```text
FULL RENDERER-DOMAIN RE-AUDIT COMPLETE / FULL-PRODUCT MAJOR-ENGINEERING GATE NOT YET GREEN
```

Active authority:

```text
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_1.txt
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_1.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_CLOSURE_MATRIX.csv
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


## Owner-authorized 0.0.31 integration migration

A bounded SKSE single-DLL front-door migration is now active on `feature/0.0.31-skse-single-dll-frontdoor`.
This supersedes the prior "no product code" hold only for loader/integration/packaging work. It does **not** authorize broad renderer-feature engineering or promotion of blocked renderer domains.

Migration authority:

```text
GZFX_0.0.31_SKSE_MIGRATION_FINDINGS.txt
GZFX_0.0.31_SKSE_MIGRATION_RESEARCH.txt
GZFX_0.0.31_SKSE_MIGRATION_BLUEPRINT.txt
```
