#include "stdafx.h"
#include "BuffTimerApi.h"
#include "INIReader.h"
#include "compat/hook.h"
#include "compat/wvs/statusbar.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr uintptr_t kDrawSkillCooltimeHook = 0x008E085B;
constexpr uintptr_t kDrawSkillCooltimeRet = 0x008E0990;
constexpr uintptr_t kDrawNumberByImage = 0x00988345;
constexpr uintptr_t kCWvsContextInst = 0x00BE7918;
constexpr int kCtxTempStatViewOff = 0x2EA8;
constexpr int kTempStatListHeadOff = 0x10;
constexpr int kStatusBarNumberFontOff = 0xE0;
// Match V95 reference: delay hooks until client UI is up; overlay tick stays light.
constexpr DWORD kStartupDelayMs = 12000;
constexpr DWORD kOverlayTimerMs = 500;
constexpr DWORD kDiagLogIntervalMs = 10000;
// Global-only refresh: one snapshot for ALL buffs every 500ms (never per-buff).
// ±1s display skew is acceptable — prefer no freeze under buffer NPC.
constexpr DWORD kSnapshotMinIntervalMs = 500;
constexpr int kOverlayTextTop = 37;
constexpr int kOverlayTextBottom = 59;
constexpr COLORREF kOverlayColorKey = RGB(1, 0, 1);
constexpr COLORREF kBuffTimerTextColor = RGB(255, 224, 96);
constexpr uintptr_t kUserLocalInstanceAddr = 0x00BEBF98;

using DrawNumberByImageFn = int(__cdecl*)(void*, int, int, int, void*, int);

struct BuffTimerEntry {
    int leftMs = 0;
    int id = 0;
};

HMODULE g_module = nullptr;
HWND g_overlay = nullptr;
HFONT g_timerFont = nullptr;
bool g_hooksAttached = false;
bool g_overlayReady = false;
bool g_loggedActiveTimers = false;
DWORD g_lastDiagLogTick = 0;
DWORD g_lastSnapshotTick = 0;
FILE* g_log = nullptr;
// Hot-path diag (overlay-tick / view-update) off by default — fflush to disk starved the game thread.
bool g_diagLogEnabled = false;

// V95 pattern: walk TempStat only on the game thread; overlay reads a locked snapshot.
CRITICAL_SECTION g_timerLock{};
bool g_timerLockReady = false;
std::vector<BuffTimerEntry> g_timerSnapshot;
RECT g_lastOverlayScreenRect{};
bool g_hasLastOverlayScreenRect = false;

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
    // Avoid fflush-per-line on the Maple main thread (buffer NPC freeze amplifier).
}

uint8_t* GetTemporaryStatViewBase() {
    void* ctx = *reinterpret_cast<void**>(kCWvsContextInst);
    if (!ctx) {
        return nullptr;
    }
    return reinterpret_cast<uint8_t*>(ctx) + kCtxTempStatViewOff;
}

void AttachHooksOnce();

std::vector<BuffTimerEntry> CollectBuffTimers();
std::wstring FormatBuffTime(int leftMs);

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

void RefreshBuffShadow(TemporaryStat* pThis);

static auto TEMPORARY_STAT__UpdateShadowIndex =
    reinterpret_cast<void(__thiscall*)(TemporaryStat*)>(0x007B44F4);

bool IsDurabilityBuff(int skillId) {
    return skillId == 5221006;
}

bool ShouldDrawBuffOverlay(const BuffTimerEntry& entry) {
    return entry.leftMs > 0 && !IsDurabilityBuff(entry.id);
}

static IWzPropertyPtr g_pBuffNumberFont;
static IWzPropertyPtr g_pSkillNumberFont;
static HWND g_cachedGameWindow = nullptr;
static HWND g_overlayParent = nullptr;
static int g_shadowRefreshLogCount = 0;
static bool g_loggedSkillCooldown = false;
static bool g_loggedOverlayShow = false;

struct OverlayPaintState {
    int width = 0;
    int height = 0;
    int startX = 0;
    std::vector<std::wstring> labels;
};

static OverlayPaintState g_paintState;

RECT MakeSlotRect(int startX, size_t index) {
    return RECT{
        startX + static_cast<int>(index) * 32,
        kOverlayTextTop,
        startX + static_cast<int>(index + 1) * 32,
        kOverlayTextBottom};
}

RECT MakeTimerBarRect(int startX, size_t slotCount) {
    if (slotCount == 0) {
        return RECT{0, 0, 0, 0};
    }
    return RECT{
        startX,
        kOverlayTextTop,
        startX + static_cast<int>(slotCount) * 32,
        kOverlayTextBottom};
}

bool RectsIntersect(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

void FillColorKey(HDC dc, const RECT& rect) {
    HBRUSH background = CreateSolidBrush(kOverlayColorKey);
    FillRect(dc, &rect, background);
    DeleteObject(background);
}

void BuildCharRects(HDC dc, const std::wstring& text, const RECT& slotRect, std::vector<RECT>& out) {
    out.clear();
    if (text.empty()) {
        return;
    }

    SIZE total{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &total);
    const int slotW = slotRect.right - slotRect.left;
    const int slotH = slotRect.bottom - slotRect.top;
    int x = slotRect.left + (slotW - total.cx) / 2;
    const int y = slotRect.top + (slotH - total.cy) / 2;

    for (wchar_t ch : text) {
        wchar_t glyph[2] = {ch, 0};
        SIZE charSize{};
        GetTextExtentPoint32W(dc, glyph, 1, &charSize);
        out.push_back(RECT{x, y, x + charSize.cx, y + charSize.cy});
        x += charSize.cx;
    }
}

void InvalidateLabelDiff(HWND hwnd, HDC dc, const RECT& slotRect, const std::wstring& oldLabel, const std::wstring& newLabel) {
    if (oldLabel == newLabel) {
        return;
    }
    if (oldLabel.empty() || newLabel.empty() || oldLabel.size() != newLabel.size()) {
        InvalidateRect(hwnd, &slotRect, FALSE);
        return;
    }

    SIZE oldSize{};
    SIZE newSize{};
    GetTextExtentPoint32W(dc, oldLabel.c_str(), static_cast<int>(oldLabel.size()), &oldSize);
    GetTextExtentPoint32W(dc, newLabel.c_str(), static_cast<int>(newLabel.size()), &newSize);
    if (oldSize.cx != newSize.cx) {
        InvalidateRect(hwnd, &slotRect, FALSE);
        return;
    }

    std::vector<RECT> charRects;
    BuildCharRects(dc, newLabel, slotRect, charRects);
    for (size_t i = 0; i < newLabel.size() && i < charRects.size(); ++i) {
        if (oldLabel[i] != newLabel[i]) {
            RECT dirty = charRects[i];
            InflateRect(&dirty, 2, 2);
            InvalidateRect(hwnd, &dirty, FALSE);
        }
    }
}

std::vector<BuffTimerEntry> CollectVisibleBuffTimers() {
    std::vector<BuffTimerEntry> visible;
    const auto timers = CollectBuffTimers();
    visible.reserve(timers.size());
    for (const auto& entry : timers) {
        if (ShouldDrawBuffOverlay(entry)) {
            visible.push_back(entry);
        }
    }
    return visible;
}

bool BuildOverlayLabels(int clientWidth, int& startX, std::vector<std::wstring>& labels) {
    const auto visible = CollectVisibleBuffTimers();
    labels.clear();
    labels.reserve(visible.size());
    for (const auto& entry : visible) {
        labels.push_back(FormatBuffTime(entry.leftMs));
    }
    startX = (std::max)(0, clientWidth - static_cast<int>(visible.size()) * 32 - 3);
    return !labels.empty();
}

void SyncOverlayPaint(int clientWidth, int clientHeight, bool forceFullBar) {
    int startX = 0;
    std::vector<std::wstring> labels;
    if (!BuildOverlayLabels(clientWidth, startX, labels)) {
        if (!g_paintState.labels.empty()) {
            RECT bar = MakeTimerBarRect(g_paintState.startX, g_paintState.labels.size());
            InvalidateRect(g_overlay, &bar, FALSE);
        }
        g_paintState = {};
        return;
    }

    const bool layoutChanged = forceFullBar || clientWidth != g_paintState.width || clientHeight != g_paintState.height ||
        startX != g_paintState.startX || labels.size() != g_paintState.labels.size();

    if (layoutChanged) {
        RECT dirty = MakeTimerBarRect(startX, labels.size());
        if (!g_paintState.labels.empty()) {
            RECT previous = MakeTimerBarRect(g_paintState.startX, g_paintState.labels.size());
            UnionRect(&dirty, &dirty, &previous);
        }
        InvalidateRect(g_overlay, &dirty, FALSE);
    } else if (g_timerFont) {
        HDC dc = GetDC(g_overlay);
        if (dc) {
            SelectObject(dc, g_timerFont);
            const size_t shared = (std::min)(labels.size(), g_paintState.labels.size());
            for (size_t i = 0; i < shared; ++i) {
                if (labels[i] != g_paintState.labels[i]) {
                    RECT slot = MakeSlotRect(startX, i);
                    InvalidateLabelDiff(g_overlay, dc, slot, g_paintState.labels[i], labels[i]);
                }
            }
            ReleaseDC(g_overlay, dc);
        }
    }

    g_paintState.width = clientWidth;
    g_paintState.height = clientHeight;
    g_paintState.startX = startX;
    g_paintState.labels = std::move(labels);
}

// v083 Basic.img has digits under LevelNo directly; some clients nest under LevelNo/number.
// GetObjectA throws STG_E_FILENOTFOUND (0x80030002) on missing paths — must catch.
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

void DrawBuffDurationOnCanvas(IWzCanvasPtr pCanvas, int nSeconds) {
    if (!pCanvas || nSeconds <= 0 || nSeconds >= 999) {
        return;
    }
    if (!g_pBuffNumberFont) {
        g_pBuffNumberFont = LoadLevelNoNumberFont("buff");
    }
    if (!g_pBuffNumberFont) {
        return;
    }

    int nValue = nSeconds;
    int offsetX = nValue >= 100 ? 4 : nValue >= 10 ? 8 : 12;
    int offsetY = 10;
    if (nSeconds >= 60) {
        nValue = nSeconds / 60;
        offsetX = 2;
        offsetY = 19;
    }

    DrawBuffNumberSafe(
        pCanvas.GetInterfacePtr(),
        offsetX,
        offsetY,
        nValue,
        g_pBuffNumberFont.GetInterfacePtr());
}

void RefreshAllBuffShadows(void* view) {
    if (!view) {
        return;
    }

    const int count = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(view) + 0xC);
    void* pos = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(view) + kTempStatListHeadOff);
    const int limit = (std::min)(count > 0 ? count : 64, 64);
    for (int i = 0; pos && i < limit; ++i) {
        auto* stat = *reinterpret_cast<TemporaryStat**>(reinterpret_cast<uint8_t*>(pos) + sizeof(void*));
        if (stat && stat->nID != 5221006) {
            RefreshBuffShadow(stat);
        }
        auto* link = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pos) - 0x10 + sizeof(void*));
        pos = link ? reinterpret_cast<uint8_t*>(link) + 0x10 : nullptr;
    }
}

bool IsInGameForOverlay() {
    // Status bar exists only after character enters the field — never overlay login/char-select.
    if (!CUIStatusBar::IsInstantiated()) {
        return false;
    }
    void* userLocal = *reinterpret_cast<void**>(kUserLocalInstanceAddr);
    return userLocal != nullptr;
}

std::vector<BuffTimerEntry> CollectBuffTimersUnlocked() {
    // Call only from the Maple game thread (TempStat hooks). Overlay must use SnapshotBuffTimers().
    std::vector<BuffTimerEntry> timers;
    auto* view = GetTemporaryStatViewBase();
    if (!view) {
        return timers;
    }

    const int count = *reinterpret_cast<int*>(view + 0xC);
    void* pos = *reinterpret_cast<void**>(view + kTempStatListHeadOff);
    const int limit = (std::min)(count > 0 ? count : 64, 64);
    for (int i = 0; pos && i < limit; ++i) {
        auto* stat = *reinterpret_cast<TemporaryStat**>(reinterpret_cast<uint8_t*>(pos) + sizeof(void*));
        if (stat && stat->tLeft > 0) {
            BuffTimerEntry entry{};
            entry.leftMs = stat->tLeft;
            entry.id = stat->nID;
            timers.push_back(entry);
        }

        auto* link = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pos) - 0x10 + sizeof(void*));
        pos = link ? reinterpret_cast<uint8_t*>(link) + 0x10 : nullptr;
    }
    return timers;
}

void PublishTimerSnapshotFromGameThread(bool force = false) {
    if (!g_timerLockReady) {
        return;
    }
    const DWORD now = GetTickCount();
    if (!force && g_lastSnapshotTick != 0 && (now - g_lastSnapshotTick) < kSnapshotMinIntervalMs) {
        return;
    }
    g_lastSnapshotTick = now;
    auto timers = CollectBuffTimersUnlocked();
    EnterCriticalSection(&g_timerLock);
    g_timerSnapshot = std::move(timers);
    LeaveCriticalSection(&g_timerLock);
}

std::vector<BuffTimerEntry> SnapshotBuffTimers() {
    std::vector<BuffTimerEntry> timers;
    if (!g_timerLockReady) {
        return timers;
    }
    EnterCriticalSection(&g_timerLock);
    timers = g_timerSnapshot;
    LeaveCriticalSection(&g_timerLock);
    return timers;
}

// Kept for init/diag on worker threads — never walks live list (snapshot only).
std::vector<BuffTimerEntry> CollectBuffTimers() {
    return SnapshotBuffTimers();
}

void LogActiveTimersIfNeeded(const char* reason) {
    if (!g_diagLogEnabled) {
        return;
    }
    const DWORD now = GetTickCount();
    const auto timers = CollectBuffTimers();
    if (!timers.empty() && !g_loggedActiveTimers) {
        g_loggedActiveTimers = true;
        Log("%s timers=%zu firstId=%d firstLeft=%d", reason, timers.size(), timers[0].id, timers[0].leftMs);
        return;
    }
    if (now - g_lastDiagLogTick < kDiagLogIntervalMs) {
        return;
    }
    g_lastDiagLogTick = now;
    void* ctx = *reinterpret_cast<void**>(kCWvsContextInst);
    Log("%s ctx=%p view=%p listCount=%d timers=%zu overlay=%p", reason, ctx, GetTemporaryStatViewBase(),
        GetTemporaryStatViewBase() ? *reinterpret_cast<int*>(GetTemporaryStatViewBase() + 0xC) : -1,
        timers.size(), g_overlay);
}

std::wstring FormatBuffTime(int leftMs) {
    const int seconds = (std::max)(1, (leftMs + 999) / 1000);
    if (seconds >= 3600) {
        return std::to_wstring((seconds + 3599) / 3600) + L"h";
    }
    if (seconds >= 60) {
        wchar_t buffer[16]{};
        swprintf_s(buffer, L"%d:%02d", seconds / 60, seconds % 60);
        return buffer;
    }
    return std::to_wstring(seconds);
}

bool IsOverlayWindow(HWND hwnd) {
    wchar_t className[128]{};
    GetClassNameW(hwnd, className, 128);
    return wcscmp(className, L"BeiDouBuffTimerOverlay") == 0;
}

/** Hide buff timers when Maple is covered / alt-tabbed (overlay is WS_POPUP, not a child). */
bool IsMapleForeground(HWND game) {
    HWND fg = GetForegroundWindow();
    if (!fg || !game) {
        return false;
    }
    if (fg == game || fg == g_overlay || IsOverlayWindow(fg)) {
        return true;
    }
    // In-game dialogs / owned popups still belong to this process.
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

HWND FindGameWindow() {
    if (g_cachedGameWindow && IsWindow(g_cachedGameWindow)) {
        return g_cachedGameWindow;
    }

    HWND hwnd = FindWindowW(L"MapleStoryClass", nullptr);
    if (hwnd) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) {
            g_cachedGameWindow = hwnd;
            return hwnd;
        }
    }

    struct Search {
        DWORD pid = GetCurrentProcessId();
        HWND hwnd = nullptr;
    } search;

    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            auto* search = reinterpret_cast<Search*>(lParam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            if (pid != search->pid || IsOverlayWindow(hWnd)) {
                return TRUE;
            }
            if (!IsWindowVisible(hWnd)) {
                return TRUE;
            }

            RECT rc{};
            if (!GetWindowRect(hWnd, &rc) || rc.right - rc.left < 200 || rc.bottom - rc.top < 200) {
                return TRUE;
            }

            search->hwnd = hWnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));

    g_cachedGameWindow = search.hwnd;
    return search.hwnd;
}

void DrawOutlinedText(HDC dc, const RECT& rect, const std::wstring& text, COLORREF color) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            RECT shadow = rect;
            OffsetRect(&shadow, dx, dy);
            DrawTextW(dc, text.c_str(), -1, &shadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }
    }

    SetTextColor(dc, color);
    RECT foreground = rect;
    DrawTextW(dc, text.c_str(), -1, &foreground, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
}

void DrawOutlinedChar(HDC dc, const RECT& rect, wchar_t ch, COLORREF color) {
    wchar_t glyph[2] = {ch, 0};
    DrawOutlinedText(dc, rect, glyph, color);
}

void PaintOverlay(HDC dc, const RECT& paintRect) {
    FillColorKey(dc, paintRect);

    if (g_paintState.labels.empty() || !g_timerFont) {
        return;
    }

    SelectObject(dc, g_timerFont);
    for (size_t i = 0; i < g_paintState.labels.size(); ++i) {
        const RECT slotRect = MakeSlotRect(g_paintState.startX, i);
        if (!RectsIntersect(slotRect, paintRect)) {
            continue;
        }

        const std::wstring& text = g_paintState.labels[i];
        std::vector<RECT> charRects;
        BuildCharRects(dc, text, slotRect, charRects);
        for (size_t c = 0; c < text.size() && c < charRects.size(); ++c) {
            if (!RectsIntersect(charRects[c], paintRect)) {
                continue;
            }
            RECT eraseRect = charRects[c];
            InflateRect(&eraseRect, 2, 2);
            FillColorKey(dc, eraseRect);
            DrawOutlinedChar(dc, charRects[c], text[c], kBuffTimerTextColor);
        }
    }
}

void UpdateOverlayWindow() {
    if (!g_overlay) {
        return;
    }

    // Snapshot only — never walk TempStat from the overlay thread (hang root cause).
    const auto timers = SnapshotBuffTimers();
    bool hasTimer = false;
    for (const auto& entry : timers) {
        if (ShouldDrawBuffOverlay(entry)) {
            hasTimer = true;
            break;
        }
    }

    HWND game = FindGameWindow();
    // Match V95BuffTimerClient: popup over the game client (NOT WS_CHILD).
    // Child layered windows break color-key under DirectX → fullscreen black + half-clipped buffs.
    // Must NOT stay HWND_TOPMOST when Maple is covered — timers would float over other apps.
    if (!game || !hasTimer || !IsInGameForOverlay() || !IsWindowVisible(game) || IsIconic(game)
        || !IsMapleForeground(game)) {
        if (hasTimer && !g_loggedOverlayShow) {
            Log("overlay hidden: game=%p hasTimer=%d inGame=%d fg=%d", game, hasTimer ? 1 : 0,
                IsInGameForOverlay() ? 1 : 0, game ? (IsMapleForeground(game) ? 1 : 0) : 0);
        }
        g_paintState = {};
        g_overlayParent = nullptr;
        g_hasLastOverlayScreenRect = false;
        ShowWindow(g_overlay, SW_HIDE);
        return;
    }

    RECT client{};
    if (!GetClientRect(game, &client)) {
        g_paintState = {};
        ShowWindow(g_overlay, SW_HIDE);
        return;
    }

    POINT origin{0, 0};
    ClientToScreen(game, &origin);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const RECT screenRect{origin.x, origin.y, origin.x + width, origin.y + height};
    const bool movedOrResized = !g_hasLastOverlayScreenRect ||
        screenRect.left != g_lastOverlayScreenRect.left ||
        screenRect.top != g_lastOverlayScreenRect.top ||
        screenRect.right != g_lastOverlayScreenRect.right ||
        screenRect.bottom != g_lastOverlayScreenRect.bottom;
    const bool sizeChanged = width != g_paintState.width || height != g_paintState.height;

    // Keep as independent WS_POPUP (created that way); never reparent as WS_CHILD.
    if (g_overlayParent != game) {
        SetWindowLongPtr(
            g_overlay,
            GWL_EXSTYLE,
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        SetLayeredWindowAttributes(g_overlay, kOverlayColorKey, 0, LWA_COLORKEY);
        g_overlayParent = game;
        g_cachedGameWindow = game;
    }

    // Place just above the game window — never HWND_TOPMOST (leaks over other apps).
    // Throttle: SetWindowPos every tick flooded DWM/GDI and starved the Maple main thread.
    if (movedOrResized || !IsWindowVisible(g_overlay)) {
        // Leave topmost band if a previous build left us there.
        SetWindowPos(
            g_overlay,
            HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        HWND insertAfter = GetWindow(game, GW_HWNDPREV);
        if (!insertAfter || insertAfter == g_overlay) {
            insertAfter = HWND_TOP;
        }
        SetWindowPos(
            g_overlay,
            insertAfter,
            origin.x,
            origin.y,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_lastOverlayScreenRect = screenRect;
        g_hasLastOverlayScreenRect = true;
    }

    // Dirty only changed timer slots — do NOT full-window InvalidateRect each tick.
    SyncOverlayPaint(width, height, sizeChanged || movedOrResized);

    if (!g_loggedOverlayShow) {
        g_loggedOverlayShow = true;
        Log("overlay show popup game=%p size=%dx%d timers=%zu", game, width, height, timers.size());
    }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_timerFont = CreateFontW(
            -10, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            DEFAULT_PITCH, L"Tahoma");
        SetLayeredWindowAttributes(hwnd, kOverlayColorKey, 0, LWA_COLORKEY);
        SetTimer(hwnd, 1, kOverlayTimerMs, nullptr);
        g_overlayReady = true;
        return 0;
    case WM_TIMER:
        if (!g_hooksAttached) {
            AttachHooksOnce();
        }
        LogActiveTimersIfNeeded("overlay-tick");
        UpdateOverlayWindow();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        PaintOverlay(dc, ps.rcPaint);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_timerFont) {
            DeleteObject(g_timerFont);
            g_timerFont = nullptr;
        }
        g_overlayReady = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

DWORD WINAPI OverlayThreadProc(LPVOID) {
    const HINSTANCE module = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"BeiDouBuffTimerOverlay";
    RegisterClassExW(&wc);

    g_overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        module,
        nullptr);
    if (g_overlay) {
        ShowWindow(g_overlay, SW_HIDE);
        Log("overlay created hwnd=%p", g_overlay);
    } else {
        Log("overlay create failed err=%lu", GetLastError());
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void StartOverlayThread() {
    HANDLE thread = CreateThread(nullptr, 0, OverlayThreadProc, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

static void RefreshBuffShadowBody(TemporaryStat* pThis) {
    if (!pThis || pThis->bNoShadow || !pThis->pLayerShadow) {
        if (g_shadowRefreshLogCount < 3) {
            Log("shadow skip id=%d bNoShadow=%d pLayerShadow=%p", pThis ? pThis->nID : -1,
                pThis ? pThis->bNoShadow : -1, pThis ? pThis->pLayerShadow.GetInterfacePtr() : nullptr);
            ++g_shadowRefreshLogCount;
        }
        return;
    }

    const int nSeconds = pThis->tLeft / 1000;
    if (nSeconds == pThis->nIndexShadow) {
        return;
    }
    pThis->nIndexShadow = nSeconds;

    int nShadowIndex = 0;
    if (pThis->tLeftUnit) {
        nShadowIndex = pThis->tLeft / pThis->tLeftUnit;
        nShadowIndex = zclamp(nShadowIndex, 0, 15);
    }

    auto* statusBar = CUIStatusBar::GetInstance();
    if (!statusBar) {
        return;
    }

    pThis->pLayerShadow->RemoveCanvas(-2);
    IWzCanvasPtr pCanvas;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", pCanvas, nullptr);
    if (!pCanvas) {
        return;
    }
    pCanvas->Create(32, 32);
    IWzCanvas* cooltime = statusBar->m_aCanvasSkillCooltime[nShadowIndex];
    if (cooltime) {
        pCanvas->Copy(0, 0, cooltime);
    }
    // Number glyphs on the cooltime pie are crashy on this client; overlay shows time.
    // Keep pie shadow only — GDI overlay handles the countdown text.
    (void)nSeconds;
    pThis->pLayerShadow->InsertCanvas(pCanvas, 500, 210, 64);

    if (g_shadowRefreshLogCount < 5) {
        Log("shadow drawn id=%d sec=%d idx=%d tLeft=%d", pThis->nID, nSeconds, nShadowIndex, pThis->tLeft);
        ++g_shadowRefreshLogCount;
    }
}

void RefreshBuffShadow(TemporaryStat* pThis) {
    __try {
        RefreshBuffShadowBody(pThis);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("RefreshBuffShadow SEH id=%d", pThis ? pThis->nID : -1);
    }
}

void __fastcall TEMPORARY_STAT__UpdateShadowIndex_hook(TemporaryStat* pThis, void* /*edx*/) {
    // Performance: do NOT rebuild cooltime canvas per buff per frame (buffer = freeze).
    // Overlay GDI text at 500ms is the sole countdown display.
    (void)pThis;
}

static auto CTemporaryStatView__Update =
    reinterpret_cast<void*(__thiscall*)(void*)>(0x007B2829);

void* __fastcall CTemporaryStatView__Update_hook(void* pThis, void* /*edx*/) {
    void* result = CTemporaryStatView__Update(pThis);
    // Single global snapshot ≤1/500ms for the whole buff bar (not per entry).
    PublishTimerSnapshotFromGameThread(false);
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
    if (!stack || !stack->pCanvas || nIndex < 0 || !stack->pnLastIndex || *stack->pnLastIndex == nSeconds) {
        return;
    }

    if (!g_loggedSkillCooldown && nSeconds > 0) {
        g_loggedSkillCooldown = true;
        Log("skill cooltime draw sec=%d idx=%d canvas=%p", nSeconds, nIndex, stack->pCanvas);
    }

    *stack->pnLastIndex = nSeconds;
    stack->pCanvas->Copy(
        stack->nCanvasX,
        stack->nCanvasY,
        CUIStatusBar::GetInstance()->m_aCanvasSkillCooltime[nIndex]);
    DrawSkillCooldownNumber(
        stack->pCanvas,
        stack->nCanvasX,
        stack->nCanvasY,
        nSeconds,
        stack->pStatusBar ? stack->pStatusBar : CUIStatusBar::GetInstance());
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
    // Must resolve beside ijl15.dll — cwd may not be the client folder (shortcut).
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
    return reader.GetBoolean("optional", "enableBuffTimer", true);
}

void AttachHooksOnce() {
    if (g_hooksAttached) {
        return;
    }
    if (!IsEnabledByConfig()) {
        Log("disabled by config optional.enableBuffTimer=false");
        g_hooksAttached = true; // stop retry loop
        return;
    }

    if (!g_timerLockReady) {
        InitializeCriticalSection(&g_timerLock);
        g_timerLockReady = true;
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

    StartOverlayThread();
    g_hooksAttached = shadowHook;

    Log("hooks attached shadow=%d update=%d module=%p overlayThread=1 snapshot=1", shadowHook ? 1 : 0, updateHook ? 1 : 0, g_module);
}

DWORD WINAPI InitThreadProc(LPVOID) {
    // Single delayed attach (V95 uses 12s). Avoid polling TempStat from worker threads.
    Sleep(kStartupDelayMs);
    if (!g_hooksAttached) {
        AttachHooksOnce();
    }
    Log("init complete hooks=%d overlay=%p inGame=%d", g_hooksAttached ? 1 : 0, g_overlay,
        IsInGameForOverlay() ? 1 : 0);
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
