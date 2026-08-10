#include "../stdafx.h"
#include "BootLog.h"
#include "LoadTraceApi.h"
#include "../Memory.h"

#include <cstring>

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

static int __cdecl Hook_InitResMan() {
    BootLogStage("InitializeResMan BEGIN");
    const int r = g_InitResMan();
    BootLogStage("InitializeResMan END");
    BootLog("InitializeResMan returned %d", r);
    return r;
}

// --- InitializeGr2D / Input / Sound (best-effort labels) --------------------

typedef int(__fastcall* Thiscall0_t)(void* pThis, void* edx);

static auto g_InitGr2D = reinterpret_cast<Thiscall0_t>(0x009F7A3B);
static int __fastcall Hook_InitGr2D(void* pThis, void* edx) {
    BootLogStage("InitializeGr2D BEGIN");
    // BF0CC0 = PcCreateObject entry; BF14EC = IWzGr2D*; BEC33C = CInputSystem*
    BootLog("Gr2D pre BF0CC0=%p BF14EC=%p BEC33C=%p",
            *reinterpret_cast<void**>(0x00BF0CC0),
            *reinterpret_cast<void**>(0x00BF14EC),
            *reinterpret_cast<void**>(0x00BEC33C));
    const int r = g_InitGr2D(pThis, edx);
    BootLog("Gr2D post BF14EC=%p BEC33C=%p rc=%d",
            *reinterpret_cast<void**>(0x00BF14EC),
            *reinterpret_cast<void**>(0x00BEC33C), r);
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
    BootLog("*** throw_hr A5FDE4 hr=0x%08X stage=%s",
            static_cast<unsigned>(hr), BootLog_LastStage());
    g_ThrowHr(hr);
}

typedef void(__stdcall* ThrowHrObj_t)(int hr, int obj, int a3);
static auto g_ThrowHrObj = reinterpret_cast<ThrowHrObj_t>(0x00A5FDF2);
static void __stdcall Hook_ThrowHrObj(int hr, int obj, int a3) {
    BootLog("*** throw_hr A5FDF2 hr=0x%08X obj=%p stage=%s",
            static_cast<unsigned>(hr), reinterpret_cast<void*>(obj), BootLog_LastStage());
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
HOOK_LOADER_THIS(0x0068487C, "PostGD/MorphPack", g_MorphPack, Hook_MorphPack)

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
