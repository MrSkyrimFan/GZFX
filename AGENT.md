# AGENT.md

> **NEW-CHAT TAKEOVER:** Before doing any GZFX work in a fresh Project Chat, read `GZFXWorkFolder/old/GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.35.txt` first, then `GZFXWorkFolder/old/GZFX_0.0.35_PRODUCT_CONTROL_RECOVERY_ENGINEERING_RESULT.txt`. Follow the 0.0.34 / 0.0.33 / 0.0.32 / 0.0.31 / 0.0.30 / 0.0.29 records referenced by that index for mouse ownership, loader, CommonLib, SKSE migration, renderer-domain, and locked HDR/tonemap/TAA evidence.

— GZFX MANDATORY AGENT OPERATING CONTRACT

## 0. READ THIS FIRST

Every new ChatGPT/Codex/engineering agent working on GZFX **MUST read this file before inspecting, planning, researching, or modifying the project**.

These rules are permanent project governance. Do not silently replace them with an older chat workflow.

Only the repository owner may explicitly change this workflow.

## 1. Repository is the authority workspace

The authority workspace is:

```text
GZFXWorkFolder/
  new/
  old/
```

Do **not** ask the user to download authority TXT files, manually place them into a local folder, or upload that folder back.

Do **not** create an off-repo handoff workflow.

All durable GZFX work product belongs in the repository.

## 2. Exact development cycle

Every field-test / engineering cycle follows this order:

```text
DATA
->
FULL FINDINGS TXT IN GZFXWorkFolder/new/
->
SHORT CHAT SUMMARY
->
RESEARCH
->
FULL RESEARCH TXT IN GZFXWorkFolder/new/
->
SHORT CHAT SUMMARY
->
BLUEPRINT
->
FULL BLUEPRINT TXT IN GZFXWorkFolder/new/
->
SHORT CHAT SUMMARY
->
IMPLEMENTATION DIRECTLY FROM REPO AUTHORITY
->
CI / PR / MERGE / PACKAGE PROOF
->
FULL ENGINEERING RESULT TXT IN GZFXWorkFolder/old/
->
MOVE COMPLETED FINDINGS + RESEARCH + BLUEPRINT FROM new/ TO old/
->
CLEAR new/ FOR THE NEXT CYCLE
->
EXACT IN-GAME TEST INSTRUCTIONS IN CHAT
->
NEXT FIELD DATA
```

Repeat until GZFX is complete and officially released on Nexus.

## 3. What belongs in the repository

Put these in repo TXT/MD records, not giant chat messages:

- field findings;
- forensic analysis;
- public/external research;
- renderer interpretation;
- architecture decisions;
- blueprints;
- engineering reports;
- source/branch/PR/merge proof;
- CI proof;
- package proof;
- long handoffs;
- implementation records;
- failure/retry history that matters to future engineering.

## 4. What belongs in chat

Chat stays short and fast.

Use it for:

- concise progress/status summaries;
- concise final engineering summary;
- exact user-facing in-game test instructions.

Do not dump the full report into chat when the repo record exists.

## 5. Current authority order

Repository facts:

1. current `main`;
2. newest verified real-Skyrim field evidence;
3. active-cycle `GZFXWorkFolder/new/`;
4. newest applicable `GZFXWorkFolder/old/`;
5. current source and merged PR evidence;
6. older chats.

Architecture decisions:

1. newest field evidence;
2. newest approved blueprint;
3. newest research;
4. current source where consistent;
5. older assumptions.

New verified evidence wins over stale documentation.

## 6. Phase discipline

### Data/findings phase
Inspect actual evidence first. Produce the complete findings TXT directly in `new/`. Do not modify product code unless the owner separately authorizes an emergency fix.

### Research phase
Research only questions needed by the next engineering wave. Produce the complete research TXT directly in `new/`. Do not restart broad cartography without a real blocker.

### Blueprint phase
Convert accepted findings/research into one coherent implementation authority. Produce the blueprint TXT directly in `new/`.

### Implementation phase
Read all active `new/` authority plus relevant `old/` history, inspect current main, then implement the whole authorized coherent wave. Use embedded targeted diagnostics rather than serial throwaway probe builds.

### Test handoff
Once implementation is merged and the user package is verified:
- write the engineering-result record to `old/`;
- move the completed cycle's three TXT authority files from `new/` to `old/`;
- leave `new/` ready for the next cycle;
- put the actual game-test buttons/locations/movement/capture count directly in chat.

## 7. Never regress to the retired workflow

NEVER require:

- user-managed authority folder round trips;
- manually downloaded ChatGPT TXT authority;
- a fresh-chat handoff bundle as the primary continuity mechanism;
- asking the user to repeat the project state already present in the repo.

The repo itself is the handoff.

## 8. Engineering style

- Preserve proven architecture.
- Prefer root-cause fixes.
- Fail closed.
- Do not claim passes without evidence.
- Do not guess semantics.
- Do not hardcode field ordinals as renderer contracts.
- Do not weaken architecture for premature optimization.
- Use large coherent passes once the architecture is understood.
- Add targeted probes inside the real subsystem being built.
- Keep diagnostics bounded.
- Keep user-visible testing compact.

## 9. Accessibility

The user is colorblind.

Required diagnostics must work using:

- grayscale;
- text;
- labels;
- geometry/patterns;
- binary masks.

Never make hue discrimination necessary for acceptance.

## 10. GZFX current locked foundation

Current state after the 0.0.28 real-Skyrim DATA cycle, the 0.0.29 HDR
branch-recovery engineering wave, and the owner-required 0.0.29 pre-field
no-drift acceptance-hardening cycle:

```text
foundation baseline:
0.0.20-foundation-epochs-stage-correctness

current runtime:
0.0.35-product-control-recovery

Wave 1:
CLOSED

Wave 2:
CLOSED

Wave 3:
CLOSED

broad foundation renderer cartography:
COMPLETE

Wave 4:
renderer-stack ownership architecture PROVEN
final artistic quality intentionally deferred

current phase:
0.0.35 PRODUCT CONTROL RECOVERY / REAL-SKYRIM LIVE EFFECT RESPONSE VALIDATION NEXT

graphics bootstrap:
official SKSE Preload/Load + one Data/SKSE/Plugins/GZFX.dll
0.0.33 rule: Preload is non-invasive; D3D observation arms in normal SKSE Load

engine access:
pinned MIT CommonLibSSE-GG + external Address Library v5 on Skyrim 1.7.104

universal renderer core:
D3D11/DXGI-first
independent of Skyrim executable offsets

known runtime catalog:
1.5.97
common 1.6.x families
1.7.99 / 1.7.104

optional executable engine profile:
verified 1.6.1170 only
missing profile never disables renderer core

stable renderer identity:
creation-time shader hashes + resource birth provenance

physical DXGI timeline:
explicit primary-swapchain present epochs

global TAA:
field-locked structural contract
BeforeGlobalTAA / AfterGlobalTAA
1:1 on all 12,123 active 0.0.29 field frames

HDRReduction:
field-locked structural contract
Skyrim 1.7.104 real-field proven
bounded MAIN_HDR ancestry
bounded full-resolution sibling sources
source capacity 4
ancestry depth <=3
one coherent reduction chain
true <=2x2 terminal

BeforeTonemap:
field-locked structural contract
Skyrim 1.7.104 real-field proven
immediately before Skyrim's HDR tonemap/blend/cinematic image-space pass

process lifecycle:
SKSE-hosted plugin lifetime
qualified primary renderer via internal D3D11/DXGI capture
pre-resource / pre-Present attach target
core ExitProcess-driven telemetry closure
process-scoped startup evidence

still intentionally unpromoted:
AfterShadowProduction
WaterDomain
PostTonemapPreUi

runtime field certification:
1.7.104 PROVEN
other catalog runtimes architecture-supported but not field-certified by this campaign

active graphics wave:
PostTonemapPreUi cartography / evidence only
semantic remains UNPROMOTED
```

0.0.28 real-Skyrim DATA proved the lifecycle/front-door hardening in live Skyrim
1.7.104: the qualified primary attached before tracked renderer-resource birth,
core resources gained SkyrimSE.exe creation provenance, Present/ownership/TAA
remained coherent, shutdown completed naturally with zero drops, and the true
2x2 HDR terminal physically existed on every active scene frame.

That same field session exposed one deterministic semantic-state defect:
2,640 / 2,881 interior frames inserted a later full-resolution HDR sibling after
an earlier plausible reduction root had already started. The physical HDR spine
remained present, but the old single-source detector could not restart from that
later verified sibling.

0.0.29 replaces that greedy single-source state with a fixed, bounded set of
verified full-resolution HDR sources descended from canonical MAIN_HDR. One
conservative reduction chain remains; it may restart only from canonical
MAIN_HDR or a verified bounded source. The field-locked coherent descent and true
<=2x2 terminal remain unchanged.

Before field testing, the owner required another complete XForge-style no-drift
pipeline. The product algorithm was replayed independently over all 1,022,351
detailed field rows and recovered 7,541/7,541 active frames. PR #33 then
strengthened acceptance only: equal-size delayed sibling recovery, exact
per-active-frame stage populations, capacity 4/5, ancestry depth 3/4, and
frame-local source isolation. Final PR and merged-main CI are 37/37. Product
source remained unchanged, and binary comparison proved zero executable
code/data drift after normalizing PE linker/debug timestamps.

The 0.0.29 real-Skyrim field session closed that validation requirement on
Skyrim 1.7.104. Across 12,123 active scene frames, HDRReduction, BeforeTonemap,
BeforeGlobalTAA, and AfterGlobalTAA each occurred exactly once per active frame;
the true <=2x2 terminal was present on every active frame; and the former
derived-HDR sibling failure topology was exercised successfully on 2,884
interior frames. A second independent raw replay of the current 0.0.29 detector
matched the emitted HDRReduction and BeforeTonemap frame sets with zero
mismatch.

HDRReduction and BeforeTonemap are therefore field-locked structural contracts
for the currently field-proven 1.7.104 runtime. The D3D architecture remains
runtime-independent, but this campaign does not claim real-field certification
for 1.5.97, other 1.6.x families, 1.6.1179, or 1.7.99.

Do not invent WaterDomain, PostTonemapPreUi, or AfterShadowProduction from
names, hashes, draw ordinals, TAA completion, or proximity to Present.

0.0.30 is an evidence-only PostTonemapPreUi cartography wave. It preserves all
field-locked HDR/TAA semantics, extends detailed late telemetry through physical
BeforeOriginalPresent, and performs discovery-only masked scans on Skyrim
1.7.104 for the known post-processing and pre-UI opcode skeletons. Discovery
does NOT set engineProfileAvailable and does NOT install hooks. The next field
DATA must decide whether those candidates are unique/credible and where the
post-TAA scene-to-UI interval actually lies before PostTonemapPreUi can ever be
promoted.

## 10A. Owner-authorized 0.0.31 integration migration

The owner explicitly reversed the historical SKSE-free requirement on
2026-11-04. The current authorized architecture uses released SKSE 2.3.1 for
Steam Skyrim 1.7.104 as the loader/lifecycle host and ships exactly one GZFX
binary at `Data/SKSE/Plugins/GZFX.dll`.

This authorization is bounded to integration, lifecycle, packaging, and the
minimum code movement required to preserve the existing renderer. It does not
promote blocked renderer semantics or authorize invention of missing domains.

CommonLibSSE-NG is not linked in 0.0.31 because current 15.x licensing is
GPL-3.0-or-later and no owner license conversion has been authorized.

## 10B. Owner-authorized 0.0.32 CommonLib integration

The owner authorized the current MIT CommonLib engine-access plane on
2026-11-05. PR #37 is merged and merged-main workflow 42102969205 passed 38/38.
The package remains exactly one product binary at
`Data/SKSE/Plugins/GZFX.dll`.

Selected dependency:

`eddoursul/CommonLibSSE-GG@2053e94fd1c147b36eae2b4338118552fba407e2`

License: MIT.

Address Library for SKSE Plugins is now an external runtime dependency. For the
current 1.7.104 target, the expected database is
`Data/SKSE/Plugins/versionlib-1-7-104-0.bin`.

CommonLib is a typed, read-only engine evidence/access plane in 0.0.32. It does
not replace GZFX's field-proven D3D11/DXGI renderer or locked HDR/TAA semantic
contracts, and this wave authorizes no weather or renderer-state writes.

Real-Skyrim field validation of the combined SKSE + CommonLib path is the next
required action before broad graphics-feature engineering.

## 10C. 0.0.33 real-field loader rescue

The first real-Skyrim test of the 0.0.32 SKSE + CommonLib integration failed
before healthy GZFX runtime output. The bounded 0.0.33 rescue corrected that
loader boundary.

GZFX1 field evidence on 2026-11-05 now closes 0.0.33 as REAL-SKYRIM PASS:

- SKSE 2.3.1 Load reached and messaging registered;
- CommonLib SKSE::Init succeeded;
- Address Library v5 was present;
- DataLoaded Renderer/Sky/TESDataHandler binding succeeded;
- GZFX/CommonLib renderer correlation succeeded without fallback attach;
- primary renderer attached before tracked resource/shader birth;
- telemetry completed with zero drops;
- natural process exit completed;
- HDRReduction / BeforeTonemap / BeforeGlobalTAA / AfterGlobalTAA remained 1:1
  on all 911 world-active frames in the long session.

The remaining field defect is not loader-related: the GZFX menu opens, but the
DirectInput producer/suppression layer retired with the old dinput8 proxy was
never migrated into the one-DLL architecture. 0.0.34 repairs only that input
ownership regression.

Do not use the former loader failure to reopen HDR/TAA contracts or broad
renderer semantics.

## 10D. Owner field-upload intake — permanent workflow

The owner now uploads GZFX field ZIPs directly to this GitHub repository.

Permanent intake folder:

```text
GZFXWorkFolder/field_uploads/
```

Permanent rules:

- the owner may upload a sequential raw field ZIP such as `GZFX1.zip`,
  `GZFX2.zip`, etc. directly to the repository;
- if the owner uploads it at repository root, the agent moves it into
  `GZFXWorkFolder/field_uploads/`;
- preserve the raw ZIP bytes/blob exactly when moving it;
- never overwrite a prior raw field ZIP;
- process it through `.github/workflows/field-upload-intake.yml` or another
  repo-native mechanism;
- write findings/research/blueprint from that evidence into
  `GZFXWorkFolder/new/` under the normal project workflow;
- never ask the owner to upload the same field ZIP into ChatGPT after it is
  already in the repository.

This repo-native intake replaces chat attachment round-trips for GZFX field data.

## 10E. 0.0.34 DirectInput menu ownership

GZFX1 field evidence proved the 0.0.33 loader rescue in real Skyrim and exposed
the remaining menu regression:

- GZFX menu opened;
- DirectInput menu packets remained zero;
- the physical mouse continued moving Skyrim camera/world.

Root cause: the previously field-working DirectInput producer/suppression layer
was retired with the old dinput8 proxy and was not migrated into the one-DLL
SKSE architecture.

0.0.34 is now automated-engineering CLOSED:

- DirectInput mouse capture/suppression lives inside GZFX.dll;
- it arms from normal SKSEPlugin_Load, never Preload;
- it hooks the real system DirectInput implementation beneath SKSE's wrapper;
- only the exact GUID_SysMouse device is eligible for suppression;
- menu closed = passthrough;
- menu open = GZFX receives the physical mouse and Skyrim receives suppressed
  physical mouse state/events;
- no dinput8 proxy was restored;
- PR #39 merged;
- merged-main product validation passed PE + 39/39 tests;
- validated package is published to main.

Controlling records:

```text
GZFXWorkFolder/old/GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.34.txt
GZFXWorkFolder/old/GZFX_0.0.34_DIRECTINPUT_MENU_ENGINEERING_RESULT.txt
GZFXWorkFolder/old/GZFX_0.0.34_REAL_SKYRIM_FIELD_FINDINGS.txt
GZFXWorkFolder/old/GZFX_0.0.34_DIRECTINPUT_MENU_RESEARCH.txt
GZFXWorkFolder/old/GZFX_0.0.34_DIRECTINPUT_MENU_BLUEPRINT.txt
```

Next required action is the compact real-Skyrim menu mouse validation. Do not
start broad graphics feature work before that field result.

## 10F. 0.0.35 product-control recovery and historical target rebase

GZFX2 field evidence closes 0.0.34 DirectInput mouse ownership as a real-Skyrim
PASS. The menu received physical mouse input and Skyrim mouse-look was suppressed
only while the GZFX menu was open.

GZFX2 then exposed a separate product defect: current artistic slider values
persisted to Settings.ini but did not materially drive the active renderer.
The active AV2 path used hard-coded artistic parameters, SceneEffects returned
early whenever AV2 owned modes 1/2, and the Present fallback was suppressed after
AV2 application.

0.0.35 reconnects the current control plane without abandoning the current
renderer foundation:

- AV2 owns Horizon/HBAO and receives live enable/strength/radius/quality values;
- AmbientContact and LocalGlow remain SceneEffects responsibilities;
- the legacy ambient-depth stack may supply spatial IndirectBounce/SSIL under
  AV2 ownership with its legacy AO contribution disabled;
- product_control_submission.txt records submitted control values and active
  ownership;
- routine broad late pipeline telemetry is disabled by default and is opt-in for
  targeted forensic work.

Historical GZFX 0.0.51 product breadth was recovered as roadmap evidence, not
current implementation proof. The repo now preserves:

```text
GZFXWorkFolder/old/GZFX_HISTORICAL_0.0.51_652_TARGET_INDEX.csv
GZFXWorkFolder/old/GZFX_0.0.35_PRODUCT_RECOVERY_MATRIX.csv
```

The historical 0.0.51 source report described a much broader player-facing post
stack, including DoF, bloom/glow, exposure/adaptation, color grading,
pre-tonemap shaping, denoise, lens dirt, depth fog, motion blur, vignette, film
grain, chromatic aberration, sharpening, masking, quality tiers and preset/profile
support. Those historical claims are NOT current source proof. The old
product_hdr/product_ldr executor/control-model files are absent from current
main.

Recovery order after 0.0.35 field-green:

1. HDR / exposure / bloom / color / sharpening core post;
2. DoF / motion blur / depth fog / cinematic optics;
3. product master switch / quality tiers / built-ins / custom profiles;
4. evidence-driven native shadows / sky / weather / water / reflections /
   materials / fire-emissive-light domains.

Do not call AO a native-shadow implementation and do not claim historical 0.0.51
features are restored until current source + tests + real Skyrim prove them.

## 11. Repository lifecycle proof

For implementation waves:

1. branch from current `main`;
2. implement coherent scope;
3. expand/retain CI;
4. open PR;
5. verify Windows x64 CI;
6. fix real failures;
7. merge;
8. verify merged-main CI;
9. verify the validated ZIP staged on the `lance-package` branch;
10. prove that staged ZIP matches the merged-main artifact;
11. sync the exact validated ZIP blob into main with the connected GitHub authority;
12. prove the main package-sync commit changed only the ZIP;
13. record the result in the repo authority history;
14. hand the user concise in-game instructions.

## 12. Absolute continuity rule

A future agent must be able to open the repository, read:

```text
AGENT.md
README.md
GZFXWorkFolder/new/
GZFXWorkFolder/old/
```

and continue without asking the user to reconstruct history.

That is the permanent GZFX workflow.


## 13. Human field-test ergonomics — permanent rule

The user is one human with two hands. Never design a required test around simultaneous "octopus controls" such as movement + strafe + camera turn + Shift + Numpad5.

Prefer:
- arm-then-act capture windows;
- short buffered before/after capture windows;
- automatic classification of stationary/slow/fast motion.

A field test must remain valid when the user performs movement naturally and presses the capture control immediately before or after the movement. Build diagnostics to accommodate the operator, not the reverse.
