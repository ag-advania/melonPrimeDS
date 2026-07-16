#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "MelonPrimeBuildInfo.h"
#include "MelonPrimeFirstVulkanFrameTrace.h"
#include "MelonPrimeRunIdentity.h"
#include "MelonPrimeWindowsCrashHandler.h"

#pragma comment(lib, "dbghelp.lib")

namespace
{

constexpr DWORD kMaxStackFrames = 64;

void writeBuildIdentity(FILE* out)
{
    std::fprintf(out, "buildIdentity.runId=%llu\n", static_cast<unsigned long long>(MelonPrime::runId()));
    std::fprintf(out, "buildIdentity.gitCommit=%s\n", MelonPrime::effectiveGitCommitShort());
    std::fprintf(out, "buildIdentity.gitCommitFull=%s\n", MELONPRIME_GIT_COMMIT_FULL);
    std::fprintf(out, "buildIdentity.gitBranch=%s\n", MELONPRIME_GIT_BRANCH);
    std::fprintf(out, "buildIdentity.gitDirty=%d\n", MELONPRIME_GIT_DIRTY);
    std::fprintf(out, "buildIdentity.binarySha256=%s\n", MelonPrime::binarySha256Hex());
    std::fprintf(out, "buildIdentity.buildTz=%s\n", MELONPRIMEDS_BUILD_TZ);
    std::fprintf(out, "buildIdentity.timestamp=%lld\n", static_cast<long long>(std::time(nullptr)));
}

void writeAccessViolationDetails(FILE* out, EXCEPTION_RECORD* record)
{
    if (record == nullptr)
        return;

    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return;

    if (record->NumberParameters < 2)
    {
        std::fprintf(out, "exception.accessViolationParametersMissing count=%lu\n",
            record->NumberParameters);
        return;
    }

    const char* accessKind = "unknown";
    switch (record->ExceptionInformation[0])
    {
    case 0: accessKind = "read"; break;
    case 1: accessKind = "write"; break;
    case 8: accessKind = "execute"; break;
    default: break;
    }

    std::fprintf(
        out,
        "exception.accessType=%llu exception.accessKind=%s exception.faultAddress=0x%016llX\n",
        static_cast<unsigned long long>(record->ExceptionInformation[0]),
        accessKind,
        static_cast<unsigned long long>(record->ExceptionInformation[1]));
}

void writeExceptionModuleInfo(FILE* out, EXCEPTION_RECORD* record)
{
    if (record == nullptr || record->ExceptionAddress == nullptr)
        return;

    HMODULE module = nullptr;
    char modulePath[MAX_PATH]{};
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(record->ExceptionAddress),
            &module)
        || module == nullptr)
    {
        std::fprintf(out, "exception.moduleLookupFailed address=%p\n", record->ExceptionAddress);
        return;
    }

    const DWORD pathLength = GetModuleFileNameA(module, modulePath, MAX_PATH);
    const auto moduleBase = reinterpret_cast<uintptr_t>(module);
    const auto exceptionAddress = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    const uintptr_t exceptionRva = exceptionAddress - moduleBase;

    std::fprintf(
        out,
        "exception.moduleBase=0x%p exceptionRva=0x%llX modulePath=%s\n",
        module,
        static_cast<unsigned long long>(exceptionRva),
        pathLength > 0 ? modulePath : "unknown");
}

void writeRegisters(FILE* out, CONTEXT* context)
{
    if (context == nullptr)
        return;

#if defined(_M_X64) || defined(__x86_64__)
    std::fprintf(
        out,
        "registers.rip=0x%016llX rsp=0x%016llX rbp=0x%016llX rax=0x%016llX\n",
        static_cast<unsigned long long>(context->Rip),
        static_cast<unsigned long long>(context->Rsp),
        static_cast<unsigned long long>(context->Rbp),
        static_cast<unsigned long long>(context->Rax));
    std::fprintf(
        out,
        "registers.rbx=0x%016llX rcx=0x%016llX rdx=0x%016llX rsi=0x%016llX\n",
        static_cast<unsigned long long>(context->Rbx),
        static_cast<unsigned long long>(context->Rcx),
        static_cast<unsigned long long>(context->Rdx),
        static_cast<unsigned long long>(context->Rsi));
    std::fprintf(
        out,
        "registers.rdi=0x%016llX r8=0x%016llX r9=0x%016llX r10=0x%016llX\n",
        static_cast<unsigned long long>(context->Rdi),
        static_cast<unsigned long long>(context->R8),
        static_cast<unsigned long long>(context->R9),
        static_cast<unsigned long long>(context->R10));
    std::fprintf(
        out,
        "registers.r11=0x%016llX r12=0x%016llX r13=0x%016llX r14=0x%016llX r15=0x%016llX\n",
        static_cast<unsigned long long>(context->R11),
        static_cast<unsigned long long>(context->R12),
        static_cast<unsigned long long>(context->R13),
        static_cast<unsigned long long>(context->R14),
        static_cast<unsigned long long>(context->R15));
    std::fprintf(
        out,
        "registers.eflags=0x%08lX\n",
        context->EFlags);
#elif defined(_M_IX86) || defined(__i386__)
    std::fprintf(
        out,
        "registers.eip=0x%08lX esp=0x%08lX ebp=0x%08lX eax=0x%08lX\n",
        context->Eip,
        context->Esp,
        context->Ebp,
        context->Eax);
    std::fprintf(
        out,
        "registers.ebx=0x%08lX ecx=0x%08lX edx=0x%08lX esi=0x%08lX edi=0x%08lX eflags=0x%08lX\n",
        context->Ebx,
        context->Ecx,
        context->Edx,
        context->Esi,
        context->Edi,
        context->EFlags);
#else
    std::fprintf(out, "registers.unsupportedArchitecture\n");
#endif
}

bool lookupModuleForAddress(
    DWORD64 address,
    HMODULE* moduleOut,
    char* modulePath,
    DWORD modulePathCapacity,
    DWORD64* moduleBaseOut)
{
    if (moduleOut == nullptr || moduleBaseOut == nullptr)
        return false;

    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),
            &module)
        || module == nullptr)
    {
        return false;
    }

    *moduleOut = module;
    *moduleBaseOut = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(module));
    if (modulePath != nullptr && modulePathCapacity > 0)
    {
        const DWORD pathLength = GetModuleFileNameA(module, modulePath, modulePathCapacity);
        if (pathLength == 0)
            modulePath[0] = '\0';
    }
    return true;
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

        HMODULE module = nullptr;
        char modulePath[MAX_PATH]{};
        DWORD64 moduleBase = 0;
        const bool hasModule = lookupModuleForAddress(
            frame.AddrPC.Offset,
            &module,
            modulePath,
            MAX_PATH,
            &moduleBase);
        const DWORD64 rva = hasModule ? frame.AddrPC.Offset - moduleBase : 0;

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)]{};
        auto* symbol = reinterpret_cast<PSYMBOL_INFO>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        const bool hasSymbol = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) != FALSE;

        std::fprintf(
            out,
            "#%02u abs=0x%016llX module=%s moduleBase=0x%016llX rva=0x%llX\n",
            depth,
            static_cast<unsigned long long>(frame.AddrPC.Offset),
            hasModule && modulePath[0] != '\0' ? modulePath : "unknown",
            static_cast<unsigned long long>(moduleBase),
            static_cast<unsigned long long>(rva));

        if (hasSymbol)
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
                    "    symbol=%s+0x%llX file=%s line=%lu\n",
                    symbol->Name,
                    static_cast<unsigned long long>(displacement),
                    line.FileName,
                    line.LineNumber);
            }
            else
            {
                std::fprintf(
                    out,
                    "    symbol=%s+0x%llX\n",
                    symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
        }
    }

    SymCleanup(process);
}

void writeCrashArtifacts(EXCEPTION_POINTERS* exceptionInfo)
{
    char dumpPath[MAX_PATH]{};
    char reportPath[MAX_PATH]{};
    std::snprintf(
        dumpPath,
        sizeof(dumpPath),
        "melonPrimeDS-%s-run-%llu.dmp",
        MelonPrime::effectiveGitCommitShort(),
        static_cast<unsigned long long>(MelonPrime::runId()));
    std::snprintf(
        reportPath,
        sizeof(reportPath),
        "melonPrimeDS-%s-run-%llu.crash.txt",
        MelonPrime::effectiveGitCommitShort(),
        static_cast<unsigned long long>(MelonPrime::runId()));

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
            writeAccessViolationDetails(report, exceptionInfo->ExceptionRecord);
            writeExceptionModuleInfo(report, exceptionInfo->ExceptionRecord);
            if (exceptionInfo->ContextRecord != nullptr)
            {
                writeRegisters(report, exceptionInfo->ContextRecord);
                writeSymbolizedStack(report, exceptionInfo->ContextRecord);
            }
        }
        std::fclose(report);
    }

    MelonPrime::FirstVulkanFrameTrace::dumpRingToCrashReport(reportPath);

    if (exceptionInfo != nullptr)
        (void)writeMiniDump(exceptionInfo, dumpPath);

    std::fprintf(
        stderr,
        "[MelonPrimeCrash] runId=%llu buildIdentity=%s minidump=%s report=%s\n",
        static_cast<unsigned long long>(MelonPrime::runId()),
        MelonPrime::effectiveGitCommitShort(),
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
