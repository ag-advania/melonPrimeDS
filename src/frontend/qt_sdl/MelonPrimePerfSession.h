#ifndef MELONPRIME_PERF_SESSION_H
#define MELONPRIME_PERF_SESSION_H

// One measurement identity shared by the Generic and Windows Raw Input
// performance probes. The runner supplies this value so a concatenated log
// cannot silently combine evidence from different processes.

#if defined(MELONPRIME_DS) && \
    (defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) || \
     (defined(_WIN32) && \
      defined(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY)))

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace MelonPrimePerfSession {

struct SessionId {
    static constexpr std::size_t kMaxLength = 64;

    char value[kMaxLength + 1]{};
    bool valid = false;
};

inline SessionId ReadFromEnvironment() noexcept
{
    SessionId result;
    const char* environment = std::getenv("MELONPRIME_PERF_SESSION_ID");
    if (!environment || !environment[0])
        return result;

    const std::size_t length = std::strlen(environment);
    if (length > SessionId::kMaxLength)
        return result;
    for (std::size_t i = 0; i < length; ++i) {
        // The value is emitted as one log token.  Reject whitespace rather
        // than truncating or escaping it, so strict parsing fails closed.
        if (static_cast<unsigned char>(environment[i]) <= 0x20)
            return result;
        result.value[i] = environment[i];
    }
    result.value[length] = '\0';
    result.valid = true;
    return result;
}

inline const SessionId& Get() noexcept
{
    static const SessionId session = ReadFromEnvironment();
    return session;
}

inline const char* Text() noexcept
{
    return Get().valid ? Get().value : "missing";
}

} // namespace MelonPrimePerfSession

#endif // MELONPRIME_DS && (developer features || Windows Raw telemetry)

#endif // MELONPRIME_PERF_SESSION_H
