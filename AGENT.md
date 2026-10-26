# AGENT.md

> **NEW-CHAT TAKEOVER:** Before doing any GZFX work in a fresh Project Chat, read `GZFXWorkFolder/old/GZFX_FULL_NEW_CHAT_CONTINUITY_INDEX_0.0.29_FIELD_CLOSED.txt` first, then `GZFXWorkFolder/old/GZFX_NEW_CHAT_TAKEOVER_0.0.29_FIELD_CLOSED.txt`, `GZFXWorkFolder/old/GZFX_NEW_CHAT_TECHNICAL_APPENDIX_0.0.29_FIELD_CLOSED.txt`, and `GZFXWorkFolder/old/GZFX_0.0.29_HDR_TONEMAP_FIELD_CLOSURE_ENGINEERING_RESULT.txt` in full. Follow the older 0.0.29 no-drift and 0.0.28 records referenced by that index only for underlying implementation/history context.

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
0.0.29-hdr-branch-recovery

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
HDR / TONEMAP / GLOBAL-TAA IMAGE-SPACE SPINE FIELD-CLOSED
next graphics wave NOT YET SELECTED

graphics bootstrap:
standalone GZFX d3d11.dll + dinput8.dll

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
qualified primary renderer
pre-resource / pre-Present attach
ExitProcess-driven telemetry closure
process-scoped startup evidence

still intentionally unpromoted:
AfterShadowProduction
WaterDomain
PostTonemapPreUi

runtime field certification:
1.7.104 PROVEN
other catalog runtimes architecture-supported but not field-certified by this campaign

next graphics wave:
NOT YET SELECTED
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
names, hashes, draw ordinals, TAA completion, or proximity to Present. Those
remain separate future evidence problems.

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
9. verify the Lance ZIP auto-refresh;
10. prove the package-refresh commit changed only the ZIP;
11. record the result in the repo authority history;
12. hand the user concise in-game instructions.

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
