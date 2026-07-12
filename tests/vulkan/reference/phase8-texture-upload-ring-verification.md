# Vulkan Phase 8.3 Texture Upload Ring verification

Phase 8.3 adds a persistently mapped host-visible staging ring for decoded
RGB6A5 texture uploads. Reservations obey `optimalBufferCopyOffsetAlignment`,
flush ranges obey `nonCoherentAtomSize`, and ring reuse is blocked until the
fence serial owning the old interval has retired.

The developer harness submits three in-flight copies, proves that a wrapping
fourth reservation is rejected while serial 1 is active, waits the first fence,
retires serial 1, then reuses offset zero for the fourth copy. Every payload is
copied through a device-local buffer and compared byte-for-byte after readback.

The ROM renderer remains on the Software correctness baseline. Phase 8.3 does
not claim timeline semaphore, capture texture, savestate, or native DS polygon
raster integration.
