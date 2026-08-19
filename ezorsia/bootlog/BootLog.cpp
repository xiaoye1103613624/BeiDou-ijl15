#include "../stdafx.h"
#include "BootLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

CRITICAL_SECTION g_cs;
bool g_csReady = false;
char g_logPath[MAX_PATH] = {};
char g_lastStage[128] = "pre-init";
volatile LONG g_inVeh = 0;
volatile LONG g_quiet = 0;
PVOID g_veh = nullptr;
HANDLE g_file = INVALID_HANDLE_VALUE;

void EnsureCs() {
    if (!g_csReady) {
        InitializeCriticalSection(&g_cs);
        g_csReady = true;
    }
}

bool OpenLogFile() {
    if (g_file != INVALID_HANDLE_VALUE) {
        return true;
    }
    if (!g_logPath[0]) {
        return false;
    }
    g_file = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void WriteRaw(const char* line, int len, bool flush) {
    if (!g_logPath[0] || len <= 0) {
        return;
    }
    if (!OpenLogFile()) {
        return;
    }
    DWORD written = 0;
    WriteFile(g_file, line, static_cast<DWORD>(len), &written, nullptr);
    if (flush) {
        FlushFileBuffers(g_file);
    }
}

void WriteLine(const char* msg, bool flush) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char line[1200]{};
    const int n = sprintf_s(line, "[%02d:%02d:%02d.%03d] %s\r\n",
                            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    if (n <= 0) {
        return;
    }
    EnterCriticalSection(&g_cs);
    WriteRaw(line, n, flush);
    LeaveCriticalSection(&g_cs);
}

LONG CALLBACK BootCrashVeh(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Maple uses C++ EH heavily for normal control flow. Logging every
    // 0xE06D7363 with disk flush caused in-game stutter (900+/min).
    if (code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_IN_PAGE_ERROR:
        break;
    case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN / __fastfail (GS cookie)
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedCompareExchange(&g_inVeh, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void* addr = ep->ExceptionRecord->ExceptionAddress;
    char modName[MAX_PATH] = "unknown";
    DWORD off = 0;
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod) && mod) {
        GetModuleFileNameA(mod, modName, MAX_PATH);
        if (char* slash = strrchr(modName, '\\')) {
            memmove(modName, slash + 1, strlen(slash + 1) + 1);
        }
        off = static_cast<DWORD>(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod));
    }

    DWORD target = 0;
    const char* acc = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        target = static_cast<DWORD>(ep->ExceptionRecord->ExceptionInformation[1]);
        acc = (op == 0) ? "read" : (op == 1) ? "write" : "exec";
    }

    char msg[512]{};
    sprintf_s(msg, "*** CRASH code=0x%08X stage=%s at %s+0x%X abs=0x%08X %s target=0x%08X",
              static_cast<unsigned>(code), g_lastStage, modName, static_cast<unsigned>(off),
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr)), acc,
              static_cast<unsigned>(target));
    WriteLine(msg, true);

    if (ep->ContextRecord) {
        CONTEXT* ctx = ep->ContextRecord;
        sprintf_s(msg, "    Eip=0x%08X Esp=0x%08X Ebp=0x%08X Eax=0x%08X Ecx=0x%08X",
                  ctx->Eip, ctx->Esp, ctx->Ebp, ctx->Eax, ctx->Ecx);
        WriteLine(msg, true);
        const DWORD* sp = reinterpret_cast<const DWORD*>(ctx->Esp);
        int logged = 0;
        for (int i = 0; i < 384 && logged < 20; ++i) {
            __try {
                const DWORD v = sp[i];
                if (v >= 0x00401000 && v < 0x00B00000) {
                    sprintf_s(msg, "    stack+0x%-4X BeiDou.exe+0x%X", i * 4,
                              static_cast<unsigned>(v - 0x00400000));
                    WriteLine(msg, false);
                    ++logged;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                break;
            }
        }
        FlushFileBuffers(g_file);
    }

    InterlockedExchange(&g_inVeh, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void BootLog_Init(HMODULE hModule) {
    EnsureCs();
    char path[MAX_PATH]{};
    if (GetModuleFileNameA(hModule ? hModule : nullptr, path, MAX_PATH) == 0) {
        GetModuleFileNameA(nullptr, path, MAX_PATH);
    }
    if (char* slash = strrchr(path, '\\')) {
        *(slash + 1) = '\0';
    }
    strcpy_s(g_logPath, path);
    strcat_s(g_logPath, "client_boot.log");

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char header[256]{};
    sprintf_s(header,
              "========== BOOT session %04d-%02d-%02d %02d:%02d:%02d.%03d ==========\r\n",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    EnterCriticalSection(&g_cs);
    WriteRaw(header, static_cast<int>(strlen(header)), true);
    LeaveCriticalSection(&g_cs);
    BootLogStage("BootLog_Init");
}

void BootLog(const char* fmt, ...) {
    if (InterlockedCompareExchange(&g_quiet, 0, 0) != 0) {
        return;
    }
    EnsureCs();
    char msg[1024]{};
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(msg, _TRUNCATE, fmt, args);
    va_end(args);
    WriteLine(msg, false);
}

void BootLogStage(const char* stage) {
    if (stage && stage[0]) {
        EnsureCs();
        EnterCriticalSection(&g_cs);
        strncpy_s(g_lastStage, stage, _TRUNCATE);
        LeaveCriticalSection(&g_cs);
    }
    if (InterlockedCompareExchange(&g_quiet, 0, 0) != 0) {
        return;
    }
    BootLog("STAGE %s", stage ? stage : "(null)");
}

const char* BootLog_LastStage() {
    return g_lastStage;
}

void BootLog_InstallCrashVeh() {
    if (g_veh) {
        return;
    }
    g_veh = AddVectoredExceptionHandler(1, BootCrashVeh);
    BootLog("Crash VEH installed handle=%p", g_veh);
}

void BootLog_SetQuiet(bool quiet) {
    InterlockedExchange(&g_quiet, quiet ? 1 : 0);
    if (quiet) {
        EnsureCs();
        EnterCriticalSection(&g_cs);
        if (g_file != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(g_file);
        }
        LeaveCriticalSection(&g_cs);
    }
}

bool BootLog_IsQuiet() {
    return InterlockedCompareExchange(&g_quiet, 0, 0) != 0;
}
