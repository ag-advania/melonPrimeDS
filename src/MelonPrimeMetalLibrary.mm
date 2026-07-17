// MelonPrimeDS - shared production Metal shader library implementation.
// See MelonPrimeMetalLibrary.h for the release/debug contract.

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "MelonPrimeMetalLibrary.h"

#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

namespace MelonPrime::Metal {

namespace {

struct CachedLibraryEntry
{
    __unsafe_unretained id<MTLDevice> Device = nil;
    id<MTLLibrary> Library = nil;
};

std::mutex& CacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<CachedLibraryEntry>& CacheEntries()
{
    static std::vector<CachedLibraryEntry> entries;
    return entries;
}

bool g_loadAttempted = false;
bool g_loadSucceeded = false;

NSURL* BundledMetallibURL()
{
    NSBundle* bundle = [NSBundle mainBundle];
    if (!bundle)
        return nil;
    return [bundle URLForResource:@"melonPrimeDS" withExtension:@"metallib"];
}

id<MTLLibrary> LoadBundledLibrary(id<MTLDevice> device)
{
    NSURL* url = BundledMetallibURL();
    if (!url)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal library: melonPrimeDS.metallib not found in "
            "app bundle Resources\n");
        return nil;
    }

    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithURL:url error:&error];
    if (!library)
    {
        const char* message =
            error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal library: failed to load bundled metallib at "
            "%s: %s\n",
            [[url path] UTF8String], message);
        return nil;
    }

    std::fprintf(stderr,
        "[MelonPrime] metal library: loaded bundled metallib from %s "
        "(functions=%lu)\n",
        [[url path] UTF8String],
        (unsigned long)[[library functionNames] count]);
    return library;
}

} // namespace

id<MTLLibrary> MelonPrimeMetalDefaultLibrary(id<MTLDevice> device)
{
    if (!device)
        return nil;

    std::lock_guard<std::mutex> lock(CacheMutex());
    for (const CachedLibraryEntry& entry : CacheEntries())
    {
        if (entry.Device == device)
            return entry.Library;
    }

    g_loadAttempted = true;
    id<MTLLibrary> library = LoadBundledLibrary(device);
    if (library)
        g_loadSucceeded = true;

    CachedLibraryEntry entry;
    entry.Device = device;
    entry.Library = library;
    CacheEntries().push_back(std::move(entry));
    return library;
}

bool MelonPrimeMetalDefaultLibraryLoadAttempted() noexcept
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    return g_loadAttempted;
}

bool MelonPrimeMetalDefaultLibraryLoadSucceeded() noexcept
{
    std::lock_guard<std::mutex> lock(CacheMutex());
    return g_loadSucceeded;
}

} // namespace MelonPrime::Metal

#endif // MELONPRIME_ENABLE_METAL
