# Field Cartography 001 — Skyrim Renderer Atlas

Field run: October 23, 2026  
Runtime: GZFX 0.0.17-renderer-pipeline-cartography  
Benchmark: Guardian Standing Stones campaign plus interior  
Evidence source: uploaded GZFX runtime diagnostics bundle

## Renderer environment evidence

The startup module inventory for this field run shows:

- SkyrimSE.exe;
- GZFX's root DINPUT8.dll and GZFX.dll;
- Windows System32 `d3d11.dll`;
- Windows System32 `dxgi.dll`;
- NVIDIA driver/runtime modules;
- Steam overlay/runtime modules.

At GZFX initialization, no module path containing SKSE, Community Shaders, ReShade or ENB appears, and the active D3D11/DXGI modules are the Windows system binaries rather than root proxy DLLs.

This is **strong evidence** that ENB/ReShade did not own the captured renderer path.

It is not treated as absolute proof that no plugin could load later, because the original module inventory is taken early in process startup.

0.0.18 therefore records a fresh `loaded_modules[_manual_####].txt` snapshot at every semantic capture so later-loaded renderer plugins can be ruled in or out at the actual capture time.

---

## Acceptance vocabulary

- **HARD** — directly established by GZFX D3D11 execution/resource evidence.
- **STRONG** — D3D structure, chronology, formats and dependencies match a known Skyrim renderer family with little ambiguity, but one more direct surface/channel proof is useful.
- **HYPOTHESIS** — plausible but not promoted.

Do not silently promote lower-confidence findings.

---

# 1. Capture inventory

Seven manual captures were present.

| Capture | Field state | Notes |
|---|---|---|
| manual 0001 | exterior daylight / still | baseline exterior |
| manual 0002 | exterior daylight / moving | much higher draw activity; useful temporal comparison |
| manual 0003 | exterior night | controlled night state |
| manual 0004 | exterior night / transition-like extra | extra useful night-state evidence |
| manual 0005 | water-focused exterior | water/reflection-heavy view |
| manual 0006 | daylight / strong shadows | shadow-heavy view |
| manual 0007 | interior | cleanly loses exterior volumetric chain |

Representative three-frame pass counts:

- automatic: 15,086 passes, 294 compute dispatches;
- manual 0001: 14,072 passes, 294 compute dispatches;
- manual 0002 moving: 27,233 passes, 294 compute dispatches;
- manual 0007 interior: 5,856 passes, only 15 compute dispatches.

The interior/exterior dispatch collapse is a high-value discriminator.

---

# 2. Main opaque MRT — HARD

The stock main opaque scene repeatedly writes this four-target MRT together with the main depth target:

- slot 0: 2560x1440 R16G16B16A16_FLOAT — main HDR scene color;
- slot 1: 2560x1440 R16G16_FLOAT — motion vectors;
- slot 2: 2560x1440 R8G8B8A8_UNORM — encoded normal target, ping-ponged frame to frame;
- slot 3: 2560x1440 R8G8_UNORM — auxiliary MRT, exact semantics unresolved;
- DSV: 2560x1440 R24G8_TYPELESS family — main depth.

The normal target alternates between two backing textures. This is real ping-pong behavior.

The old shorthand that B/A of the RGBA8 normal target are automatically “TAA/SSR masks” is revoked. RG normal evidence is strong; B/A semantics remain unproven.

---

# 3. Main-depth and HDR copies — HARD

Exterior frames show three important CopyResource events:

1. main depth -> an early depth copy before the stock AO/indirect block;
2. main HDR -> a second HDR surface immediately after the stock AO/indirect lighting composite;
3. main depth -> a later depth/history surface after the exterior volumetric temporal resolve.

The interior capture keeps the early depth/HDR behavior but loses the exterior late depth-copy pattern.

Exact Bethesda enum names are not yet promoted from the D3D evidence alone. Preserve chronology as the controlling fact.

---

# 4. Shadow mask — STRONG

A full-resolution RGBA8 surface is rendered after the early depth copy from depth plus large shadow resources and later becomes an input to the stock AO/indirect/HDR lighting composite.

This is structurally the stock shadow-mask family.

Next proof target: raw surface/channel capture across daylight, night and strong-shadow states.

---

# 5. Exterior volumetric-light generation — HARD

One exterior compute pass has a highly distinctive dependency set:

Inputs include:

- 4096x4096 array shadow depth;
- 512x512 array shadow depth;
- 4096x1 R32_FLOAT lookup;
- 32x32x32 R8_UNORM 3D noise.

It dispatches 10 x 6 x 90 and writes two:

- 320x192x90 R16_FLOAT Texture3D volumes.

A second compute shader then dispatches 10 x 6 x 1 ninety times and ping-pongs those two 3D volumes slice by slice.

This entire chain is present throughout exterior captures and absent from the interior capture.

Promotion: stock exterior volumetric-light 3D generation / slice-blur family.

---

# 6. Reflection chain — STRONG

Immediately after main opaque rendering, the exterior frame contains:

1. half-resolution RGBA8 mask-like target;
2. half-resolution RGBA16F color/reflection target;
3. horizontal compute blur into another half-resolution RGBA16F target;
4. vertical compute blur into the final half-resolution RGBA16F target;
5. the final blurred target is consumed by the later full-resolution lighting composite.

This matches the public Skyrim reflection-raytrace / reflection-blur family structurally.

Next proof target: capture the mask and each color/blur surface, including RGBA channels for the mask.

---

# 7. Stock SAO camera-Z pyramid — HARD

After opaque/reflection setup, stock Skyrim builds a full-resolution mipmapped R32_FLOAT surface from scene depth.

The same resource is then written/read through progressively smaller resolutions down to 1x1.

Observed viewport chain includes:

- 1280x720;
- 640x360;
- 320x180;
- 160x90;
- 80x45;
- 40x22;
- 20x11;
- 10x5;
- 5x2;
- 2x1;
- 1x1.

Promotion: stock camera-Z / depth-pyramid family used by SAO and later screen-space effects.

---

# 8. Temporal RawAO — STRONG, near-HARD

The next full-resolution RGBA8 pass reads:

- camera-Z;
- current encoded normal;
- previous AO ping-pong surface;
- motion vectors;

and writes a full-resolution RGBA8 output.

The output ping-pongs between two resources on subsequent frames.

This is structurally the stock temporal RawAO stage.

Next proof target: raw combined + R/G/B/A previews and statistics.

---

# 9. AO blur chain — STRONG

Two consecutive full-resolution RGBA8 passes consume the RawAO output:

- RawAO -> blur intermediate;
- blur intermediate -> final blurred AO.

The final blurred AO is consumed by the later HDR lighting composite.

Next proof target: surface/channel previews of RawAO, blur intermediate and final blurred AO.

---

# 10. Raw indirect light — STRONG, near-HARD

A full-resolution R11G11B10_FLOAT pass reads:

- camera-Z;
- HDR scene color;
- auxiliary MRT;

and writes a full-resolution indirect-light target.

The output is consumed directly by the following HDR lighting composite.

This matches stock SIL RawInd / raw-indirect behavior structurally.

---

# 11. AO + indirect + shadow + reflection HDR lighting composite — HARD boundary

A full-resolution draw writes directly back into the main HDR scene color while consuming:

- raw indirect light;
- blurred AO;
- early depth copy;
- encoded normal;
- auxiliary MRT;
- shadow-mask surface;
- blurred reflection surface.

Immediately after this draw, main HDR is copied to another HDR surface.

This is the most important architectural finding from Field Cartography 001.

It proves that stock Skyrim has an integrated screen-space lighting/composite boundary where AO, indirect light, depth, normals, shadow information and reflections meet before later image-space/HDR stages.

**Architectural consequence:** future GZFX AO/GI should be evaluated for integration at or around this boundary rather than assuming a late HDR multiplicative pass is the correct home.

This supports the user’s pipeline-first hypothesis. It does not yet prove every current GZFX effect equation is wrong; it proves the prior integration model was incomplete.

---

# 12. Exterior volumetric temporal resolve — STRONG, near-HARD

Later in the exterior frame, a full-resolution R16_FLOAT pass reads:

- main depth;
- prior screen-volumetric history;
- generated 3D volumetric density;
- 4096x1 lookup;
- motion vectors;
- late depth/history surface;

and writes a full-resolution R16_FLOAT result.

The main-depth late copy then occurs.

Two compute passes perform horizontal/vertical full-resolution R16 blur.

The entire chain disappears in the interior capture.

Promotion: exterior volumetric temporal screen resolve + blur family.

---

# 13. Later unresolved compute families

The run also exposes later screen/image-space compute chains that are not yet promoted:

- quarter-resolution RGBA16F temporal/history-like chain reading full-res HDR + motion;
- quarter-resolution RGBA8 horizontal/vertical chain with depth;
- additional post/image-space passes after the main lighting composite.

Keep these unresolved until surface previews and cross-state behavior establish semantics.

---

# 14. HDR / luminance / exposure reduction — STRONG family

A long RGBA16F reduction chain descends through approximately:

640x360 -> 160x90 -> 40x23 -> 10x6 -> 3x2 -> 2x2

with a persistent small adaptation/history target.

This strongly matches Skyrim’s HDR luminance / exposure / light-adaptation family.

Exact subpass names remain open.

---

# 15. Interior vs exterior — HARD difference

Interior capture manual 0007:

- removes the 3D exterior volumetric generation chain;
- removes the 90-slice volumetric blur compute sequence;
- removes the exterior volumetric temporal screen resolve and full-res R16 blur;
- drops three-frame compute activity from 294 to 15;
- preserves the stock SAO / RawAO / AO blur / raw-indirect / HDR lighting-composite family.

This gives GZFX a clean condition discriminator for later semantic mapping.

---

# 16. Constant-buffer behavior

The field run shows image-space passes repeatedly bind:

- a 720-byte per-frame camera buffer at slot 12;
- a small stable buffer;
- a larger pass-parameter buffer whose contents are CPU-updated immediately before different image-space passes.

Different SAO / blur / indirect / composite passes use distinct update hashes.

0.0.18 therefore correlates each constant-buffer use with the most recent Map/Unmap or UpdateSubresource fingerprint.

This allows cross-state clustering of pass parameters without dumping arbitrary CPU/GPU memory.

---

# 17. Diagnostic defects discovered in 0.0.17

These are probe defects, not renderer conclusions.

## 17.1 COM identity mismatch

The semantic probe keyed resources by one D3D interface pointer while the cartographer keyed them by canonical COM identity.

Result: known HDR/depth/motion/normal/aux resources existed in both systems but their pointer strings did not match.

0.0.18 canonicalizes semantic resources via IUnknown identity.

## 17.2 Session-cumulative semantic counters

Old semantic candidate write/read counts accumulated across the whole Skyrim process instead of resetting per capture.

The execution pass graph was already capture-local, so the frame-graph conclusions above remain valid.

0.0.18 resets semantic counters at every capture window.

## 17.3 Session-wide resource totals

0.0.17 resource/shader totals represented the whole session, not only the three-frame window.

0.0.18 reports both:

- capture-local touched/used resources;
- session-known totals.

## 17.4 Device-attachment wording

The game device existed before the late lifecycle creation hook could observe its creation, producing a misleading safe-mode device-attachment line.

The real device is acquired from the live swap chain.

0.0.18 explicitly attaches/marks that real device when the swap chain is acquired.

## 17.5 Bound is not instruction-used

D3D11 can retain non-conflicting SRVs in slots across draws. The cartographer records binding state, not decoded shader bytecode declarations in safe mode.

Therefore, semantic promotion must use:

- producer/consumer chronology;
- output ownership;
- strong dimensional/format structure;
- controlled state changes;
- surface evidence;

not the mere presence of one bound slot.

---

# 18. 0.0.18 semantic-atlas objectives

The follow-up field run will add:

- canonical known-resource crosswalk;
- per-capture semantic counters;
- structural landmark classification;
- surface snapshots for important 2D landmarks;
- R/G/B/A splits for mask-like RGBA8 targets;
- normal B/A evidence;
- auxiliary R/G evidence;
- objective channel means/nonzero fractions/min/max;
- direct constant-buffer use -> latest CPU-update correlation.

The classifier uses structure, chronology and dependencies rather than one-session shader pointer constants.

Effect tuning remains frozen.
