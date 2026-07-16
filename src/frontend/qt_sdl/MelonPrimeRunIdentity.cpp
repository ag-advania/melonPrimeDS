#include "MelonPrimeBuildInfo.h"
#include "MelonPrimeRunIdentity.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#else
#include <unistd.h>
#endif

namespace MelonPrime
{
namespace
{

std::atomic<std::uint64_t> g_nextRunSequence{0};
std::uint64_t g_runId = 0;
char g_binarySha256Hex[65]{};
char g_runtimeGitCommitShort[13]{};

bool readTextFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool resolveGitCommitShortFromRepo(const std::filesystem::path& startDir, char* out, size_t outSize)
{
    if (outSize < 13)
        return false;

    std::filesystem::path dir = startDir;
    for (int depth = 0; depth < 8; ++depth)
    {
        const auto gitDir = dir / ".git";
        const auto headPath = gitDir / "HEAD";
        if (!std::filesystem::exists(headPath))
        {
            if (!dir.has_parent_path() || dir.parent_path() == dir)
                break;
            dir = dir.parent_path();
            continue;
        }

        std::string headText;
        if (!readTextFile(headPath, headText))
            return false;

        while (!headText.empty() && (headText.back() == '\n' || headText.back() == '\r'))
            headText.pop_back();

        std::string commitFull;
        if (headText.rfind("ref: ", 0) == 0)
        {
            const auto refRel = headText.substr(5);
            const auto refPath = gitDir / refRel;
            if (!readTextFile(refPath, commitFull))
                return false;
        }
        else
        {
            commitFull = headText;
        }

        while (!commitFull.empty() && (commitFull.back() == '\n' || commitFull.back() == '\r'))
            commitFull.pop_back();
        if (commitFull.size() < 12)
            return false;

        std::snprintf(out, outSize, "%.12s", commitFull.c_str());
        return true;
    }

    return false;
}

void resolveRuntimeGitCommitShort()
{
    std::snprintf(g_runtimeGitCommitShort, sizeof(g_runtimeGitCommitShort), "%s", MELONPRIME_GIT_COMMIT);
    if (std::strcmp(g_runtimeGitCommitShort, "unknown") != 0)
        return;

    std::filesystem::path start = std::filesystem::current_path();
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
        start = std::filesystem::path(modulePath).parent_path();
#endif

    char resolved[13]{};
    if (resolveGitCommitShortFromRepo(start, resolved, sizeof(resolved)))
        std::snprintf(g_runtimeGitCommitShort, sizeof(g_runtimeGitCommitShort), "%s", resolved);
}

std::uint64_t makeRunId() noexcept
{
    const std::uint64_t sequence = ++g_nextRunSequence;
#ifdef _WIN32
    const std::uint64_t pid = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const std::uint64_t pid = static_cast<std::uint64_t>(getpid());
#endif
    return (pid << 32) | (sequence & 0xFFFFFFFFu);
}

#ifdef _WIN32
bool sha256File(const wchar_t* path, char* outHex, size_t outHexSize) noexcept
{
    if (outHexSize < 65)
        return false;

    HANDLE file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        CloseHandle(file);
        return false;
    }
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
    {
        CryptReleaseContext(provider, 0);
        CloseHandle(file);
        return false;
    }

    std::uint8_t buffer[64 * 1024];
    DWORD bytesRead = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        if (!CryptHashData(hash, buffer, bytesRead, 0))
        {
            CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            CloseHandle(file);
            return false;
        }
    }

    std::uint8_t digest[32]{};
    DWORD digestSize = sizeof(digest);
    const bool ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0) != FALSE;
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    CloseHandle(file);
    if (!ok)
        return false;

    for (size_t i = 0; i < digestSize; ++i)
        std::snprintf(outHex + (i * 2), 3, "%02x", digest[i]);
    outHex[64] = '\0';
    return true;
}
#endif

void computeBinarySha256() noexcept
{
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        std::snprintf(g_binarySha256Hex, sizeof(g_binarySha256Hex), "unavailable");
        return;
    }
    if (!sha256File(modulePath, g_binarySha256Hex, sizeof(g_binarySha256Hex)))
        std::snprintf(g_binarySha256Hex, sizeof(g_binarySha256Hex), "unavailable");
#else
    std::snprintf(g_binarySha256Hex, sizeof(g_binarySha256Hex), "unavailable");
#endif
}

} // namespace

void initRunIdentity()
{
    resolveRuntimeGitCommitShort();
    g_runId = makeRunId();
    computeBinarySha256();
    std::printf(
        "[RunIdentity] runId=%llu binarySha256=%s\n",
        static_cast<unsigned long long>(g_runId),
        g_binarySha256Hex);
    std::fflush(stdout);
}

std::uint64_t runId() noexcept
{
    return g_runId;
}

const char* binarySha256Hex() noexcept
{
    return g_binarySha256Hex;
}

const char* effectiveGitCommitShort() noexcept
{
    return g_runtimeGitCommitShort;
}

} // namespace MelonPrime
