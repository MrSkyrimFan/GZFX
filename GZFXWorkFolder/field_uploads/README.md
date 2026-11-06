# GZFX field upload intake

Permanent raw field-evidence intake for owner-uploaded GZFX ZIPs.

Workflow:
1. Owner uploads the next sequential logical field return directly to the repository. Prefer one ZIP such as `GZFX3.zip`; if GitHub's per-file limit requires a small split, use matching names such as `GZFX3.zip` + `GZFX3Telemetry.zip` or `GZFX3-part01.zip` + `GZFX3-part02.zip`.
2. If it lands at repository root, the agent moves it here without altering its bytes.
3. The repo-native intake workflow unpacks the ZIP for analysis.
4. The agent reads the evidence directly and writes findings/research/blueprint under `GZFXWorkFolder/new/`.
5. Completed authority records move to `GZFXWorkFolder/old/` per `AGENT.md`.
6. The owner is never asked to upload the same field ZIP into ChatGPT after it already exists in the repository.

Raw uploads are evidence:
- never overwrite an existing ZIP;
- never rewrite a raw ZIP in place;
- preserve the original blob/bytes when moving it;
- use a new sequential logical prefix for each field return;
- matching split files with the same logical prefix are one DATA return, not separate tests;
- routine field evidence should stay under ~24 MB per ZIP by construction;
- do not ask the owner to manually split multi-gigabyte forensic captures; broad high-volume tracing must be explicitly armed only when a targeted blocker requires it.

Intake workflow:
`.github/workflows/field-upload-intake.yml`

The product Windows build ignores root `GZFX*.zip` uploads so field evidence does not waste product CI.
