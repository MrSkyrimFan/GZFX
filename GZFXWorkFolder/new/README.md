# Current cycle workspace

Current runtime:

```text
0.0.36-unified-product-renderer
```

Current phase:

```text
0.0.36 ENGINEERING CLOSED / 0.0.37 COMBINED NATIVE-DOMAIN ANCHOR WAVE NEXT / FIELD TEST DEFERRED TO COMBINED BUILD
```

0.0.36 completed authority has moved to `GZFXWorkFolder/old/`.

The remaining long-lived renderer-domain re-audit records stay active in this
workspace because they directly govern 0.0.37:

```text
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_1.txt
GZFX_0.0.30_REAL_SKYRIM_FIELD_DATA_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_1.txt
GZFX_FULL_RENDERER_DOMAIN_REAUDIT_PASS_2.txt
GZFX_FULL_RENDERER_DOMAIN_CLOSURE_MATRIX.csv
```

0.0.37 objective: add one combined semantic-anchor/telemetry layer for native
shadow, water/reflection/underwater, volumetric, sky/particle and post/UI
domains using existing D3D semantic evidence plus typed CommonLib/Address
Library engine access. Do not request another generic field run and do not split
the work into one-feature DLLs.
