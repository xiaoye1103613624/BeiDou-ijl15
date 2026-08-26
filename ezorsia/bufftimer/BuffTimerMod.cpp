#include "stdafx.h"
#include "BuffTimerApi.h"
#include "INIReader.h"
#include "compat/hook.h"
#include "compat/wvs/statusbar.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include "compat/WzLib/IWzCanvas.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uintptr_t kDrawSkillCooltimeHook = 0x008E085B;
constexpr uintptr_t kDrawSkillCooltimeRet = 0x008E0990;
constexpr uintptr_t kDrawNumberByImage = 0x00988345;
constexpr uintptr_t kCWvsContextInst = 0x00BE7918;
constexpr int kCtxTempStatViewOff = 0x2EA8;
constexpr int kTempStatListHeadOff = 0x10;
constexpr int kStatusBarNumberFontOff = 0xE0;
constexpr uintptr_t kWzFontCreate = 0x0046341A;
constexpr DWORD kStartupDelayMs = 12000;
constexpr DWORD kDiagLogIntervalMs = 10000;
constexpr DWORD kDefaultBuffTimerRefreshMs = 500;
constexpr DWORD kBuffTimerRefreshMinMs = 200;
constexpr DWORD kBuffTimerRefreshMaxMs = 5000;
constexpr int kShadowCanvasSize = 32;
constexpr unsigned long kBuffTimerTextArgb = 0xFFFFE060; // RGB(255, 224, 96)
constexpr unsigned long kBuffTimerOutlineArgb = 0xFF000000;
constexpr uintptr_t kUserLocalInstanceAddr = 0x00BEBF98;

using DrawNumberByImageFn = int(__cdecl*)(void*, int, int, int, void*, int);
using WzFontCreateFn = HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);

struct TemporaryStat {
    unsigned char pad0[0x1C];
    int nType;
    int nID;
    unsigned char pad1[4];
    IWzGr2DLayerPtr pLayer;
    IWzGr2DLayerPtr pLayerShadow;
    int nIndexShadow;
    int bNoShadow;
    int tLeft;
    int tLeftUnit;
};

struct BuffShadowState {
    TemporaryStat* stat = nullptr;
    int lastPieIndex = -1;
    std::wstring lastLabel;
};

HMODULE g_module = nullptr;
bool g_hooksAttached = false;
bool g_loggedActiveTimers = false;
DWORD g_lastDiagLogTick = 0;
DWORD g_lastShadowSyncTick = 0;
FILE* g_log = nullptr;
bool g_diagLogEnabled = false;
int g_shadowRefreshLogCount = 0;
bool g_loggedSkillCooldown = false;

static IWzPropertyPtr g_pBuffNumberFont;
static IWzPropertyPtr g_pSkillNumberFont;
IWzFontPtr g_buffFontYellow;
IWzFontPtr g_buffFontBlack;
std::vector<BuffShadowState> g_buffShadowStates;

DWORD g_buffTimerRefreshMs = kDefaultBuffTimerRefreshMs;

static auto TEMPORARY_STAT__UpdateShadowIndex =
    reinterpret_cast<void(__thiscall*)(TemporaryStat*)>(0x007B44F4);
static auto CTemporaryStatView__Update =
    reinterpret_cast<void*(__thiscall*)(void*)>(0x007B2829);
static auto WzFontCreate = reinterpret_cast<WzFontCreateFn>(kWzFontCreate);

void Log(const char* fmt, ...) {
    if (!g_log) {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring logPath = exePath;
        const auto slash = logPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            logPath.resize(slash + 1);
        }
        logPath += L"bufftimer.log";
        _wfopen_s(&g_log, logPath.c_str(), L"a");
    }
    if (!g_log) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
}

uint8_t* GetTemporaryStatViewBase() {
    void* ctx = *reinterpret_cast<void**>(kCWvsContextInst);
    if (!ctx) {
        return nullptr;
    }
    return reinterpret_cast<uint8_t*>(ctx) + kCtxTempStatViewOff;
}

void AttachHooksOnce();
std::wstring FormatBuffTime(int leftMs);

bool IsDurabilityBuff(int skillId) {
    return skillId == 5221006 || skillId == 35001002;
}

bool ShouldDrawBuffTimer(const TemporaryStat* stat) {
    return stat && stat->tLeft > 0 && !IsDurabilityBuff(stat->nID);
}

int ComputePieIndex(const TemporaryStat* stat) {
    if (!stat || !stat->tLeftUnit) {
        return 0;
    }
    return zclamp(stat->tLeft / stat->tLeftUnit, 0, 15);
}

IWzPropertyPtr LoadLevelNoNumberFont(const char* logTag) {
    static const wchar_t* kPaths[] = {
        L"UI/Basic.img/LevelNo",
        L"UI/Basic.img/LevelNo/number",
    };
    for (const wchar_t* path : kPaths) {
        try {
            IWzPropertyPtr font = get_rm()->GetObjectA(const_cast<wchar_t*>(path)).GetUnknown();
            if (font) {
                Log("%s font loaded from %ls", logTag, path);
                return font;
            }
        } catch (...) {
        }
    }
    Log("%s font MISSING (tried LevelNo and LevelNo/number)", logTag);
    return nullptr;
}

static void DrawBuffNumberSafe(IWzCanvas* canvas, int left, int top, int value, void* font) {
    if (!canvas || !font || value <= 0) {
        return;
    }
    auto drawNumber = reinterpret_cast<DrawNumberByImageFn>(kDrawNumberByImage);
    __try {
        drawNumber(canvas, left, top, value, font, -1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool EnsureBuffTimerFonts() {
    if (g_buffFontYellow && g_buffFontBlack) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_buffFontYellow, nullptr);
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_buffFontBlack, nullptr);
        const Ztl_variant_t style(L"");
        if (g_buffFontYellow &&
            SUCCEEDED(WzFontCreate(g_buffFontYellow, L"Tahoma", 10, kBuffTimerTextArgb, style)) &&
            g_buffFontBlack &&
            SUCCEEDED(WzFontCreate(g_buffFontBlack, L"Tahoma", 10, kBuffTimerOutlineArgb, style))) {
            return true;
        }
    } catch (...) {
    }
    g_buffFontYellow = nullptr;
    g_buffFontBlack = nullptr;
    return false;
}

int MeasureLabelWidth(const std::wstring& label) {
    if (!EnsureBuffTimerFonts() || label.empty()) {
        return 0;
    }
    try {
        return static_cast<int>(g_buffFontYellow->CalcTextWidth(Ztl_bstr_t(label.c_str()), Ztl_variant_t()));
    } catch (...) {
        return 0;
    }
}

void DrawBuffTimeLabelOnCanvas(IWzCanvas* canvas, const std::wstring& label) {
    if (!canvas || label.empty() || !EnsureBuffTimerFonts()) {
        return;
    }

    const int textW = MeasureLabelWidth(label);
    const int x = (kShadowCanvasSize - textW) / 2;
    // Bottom-align within the 32x32 icon slot (matches prior overlay DT_BOTTOM).
    const int y = kShadowCanvasSize - 11;

    try {
        const Ztl_bstr_t bs(label.c_str());
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                canvas->DrawTextA(
                    x + dx,
                    y + dy,
                    bs,
                    g_buffFontBlack,
                    Ztl_variant_t(),
                    Ztl_variant_t());
            }
        }
        canvas->DrawTextA(x, y, bs, g_buffFontYellow, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static void CopyCanvasSafe(IWzCanvas* canvas, IWzCanvas* src) {
    if (!canvas || !src) {
        return;
    }
    try {
        canvas->Copy(0, 0, src);
    } catch (...) {
    }
}

static bool InsertShadowCanvasSafe(IWzGr2DLayer* layer, IWzCanvas* canvas) {
    if (!layer || !canvas) {
        return false;
    }
    try {
        // IDA TemporaryStat::UpdateShadowIndex: InsertCanvas(delay=500, alpha=210, zoom=64).
        layer->InsertCanvas(canvas, 500, 210, 64);
        return true;
    } catch (...) {
        return false;
    }
}

static void RemoveShadowCanvasSafe(IWzGr2DLayer* layer) {
    if (!layer) {
        return;
    }
    try {
        layer->RemoveCanvas(-2);
    } catch (...) {
    }
}

bool RebuildBuffShadowCanvas(TemporaryStat* stat, const std::wstring& label, int pieIndex) {
    if (!stat || stat->bNoShadow || !stat->pLayerShadow) {
        return false;
    }

    auto* statusBar = CUIStatusBar::GetInstance();
    if (!statusBar) {
        return false;
    }

    RemoveShadowCanvasSafe(stat->pLayerShadow);
    IWzCanvasPtr canvas;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", canvas, nullptr);
    if (!canvas) {
        return false;
    }
    canvas->Create(kShadowCanvasSize, kShadowCanvasSize);

    IWzCanvas* cooltime = statusBar->m_aCanvasSkillCooltime[pieIndex];
    CopyCanvasSafe(canvas.GetInterfacePtr(), cooltime);

    DrawBuffTimeLabelOnCanvas(canvas.GetInterfacePtr(), label);

    if (!InsertShadowCanvasSafe(stat->pLayerShadow.GetInterfacePtr(), canvas.GetInterfacePtr())) {
        Log("InsertCanvas SEH id=%d", stat->nID);
        return false;
    }

    stat->nIndexShadow = stat->tLeft / 1000;

    if (g_shadowRefreshLogCount < 5) {
        Log("shadow label id=%d label=%ls pie=%d tLeft=%d", stat->nID, label.c_str(), pieIndex, stat->tLeft);
        ++g_shadowRefreshLogCount;
    }
    return true;
}

BuffShadowState* FindShadowState(TemporaryStat* stat) {
    for (auto& state : g_buffShadowStates) {
        if (state.stat == stat) {
            return &state;
        }
    }
    return nullptr;
}

void ClearBuffShadowState(TemporaryStat* stat) {
    if (!stat || !stat->pLayerShadow) {
        return;
    }
    RemoveShadowCanvasSafe(stat->pLayerShadow.GetInterfacePtr());
}

void UpdateBuffShadowIfNeeded(TemporaryStat* stat) {
    if (!ShouldDrawBuffTimer(stat)) {
        if (BuffShadowState* state = FindShadowState(stat)) {
            ClearBuffShadowState(stat);
            state->lastLabel.clear();
            state->lastPieIndex = -1;
        }
        return;
    }

    const std::wstring label = FormatBuffTime(stat->tLeft);
    const int pieIndex = ComputePieIndex(stat);

    BuffShadowState* state = FindShadowState(stat);
    if (!state) {
        g_buffShadowStates.push_back(BuffShadowState{stat, -1, {}});
        state = &g_buffShadowStates.back();
    }

    if (label == state->lastLabel && pieIndex == state->lastPieIndex) {
        return;
    }

    if (RebuildBuffShadowCanvas(stat, label, pieIndex)) {
        state->lastLabel = label;
        state->lastPieIndex = pieIndex;
    }
}

void PruneBuffShadowStates(const std::vector<TemporaryStat*>& active) {
    g_buffShadowStates.erase(
        std::remove_if(
            g_buffShadowStates.begin(),
            g_buffShadowStates.end(),
            [&](const BuffShadowState& state) {
                return std::find(active.begin(), active.end(), state.stat) == active.end();
            }),
        g_buffShadowStates.end());
}

void SyncBuffTimerShadowsOnGameThread() {
    auto* view = GetTemporaryStatViewBase();
    if (!view) {
        g_buffShadowStates.clear();
        return;
    }

    const int count = *reinterpret_cast<int*>(view + 0xC);
    void* pos = *reinterpret_cast<void**>(view + kTempStatListHeadOff);
    const int limit = (std::min)(count > 0 ? count : 64, 64);

    std::vector<TemporaryStat*> active;
    active.reserve(static_cast<size_t>(limit));

    for (int i = 0; pos && i < limit; ++i) {
        auto* stat = *reinterpret_cast<TemporaryStat**>(reinterpret_cast<uint8_t*>(pos) + sizeof(void*));
        if (stat) {
            active.push_back(stat);
            UpdateBuffShadowIfNeeded(stat);
        }

        auto* link = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pos) - 0x10 + sizeof(void*));
        pos = link ? reinterpret_cast<uint8_t*>(link) + 0x10 : nullptr;
    }

    PruneBuffShadowStates(active);
}

void ThrottledBuffTimerTickOnGameThread(bool force = false) {
    const DWORD now = GetTickCount();
    if (!force && g_lastShadowSyncTick != 0 && (now - g_lastShadowSyncTick) < g_buffTimerRefreshMs) {
        return;
    }
    g_lastShadowSyncTick = now;
    SyncBuffTimerShadowsOnGameThread();
}

bool IsInGameForBuffTimer() {
    if (!CUIStatusBar::IsInstantiated()) {
        return false;
    }
    void* userLocal = *reinterpret_cast<void**>(kUserLocalInstanceAddr);
    return userLocal != nullptr;
}

int CountActiveBuffTimers() {
    int count = 0;
    auto* view = GetTemporaryStatViewBase();
    if (!view) {
        return 0;
    }

    const int listCount = *reinterpret_cast<int*>(view + 0xC);
    void* pos = *reinterpret_cast<void**>(view + kTempStatListHeadOff);
    const int limit = (std::min)(listCount > 0 ? listCount : 64, 64);
    for (int i = 0; pos && i < limit; ++i) {
        auto* stat = *reinterpret_cast<TemporaryStat**>(reinterpret_cast<uint8_t*>(pos) + sizeof(void*));
        if (stat && ShouldDrawBuffTimer(stat)) {
            ++count;
        }
        auto* link = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pos) - 0x10 + sizeof(void*));
        pos = link ? reinterpret_cast<uint8_t*>(link) + 0x10 : nullptr;
    }
    return count;
}

void LogActiveTimersIfNeeded(const char* reason) {
    if (!g_diagLogEnabled) {
        return;
    }
    const DWORD now = GetTickCount();
    const int timers = CountActiveBuffTimers();
    if (timers > 0 && !g_loggedActiveTimers) {
        g_loggedActiveTimers = true;
        Log("%s timers=%d", reason, timers);
        return;
    }
    if (now - g_lastDiagLogTick < kDiagLogIntervalMs) {
        return;
    }
    g_lastDiagLogTick = now;
    void* ctx = *reinterpret_cast<void**>(kCWvsContextInst);
    Log("%s ctx=%p view=%p listCount=%d timers=%d shadowStates=%zu", reason, ctx, GetTemporaryStatViewBase(),
        GetTemporaryStatViewBase() ? *reinterpret_cast<int*>(GetTemporaryStatViewBase() + 0xC) : -1,
        timers, g_buffShadowStates.size());
}

std::wstring FormatBuffTime(int leftMs) {
    const int seconds = (std::max)(1, (leftMs + 999) / 1000);
    wchar_t buffer[24]{};
    if (seconds >= 3600) {
        swprintf_s(buffer, L"%dh%dm", seconds / 3600, (seconds % 3600) / 60);
        return buffer;
    }
    if (seconds >= 60) {
        swprintf_s(buffer, L"%dm%ds", seconds / 60, seconds % 60);
        return buffer;
    }
    swprintf_s(buffer, L"%ds", seconds);
    return buffer;
}

void __fastcall TEMPORARY_STAT__UpdateShadowIndex_hook(TemporaryStat* pThis, void* /*edx*/) {
    // Empty on purpose: vanilla per-entry RemoveCanvas+InsertCanvas every shadow tick freezes buffer NPC.
    // Countdown text + cooltime pie are updated on CTemporaryStatView::Update at buffTimerRefreshMs.
    (void)pThis;
}

void* __fastcall CTemporaryStatView__Update_hook(void* pThis, void* /*edx*/) {
    void* result = CTemporaryStatView__Update(pThis);
    if (IsInGameForBuffTimer()) {
        ThrottledBuffTimerTickOnGameThread(false);
    }
    LogActiveTimersIfNeeded("view-update");
    return result;
}

void* GetStatusBarNumberFont(CUIStatusBar* /*statusBar*/) {
    if (g_pSkillNumberFont) {
        return g_pSkillNumberFont.GetInterfacePtr();
    }

    g_pSkillNumberFont = LoadLevelNoNumberFont("skill");
    return g_pSkillNumberFont ? g_pSkillNumberFont.GetInterfacePtr() : nullptr;
}

static void DrawSkillCooldownNumberSafe(DrawNumberByImageFn drawNumber, IWzCanvas* canvas, int left, int top, int seconds, void* font) {
    __try {
        drawNumber(canvas, left, top, seconds, font, -1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void DrawSkillCooldownNumber(IWzCanvas* canvas, int x, int y, int seconds, CUIStatusBar* statusBar) {
    if (!canvas || seconds <= 0 || seconds > 999) {
        return;
    }

    void* font = GetStatusBarNumberFont(statusBar);
    if (!font) {
        return;
    }

    const int left = x + (seconds >= 100 ? 4 : seconds >= 10 ? 8 : 12);
    const int top = y + 11;
    auto drawNumber = reinterpret_cast<DrawNumberByImageFn>(kDrawNumberByImage);
    DrawSkillCooldownNumberSafe(drawNumber, canvas, left, top, seconds, font);
}

struct CUIStatusBar__DrawSkillCooltime_stack {
    MEMBER_AT(int, 0x64, nCanvasX)
    MEMBER_AT(int, 0x68, nCanvasY)
    MEMBER_AT(int*, 0x74, pnLastIndex)
    MEMBER_AT(IWzCanvas*, 0x7C, pCanvas)
    MEMBER_AT(CUIStatusBar*, 0xF8, pStatusBar)
};

void __stdcall CUIStatusBar__DrawSkillCooltime_helper(
    CUIStatusBar__DrawSkillCooltime_stack* stack, int nSeconds, int nIndex) {
    if (!stack || !stack->pCanvas || !stack->pnLastIndex) {
        return;
    }
    if (nIndex < 0 || nIndex >= 16) {
        return;
    }
    if (*stack->pnLastIndex == nIndex) {
        return;
    }

    auto* statusBar = CUIStatusBar::IsInstantiated() ? CUIStatusBar::GetInstance() : nullptr;
    if (!statusBar) {
        return;
    }
    IWzCanvas* src = statusBar->m_aCanvasSkillCooltime[nIndex];
    if (!src) {
        return;
    }

    if (!g_loggedSkillCooldown && nSeconds > 0) {
        g_loggedSkillCooldown = true;
        Log("skill cooltime draw sec=%d idx=%d canvas=%p src=%p", nSeconds, nIndex, stack->pCanvas, src);
    }

    *stack->pnLastIndex = nIndex;
    __try {
        stack->pCanvas->Copy(stack->nCanvasX, stack->nCanvasY, src);
        DrawSkillCooldownNumber(
            stack->pCanvas,
            stack->nCanvasX,
            stack->nCanvasY,
            nSeconds,
            stack->pStatusBar ? stack->pStatusBar : statusBar);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("skill cooltime Copy/draw SEH sec=%d idx=%d", nSeconds, nIndex);
    }
}

static const uintptr_t kDrawSkillCooltimeReturn = kDrawSkillCooltimeRet;

void __declspec(naked) CUIStatusBar__DrawSkillCooltime_hook() {
    __asm {
        push    esi
        push    ebx
        lea     eax, [ebp - 0x8C]
        push    eax
        call    CUIStatusBar__DrawSkillCooltime_helper
        jmp     dword ptr [kDrawSkillCooltimeReturn]
    }
}

bool IsEnabledByConfig() {
    char dllPath[MAX_PATH]{};
    std::string configPath = "config.ini";
    HMODULE self = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&IsEnabledByConfig),
            &self) &&
        self &&
        GetModuleFileNameA(self, dllPath, MAX_PATH) != 0) {
        std::string path(dllPath);
        const auto slash = path.find_last_of("\\/");
        if (slash != std::string::npos) {
            configPath = path.substr(0, slash + 1) + "config.ini";
        }
    }
    INIReader reader(configPath);
    if (reader.ParseError() != 0) {
        return true;
    }
    g_diagLogEnabled = reader.GetBoolean("optional", "enableBuffTimerDiag", false);
    long refreshMs = reader.GetInteger(
        "optional",
        "buffTimerRefreshMs",
        static_cast<long>(kDefaultBuffTimerRefreshMs));
    if (refreshMs < static_cast<long>(kBuffTimerRefreshMinMs)) {
        refreshMs = static_cast<long>(kBuffTimerRefreshMinMs);
    } else if (refreshMs > static_cast<long>(kBuffTimerRefreshMaxMs)) {
        refreshMs = static_cast<long>(kBuffTimerRefreshMaxMs);
    }
    g_buffTimerRefreshMs = static_cast<DWORD>(refreshMs);
    return reader.GetBoolean("optional", "enableBuffTimer", true);
}

void AttachHooksOnce() {
    if (g_hooksAttached) {
        return;
    }
    if (!IsEnabledByConfig()) {
        Log("disabled by config optional.enableBuffTimer=false");
        g_hooksAttached = true;
        return;
    }

    g_module = GetModuleHandleW(L"ijl15.dll");
    if (!g_module) {
        g_module = GetModuleHandleW(nullptr);
    }

    const bool shadowHook = Memory::SetHook(
        true,
        reinterpret_cast<void**>(&TEMPORARY_STAT__UpdateShadowIndex),
        CastHook(&TEMPORARY_STAT__UpdateShadowIndex_hook));
    const bool updateHook = Memory::SetHook(
        true,
        reinterpret_cast<void**>(&CTemporaryStatView__Update),
        CastHook(&CTemporaryStatView__Update_hook));

    PatchJmp(kDrawSkillCooltimeHook, CUIStatusBar__DrawSkillCooltime_hook);

    g_hooksAttached = shadowHook && updateHook;

    Log("hooks attached shadow=%d update=%d module=%p refreshMs=%lu (shadow-layer labels)",
        shadowHook ? 1 : 0, updateHook ? 1 : 0, g_module, g_buffTimerRefreshMs);
}

DWORD WINAPI InitThreadProc(LPVOID) {
    Sleep(kStartupDelayMs);
    if (!g_hooksAttached) {
        AttachHooksOnce();
    }
    Log("init complete hooks=%d inGame=%d", g_hooksAttached ? 1 : 0, IsInGameForBuffTimer() ? 1 : 0);
    return 0;
}

struct BuffTimerAutoInit {
    BuffTimerAutoInit() {
        Log("BuffTimer auto init thread scheduled (delay=%ums)", kStartupDelayMs);
        HANDLE thread = CreateThread(nullptr, 0, InitThreadProc, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }
};

BuffTimerAutoInit g_buffTimerAutoInit;

} // namespace

namespace BuffTimer {

void EnsureHooks() {
    AttachHooksOnce();
}

} // namespace BuffTimer
