# MelonPrimeDS Documentation

This is the maintained entry point for the repository documentation. The
root README remains the quickest user-facing introduction; this tree is where
implementation behavior, configuration contracts, patch scope, and validation
boundaries are recorded.

## Start here

| Need | Read first | What it owns |
| --- | --- | --- |
| User-facing feature map | [Feature notes](features/README.md) | Links to focused behavior references |
| MelonPrime Settings tab | [Settings coverage map](features/melonprime-settings.md) | Every control in tabMetroid and its detailed references |
| Per-setting keys and patch data | [Settings detail directory](features/melonprime-settings/README.md) | Defaults, guards, lifecycle, ROM addresses, and research pointers |
| Custom HUD configuration | [Custom HUD settings guide](features/hud/custom-hud-settings.md) | Dialog workflow, previews, edit mode, and TOML import/export |
| Input behavior | [Input feature index](features/input/README.md) | Aim, stylus, zoom, weapon switching, and movement behavior |
| Gameplay behavior | [Gameplay feature index](features/gameplay/README.md) | Weapon-switch fallback, morph boost, Wi-Fi, and related options |
| Renderer selection and backend notes | [Rendering feature index](features/rendering/README.md) | Backend-specific documents and build/validation routes |
| Runtime and ownership | [Architecture index](architecture/README.md) | Subsystem boundaries and lifecycle contracts |
| Building and testing | [Development index](development/README.md) | Build, CI, testing, localization, and UI development |
| Reverse-engineering trail | [mphCodex workflow](reverse-engineering/mphcodex-workflow.md) | How to use the sibling research checkout without copying it |
| Open audits and plans | [Audit index](audit/README.md), [plan index](plans/README.md) | Work in progress and explicit evidence gaps |
| Generated references | [Generated-document index](generated/README.md) | Files produced from source schemas or generators |
| Historical material | [Archive](archive/README.md) | Retained evidence that is no longer the active contract |

## Documentation ownership

The current melonPrimeDS source is authoritative for behavior, configuration
keys, defaults, platform guards, and patch values. A document should link to
the owning source and explain the contract; it should not create a second
hand-maintained copy of a generated table or reverse-engineering report.

The sibling checkout at
C:\Users\Admin\Documents\git\mphCodex is read-mostly research context. Use
its current reports to explain why an address or behavior matters, then point
back to the melonPrimeDS implementation that actually ships. If the two
repositories disagree, record the discrepancy and follow the current source.

Validation claims use explicit boundaries:

- source/static checks prove source shape and link/config coverage;
- build checks prove compilation for the named configuration;
- runtime, physical hardware, long-duration performance, and remote CI require
  their own evidence.

## Updating the tree

When a feature changes:

1. update the owning detailed document or generated source;
2. update the nearest index if the scope or entry point changed;
3. preserve unresolved evidence as OPEN, NOT RUN, or UNVERIFIED instead of
   silently turning it into a completion claim; and
4. run the repository documentation link audit and git diff check.
