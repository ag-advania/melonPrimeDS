# Git Workflow

## Checking Diffs
Before summarizing code edits or verifying what changed, inspect the worktree:

```powershell
git status --short
```

Use targeted diffs whenever possible:

```powershell
git diff -- path/to/file
```

For staged changes, use:

```powershell
git diff --cached -- path/to/file
```

If the diff is large, start with:

```powershell
git diff --stat
```

Then inspect only the relevant files with `git diff -- <path>`.

Do not use destructive commands such as `git reset --hard` or `git checkout -- <path>` unless the user explicitly asks for them. Treat unrelated worktree changes as user changes and leave them intact.