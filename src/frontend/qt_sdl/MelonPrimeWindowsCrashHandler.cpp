#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "MelonPrimeBuildInfo.h"
#include "MelonPrimeWindowsCrashHandler.h"

#pragma comment(lib, "dbghelp.lib")

namespace
{

constexpr DWORD kMaxStackFrames = 64;

void writeBuildIdentity(FILE* out)
{
    std::fprintf(out, "buildIdentity.gitCommit=%s\n", MELONPRIME_GIT_COMMIT);
    std::fprintf(out, "buildIdentity.buildTz=%s\n", MELONPRIMEDS_BUILD_TZ);
    std::fprintf(out, "buildIdentity.timestamp=%lld\n", static_cast<long long>(std::time(nullptr)));
}

bool writeMiniDump(EXCEPTION_POINTERS* exceptionInfo, const char* dumpPath)
{
    HANDLE file = CreateFileA(
        dumpPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION exceptionParam{};
    exceptionParam.ThreadId = GetCurrentThreadId();
    exceptionParam.ExceptionPointers = exceptionInfo;
    exceptionParam.ClientPointers = FALSE;

    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory
        | MiniDumpScanMemory
        | MiniDumpWithThreadInfo);

    const BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        dumpType,
        exceptionInfo != nullptr ? &exceptionParam : nullptr,
        nullptr,
        nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

void writeSymbolizedStack(FILE* out, CONTEXT* context)
{
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE))
    {
        std::fprintf(out, "stack.symbolInitFailed error=%lu\n", GetLastError());
        return;
    }

    STACKFRAME64 frame{};
#if defined(_M_X64) || defined(__x86_64__)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86) || defined(__i386__)
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    std::fprintf(out, "stack.unsupportedArchitecture\n");
    SymCleanup(process);
    return;
#endif

    for (DWORD depth = 0; depth < kMaxStackFrames; ++depth)
    {
        if (!StackWalk64(
                machine,
                process,
                GetCurrentThread(),
                &frame,
                context,
                nullptr,
                SymFunctionTableAccess64,
                SymGetModuleBase64,
                nullptr))
        {
            break;
        }

        if (frame.AddrPC.Offset == 0)
            break;

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)]{};
        auto* symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
        {
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(
                    process,
                    frame.AddrPC.Offset,
                    &lineDisplacement,
                    &line))
            {
                std::fprintf(
                    out,
                    "#%02u %s+0x%llX %s:%lu\n",
                    depth,
                    symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName,
                    line.LineNumber);
            }
            else
            {
                std::fprintf(
                    out,
                    "#%02u %s+0x%llX\n",
                    depth,
                    symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
        }
        else
        {
            std::fprintf(
                out,
                "#%02u 0x%016llX\n",
                depth,
                static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
    }

    SymCleanup(process);
}

void writeCrashArtifacts(EXCEPTION_POINTERS* exceptionInfo)
{
    const std::time_t now = std::time(nullptr);
    char dumpPath[MAX_PATH]{};
    char reportPath[MAX_PATH]{};
    std::snprintf(
        dumpPath,
        sizeof(dumpPath),
        "melonPrimeDS-%s-%lld.dmp",
        MELONPRIME_GIT_COMMIT,
        static_cast<long long>(now));
    std::snprintf(
        reportPath,
        sizeof(reportPath),
        "melonPrimeDS-%s-%lld.crash.txt",
        MELONPRIME_GIT_COMMIT,
        static_cast<long long>(now));

    FILE* report = std::fopen(reportPath, "w");
    if (report != nullptr)
    {
        writeBuildIdentity(report);
        if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr)
        {
            std::fprintf(
                report,
                "exception.code=0x%08lX address=0x%p threadId=%lu\n",
                exceptionInfo->ExceptionRecord->ExceptionCode,
                exceptionInfo->ExceptionRecord->ExceptionAddress,
                GetCurrentThreadId());
            if (exceptionInfo->ContextRecord != nullptr)
                writeSymbolizedStack(report, exceptionInfo->ContextRecord);
        }
        std::fclose(report);
    }

    if (exceptionInfo != nullptr)
        (void)writeMiniDump(exceptionInfo, dumpPath);

    std::fprintf(
        stderr,
        "[MelonPrimeCrash] buildIdentity=%s minidump=%s report=%s\n",
        MELONPRIME_GIT_COMMIT,
        dumpPath,
        reportPath);
    std::fflush(stderr);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    writeCrashArtifacts(exceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace MelonPrime
{

void installWindowsCrashHandler()
{
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
}

} // namespace MelonPrime

#else

#include "MelonPrimeWindowsCrashHandler.h"

namespace MelonPrime
{

void installWindowsCrashHandler()
{
}

} // namespace MelonPrime

#endif
