#include "../stdafx.h"
#include "CrashDiag.h"
#include "../Memory.h"

#include <intrin.h>
#include <oleauto.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "psapi.lib")

namespace {

// --- addresses (IDA-verified, imagebase 0x400000) ---------------------------

constexpr uintptr_t kVa_ComRaiseError = 0x00A605C3; // _com_raise_error(HRESULT, IErrorInfo*)
constexpr uintptr_t kVa_IWzResMan_GetObjectA = 0x00403A93;
constexpr uintptr_t kVa_IWzGr2D = 0x00BF14EC;

constexpr HRESULT kE_POINTER = static_cast<HRESULT>(0x80004003);

// --- fixed buffers ----------------------------------------------------------

constexpr int kWzRingSlots = 32;
constexpr int kWzPathChars = 512;
constexpr int kComErrorMax = 16;
constexpr int kStackDwords = 256;
constexpr int kBacktraceMax = 64;

wchar_t g_wzPaths[kWzRingSlots][kWzPathChars]{};
volatile LONG g_wzSeq = 0;

char g_clientDir[MAX_PATH]{};
char g_crashLogPath[MAX_PATH]{};
char g_comLogPath[MAX_PATH]{};

volatile LONG g_crashLogged = 0;
volatile LONG g_comLogged = 0;
PVOID g_veh = nullptr;

SRWLOCK g_comLogLock = SRWLOCK_INIT;

volatile LONG g_msgThreadId = 0;
volatile LONG g_msgCode = 0;
volatile LONG g_msgWParam = 0;
volatile LONG g_msgLParam = 0;

HMODULE g_hSelf = nullptr;

// --- helpers ----------------------------------------------------------------

void SafeCopyWzPath(wchar_t* dst, size_t dstCch, const wchar_t* src) {
    if (!dst || dstCch == 0) {
        return;
    }
    dst[0] = L'\0';
    if (!src) {
        return;
    }
    __try {
        wcsncpy_s(dst, dstCch, src, _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = L'\0';
    }
}

void NoteWzPathInternal(const wchar_t* path) {
    if (!path) {
        return;
    }
    const LONG seq = InterlockedIncrement(&g_wzSeq) - 1;
    const int slot = static_cast<int>(seq & (kWzRingSlots - 1));
    SafeCopyWzPath(g_wzPaths[slot], kWzPathChars, path);
}

// SHARE_WRITE is required: Cursor/editors often keep the log open; FILE_SHARE_READ
// alone makes CreateFile(FILE_APPEND_DATA) fail silently and we lose RecentWZ dumps.
constexpr DWORD kLogShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

bool AppendFileA(const char* path, const char* data, int len) {
    if (!path || !path[0] || !data || len <= 0) {
        return false;
    }
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, kLogShare, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && written == static_cast<DWORD>(len);
}

bool AppendFileW(const wchar_t* path, const char* data, int len) {
    if (!path || !path[0] || !data || len <= 0) {
        return false;
    }
    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, kLogShare, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return ok && written == static_cast<DWORD>(len);
}

bool DirFromModuleW(HMODULE mod, wchar_t* outDir, size_t outCch) {
    if (!outDir || outCch < 4) {
        return false;
    }
    outDir[0] = L'\0';
    wchar_t full[MAX_PATH]{};
    if (!GetModuleFileNameW(mod, full, MAX_PATH) || !full[0]) {
        return false;
    }
    wchar_t* slash = wcsrchr(full, L'\\');
    if (!slash) {
        return false;
    }
    *(slash + 1) = L'\0';
    wcscpy_s(outDir, outCch, full);
    return true;
}

void FormatLocalTimeMs(char* out, size_t outCch) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    sprintf_s(out, outCch, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

bool ModuleFromAddress(void* addr, HMODULE* outMod, char* path, size_t pathCch, DWORD* outRva) {
    if (outMod) {
        *outMod = nullptr;
    }
    if (path && pathCch) {
        path[0] = '\0';
    }
    if (outRva) {
        *outRva = 0;
    }
    if (!addr) {
        return false;
    }
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &mod) || !mod) {
        return false;
    }
    if (outMod) {
        *outMod = mod;
    }
    if (path && pathCch) {
        GetModuleFileNameA(mod, path, static_cast<DWORD>(pathCch));
    }
    if (outRva) {
        *outRva = static_cast<DWORD>(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod));
    }
    return true;
}

struct PeStampInfo {
    DWORD base;
    DWORD timeDateStamp;
    WORD characteristics;
    bool laa;
    char path[MAX_PATH];
};

bool ReadPeStamp(HMODULE mod, PeStampInfo* out) {
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!mod) {
        return false;
    }
    out->base = static_cast<DWORD>(reinterpret_cast<uintptr_t>(mod));
    GetModuleFileNameA(mod, out->path, MAX_PATH);
    __try {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(mod) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }
        out->timeDateStamp = nt->FileHeader.TimeDateStamp;
        out->characteristics = nt->FileHeader.Characteristics;
        out->laa = (out->characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void AppendModuleLine(char* buf, size_t bufCch, size_t* used, const char* tag, HMODULE mod) {
    if (!buf || !used || *used >= bufCch) {
        return;
    }
    PeStampInfo pe{};
    if (!ReadPeStamp(mod, &pe)) {
        *used += sprintf_s(buf + *used, bufCch - *used, "%s: <unavailable>\r\n", tag);
        return;
    }
    *used += sprintf_s(buf + *used, bufCch - *used,
                       "%s: base=0x%08X TimeDateStamp=0x%08X Characteristics=0x%04X LAA=%d path=%s\r\n",
                       tag, pe.base, pe.timeDateStamp, pe.characteristics, pe.laa ? 1 : 0, pe.path);
}

void AppendMemoryLines(char* buf, size_t bufCch, size_t* used) {
    if (!buf || !used || *used >= bufCch) {
        return;
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        *used += sprintf_s(buf + *used, bufCch - *used,
                           "PrivateUsage=%llu WorkingSet=%llu PagefileUsage=%llu\r\n",
                           static_cast<unsigned long long>(pmc.PrivateUsage),
                           static_cast<unsigned long long>(pmc.WorkingSetSize),
                           static_cast<unsigned long long>(pmc.PagefileUsage));
    }
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        *used += sprintf_s(buf + *used, bufCch - *used, "MemoryLoad=%u%%\r\n", ms.dwMemoryLoad);
    }
    const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    *used += sprintf_s(buf + *used, bufCch - *used, "GDI=%u USER=%u\r\n", gdi, user);

    // User VA summary (x86 2/3GB user space walk).
    uintptr_t addr = 0;
    unsigned long long freeTotal = 0;
    unsigned long long maxFree = 0;
    unsigned long long reserveTotal = 0;
    unsigned long long commitTotal = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < 0x80000000u && VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
        const unsigned long long region = static_cast<unsigned long long>(mbi.RegionSize);
        if (mbi.State == MEM_FREE) {
            freeTotal += region;
            if (region > maxFree) {
                maxFree = region;
            }
        } else if (mbi.State == MEM_RESERVE) {
            reserveTotal += region;
        } else if (mbi.State == MEM_COMMIT) {
            commitTotal += region;
        }
        const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + static_cast<uintptr_t>(mbi.RegionSize);
        if (next <= addr) {
            break;
        }
        addr = next;
    }
    *used += sprintf_s(buf + *used, bufCch - *used,
                       "VA free=%llu maxFree=%llu reserve=%llu commit=%llu\r\n",
                       freeTotal, maxFree, reserveTotal, commitTotal);
}

// Last COM fail description (UTF-16); written as fail_desc= on dump.
wchar_t g_failDesc[kWzPathChars]{};
SRWLOCK g_failDescLock = SRWLOCK_INIT;

// GetObjectA path-spy state (TLS): inflight + last FAILED return.
thread_local wchar_t g_tlsGetObjInFlight[kWzPathChars]{};
thread_local wchar_t g_tlsGetObjFail[kWzPathChars]{};
thread_local HRESULT g_tlsGetObjFailHr = S_OK;
wchar_t g_getObjFailShared[kWzPathChars]{};
wchar_t g_getObjInFlightShared[kWzPathChars]{};
HRESULT g_getObjFailHrShared = S_OK;
SRWLOCK g_getObjSpyLock = SRWLOCK_INIT;

void AppendRecentWz(char* buf, size_t bufCch, size_t* used) {
    if (!buf || !used || *used >= bufCch) {
        return;
    }
    *used += sprintf_s(buf + *used, bufCch - *used, "RecentWZ (newest last, ring=%d):\r\n", kWzRingSlots);
    const LONG seq = InterlockedCompareExchange(&g_wzSeq, 0, 0);
    const int count = (seq < kWzRingSlots) ? static_cast<int>(seq) : kWzRingSlots;
    char lastUtf8[kWzPathChars * 3]{};
    lastUtf8[0] = '\0';
    for (int i = 0; i < count; ++i) {
        const int idx = static_cast<int>((seq - count + i) & (kWzRingSlots - 1));
        char utf8[kWzPathChars * 3]{};
        __try {
            WideCharToMultiByte(CP_UTF8, 0, g_wzPaths[idx], -1, utf8, sizeof(utf8), nullptr, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            strcpy_s(utf8, "<fault>");
        }
        if (i + 1 == count) {
            strcpy_s(lastUtf8, utf8);
        }
        *used += sprintf_s(buf + *used, bufCch - *used, "  [%02d] %s\r\n", i, utf8);
        if (*used + 256 >= bufCch) {
            break;
        }
    }
    if (lastUtf8[0]) {
        *used += sprintf_s(buf + *used, bufCch - *used,
                           "suspect_last=%s\r\n", lastUtf8);
    }
    // fail_desc from IErrorInfo (actual failing UOL); suspect_last is only last OK GetObjectA.
    char failUtf8[kWzPathChars * 3]{};
    failUtf8[0] = '\0';
    AcquireSRWLockShared(&g_failDescLock);
    if (g_failDesc[0]) {
        WideCharToMultiByte(CP_UTF8, 0, g_failDesc, -1, failUtf8, sizeof(failUtf8) - 1, nullptr, nullptr);
    }
    ReleaseSRWLockShared(&g_failDescLock);
    if (failUtf8[0]) {
        *used += sprintf_s(buf + *used, bufCch - *used, "fail_desc=%s\r\n", failUtf8);
        *used += sprintf_s(buf + *used, bufCch - *used, "suspect_fail=%s\r\n", failUtf8);
    } else {
        *used += sprintf_s(buf + *used, bufCch - *used, "fail_desc=\r\n");
    }
    // Always emit spy fields so empty IErrorInfo still pinpoints last FAILED GetObjectA.
    char inflightUtf8[kWzPathChars * 3]{};
    char failPathUtf8[kWzPathChars * 3]{};
    HRESULT failHr = S_OK;
    inflightUtf8[0] = '\0';
    failPathUtf8[0] = '\0';
    AcquireSRWLockShared(&g_getObjSpyLock);
    if (g_getObjInFlightShared[0]) {
        WideCharToMultiByte(CP_UTF8, 0, g_getObjInFlightShared, -1, inflightUtf8,
                            sizeof(inflightUtf8) - 1, nullptr, nullptr);
    }
    if (g_getObjFailShared[0]) {
        WideCharToMultiByte(CP_UTF8, 0, g_getObjFailShared, -1, failPathUtf8,
                            sizeof(failPathUtf8) - 1, nullptr, nullptr);
    }
    failHr = g_getObjFailHrShared;
    ReleaseSRWLockShared(&g_getObjSpyLock);
    // Prefer TLS (throwing thread) over shared snapshot.
    if (g_tlsGetObjInFlight[0]) {
        WideCharToMultiByte(CP_UTF8, 0, g_tlsGetObjInFlight, -1, inflightUtf8,
                            sizeof(inflightUtf8) - 1, nullptr, nullptr);
    }
    if (g_tlsGetObjFail[0]) {
        WideCharToMultiByte(CP_UTF8, 0, g_tlsGetObjFail, -1, failPathUtf8,
                            sizeof(failPathUtf8) - 1, nullptr, nullptr);
        failHr = g_tlsGetObjFailHr;
    }
    *used += sprintf_s(buf + *used, bufCch - *used, "getobj_inflight=%s\r\n",
                       inflightUtf8[0] ? inflightUtf8 : "");
    *used += sprintf_s(buf + *used, bufCch - *used, "getobj_fail=%s\r\n",
                       failPathUtf8[0] ? failPathUtf8 : "");
    *used += sprintf_s(buf + *used, bufCch - *used, "getobj_fail_hr=0x%08X\r\n",
                       static_cast<unsigned>(failHr));
    if (!failUtf8[0] && failPathUtf8[0]) {
        *used += sprintf_s(buf + *used, bufCch - *used, "suspect_fail=%s\r\n", failPathUtf8);
    } else if (!failUtf8[0] && inflightUtf8[0]) {
        *used += sprintf_s(buf + *used, bufCch - *used, "suspect_fail=%s\r\n", inflightUtf8);
    }
}

void AppendMsgContext(char* buf, size_t bufCch, size_t* used) {
    if (!buf || !used || *used >= bufCch) {
        return;
    }
    const DWORD msgTid = static_cast<DWORD>(InterlockedCompareExchange(&g_msgThreadId, 0, 0));
    const UINT msg = static_cast<UINT>(InterlockedCompareExchange(&g_msgCode, 0, 0));
    const WPARAM wp = static_cast<WPARAM>(InterlockedCompareExchange(&g_msgWParam, 0, 0));
    const LPARAM lp = static_cast<LPARAM>(InterlockedCompareExchange(&g_msgLParam, 0, 0));
    *used += sprintf_s(buf + *used, bufCch - *used,
                       "MsgCtx thread=%u msg=0x%X wParam=0x%08X lParam=0x%08X curTid=%u\r\n",
                       msgTid, msg,
                       static_cast<unsigned>(wp), static_cast<unsigned>(lp),
                       GetCurrentThreadId());
    *used += sprintf_s(buf + *used, bufCch - *used,
                       "Foreground=0x%p Active=0x%p Focus=0x%p\r\n",
                       GetForegroundWindow(), GetActiveWindow(), GetFocus());
    __try {
        void* gr2d = *reinterpret_cast<void**>(kVa_IWzGr2D);
        *used += sprintf_s(buf + *used, bufCch - *used, "IWzGr2D*=0x%p\r\n", gr2d);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *used += sprintf_s(buf + *used, bufCch - *used, "IWzGr2D*=<fault>\r\n");
    }
}

void AppendCommonTail(char* buf, size_t bufCch, size_t* used) {
    AppendMemoryLines(buf, bufCch, used);
    HMODULE exe = GetModuleHandleA(nullptr);
    AppendModuleLine(buf, bufCch, used, "MainEXE", exe);
    AppendModuleLine(buf, bufCch, used, "ijl15", g_hSelf ? g_hSelf : GetModuleHandleA("ijl15.dll"));
    AppendModuleLine(buf, bufCch, used, "Gr2D_DX8", GetModuleHandleA("Gr2D_DX8.dll"));
    AppendModuleLine(buf, bufCch, used, "SOUND_DX8", GetModuleHandleA("SOUND_DX8.DLL"));
    if (!GetModuleHandleA("SOUND_DX8.DLL")) {
        AppendModuleLine(buf, bufCch, used, "Sound_DX8", GetModuleHandleA("Sound_DX8.dll"));
    }
    AppendRecentWz(buf, bufCch, used);
}

// --- crash VEH --------------------------------------------------------------

void WriteCrashLog(EXCEPTION_POINTERS* ep) {
    // Fixed buffer - no heap.
    char buf[24576]{};
    size_t used = 0;
    char tbuf[64]{};
    FormatLocalTimeMs(tbuf, sizeof(tbuf));

    EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : nullptr;
    CONTEXT* ctx = ep ? ep->ContextRecord : nullptr;
    const DWORD code = er ? er->ExceptionCode : 0;
    void* addr = er ? er->ExceptionAddress : nullptr;

    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "========== beidou-crash %s ==========\r\n", tbuf);
    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "ExceptionCode=0x%08X ExceptionAddress=0x%p\r\n",
                      static_cast<unsigned>(code), addr);

    if (ctx) {
        used += sprintf_s(buf + used, sizeof(buf) - used,
                          "EIP=0x%08X EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X\r\n"
                          "ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X\r\n",
                          ctx->Eip, ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx,
                          ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    }

    ULONG_PTR info0 = 0;
    ULONG_PTR info1 = 0;
    if (er && er->NumberParameters >= 1) {
        info0 = er->ExceptionInformation[0];
    }
    if (er && er->NumberParameters >= 2) {
        info1 = er->ExceptionInformation[1];
    }
    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "ExceptionInformation[0]=0x%p [1]=0x%p\r\n",
                      reinterpret_cast<void*>(info0), reinterpret_cast<void*>(info1));

    auto dumpPtrMod = [&](const char* tag, void* p) {
        char path[MAX_PATH]{};
        DWORD rva = 0;
        HMODULE mod = nullptr;
        if (ModuleFromAddress(p, &mod, path, MAX_PATH, &rva)) {
            used += sprintf_s(buf + used, sizeof(buf) - used,
                              "%s=0x%p moduleBase=0x%p RVA=0x%X path=%s\r\n",
                              tag, p, reinterpret_cast<void*>(mod), rva, path);
        } else {
            used += sprintf_s(buf + used, sizeof(buf) - used, "%s=0x%p <no module>\r\n", tag, p);
        }
    };

    dumpPtrMod("ExceptionAddress", addr);
    if (ctx) {
        dumpPtrMod("EIP", reinterpret_cast<void*>(ctx->Eip));
        dumpPtrMod("EAX", reinterpret_cast<void*>(ctx->Eax));
        dumpPtrMod("EBX", reinterpret_cast<void*>(ctx->Ebx));
        dumpPtrMod("ECX", reinterpret_cast<void*>(ctx->Ecx));
        dumpPtrMod("EDX", reinterpret_cast<void*>(ctx->Edx));
        dumpPtrMod("ESI", reinterpret_cast<void*>(ctx->Esi));
        dumpPtrMod("EDI", reinterpret_cast<void*>(ctx->Edi));
        dumpPtrMod("EBP", reinterpret_cast<void*>(ctx->Ebp));
        dumpPtrMod("ESP", reinterpret_cast<void*>(ctx->Esp));
    }

    AppendMsgContext(buf, sizeof(buf), &used);
    AppendCommonTail(buf, sizeof(buf), &used);

    if (ctx) {
        used += sprintf_s(buf + used, sizeof(buf) - used, "Stack DWORD dump (ESP, max %d):\r\n", kStackDwords);
        const DWORD* sp = reinterpret_cast<const DWORD*>(ctx->Esp);
        for (int i = 0; i < kStackDwords; ++i) {
            DWORD v = 0;
            bool ok = false;
            __try {
                v = sp[i];
                ok = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                used += sprintf_s(buf + used, sizeof(buf) - used, "  +0x%03X <fault>\r\n", i * 4);
                break;
            }
            if (!ok) {
                break;
            }
            char path[MAX_PATH]{};
            DWORD rva = 0;
            HMODULE mod = nullptr;
            if (ModuleFromAddress(reinterpret_cast<void*>(static_cast<uintptr_t>(v)), &mod, path, MAX_PATH, &rva)) {
                used += sprintf_s(buf + used, sizeof(buf) - used,
                                  "  +0x%03X 0x%08X base=0x%p RVA=0x%X %s\r\n",
                                  i * 4, v, reinterpret_cast<void*>(mod), rva, path);
            } else {
                used += sprintf_s(buf + used, sizeof(buf) - used, "  +0x%03X 0x%08X\r\n", i * 4, v);
            }
            if (used + 200 >= sizeof(buf)) {
                break;
            }
        }
    }

    AppendFileA(g_crashLogPath, buf, static_cast<int>(used));
    // Also refresh beidou-wz-last.log so EOF/quiet-mode map crashes leave a
    // dedicated RecentWZ snapshot even when throw_hr dump HRs were incomplete.
    CrashDiag_DumpRecentWzPaths("VEH");
}

LONG CALLBACK CrashDiagVeh(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedExchange(&g_crashLogged, 1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    __try {
        WriteCrashLog(ep);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // swallow logger faults only
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// --- COM raise hook ---------------------------------------------------------

typedef void(__cdecl* ComRaiseError_t)(HRESULT hr, IErrorInfo* pInfo);
ComRaiseError_t g_ComRaiseError = reinterpret_cast<ComRaiseError_t>(kVa_ComRaiseError);

void WriteComErrorLog(HRESULT hr, void* retAddr) {
    char buf[24576]{};
    size_t used = 0;
    char tbuf[64]{};
    FormatLocalTimeMs(tbuf, sizeof(tbuf));

    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "========== beidou-com-error %s ==========\r\n", tbuf);
    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "HRESULT=0x%08X retAddr=0x%p thread=%u\r\n",
                      static_cast<unsigned>(hr), retAddr, GetCurrentThreadId());

    char path[MAX_PATH]{};
    DWORD rva = 0;
    HMODULE mod = nullptr;
    if (ModuleFromAddress(retAddr, &mod, path, MAX_PATH, &rva)) {
        used += sprintf_s(buf + used, sizeof(buf) - used,
                          "retAddr moduleBase=0x%p RVA=0x%X path=%s\r\n",
                          reinterpret_cast<void*>(mod), rva, path);
    }

    AppendMsgContext(buf, sizeof(buf), &used);
    AppendCommonTail(buf, sizeof(buf), &used);

    void* frames[kBacktraceMax]{};
    const USHORT n = CaptureStackBackTrace(1, kBacktraceMax, frames, nullptr);
    used += sprintf_s(buf + used, sizeof(buf) - used, "Backtrace (%u frames):\r\n", n);
    for (USHORT i = 0; i < n; ++i) {
        char fpath[MAX_PATH]{};
        DWORD frva = 0;
        HMODULE fmod = nullptr;
        if (ModuleFromAddress(frames[i], &fmod, fpath, MAX_PATH, &frva)) {
            used += sprintf_s(buf + used, sizeof(buf) - used,
                              "  [%02u] 0x%p base=0x%p RVA=0x%X %s\r\n",
                              i, frames[i], reinterpret_cast<void*>(fmod), frva, fpath);
        } else {
            used += sprintf_s(buf + used, sizeof(buf) - used, "  [%02u] 0x%p\r\n", i, frames[i]);
        }
        if (used + 200 >= sizeof(buf)) {
            break;
        }
    }

    AcquireSRWLockExclusive(&g_comLogLock);
    AppendFileA(g_comLogPath, buf, static_cast<int>(used));
    ReleaseSRWLockExclusive(&g_comLogLock);
}

void NoteErrorInfoDescription(IErrorInfo* pInfo) {
    if (!pInfo) {
        return;
    }
    __try {
        BSTR desc = nullptr;
        BSTR src = nullptr;
        if (SUCCEEDED(pInfo->GetDescription(&desc)) && desc) {
            NoteWzPathInternal(desc);
            // Also append a one-line marker file for quick grepping.
            char path[MAX_PATH]{};
            strcpy_s(path, g_clientDir);
            strcat_s(path, "beidou-com-error.log");
            char line[1024]{};
            char tbuf[64]{};
            FormatLocalTimeMs(tbuf, sizeof(tbuf));
            char narrow[700]{};
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
            const int n = sprintf_s(line, "[%s] IErrorInfo.Description=%s\r\n", tbuf, narrow);
            if (n > 0) {
                AppendFileA(path, line, n);
            }
            SysFreeString(desc);
        }
        if (SUCCEEDED(pInfo->GetSource(&src)) && src) {
            char path[MAX_PATH]{};
            strcpy_s(path, g_clientDir);
            strcat_s(path, "beidou-com-error.log");
            char line[1024]{};
            char tbuf[64]{};
            FormatLocalTimeMs(tbuf, sizeof(tbuf));
            char narrow[700]{};
            WideCharToMultiByte(CP_UTF8, 0, src, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
            const int n = sprintf_s(line, "[%s] IErrorInfo.Source=%s\r\n", tbuf, narrow);
            if (n > 0) {
                AppendFileA(path, line, n);
            }
            SysFreeString(src);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __cdecl Hook_ComRaiseError(HRESULT hr, IErrorInfo* pInfo) {
    const unsigned uhr = static_cast<unsigned>(hr);
    // Include 0x8007000D (ERROR_INVALID_DATA) and 0x80070026 (EOF) —
    // enter-map throw_hr family (missing/truncated canvas in existing .img).
    if (uhr == static_cast<unsigned>(kE_POINTER) || uhr == 0x80030002u ||
        uhr == 0x80004003u || uhr == 0x8007000Du || uhr == 0x80070026u) {
        const LONG n = InterlockedIncrement(&g_comLogged);
        if (n <= kComErrorMax) {
            void* retAddr = _ReturnAddress();
            __try {
                NoteErrorInfoDescription(pInfo);
                bool noted = false;
                if (pInfo) {
                    BSTR desc = nullptr;
                    if (SUCCEEDED(pInfo->GetDescription(&desc)) && desc && desc[0]) {
                        CrashDiag_NoteFailDescription(desc);
                        noted = true;
                        SysFreeString(desc);
                    } else if (desc) {
                        SysFreeString(desc);
                    }
                }
                if (!noted) {
                    CrashDiag_EnsureFailDescFromGetObjectSpy();
                }
                WriteComErrorLog(hr, retAddr);
                if (uhr == 0x80030002u || uhr == 0x80004003u || uhr == 0x8007000Du ||
                    uhr == 0x80070026u) {
                    CrashDiag_DumpRecentWzPaths("ComRaiseError");
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    g_ComRaiseError(hr, pInfo);
}

// --- WZ GetObjectA hook -----------------------------------------------------

// IDA: VARIANTARG *__thiscall sub_403A93(..., Ztl_bstr_t by value = Data_t*, ...)
// Data_t first field is BSTR m_wstr — one deref yields the UOL string.
typedef VARIANT*(__fastcall* IWzResMan_GetObjectA_t)(
    void* pThis, void* edx, VARIANT* pResult, void* pZtlBstr, void* pParam, void* pAux);
IWzResMan_GetObjectA_t g_GetObjectA =
    reinterpret_cast<IWzResMan_GetObjectA_t>(kVa_IWzResMan_GetObjectA);

VARIANT* __fastcall Hook_GetObjectA(
    void* pThis, void* edx, VARIANT* pResult, void* pZtlBstr, void* pParam, void* pAux) {
    __try {
        if (pZtlBstr) {
            const wchar_t* path = *reinterpret_cast<const wchar_t* const*>(pZtlBstr);
            if (path && path[0]) {
                NoteWzPathInternal(path);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_GetObjectA(pThis, edx, pResult, pZtlBstr, pParam, pAux);
}

} // namespace

void CrashDiag_NoteWzPath(const wchar_t* path) {
    NoteWzPathInternal(path);
}

void CrashDiag_NoteFailDescription(const wchar_t* desc) {
    if (!desc || !desc[0]) {
        return;
    }
    AcquireSRWLockExclusive(&g_failDescLock);
    wcsncpy_s(g_failDesc, desc, _TRUNCATE);
    ReleaseSRWLockExclusive(&g_failDescLock);
    // Also push into RecentWZ ring so suspect_last can move off MapHelper
    // when description itself is a WZ UOL path.
    NoteWzPathInternal(desc);
}

void CrashDiag_NoteGetObjectInFlight(const wchar_t* path) {
    if (!path || !path[0]) {
        g_tlsGetObjInFlight[0] = L'\0';
        return;
    }
    wcsncpy_s(g_tlsGetObjInFlight, path, _TRUNCATE);
    AcquireSRWLockExclusive(&g_getObjSpyLock);
    wcsncpy_s(g_getObjInFlightShared, path, _TRUNCATE);
    ReleaseSRWLockExclusive(&g_getObjSpyLock);
}

void CrashDiag_ClearGetObjectInFlight() {
    g_tlsGetObjInFlight[0] = L'\0';
    AcquireSRWLockExclusive(&g_getObjSpyLock);
    g_getObjInFlightShared[0] = L'\0';
    ReleaseSRWLockExclusive(&g_getObjSpyLock);
}

void CrashDiag_NoteGetObjectFail(const wchar_t* path, HRESULT hr) {
    if (!path || !path[0]) {
        return;
    }
    wcsncpy_s(g_tlsGetObjFail, path, _TRUNCATE);
    g_tlsGetObjFailHr = hr;
    AcquireSRWLockExclusive(&g_getObjSpyLock);
    wcsncpy_s(g_getObjFailShared, path, _TRUNCATE);
    g_getObjFailHrShared = hr;
    ReleaseSRWLockExclusive(&g_getObjSpyLock);
    NoteWzPathInternal(path);
}

void CrashDiag_EnsureFailDescFromGetObjectSpy() {
    AcquireSRWLockShared(&g_failDescLock);
    const bool haveDesc = g_failDesc[0] != L'\0';
    ReleaseSRWLockShared(&g_failDescLock);
    if (haveDesc) {
        return;
    }
    wchar_t synth[kWzPathChars]{};
    if (g_tlsGetObjFail[0]) {
        swprintf_s(synth, L"getobj_fail:%s hr=0x%08X", g_tlsGetObjFail,
                   static_cast<unsigned>(g_tlsGetObjFailHr));
    } else if (g_tlsGetObjInFlight[0]) {
        swprintf_s(synth, L"getobj_inflight:%s", g_tlsGetObjInFlight);
    } else {
        AcquireSRWLockShared(&g_getObjSpyLock);
        if (g_getObjFailShared[0]) {
            swprintf_s(synth, L"getobj_fail:%s hr=0x%08X", g_getObjFailShared,
                       static_cast<unsigned>(g_getObjFailHrShared));
        } else if (g_getObjInFlightShared[0]) {
            swprintf_s(synth, L"getobj_inflight:%s", g_getObjInFlightShared);
        }
        ReleaseSRWLockShared(&g_getObjSpyLock);
    }
    if (synth[0]) {
        CrashDiag_NoteFailDescription(synth);
    }
}

bool CrashDiag_DumpRecentWzPaths(const char* reason, DWORD* outGle) {
    if (outGle) {
        *outGle = 0;
    }

    char buf[8192]{};
    size_t used = 0;
    char tbuf[64]{};
    FormatLocalTimeMs(tbuf, sizeof(tbuf));
    used += sprintf_s(buf + used, sizeof(buf) - used,
                      "========== beidou-wz-last %s ==========\r\n", tbuf);
    used += sprintf_s(buf + used, sizeof(buf) - used, "reason=%s\r\n",
                      reason ? reason : "(null)");
    AppendRecentWz(buf, sizeof(buf), &used);

    // 1) Prefer ANSI path under g_clientDir (same as BootLog).
    if (g_clientDir[0]) {
        char pathA[MAX_PATH]{};
        strcpy_s(pathA, g_clientDir);
        strcat_s(pathA, "beidou-wz-last.log");
        if (AppendFileA(pathA, buf, static_cast<int>(used))) {
            return true;
        }
        if (outGle) {
            *outGle = GetLastError();
        }
    }

    // 2) Unicode path from main EXE (handles non-ACP / Chinese dirs reliably).
    wchar_t dirW[MAX_PATH]{};
    if (DirFromModuleW(GetModuleHandleW(nullptr), dirW, MAX_PATH)) {
        wchar_t pathW[MAX_PATH]{};
        wcscpy_s(pathW, dirW);
        wcscat_s(pathW, L"beidou-wz-last.log");
        if (AppendFileW(pathW, buf, static_cast<int>(used))) {
            // Refresh ANSI g_clientDir if it was empty/wrong.
            if (!g_clientDir[0]) {
                WideCharToMultiByte(CP_ACP, 0, dirW, -1, g_clientDir, MAX_PATH, nullptr, nullptr);
            }
            return true;
        }
        if (outGle) {
            *outGle = GetLastError();
        }
    }

    // 3) Unicode path beside ijl15.dll.
    HMODULE ijl = g_hSelf ? g_hSelf : GetModuleHandleW(L"ijl15.dll");
    if (DirFromModuleW(ijl, dirW, MAX_PATH)) {
        wchar_t pathW[MAX_PATH]{};
        wcscpy_s(pathW, dirW);
        wcscat_s(pathW, L"beidou-wz-last.log");
        if (AppendFileW(pathW, buf, static_cast<int>(used))) {
            return true;
        }
        if (outGle) {
            *outGle = GetLastError();
        }
    }

    if (outGle && *outGle == 0) {
        *outGle = ERROR_PATH_NOT_FOUND;
    }
    return false;
}

void CrashDiag_SetMsgContext(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam) {
    InterlockedExchange(&g_msgThreadId, static_cast<LONG>(threadId));
    InterlockedExchange(&g_msgCode, static_cast<LONG>(msg));
    InterlockedExchange(&g_msgWParam, static_cast<LONG>(wParam));
    InterlockedExchange(&g_msgLParam, static_cast<LONG>(lParam));
}

void CrashDiag_ClearMsgContext() {
    InterlockedExchange(&g_msgThreadId, 0);
    InterlockedExchange(&g_msgCode, 0);
    InterlockedExchange(&g_msgWParam, 0);
    InterlockedExchange(&g_msgLParam, 0);
}

void CrashDiag_Init(HMODULE hModule) {
    g_hSelf = hModule;
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        if (hModule) {
            GetModuleFileNameA(hModule, path, MAX_PATH);
        }
    }
    if (char* slash = strrchr(path, '\\')) {
        *(slash + 1) = '\0';
    }
    strcpy_s(g_clientDir, path);
    strcpy_s(g_crashLogPath, path);
    strcat_s(g_crashLogPath, "beidou-crash.log");
    strcpy_s(g_comLogPath, path);
    strcat_s(g_comLogPath, "beidou-com-error.log");

    if (!g_veh) {
        g_veh = AddVectoredExceptionHandler(1, CrashDiagVeh);
    }
}

static bool g_comRaiseDiagOn = false;

void CrashDiag_AttachComRaiseDiag() {
    if (g_comRaiseDiagOn) {
        return;
    }
    g_comRaiseDiagOn = Memory::SetHook(true, reinterpret_cast<void**>(&g_ComRaiseError),
                                       reinterpret_cast<void*>(&Hook_ComRaiseError));
}

void CrashDiag_AttachHooks() {
    CrashDiag_AttachComRaiseDiag();
    Memory::SetHook(true, reinterpret_cast<void**>(&g_GetObjectA),
                    reinterpret_cast<void*>(&Hook_GetObjectA));
    // IWzNameSpace local lookups: recorded via CrashDiag_NoteWzPath in rs_ns_hook
    // (NAMESPACE.DLL pattern owned by rs_resman - do not double-PatFind).
}
