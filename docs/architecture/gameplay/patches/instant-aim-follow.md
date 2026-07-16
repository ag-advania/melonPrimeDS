# Instant Aim Follow - LowLatencyAimMode note

`Instant Aim Follow` is developer-only and is stored as
`Metroid.Aim.LowLatencyMode = 3` only in developer builds.

Low-latency aim mode values:

```text
0 = Off
1 = Immediate Sync
2 = MoonLike Aim
3 = Instant Aim Follow (developer-only)
```

`Metroid.Aim.Enable.InstantAimFollow` is kept only as a legacy compatibility key. Public builds migrate old `InstantAimFollow` users to `Immediate Sync`: `LowLatencyMode = 3` is normalized to `1`, and the legacy bool maps to `1` when the enum is still `0`. Saving from a public build writes the legacy bool back to `false`. Developer builds may still expose mode `3` for local testing.

All low-latency aim modes require `Metroid.Aim.Disable.MphAimSmoothing = true`. The UI disables the mode selector when that prerequisite is off, and runtime gates prevent stale config values from applying.

Behavior differences:

```text
Instant Aim Follow:
  Applies an ARM9 code patch to the game's original aim-follow routine so
  currentAim copies targetAim immediately inside the native game path.

Immediate Sync:
  Uses the LowLatencyAim ARM9 hook at runtime to copy targetAim into currentAim
  at the hook point, then rebuilds the aim side/up basis.
```

`Immediate Sync` and `MoonLike Aim` install the LowLatencyAim hook addresses. `Instant Aim Follow` does not install that hook; developer builds use `MelonPrimePatchInstantAimFollow.cpp` patch words instead.
