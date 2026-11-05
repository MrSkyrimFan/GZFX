# GZFX field upload intake

Permanent raw field-evidence intake for owner-uploaded GZFX ZIPs.

Workflow:
1. Owner uploads the next sequential field ZIP directly to the repository (for example `GZFX2.zip`).
2. If it lands at repository root, the agent moves it here without altering its bytes.
3. The repo-native intake workflow unpacks the ZIP for analysis.
4. The agent reads the evidence directly and writes findings/research/blueprint under `GZFXWorkFolder/new/`.
5. Completed authority records move to `GZFXWorkFolder/old/` per `AGENT.md`.
6. The owner is never asked to upload the same field ZIP into ChatGPT after it already exists in the repository.

Raw uploads are evidence:
- never overwrite an existing ZIP;
- never rewrite a raw ZIP in place;
- preserve the original blob/bytes when moving it;
- use a new sequential name for each field return.

Intake workflow:
`.github/workflows/field-upload-intake.yml`

The product Windows build ignores root `GZFX*.zip` uploads so field evidence does not waste product CI.
