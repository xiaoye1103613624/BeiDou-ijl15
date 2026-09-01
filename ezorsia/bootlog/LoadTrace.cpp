#include "../stdafx.h"
#include "BootLog.h"
#include "LoadTraceApi.h"
#include "CrashDiag.h"
#include "../Memory.h"
#include "../Client.h"
#include "../gamedata/MapEnterNullGuardApi.h"

#include <cstring>
#include <oleauto.h>
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// --- helpers -----------------------------------------------------------------

struct ZtlBstrData {
    wchar_t* wstr;
    char* str;
    unsigned int refCount;
};

static void LogZtlBstr(const char* tag, void* ztl) {
    if (!ztl) {
        BootLog("%s <null-bstr>", tag);
        return;
    }
    __try {
        auto* data = reinterpret_cast<ZtlBstrData*>(ztl);
        if (data->str && !IsBadReadPtr(data->str, 1)) {
            BootLog("%s \"%s\"", tag, data->str);
            return;
        }
        if (data->wstr && !IsBadReadPtr(data->wstr, 2)) {
            char buf[512]{};
            WideCharToMultiByte(CP_UTF8, 0, data->wstr, -1, buf, sizeof(buf), nullptr, nullptr);
            BootLog("%s L\"%s\"", tag, buf);
            return;
        }
        BootLog("%s <empty-bstr data=%p>", tag, ztl);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BootLog("%s <bstr-fault ptr=%p>", tag, ztl);
    }
}

template <typename Fn>
static bool Attach(Fn** target, Fn* detour, const char* name) {
    const bool ok = Memory::SetHook(true, reinterpret_cast<void**>(target), reinterpret_cast<void*>(detour));
    BootLog("hook %s %s @%p", name, ok ? "OK" : "FAIL", reinterpret_cast<void*>(*target));
    return ok;
}

// --- CWvsApp::Init (0x9F5239) thiscall --------------------------------------

typedef int(__fastcall* CWvsAppInit_t)(void* pThis, void* edx);
static auto g_CWvsAppInit = reinterpret_cast<CWvsAppInit_t>(0x009F5239);
static void AttachGetObjectAPathSpy(const char* why);

// Runs at __except FILTER time (stack still at the throw site) — captures the
// actual throwing frame before unwinding, for the 0xE06D7363 C++/COM throw
// that aborts current-source builds inside CWvsApp::Init.
static void LogInitSeh(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) {
        return;
    }
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    CONTEXT* ctx = ep->ContextRecord;
    const DWORD code = er->ExceptionCode;
    void* addr = er->ExceptionAddress;
    const ULONG_PTR i0 = er->NumberParameters >= 1 ? er->ExceptionInformation[0] : 0;
    const ULONG_PTR i1 = er->NumberParameters >= 2 ? er->ExceptionInformation[1] : 0;
    const ULONG_PTR i2 = er->NumberParameters >= 3 ? er->ExceptionInformation[2] : 0;
    BootLog("*** Init SEH code=0x%08X addr=0x%p N=%lu i0=0x%p i1=0x%p i2=0x%p lastStage=%s",
            code, addr, static_cast<unsigned long>(er->NumberParameters),
            reinterpret_cast<void*>(i0), reinterpret_cast<void*>(i1), reinterpret_cast<void*>(i2),
            BootLog_LastStage());
    if (ctx) {
        BootLog("*** Init SEH EIP=0x%08X ESP=0x%08X EBP=0x%08X", ctx->Eip, ctx->Esp, ctx->Ebp);
    }
    if (i0) {
        __try {
            const BYTE* p = reinterpret_cast<const BYTE*>(i0);
            BootLog("*** exception object @0x%p : %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X",
                    reinterpret_cast<const void*>(p),
                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                    p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15],
                    p[16], p[17], p[18], p[19]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            BootLog("*** exception object @0x%p <unreadable>", reinterpret_cast<const void*>(i0));
        }
    }
    if (ctx) {
        // Walk ESP return-address chain via module-mapped dwords (throw frames
        // inside Init survive the __except wrapper truncation in this dump).
        BootLog("*** Init stack-return chain (ESP walk):");
        const DWORD* sp = reinterpret_cast<const DWORD*>(ctx->Esp);
        int logged = 0;
        for (int i = 0; i < 220 && logged < 140; ++i) {
            DWORD v = 0;
            __try {
                v = sp[i];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                break;
            }
            HMODULE mod = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(static_cast<DWORD_PTR>(v)), &mod) && mod) {
                const DWORD rva = static_cast<DWORD>(static_cast<DWORD_PTR>(v) - reinterpret_cast<DWORD_PTR>(mod));
                char path[MAX_PATH]{};
                GetModuleFileNameA(mod, path, MAX_PATH);
                const char* name = strrchr(path, '\\');
                name = name ? name + 1 : path;
                BootLog("***   +%04X 0x%08X RVA=0x%X %s", i * 4, v, rva, name);
                ++logged;
            }
        }
    }
    void* frames[32]{};
    const USHORT n = CaptureStackBackTrace(1, 32, frames, nullptr);
    BootLog("*** Init throw backtrace (%u frames):", n);
    for (USHORT i = 0; i < n; ++i) {
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &mod) && mod) {
            const DWORD rva = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(frames[i]) - reinterpret_cast<DWORD_PTR>(mod));
            char path[MAX_PATH]{};
            GetModuleFileNameA(mod, path, MAX_PATH);
            const char* name = strrchr(path, '\\');
            name = name ? name + 1 : path;
            BootLog("***   [%02u] 0x%p RVA=0x%X %s", i, frames[i], rva, name);
        } else {
            BootLog("***   [%02u] 0x%p", i, frames[i]);
        }
    }
}

static int Filter_InitSeh(EXCEPTION_POINTERS* ep) {
    LogInitSeh(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

// --- static-CRT _CxxThrowException (0xA60BB7) probe ---------------------------
// The exe links its own CRT; the throw inside CWvsApp::Init's first phase is a
// std::out_of_range ("invalid string position") via this helper. Log the real
// caller chain inside Init before unwinding destroys it.
typedef void(__cdecl* CxxThrow_t)(void* pObj, void* pThrowInfo);
static auto g_CxxThrow = reinterpret_cast<CxxThrow_t>(0x00A60BB7);

static void __cdecl Hook_CxxThrow(void* pObj, void* pThrowInfo) {
    BootLog("*** _CxxThrowException obj=0x%p throwInfo=0x%p stage=%s",
            pObj, pThrowInfo, BootLog_LastStage());
    void* frames[24]{};
    const USHORT n = CaptureStackBackTrace(1, 24, frames, nullptr);
    BootLog("*** throw real callers (%u):", n);
    for (USHORT i = 0; i < n && i < 14; ++i) {
        HMODULE mod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &mod) && mod) {
            const DWORD rva = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(frames[i]) - reinterpret_cast<DWORD_PTR>(mod));
            char path[MAX_PATH]{};
            GetModuleFileNameA(mod, path, MAX_PATH);
            const char* name = strrchr(path, '\\');
            name = name ? name + 1 : path;
            BootLog("***   [%02u] 0x%p RVA=0x%X %s", i, frames[i], rva, name);
        } else {
            BootLog("***   [%02u] 0x%p", i, frames[i]);
        }
    }
    g_CxxThrow(pObj, pThrowInfo);
}

static int __fastcall Hook_CWvsAppInit(void* pThis, void* edx) {
    BootLogStage("CWvsApp::Init BEGIN");
    int r = 0;
    __try {
        r = g_CWvsAppInit(pThis, edx);
        BootLogStage("CWvsApp::Init END");
        BootLog("CWvsApp::Init returned %d", r);
        // Path-spy only after ResMan/logo path is healthy (never FsInit/DllMain).
        // Soft-fail inside spy is gated OFF — VT_EMPTY Growth tip → login 0x80004003
        // (client_boot 2026-08-13 13:45: SOFTFAIL then throw_hr E_POINTER).
        AttachGetObjectAPathSpy("post-CWvsApp::Init");
        AttachMapEnterNullGuard();
        // ComRaise diag also deferred: historical early ComRaise → 0x80004003 at
        // logo/login; path-spy + throw_hr hooks already capture enter 0x8007000D.
        // CrashDiag_AttachComRaiseDiag();
        BootLog("ComRaise diag DISABLED post-Init (login E_POINTER triage)");
        // Stop flush-heavy boot tracing once Init completes — in-game CXX EH
        // + set_stage/Mount hooks were stuttering the client.
        BootLog("BootLog quiet ON (runtime logging disabled; crash VEH kept)");
        BootLog_SetQuiet(true);
    } __except (Filter_InitSeh(GetExceptionInformation())) {
        BootLog("*** CWvsApp::Init SEH code=0x%08X lastStage=%s",
                GetExceptionCode(), BootLog_LastStage());
        // WinMain still calls CWvsApp::Run (0x9F5C50) after a swallowed Init
        // failure → CInputSystem [BEC33C]==NULL → AV @59B2D9. Soft-fail exit.
        BootLog("*** Init failed — ExitProcess(1) to avoid null-Input Run AV");
        ExitProcess(1);
    }
    return r;
}

// --- InitializeResMan (0x9F7159) cdecl --------------------------------------

typedef int(__cdecl* InitResMan_t)();
static auto g_InitResMan = reinterpret_cast<InitResMan_t>(0x009F7159);

// Path-only GetObjectA spy — NEVER in DllMain / FsInit / InitGr2D.
// Attach only from Hook_CWvsAppInit after Init returns (post-logo-safe).
constexpr uintptr_t kVa_IWzResMan_GetObjectA = 0x00403A93;
// 4th arg = Ztl_bstr_t by value = Data_t* (first field BSTR m_wstr).
typedef VARIANT*(__fastcall* IWzResMan_GetObjectA_t)(
    void* pThis, void* edx, VARIANT* pResult, void* pZtlBstr, void* pParam, void* pAux);
static IWzResMan_GetObjectA_t g_GetObjectASpy =
    reinterpret_cast<IWzResMan_GetObjectA_t>(kVa_IWzResMan_GetObjectA);
static bool g_getObjectASpyOn = false;

static bool LooksLikeWzPath(const wchar_t* p) {
    if (!p) {
        return false;
    }
    __try {
        const wchar_t c0 = p[0];
        if (!c0 || c0 > 0x7FF) {
            return false;
        }
        for (int i = 0; i < 96 && p[i]; ++i) {
            if (p[i] == L'/' || p[i] == L'\\' || p[i] == L'.') {
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

static const wchar_t* ResolveGetObjectAPath(void* pZtlBstr) {
    if (!pZtlBstr) {
        return nullptr;
    }
    __try {
        // 1) Ztl_bstr_t by value → Data_t*; first field BSTR m_wstr
        const wchar_t* p = *reinterpret_cast<const wchar_t* const*>(pZtlBstr);
        if (LooksLikeWzPath(p)) {
            return p;
        }
        // 2) Direct BSTR / wchar_t*
        p = reinterpret_cast<const wchar_t*>(pZtlBstr);
        if (LooksLikeWzPath(p)) {
            return p;
        }
        // 3) Ztl_bstr_t*: *arg = Data_t*, then m_wstr
        void* data = *reinterpret_cast<void* const*>(pZtlBstr);
        if (data) {
            p = *reinterpret_cast<const wchar_t* const*>(data);
            if (LooksLikeWzPath(p)) {
                return p;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return nullptr;
}

static void NoteGetObjectAPath(void* pZtlBstr) {
    const wchar_t* path = ResolveGetObjectAPath(pZtlBstr);
    if (path) {
        CrashDiag_NoteWzPath(path);
    }
}

// Growth tip canvases throw 0x8007000D (ERROR_INVALID_DATA) on select→enter
// (bad GrowthEnabled/Disabled PNG/node). Workarounds that BOUNCE login↔enter:
//   SoftFail VT_EMPTY → login 0x80004003 E_POINTER (2026-08-13 13:45)
//   PATH-REWRITE Growth→Can/Cannot with stack wchar_t as fake BSTR → login
//     0x80030002 STG_E_FILENOTFOUND (2026-08-13 13:59; suspect_last=Cannot)
// NEVER SoftFail / NEVER rewrite here. Path-spy is note-only. Enter fix =
// MCP-repair Growth nodes from proven-good OR IDA tip-ctor nullguard (no
// global GetObject mutation). Keep kEnableGrowthPathRewrite=false.
constexpr bool kEnableGrowthPathRewrite = false;

static bool IsGrowthTipEquipPath(const wchar_t* path) {
    if (!path) {
        return false;
    }
    __try {
        // Exact UOL suffix only — avoid substring hits on unrelated names.
        return (wcscmp(path, L"UI/UIWindow.img/ToolTip/Equip/GrowthEnabled") == 0) ||
               (wcscmp(path, L"UI/UIWindow.img/ToolTip/Equip/GrowthDisabled") == 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Stage-gated + SysAllocString rewrite (DISABLED). Do not re-enable without
// login smoke first; stack wchar_t* is NOT a BSTR (length prefix missing →
// 0x80030002). Only consider during set_stage/enter, never world-select.
static bool RewriteGrowthTipPathToSibling(const wchar_t* path, wchar_t* outBuf,
                                          size_t outCch) {
    if (!kEnableGrowthPathRewrite || !path || !outBuf || outCch < 8) {
        return false;
    }
    const char* stage = BootLog_LastStage();
    if (!stage || !strstr(stage, "set_stage")) {
        return false; // login / world-select must never rewrite
    }
    __try {
        if (!IsGrowthTipEquipPath(path)) {
            return false;
        }
        if (wcscmp(path, L"UI/UIWindow.img/ToolTip/Equip/GrowthEnabled") == 0) {
            wcsncpy_s(outBuf, outCch, L"UI/UIWindow.img/ToolTip/Equip/Can", _TRUNCATE);
            return true;
        }
        if (wcscmp(path, L"UI/UIWindow.img/ToolTip/Equip/GrowthDisabled") == 0) {
            wcsncpy_s(outBuf, outCch, L"UI/UIWindow.img/ToolTip/Equip/Cannot", _TRUNCATE);
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

static VARIANT* __fastcall Hook_GetObjectAPathSpy(
    void* pThis, void* edx, VARIANT* pResult, void* pZtlBstr, void* pParam, void* pAux) {
    // Path noted BEFORE call so throw_hr RecentWZ ends on the failing GetObjectA
    // (nested UOL may still not appear — suspect_last in dump highlights ring tail).
    const wchar_t* path = ResolveGetObjectAPath(pZtlBstr);
    if (path) {
        CrashDiag_NoteWzPath(path);
        CrashDiag_NoteGetObjectInFlight(path);
        if (IsGrowthTipEquipPath(path)) {
            const bool wasQuiet = BootLog_IsQuiet();
            if (wasQuiet) {
                BootLog_SetQuiet(false);
            }
            char narrow[256]{};
            WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow) - 1,
                                nullptr, nullptr);
            BootLog("*** GetObjectA growth-tip (rewrite OFF) path=%s", narrow);
            if (wasQuiet) {
                BootLog_SetQuiet(true);
            }
        }
    } else {
        NoteGetObjectAPath(pZtlBstr);
    }

    // Rewrite stays OFF (kEnableGrowthPathRewrite=false). If ever re-enabled,
    // MUST SysAllocString the rewritten UOL into a real ZtlBstrData — never
    // point m_wstr at a stack wchar_t[] (that caused login 0x80030002).
    void* callBstr = pZtlBstr;
    const wchar_t* notePath = path;
    BSTR rewriteBstr = nullptr;
    ZtlBstrData rewriteData{};
    wchar_t rewriteBuf[512]{};
    if (path && RewriteGrowthTipPathToSibling(path, rewriteBuf, 512)) {
        rewriteBstr = SysAllocString(rewriteBuf);
        if (rewriteBstr) {
            rewriteData.wstr = rewriteBstr;
            rewriteData.str = nullptr;
            rewriteData.refCount = 1;
            callBstr = &rewriteData;
            notePath = rewriteBuf;
            const bool wasQuiet = BootLog_IsQuiet();
            if (wasQuiet) {
                BootLog_SetQuiet(false);
            }
            char narrowFrom[256]{};
            char narrowTo[256]{};
            WideCharToMultiByte(CP_UTF8, 0, path, -1, narrowFrom, sizeof(narrowFrom) - 1,
                                nullptr, nullptr);
            WideCharToMultiByte(CP_UTF8, 0, rewriteBuf, -1, narrowTo, sizeof(narrowTo) - 1,
                                nullptr, nullptr);
            BootLog("*** GetObjectA growth-tip PATH-REWRITE %s -> %s", narrowFrom, narrowTo);
            if (wasQuiet) {
                BootLog_SetQuiet(true);
            }
            CrashDiag_NoteWzPath(rewriteBuf);
        }
    }

    VARIANT* ret = g_GetObjectASpy(pThis, edx, pResult, callBstr, pParam, pAux);
    if (rewriteBstr) {
        SysFreeString(rewriteBstr);
    }
    // Non-throw failure: VT_ERROR / DISP_E_* in result — record as getobj_fail.
    if (notePath && pResult) {
        __try {
            const VARTYPE vt = V_VT(pResult);
            if (vt == VT_ERROR) {
                const HRESULT ehr = V_ERROR(pResult);
                if (FAILED(ehr)) {
                    CrashDiag_NoteGetObjectFail(notePath, ehr);
                    const bool wasQuiet = BootLog_IsQuiet();
                    if (wasQuiet) {
                        BootLog_SetQuiet(false);
                    }
                    char narrow[512]{};
                    WideCharToMultiByte(CP_UTF8, 0, notePath, -1, narrow, sizeof(narrow) - 1,
                                        nullptr, nullptr);
                    BootLog("*** GetObjectA FAIL VT_ERROR path=%s hr=0x%08X",
                            narrow, static_cast<unsigned>(ehr));
                    if (wasQuiet) {
                        BootLog_SetQuiet(true);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    CrashDiag_ClearGetObjectInFlight();
    return ret;
}

static void AttachGetObjectAPathSpy(const char* why) {
    if (g_getObjectASpyOn) {
        return;
    }
    const bool ok = Memory::SetHook(true,
                                    reinterpret_cast<void**>(&g_GetObjectASpy),
                                    reinterpret_cast<void*>(&Hook_GetObjectAPathSpy));
    g_getObjectASpyOn = ok;
    BootLog("GetObjectA path-spy %s @%p via %s", ok ? "OK" : "FAIL",
            reinterpret_cast<void*>(kVa_IWzResMan_GetObjectA), why ? why : "?");
    // NEVER attach ComRaise/CrashDiag GetObjectA here or in DllMain:
    // early ComRaise → 0x80004003「无效的指针」; combined with Level300 rebuild
    // also reproduced 0xC00000FD stack overflow before logo (2026-08-13).
}

static int __cdecl Hook_InitResMan() {
    BootLogStage("InitializeResMan BEGIN");
    const int r = g_InitResMan();
    BootLogStage("InitializeResMan END");
    BootLog("InitializeResMan returned %d", r);
    // NEVER AttachGetObjectAPathSpy here — mid-Init ResMan hook races logo
    // set_stage and has reproduced 0xC0000005 @ EIP=0 / black screen (2026-08-13).
    // Path-spy attaches only from Hook_CWvsAppInit after Init returns.
    return r;
}

// --- InitializeGr2D / Input / Sound (best-effort labels) --------------------

typedef int(__fastcall* Thiscall0_t)(void* pThis, void* edx);

static auto g_InitGr2D = reinterpret_cast<Thiscall0_t>(0x009F7A3B);
static int __fastcall Hook_InitGr2D(void* pThis, void* edx) {
    BootLogStage("InitializeGr2D BEGIN");
    // Path-spy deferred until after CWvsApp::Init (FsInit/Gr2D attach raced
    // with ResMan bring-up on some builds → boot stack overflow).
    // BF0CC0 = PcCreateObject entry; BF14EC = IWzGr2D*; BEC33C = CInputSystem*
    BootLog("Gr2D pre BF0CC0=%p BF14EC=%p BEC33C=%p",
            *reinterpret_cast<void**>(0x00BF0CC0),
            *reinterpret_cast<void**>(0x00BF14EC),
            *reinterpret_cast<void**>(0x00BEC33C));
    const int r = g_InitGr2D(pThis, edx);
    BootLog("Gr2D post BF14EC=%p BEC33C=%p rc=%d",
            *reinterpret_cast<void**>(0x00BF14EC),
            *reinterpret_cast<void**>(0x00BEC33C), r);
    if (r == 0) {
        // IWzGr2D* is valid only after InitializeGr2D; DllMain RefreshRate caused E_FAIL.
        static bool refreshRateInstalled = false;
        if (!refreshRateInstalled) {
            refreshRateInstalled = true;
            Client::RefreshRate();
        }
    }
    BootLogStage("InitializeGr2D END");
    return r;
}

// CInputSystem method @59B2D2 — null singleton → AV read [ecx+8]. Soft-fail.
typedef int(__fastcall* InputPoll_t)(void* pThis, void* edx, void* a2);
static auto g_InputPoll = reinterpret_cast<InputPoll_t>(0x0059B2D2);
static int __fastcall Hook_InputPoll(void* pThis, void* edx, void* a2) {
    if (!pThis) {
        BootLog("*** CInputSystem@59B2D2 this=NULL (BEC33C) — return 0");
        return 0;
    }
    return g_InputPoll(pThis, edx, a2);
}

// Log COM/_com_error throws from Gr2D path (A5FDE4 / A5FDF2 → A605C3).
typedef void(__stdcall* ThrowHr_t)(int hr);
static auto g_ThrowHr = reinterpret_cast<ThrowHr_t>(0x00A5FDE4);
static void __stdcall Hook_ThrowHr(int hr) {
    // Always log COM failures (even after BootLog quiet) — needed for 0x80030002 channel enter.
    const bool wasQuiet = BootLog_IsQuiet();
    if (wasQuiet) {
        BootLog_SetQuiet(false);
    }
    BootLog("*** throw_hr A5FDE4 hr=0x%08X stage=%s",
            static_cast<unsigned>(hr), BootLog_LastStage());
    if (wasQuiet) {
        BootLog_SetQuiet(true);
    }
    g_ThrowHr(hr);
}

typedef void(__stdcall* ThrowHrObj_t)(int hr, int obj, int a3);
static auto g_ThrowHrObj = reinterpret_cast<ThrowHrObj_t>(0x00A5FDF2);

static void LogThrowHrStack(const char* tag, int hr, void* obj) {
    const bool wasQuiet = BootLog_IsQuiet();
    if (wasQuiet) {
        BootLog_SetQuiet(false);
    }
    BootLog("*** %s hr=0x%08X obj=%p stage=%s",
            tag, static_cast<unsigned>(hr), obj, BootLog_LastStage());
    // 0x8007000D = ERROR_INVALID_DATA (bad PNG/UOL) — same need for RecentWZ
    // as STG_E_FILENOTFOUND; enter-map crashes often surface as this HR.
    // 0x80070026 = ERROR_HANDLE_EOF ("38 已到文件尾") — truncated/missing
    // canvas inside an existing .img (e.g. Android heart without icon).
    if (static_cast<unsigned>(hr) == 0x80030002u ||
        static_cast<unsigned>(hr) == 0x80004003u ||
        static_cast<unsigned>(hr) == 0x8007000Du ||
        static_cast<unsigned>(hr) == 0x80070026u) {
        DWORD gle = 0;
        if (CrashDiag_DumpRecentWzPaths(tag, &gle)) {
            BootLog("***   dumped beidou-wz-last.log (RecentWZ ring) OK");
        } else {
            BootLog("***   dumped beidou-wz-last.log FAILED gle=0x%08X",
                    static_cast<unsigned>(gle));
        }
    }
    void* frames[12]{};
    const USHORT n = CaptureStackBackTrace(1, 12, frames, nullptr);
    HMODULE exe = GetModuleHandleA(nullptr);
    HMODULE ijl = GetModuleHandleA("ijl15.dll");
    for (USHORT i = 0; i < n; ++i) {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(frames[i]), &mod) ||
            !mod) {
            BootLog("***   [%02u] %p", i, frames[i]);
            continue;
        }
        const DWORD rva = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(frames[i]) -
                                             reinterpret_cast<DWORD_PTR>(mod));
        const char* name = (mod == exe) ? "BeiDou.exe" : (mod == ijl) ? "ijl15.dll" : "other";
        BootLog("***   [%02u] %s+0x%X", i, name, rva);
    }
    // A5FDF2 = _com_issue_errorex(hr, IUnknown* punk, REFIID) — obj is NOT IErrorInfo.
    // Real UOL lives on the thread IErrorInfo (SetErrorInfo before raise). GetErrorInfo
    // consumes it — restore via SetErrorInfo so the original raise still works.
    IErrorInfo* threadInfo = nullptr;
    BSTR desc = nullptr;
    BSTR src = nullptr;
    bool gotDesc = false;
    HRESULT geHr = E_FAIL;
    __try {
        geHr = GetErrorInfo(0, &threadInfo);
        BootLog("***   GetErrorInfo hr=0x%08X pei=%p", static_cast<unsigned>(geHr),
                threadInfo);
        if (SUCCEEDED(geHr) && threadInfo) {
            HRESULT gd = threadInfo->GetDescription(&desc);
            BootLog("***   IErrorInfo.GetDescription hr=0x%08X", static_cast<unsigned>(gd));
            if (SUCCEEDED(gd) && desc && desc[0]) {
                gotDesc = true;
            }
            if (SUCCEEDED(threadInfo->GetSource(&src)) && src && src[0]) {
                char sn[384]{};
                WideCharToMultiByte(CP_UTF8, 0, src, -1, sn, sizeof(sn) - 1, nullptr, nullptr);
                BootLog("***   IErrorInfo.Source: %s", sn);
            }
            // Restore for _com_issue_errorex / _com_raise_error.
            SetErrorInfo(0, threadInfo);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BootLog("***   GetErrorInfo SEH");
    }
    if (gotDesc && desc) {
        char narrow[768]{};
        WideCharToMultiByte(CP_UTF8, 0, desc, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
        BootLog("***   IErrorInfo.Description: %s", narrow);
        CrashDiag_NoteFailDescription(desc);
    } else {
        BootLog("***   IErrorInfo.Description: (none) punk=%p", obj);
        // Synthesize fail_desc from last FAILED / in-flight GetObjectA (path-spy).
        CrashDiag_EnsureFailDescFromGetObjectSpy();
    }
    if (desc) {
        SysFreeString(desc);
        desc = nullptr;
    }
    if (src) {
        SysFreeString(src);
        src = nullptr;
    }
    if (threadInfo) {
        threadInfo->Release();
        threadInfo = nullptr;
    }
    // Re-dump so beidou-wz-last gets fail_desc=/suspect_fail=/getobj_* fields.
    if (static_cast<unsigned>(hr) == 0x80030002u ||
        static_cast<unsigned>(hr) == 0x80004003u ||
        static_cast<unsigned>(hr) == 0x8007000Du ||
        static_cast<unsigned>(hr) == 0x80070026u) {
        DWORD gle = 0;
        if (CrashDiag_DumpRecentWzPaths(tag, &gle)) {
            BootLog("***   re-dumped beidou-wz-last with fail_desc/getobj spy OK");
        } else {
            BootLog("***   re-dumped beidou-wz-last FAILED gle=0x%08X",
                    static_cast<unsigned>(gle));
        }
    }
    if (obj) {
        __try {
            const unsigned* dw = reinterpret_cast<const unsigned*>(obj);
            BootLog("***   punk_dwords %08X %08X %08X %08X", dw[0], dw[1], dw[2], dw[3]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (wasQuiet) {
        BootLog_SetQuiet(true);
    }
}

static void __stdcall Hook_ThrowHrObj(int hr, int obj, int a3) {
    // Signature proven from EXE bytes @A5FDF2: _com_issue_errorex(hr, punk, riid).
    LogThrowHrStack("throw_hr A5FDF2", hr, reinterpret_cast<void*>(obj));
    g_ThrowHrObj(hr, obj, a3);
}

static auto g_InitInput = reinterpret_cast<Thiscall0_t>(0x009F7CE1);
static int __fastcall Hook_InitInput(void* pThis, void* edx) {
    BootLogStage("InitializeInput BEGIN");
    const int r = g_InitInput(pThis, edx);
    BootLogStage("InitializeInput END");
    return r;
}

static auto g_InitSound = reinterpret_cast<Thiscall0_t>(0x009F82BC);
static int __fastcall Hook_InitSound(void* pThis, void* edx) {
    BootLogStage("InitializeSound BEGIN");
    const int r = g_InitSound(pThis, edx);
    BootLogStage("InitializeSound END");
    return r;
}

// --- InitializeGameData (0x9F8B61) cdecl ------------------------------------

typedef int(__cdecl* InitGameData_t)();
static auto g_InitGameData = reinterpret_cast<InitGameData_t>(0x009F8B61);

static int __cdecl Hook_InitGameData() {
    BootLogStage("InitializeGameData BEGIN");
    const int r = g_InitGameData();
    BootLogStage("InitializeGameData END");
    BootLog("InitializeGameData returned %d", r);
    return r;
}

// --- GameData loaders (checked sites) ---------------------------------------

typedef int(__fastcall* LoaderThis_t)(void* pThis, void* edx);
typedef int(__cdecl* LoaderCdecl_t)();

#define HOOK_LOADER_THIS(addr, label, var, hookname) \
    static auto var = reinterpret_cast<LoaderThis_t>(addr); \
    static int __fastcall hookname(void* pThis, void* edx) { \
        BootLogStage(label " BEGIN"); \
        const int r = var(pThis, edx); \
        BootLog("%s END ok=%d", label, r); \
        BootLogStage(label " END"); \
        return r; \
    }

#define HOOK_LOADER_CDECL(addr, label, var, hookname) \
    static auto var = reinterpret_cast<LoaderCdecl_t>(addr); \
    static int __cdecl hookname() { \
        BootLogStage(label " BEGIN"); \
        const int r = var(); \
        BootLog("%s END ok=%d", label, r); \
        BootLogStage(label " END"); \
        return r; \
    }

HOOK_LOADER_THIS(0x005CA71C, "GameData/ItemInfo", g_ItemInfo, Hook_ItemInfo)
HOOK_LOADER_THIS(0x005E6A24, "GameData/ItemMake", g_ItemMake, Hook_ItemMake)
HOOK_LOADER_CDECL(0x006883CF, "GameData/Mid_6883CF", g_Mid2, Hook_Mid2)
HOOK_LOADER_CDECL(0x007AF814, "GameData/Mid_7AF814", g_Mid3, Hook_Mid3)
HOOK_LOADER_CDECL(0x0070A9C3, "GameData/Mid_70A9C3", g_Mid5, Hook_Mid5)
HOOK_LOADER_CDECL(0x00513194, "GameData/Mid_513194", g_Mid7, Hook_Mid7)
HOOK_LOADER_THIS(0x0075C060, "GameData/Loader_75C060", g_L3, Hook_L3)
HOOK_LOADER_THIS(0x00761A2D, "GameData/ItemSkill_761A2D", g_L4, Hook_L4)
HOOK_LOADER_THIS(0x0076333D, "GameData/BFSkill_76333D", g_L5, Hook_L5)
HOOK_LOADER_THIS(0x00763815, "GameData/MCGuardian_763815", g_L6, Hook_L6)
HOOK_LOADER_THIS(0x00761DDB, "GameData/ItemSkill_761DDB", g_L7, Hook_L7)
HOOK_LOADER_CDECL(0x00792D21, "GameData/Post_792D21", g_Post1, Hook_Post1)
HOOK_LOADER_CDECL(0x009FA025, "GameData/Post_9FA025", g_Post2, Hook_Post2)

// Post-InitializeGameData checks inside CWvsApp::Init (flash-exit 0x22000006)
HOOK_LOADER_THIS(0x009F7034, "PostGD/9F7034", g_PostGd1, Hook_PostGd1)
HOOK_LOADER_THIS(0x00636F4E, "PostGD/StringPool_636F4E", g_PostGd2, Hook_PostGd2)
HOOK_LOADER_THIS(0x0071D8DF, "PostGD/QuestCategory", g_QuestCat, Hook_QuestCat)
// MorphPack can COM-throw 0x800401F8 mid-load (GameDataGuard only softens false return).
// Soft-skip so CWvsApp::Init can reach login; morph skills may be incomplete until data fixed.
static auto g_MorphPack = reinterpret_cast<LoaderThis_t>(0x0068487C);
static int __fastcall Hook_MorphPack(void* pThis, void* edx) {
    BootLogStage("PostGD/MorphPack BEGIN");
    int r = 0;
    __try {
        r = g_MorphPack(pThis, edx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BootLog("*** MorphPack SEH 0x%08X — soft skip (continue boot)", GetExceptionCode());
        r = 1;
    }
    BootLog("PostGD/MorphPack END ok=%d", r);
    BootLogStage("PostGD/MorphPack END");
    return r;
}

HOOK_LOADER_CDECL(0x009FA078, "PostGD/9FA078", g_PostGd3, Hook_PostGd3)

// Miles Sound System quick-start (./redist + AIL_quick_startup).
// Even with a complete ./redist + matching mss32.dll, AIL_quick_startup still
// ExitProcess(0) on this host (SEH cannot catch). Stub the helper; main game
// audio continues via InitializeSound / Sound_DX8.dll.
typedef int(__cdecl* AilQuickInit_t)();
static auto g_AilQuickInit = reinterpret_cast<AilQuickInit_t>(0x00730252);

static bool HasMilesRedist() {
    return GetFileAttributesA("redist\\mssmp3.asi") != INVALID_FILE_ATTRIBUTES
        && GetFileAttributesA("redist\\mssvoice.asi") != INVALID_FILE_ATTRIBUTES;
}

static int __cdecl Hook_AilQuickInit() {
    BootLogStage("AIL_quick_startup BEGIN");
    if (HasMilesRedist()) {
        BootLog("AIL_quick_startup STUB skip (./redist present but Miles quick API ExitProcess)");
    } else {
        BootLog("AIL_quick_startup STUB skip (missing ./redist/mssmp3.asi)");
    }
    (void)g_AilQuickInit; // keep symbol for address documentation
    BootLogStage("AIL_quick_startup END");
    return 0; // caller sub_72FC30 ignores rc
}

// CWvsContext-ish hwnd/sound handle read used by sound mgr ctor. Null = AV.
typedef int(__cdecl* GetSoundHwnd_t)();
static auto g_GetSoundHwnd = reinterpret_cast<GetSoundHwnd_t>(0x00987257);
static int __cdecl Hook_GetSoundHwnd() {
    __try {
        const int* p = *reinterpret_cast<int**>(0x00BE7B38);
        if (!p) {
            BootLog("*** sound hwnd source BE7B38 is NULL — return 0");
            return 0;
        }
        return g_GetSoundHwnd();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BootLog("*** GetSoundHwnd SEH code=0x%08X — return 0", GetExceptionCode());
        return 0;
    }
}

// CLogo ctor + set_stage (logo → login transition)
typedef void*(__fastcall* LogoCtor_t)(void* pThis, void* edx);
static auto g_LogoCtor = reinterpret_cast<LogoCtor_t>(0x0062ECE2);
static void* __fastcall Hook_LogoCtor(void* pThis, void* edx) {
    BootLogStage("Logo/CLogo_ctor BEGIN");
    void* r = g_LogoCtor(pThis, edx);
    BootLog("Logo/CLogo_ctor END this=%p", r);
    BootLogStage("Logo/CLogo_ctor END");
    return r;
}

typedef void(__cdecl* SetStage_t)(void* pStage, void* pParam);
static auto g_SetStage = reinterpret_cast<SetStage_t>(0x00777347);
static void __cdecl Hook_SetStage(void* pStage, void* pParam) {
    char buf[96]{};
    sprintf_s(buf, "set_stage stage=%p param=%p", pStage, pParam);
    BootLogStage(buf);
    __try {
        g_SetStage(pStage, pParam);
        BootLog("set_stage END ok stage=%p", pStage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BootLog("*** set_stage SEH code=0x%08X stage=%p", GetExceptionCode(), pStage);
    }
    BootLogStage("set_stage END");
}

#undef HOOK_LOADER_THIS
#undef HOOK_LOADER_CDECL

// --- WZ package name (DISABLED) ----------------------------------------------
// 0x0051B0A9 is ZXString ctor/assign: __thiscall(this, src, n), NOT cdecl(name,n).
// LoadTrace previously hooked it as __cdecl → ECX(this) lost → *this=0 AV.
// Repro: open NPC talk (e.g. Henesys salon) → crash at BeiDou.exe+0x11B0B4
// with BootLog stage "ResMan/WZ ....wz". Do NOT re-attach without correct CC.

// --- IWzFileSystem::Init / IWzNameSpace::Mount (path breadcrumbs) ------------
// Already hooked in AutoTypes; we chain-log via fresh detours on the ORIGINAL
// addresses. Detours replaces the first hook target pointer — call through the
// current g_* which points at the prior trampoline after SetHook.

typedef HRESULT(__fastcall* FsInit_t)(void* pThis, void* edx, void* sPath);
static auto g_FsInit = reinterpret_cast<FsInit_t>(0x009F7964);

static HRESULT __fastcall Hook_FsInit(void* pThis, void* edx, void* sPath) {
    BootLogStage("IWzFileSystem::Init");
    LogZtlBstr("IWzFileSystem::Init path", sPath);
    const HRESULT hr = g_FsInit(pThis, edx, sPath);
    BootLog("IWzFileSystem::Init hr=0x%08X", static_cast<unsigned>(hr));
    // Do not AttachGetObjectAPathSpy from FsInit (too early / boot-unsafe).
    return hr;
}

typedef HRESULT(__fastcall* NsMount_t)(void* pThis, void* edx, void* sPath, void* pDown, int nPriority);
static auto g_NsMount = reinterpret_cast<NsMount_t>(0x009F790A);

static HRESULT __fastcall Hook_NsMount(void* pThis, void* edx, void* sPath, void* pDown, int nPriority) {
    LogZtlBstr("IWzNameSpace::Mount path", sPath);
    BootLog("IWzNameSpace::Mount priority=%d pDown=%p", nPriority, pDown);
    const HRESULT hr = g_NsMount(pThis, edx, sPath, pDown, nPriority);
    BootLog("IWzNameSpace::Mount hr=0x%08X", static_cast<unsigned>(hr));
    return hr;
}

} // namespace

void AttachLoadTrace() {
    BootLogStage("AttachLoadTrace BEGIN");

    Attach(&g_CWvsAppInit, &Hook_CWvsAppInit, "CWvsApp::Init");
    Attach(&g_CxxThrow, &Hook_CxxThrow, "_CxxThrowException");
    Attach(&g_InitResMan, &Hook_InitResMan, "InitializeResMan");
    Attach(&g_InitGr2D, &Hook_InitGr2D, "InitializeGr2D");
    Attach(&g_InitInput, &Hook_InitInput, "InitializeInput");
    Attach(&g_InitSound, &Hook_InitSound, "InitializeSound");
    Attach(&g_InitGameData, &Hook_InitGameData, "InitializeGameData");
    Attach(&g_InputPoll, &Hook_InputPoll, "CInputSystem_59B2D2");
    Attach(&g_ThrowHr, &Hook_ThrowHr, "throw_hr_A5FDE4");
    Attach(&g_ThrowHrObj, &Hook_ThrowHrObj, "throw_hr_A5FDF2");

    Attach(&g_ItemInfo, &Hook_ItemInfo, "ItemInfo");
    Attach(&g_ItemMake, &Hook_ItemMake, "ItemMake");
    Attach(&g_Mid2, &Hook_Mid2, "Mid_6883CF");
    Attach(&g_Mid3, &Hook_Mid3, "Mid_7AF814");
    Attach(&g_Mid5, &Hook_Mid5, "Mid_70A9C3");
    Attach(&g_Mid7, &Hook_Mid7, "Mid_513194");
    Attach(&g_L3, &Hook_L3, "Loader_75C060");
    Attach(&g_L4, &Hook_L4, "ItemSkill_761A2D");
    Attach(&g_L5, &Hook_L5, "BFSkill_76333D");
    Attach(&g_L6, &Hook_L6, "MCGuardian_763815");
    Attach(&g_L7, &Hook_L7, "ItemSkill_761DDB");
    Attach(&g_Post1, &Hook_Post1, "Post_792D21");
    Attach(&g_Post2, &Hook_Post2, "Post_9FA025");

    Attach(&g_PostGd1, &Hook_PostGd1, "PostGD_9F7034");
    Attach(&g_PostGd2, &Hook_PostGd2, "PostGD_636F4E");
    Attach(&g_QuestCat, &Hook_QuestCat, "QuestCategory");
    Attach(&g_MorphPack, &Hook_MorphPack, "MorphPack");
    Attach(&g_PostGd3, &Hook_PostGd3, "PostGD_9FA078");
    Attach(&g_AilQuickInit, &Hook_AilQuickInit, "AIL_quick_startup");
    Attach(&g_GetSoundHwnd, &Hook_GetSoundHwnd, "GetSoundHwnd");
    Attach(&g_LogoCtor, &Hook_LogoCtor, "CLogo_ctor");
    Attach(&g_SetStage, &Hook_SetStage, "set_stage");

    // WzNameHelper intentionally NOT hooked (wrong CC crashed NPC dialogue).

    // Note: dllmain already hooks FsInit/Mount via AutoTypes. Re-hooking chains
    // through the existing trampoline so both still run.
    Attach(&g_FsInit, &Hook_FsInit, "IWzFileSystem::Init");
    Attach(&g_NsMount, &Hook_NsMount, "IWzNameSpace::Mount");

    BootLogStage("AttachLoadTrace END");
}
