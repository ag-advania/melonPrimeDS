#ifndef MELON_PRIME_ZOOM_STATE_H
#define MELON_PRIME_ZOOM_STATE_H

#include <cstdint>

namespace MelonPrime::ZoomStatus {

struct ZoomCapabilityCache {
    uint32_t player = 0;
    uint32_t weapon = 0;
    bool valid = false;
    bool canZoom = false;
};

} // namespace MelonPrime::ZoomStatus

#endif // MELON_PRIME_ZOOM_STATE_H
