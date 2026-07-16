# Core Working Rules

## Project and scope

MelonPrimeDS is a melonDS fork specialized for Metroid Prime Hunters with mouse/keyboard FPS controls, game-specific patches, and optional custom HUD rendering. Windows is the primary gameplay-tuning target; macOS and Linux are supported build/runtime targets, and BSD is build-only CI coverage.

Read detailed material only when the task needs it. The long-form project context starts at [`docs/architecture/project-context.md`](../../docs/architecture/project-context.md); plans and historical evidence are documentation, not standing instructions.

## Evidence and reporting

- Separate verified facts, reasonable inferences, and untested hypotheses.
- Do not report a build, test, runtime behavior, platform result, or performance outcome as successful unless it was actually run and observed.
- When a required check cannot run, state exactly what was checked, what remains unverified, and why.
- Prefer repository evidence and reproducible commands over memory. Keep plans and docs aligned with the implemented tree.

## Git and workspace safety

- Inspect `git status` before editing. Preserve user changes and unrelated work in a dirty tree.
- Do not use destructive recovery such as `git reset --hard` or discard files unless the user explicitly requests it.
- Do not amend, rebase, force-push, create commits, or change branches unless requested.
- Keep changes scoped. Do not bundle unrelated cleanup into a task.
- `.claude/settings.local.json` is personal, ignored local state. Never add it to Git or distribution archives.

## Documentation layout

- `.claude` contains only these short standing rules. Put detailed architecture, procedures, plans, investigations, and history under `docs/`.
- Put reusable scripts under `tools/`; completed or one-time evidence belongs under `docs/archive/` or `tools/archive/`.
- If a rule changes, update the relevant long-form document and validation gate in the same change when applicable.
