# AGENT.md — GZFX MANDATORY AGENT OPERATING CONTRACT

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

As of the Wave 3 implementation start:

```text
runtime baseline:
0.0.20-foundation-epochs-stage-correctness

Wave 1:
CLOSED

Wave 2:
CLOSED

broad foundation renderer cartography:
COMPLETE

current target:
0.0.21-native-input-temporal-core

next production graphics wave after Wave 3:
Ambient Visibility v2
```

Do not reopen the closed foundation unless new real evidence directly invalidates it.

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
