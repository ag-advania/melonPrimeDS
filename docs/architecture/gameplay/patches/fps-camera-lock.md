# FPS Camera Lock - independent public patch

`FPS Camera Lock` is available in public and developer builds and is stored as
`Metroid.Aim.Enable.FpsCameraLock`. It is independent from
`Metroid.Aim.LowLatencyMode` and `Metroid.Aim.Disable.MphAimSmoothing`.

Low-latency aim mode values:

```text
0 = Off
1 = Immediate Sync
2 = MoonLike Aim
3 = InstantAimFollow (legacy alias)
```

`Metroid.Aim.Enable.InstantAimFollow` and mode `3` are retained only as
independent. They are not normalized to `Immediate Sync`: the old values can
still activate the independent camera-lock patch in either build profile.

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
`FPS Camera Lock` does not install that hook; both build profiles use
`MelonPrimePatchFpsCameraLock.cpp` static patch words instead.
