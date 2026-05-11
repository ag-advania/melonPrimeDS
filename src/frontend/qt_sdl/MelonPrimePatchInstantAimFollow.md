# Instant Aim Follow - LowLatencyAimMode note

`Instant Aim Follow` is exposed as `Metroid.Aim.LowLatencyMode = 3`.

Low-latency aim mode values:

```text
0 = Off
1 = Immediate Sync
2 = MoonLike Aim
3 = Instant Aim Follow
```

`Metroid.Aim.Enable.InstantAimFollow` is kept only as a legacy compatibility key. The settings UI writes it to `true` when mode `3` is selected and to `false` otherwise. Runtime apply also accepts the legacy `true` value when `LowLatencyMode` is still `0`, so old developer configs continue to work until the UI saves the migrated mode.

Behavior differences:

```text
Instant Aim Follow:
  Applies an ARM9 code patch to the game's original aim-follow routine so
  currentAim copies targetAim immediately inside the native game path.

Immediate Sync:
  Uses the LowLatencyAim ARM9 hook at runtime to copy targetAim into currentAim
  at the hook point, then rebuilds the aim side/up basis.
```

`Immediate Sync` and `MoonLike Aim` install the LowLatencyAim hook addresses. `Instant Aim Follow` does not install that hook; it uses `MelonPrimePatchInstantAimFollow.cpp` patch words instead.
