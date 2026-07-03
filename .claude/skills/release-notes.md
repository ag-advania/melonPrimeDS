# Release Notes Generation

## Scope

Use this skill when the user asks to generate release notes, create a release summary, or summarize changes for a GitHub release.

## Procedure

### 1. Identify the commit range

Ask the user for the base commit (the previous release tag or commit hash). If not given, check the most recent tag:

```bash
git tag --sort=-creatordate | head -5
git log --oneline <base>..<HEAD>
```

### 2. List all commits in range

```bash
git log --oneline <base>..HEAD
```

Separate **fork commits** (Zection6V author) from **upstream merges** (melonDS-emu commits pulled via merge).

### 3. Identify user-facing changes per commit

For each fork commit, check what changed:

```bash
git show --stat <hash>
```

Classify each commit:

| Category | Examples |
|---|---|
| New Feature | New settings option, new HUD element, new patch |
| Bug Fix | Patch fix, crash fix, input fix |
| Upstream merge | VRAMSTAT, UI fix, etc. pulled from melonDS-emu/melonDS |
| Internal only | `.claude/` docs, CI workflow, refactor with no UX change |

Skip internal-only commits from the release notes entirely.

### 4. Write the release notes

Use the template below. Fill only the `## Changes:` bullet list and keep the rest of the boilerplate unchanged.

**Language — English only for `## Changes:`**

- The `## Changes:` section must be written in **English**, even when the user chats in Japanese.
- The bilingual `## Information:` table (English / 日本語) is the only place for Japanese prose in the release body.

**Audience — simple, end-user focused**

- Write for players, not developers. Describe what changed in gameplay or settings, not how the code was reorganized.
- Use short bullets: bold the feature or fix name, then one plain sentence on what it does or what was wrong.
- Avoid internal terms (registry, latch, hook dispatcher, refactor, phase, `RunFrameHook`, etc.).
- Skip developer-only behavior (e.g. debug OSD messages).

**Writing style:**
- Bold the feature/fix name, then explain concisely in plain English.
- For upstream merges, group all upstream fixes into one bullet: `Merged upstream melonDS: <list>`.
- Do not mention internal refactors, `.claude/` doc updates, or CI-only changes.
- Do not invent details — base descriptions only on what the diff shows.

### 5. Output destination

Write the result to `C:\tmp\release-notes.md` on the Windows dev machine. On other environments, write to `/tmp/release-notes.md` (or ask the user for a destination). Do not commit the release notes file into the repository.

---

## Template

```markdown
## Changes:
- Added **Feature Name**: description.
- Fixed **Bug Name**: description.
- Merged upstream melonDS: fix A, fix B, fix C.

---
### Known Issues
- Please let me know if you have any problems.

---
## Information:
<table>
  <tr>
    <th>English</th>
    <th>日本語</th>
  </tr>
  <tr>
    <td>
      <strong>⚠️📂 Please avoid using special characters in paths and file names.</strong>
      <ul>
        <li>Use only standard alphanumeric characters, and place files as close to the root of the drive as possible.</li>
      </ul>
    </td>
    <td>
      <strong>⚠️📂 ドライブ直下にディレクトリを作成してください！</strong>
      <ul>
        <li>パスに日本語が含まれると正常に動作しない場合があります。</li>
        <li>日本語が含まれないよう、なるべくドライブ直下にディレクトリを作成し、配置してください。</li>
        <li>ファイル名も日本語を含めないで英数字のみにしてください。</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td>
      <strong>ℹ️ Notice about Source Code</strong>
      <ul>
        <li>Source code is not required for gameplay.</li>
      </ul>
    </td>
    <td>
      <strong>ℹ️ ソースコードに関する注意</strong>
      <ul>
        <li>ソースコードはプレイに必要ありません。</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td>
      <strong>📦 Download Files</strong>
      <p>ℹ️ Expand the Assets section and download:</p>
      <ul>
        <li><strong>Windows users</strong>: melonPrimeDS-windows-x86_64.zip</li>
        <li><strong>macOS users</strong>: melonPrimeDS-macOS-universal.zip (or the arch-specific zip if needed)</li>
        <li><strong>Linux users</strong>: melonPrimeDS-linux-appimage-x86_64.zip (or the matching aarch64 zip)</li>
      </ul>
      <p>macOS builds are ad-hoc signed and may still show a Gatekeeper warning.</p>
    </td>
    <td>
      <strong>📦 ダウンロードファイル</strong>
      <p>ℹ️ Assetsセクションを展開して、以下をダウンロードしてください:</p>
      <ul>
        <li><strong>Windows ユーザー</strong>: melonPrimeDS-windows-x86_64.zip</li>
        <li><strong>macOS ユーザー</strong>: melonPrimeDS-macOS-universal.zip（必要に応じてCPU別zip）</li>
        <li><strong>Linux ユーザー</strong>: melonPrimeDS-linux-appimage-x86_64.zip（aarch64環境では対応するzip）</li>
      </ul>
      <p>macOS版はad-hoc署名のため、Gatekeeperの警告が出る場合があります。</p>
    </td>
  </tr>
</table>

---
## Special Thanks:
- Loco / Jsun / Kikere

---
## 🗨️ Join our MPH Community Discord
https://discord.gg/x4jV4ddvv9


## You can buy a coffee for the current maintainer and updater, Zection, if you'd like:
https://ko-fi.com/zection

## If you'd like to see new HUD features added, you can also support Livetek .
https://ko-fi.com/livetek
```

---

## Classification Guide

### What to include

| Commit touches | Release note category |
|---|---|
| New `MelonPrimePatch*.cpp`, new UI checkbox | New Feature |
| Fix to existing patch (`*Patch*.cpp`, `*Hook*.inc`) | Bug Fix |
| Fix to input handling (`MelonPrimeGameInput`, `RawInput*`) | Bug Fix |
| Upstream merge commit | Upstream merge bullet |
| HUD new element or option | New Feature |
| HUD rendering fix | Bug Fix |

### What to exclude

| Commit touches | Reason |
|---|---|
| `.claude/` only | Internal docs, no UX impact |
| `.github/workflows/` only | CI only |
| `CLAUDE.md` / `README.md` only | Internal docs |
| Merge commit with only upstream CI changes | No user impact |

### Upstream merge bullet format

Group all upstream fixes into one line:

```
- Merged upstream melonDS: VRAMSTAT bug fix, ROMInfoDialog dark theme fix, UNIX signal handling cleanup.
```

If the upstream merge is large or contains something user-visible (e.g. a crash fix), give it its own bullet.

---

## Reference: prior release notes

| Release range | Notable changes |
|---|---|
| `9b9c4bd..HEAD` (2026-05-31) | Custom HUD font, Stage Select Expansion, Shadow Freeze fix, Noxus Blade fix, upstream VRAMSTAT/ROMInfoDialog |
