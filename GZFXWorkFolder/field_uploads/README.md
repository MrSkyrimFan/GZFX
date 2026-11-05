# GZFX field upload intake

Permanent raw field-evidence intake for owner-uploaded GZFX ZIPs.

Workflow:
1. Owner may upload a new GZFX ZIP to the repository.
2. Agent moves the raw ZIP here without altering its bytes.
3. Agent processes the ZIP directly from repository evidence.
4. Findings/research/blueprint remain under GZFXWorkFolder/new/ during the active cycle.
5. Completed authority records move to GZFXWorkFolder/old/ per AGENT.md.

Raw uploads are evidence. Do not overwrite or rewrite an existing ZIP in place.
Use a new sequential name for each new field return (for example GZFX1.zip, GZFX2.zip).
