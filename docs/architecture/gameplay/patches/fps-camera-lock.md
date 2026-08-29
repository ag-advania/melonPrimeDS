# FPS Camera Lock - independent developer-only patch

`FPS Camera Lock` is developer-only and is stored as
`Metroid.Aim.Enable.FpsCameraLock`. It is independent from
`Metroid.Aim.LowLatencyMode` and `Metroid.Aim.Disable.MphAimSmoothing`.

Low-latency aim mode values:

```text
0 = Off
1 = Immediate Sync
2 = MoonLike Aim
3 = InstantAimFollow (legacy developer alias)
```

`Metroid.Aim.Enable.InstantAimFollow` and mode `3` are retained only as
compatibility paths for configurations written before `FpsCameraLock` became
independent. They are not normalized to `Immediate Sync`: the old values can
still activate the independent camera-lock patch in a developer build, while
public builds do not expose or apply that patch.

Behavior differences:

```text
FPS Camera Lock:
  Applies an ARM9 code patch to the game's original aim-follow routine so
  the gun-vector-to-facing-vector copy is unconditional inside the native
  game path. This removes the game's free-aim lead and changes camera behavior.

Immediate Sync:
  Uses the LowLatencyAim ARM9 hook at runtime to copy targetAim into currentAim
  at the hook point, then rebuilds the aim side/up basis.
```

`Immediate Sync` and `MoonLike Aim` install the LowLatencyAim hook addresses.
`FPS Camera Lock` does not install that hook; developer builds use
`MelonPrimePatchFpsCameraLock.cpp` static patch words instead.
