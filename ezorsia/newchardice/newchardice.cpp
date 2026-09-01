// newchardice.cpp -- GMS083 starter AP + adventurer create-char dice UI/packet
// IDA MCP verified (BeiDou.exe / BeiDou-Client_S9, imagebase 0x400000, 2026-08-28):
//   starter AP: 0x8C6172 / 0x8C7A6D / 0x8C61A5
//   CREATE_CHAR send: 0x5F7F40 -> sub_5F6932; Encode1 0x406549; job@+532
//   Name window ctor sub_617AAA: CreateWnd 201x224 @ push imm 0x617AE8/0x617AED
//   Buttons confirm/cancel Y imm 0x617C1F / 0x617C94 (was 178); confirm X push 0x617C23
//   Vtables: CWnd Draw slot AF7564+0x2C; IUIMsgHandler OnMouseButton AF7518+0x08
//   Global name wnd ptr: dword_BEDA44
// Existing hook addresses unchanged.

#include "stdafx.h"
#include "NewCharDiceApi.h"
#include "Client.h"
#include "Memory.h"
#include "compat/hook.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/WzLib/IWzCanvas.h"
#include "ztl/ztl.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

constexpr uintptr_t kStarterApLevelGate = 0x008C6172;
constexpr uintptr_t kStarterApButtonGate = 0x008C7A6D;
constexpr uintptr_t kStarterApOverlayGate = 0x008C61A5;

constexpr uintptr_t kCreateCharSendCall = 0x005F7F40;
constexpr uintptr_t kCreateCharSendOrig = 0x005F6932;
constexpr uintptr_t kEncode1 = 0x00406549;
constexpr int kJobOffset = 532;

constexpr uintptr_t kNameWndGlobal = 0x00BEDA44;
constexpr uintptr_t kCreateWndHeightImm = 0x00617AE8; // was 0xE0 (224)
constexpr uintptr_t kBtnConfirmYImm = 0x00617C1F;     // was 0xB2 (178)
constexpr uintptr_t kBtnCancelYImm = 0x00617C94;      // was 0xB2
constexpr uintptr_t kBtnConfirmXOpcode = 0x00617C23;  // 6A 1B -> 6A 13

constexpr uintptr_t kVtableCWnd = 0x00AF7564;
constexpr uintptr_t kVtableMsg = 0x00AF7518;
constexpr size_t kDrawSlot = 0x2C;
constexpr size_t kMouseSlot = 0x08;

constexpr uintptr_t kDrawNumberByImage = 0x00988345;
constexpr uintptr_t kInvalidateRect = 0x009E04C9;

constexpr int kWindowWidth = 201;
constexpr int kWindowHeight = 332;
constexpr int kButtonY = 286;
constexpr int kConfirmButtonX = 19;
constexpr RECT kDiceHitRect{106, 146, 190, 242};

constexpr std::array<unsigned char, 5> kOriginalLevelGate{0xE8, 0x66, 0xE5, 0xBA, 0xFF};
constexpr std::array<unsigned char, 5> kPatchedLevelGate{0xE9, 0x9D, 0x01, 0x00, 0x00};
constexpr unsigned char kOriginalButtonGate = 0x0A;
constexpr unsigned char kPatchedButtonGate = 0x00;
constexpr unsigned char kOriginalOverlayGate = 0x0A;
constexpr unsigned char kPatchedOverlayGate = 0x00;
constexpr std::array<unsigned char, 5> kOriginalCreateSendCall{0xE8, 0xED, 0xE9, 0xFF, 0xFF};

constexpr int kMinimumStat = 4;
constexpr int kMaximumStat = 13;
constexpr int kRequiredTotal = 25;
constexpr DWORD kAnimFrameMs = 50;
constexpr DWORD kAnimDurationMs = 450;

struct StarterStats {
    unsigned char str = 4;
    unsigned char dex = 4;
    unsigned char intelligence = 4;
    unsigned char luk = 4;

    int Total() const {
        return static_cast<int>(str) + dex + intelligence + luk;
    }
    static bool IsInRange(unsigned char v) {
        return v >= kMinimumStat && v <= kMaximumStat;
    }
    bool IsValid() const {
        return IsInRange(str) && IsInRange(dex) && IsInRange(intelligence) && IsInRange(luk)
            && Total() == kRequiredTotal;
    }
};

StarterStats g_stats{};
bool g_has_roll = false;
bool g_starter_ap_applied = false;
bool g_create_hook_applied = false;
bool g_ui_hooks_applied = false;
bool g_layout_patched = false;
bool g_assets_loaded = false;

IWzCanvasPtr g_dice[4];
IWzPropertyPtr g_numberFont;
DWORD g_animStartTick = 0;
bool g_animating = false;
void* g_lastNameWnd = nullptr;

using Encode1Fn = void(__thiscall*)(void* packet, unsigned char value);
using SendCreateFn = void(__thiscall*)(void* uiThis, void* packet);
using DrawFn = void(__thiscall*)(void* wnd, const RECT* rect);
using MouseFn = void(__thiscall*)(void* handler, unsigned int msg, unsigned int wParam, int rx, int ry);
using DrawNumberByImageFn = int(__cdecl*)(void* canvas, int left, int top, int value, void* font, int spacing);
using InvalidateFn = void(__thiscall*)(void* wnd, const RECT* rect);

Encode1Fn g_Encode1 = reinterpret_cast<Encode1Fn>(kEncode1);
SendCreateFn g_OrigSendCreate = reinterpret_cast<SendCreateFn>(kCreateCharSendOrig);
DrawFn g_OrigDraw = nullptr;
MouseFn g_OrigMouse = nullptr;

void DiceLog(const char* format, ...) {
    char msg[512];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[640];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strcat_s(path, MAX_PATH, "newchardice_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
}

bool ExpectBytes(uintptr_t va, const unsigned char* expected, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (*reinterpret_cast<unsigned char*>(va + i) != expected[i]) {
            return false;
        }
    }
    return true;
}

void PatchMemory(uintptr_t va, const unsigned char* bytes, size_t n) {
    Memory::WriteByteArray(static_cast<DWORD>(va), const_cast<unsigned char*>(bytes), static_cast<int>(n));
}

bool PatchImm32(uintptr_t immAddr, unsigned int expect, unsigned int value, const char* label) {
    const unsigned int before = *reinterpret_cast<unsigned int*>(immAddr);
    if (before == value) {
        DiceLog("SKIP %s already %u", label, value);
        return true;
    }
    if (before != expect) {
        DiceLog("SKIP %s @ 0x%08X expect %u found %u", label, static_cast<unsigned>(immAddr), expect, before);
        return false;
    }
    Memory::WriteInt(static_cast<DWORD>(immAddr), value);
    DiceLog("OK %s %u -> %u", label, expect, value);
    return true;
}

StarterStats RollStarterStats() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 9);
    for (int attempt = 0; attempt < 4096; ++attempt) {
        StarterStats stats{
            static_cast<unsigned char>(4 + dist(rng)),
            static_cast<unsigned char>(4 + dist(rng)),
            static_cast<unsigned char>(4 + dist(rng)),
            static_cast<unsigned char>(4 + dist(rng))};
        if (stats.IsValid()) {
            return stats;
        }
    }
    return {4, 4, 4, 13};
}

void EnsureRoll() {
    if (!g_has_roll || !g_stats.IsValid()) {
        g_stats = RollStarterStats();
        g_has_roll = g_stats.IsValid();
    }
}

void RerollWithAnim() {
    g_stats = RollStarterStats();
    g_has_roll = g_stats.IsValid();
    g_animStartTick = GetTickCount();
    g_animating = true;
    DiceLog("reroll STR=%u DEX=%u INT=%u LUK=%u",
            g_stats.str, g_stats.dex, g_stats.intelligence, g_stats.luk);
}

void* GetActiveNameWnd() {
    return *reinterpret_cast<void**>(kNameWndGlobal);
}

bool LoadAssets() {
    if (g_assets_loaded) {
        return g_dice[0] != nullptr;
    }
    g_assets_loaded = true;
    try {
        if (!get_rm()) {
            DiceLog("WARN get_rm null");
            return false;
        }
        static const wchar_t* kDicePaths[4] = {
            L"UI/Login.img/NewChar/dice/0",
            L"UI/Login.img/NewChar/dice/1",
            L"UI/Login.img/NewChar/dice/2",
            L"UI/Login.img/NewChar/dice/3",
        };
        for (int i = 0; i < 4; ++i) {
            g_dice[i] = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(kDicePaths[i])));
            if (!g_dice[i]) {
                DiceLog("WARN missing dice canvas %d", i);
            }
        }
        g_numberFont = get_rm()->GetObjectA(const_cast<wchar_t*>(L"UI/Basic.img/LevelNo")).GetUnknown();
        if (!g_numberFont) {
            g_numberFont = get_rm()->GetObjectA(const_cast<wchar_t*>(L"UI/Basic.img/LevelNo/number")).GetUnknown();
        }
        DiceLog("assets dice0=%d font=%d", g_dice[0] ? 1 : 0, g_numberFont ? 1 : 0);
    } catch (...) {
        DiceLog("WARN LoadAssets exception");
        return false;
    }
    return g_dice[0] != nullptr;
}

void DrawValue(IWzCanvas* canvas, int x, int y, int value) {
    if (!canvas || !g_numberFont || value < 0) {
        return;
    }
    auto drawNumber = reinterpret_cast<DrawNumberByImageFn>(kDrawNumberByImage);
    try {
        drawNumber(canvas, x, y, value, g_numberFont.GetInterfacePtr(), -1);
    } catch (...) {
    }
}

void DrawDiceFrame(IWzCanvas* canvas, int frame) {
    if (!canvas || frame < 0 || frame > 3 || !g_dice[frame]) {
        return;
    }
    IWzCanvas* src = g_dice[frame];
    int w = 0;
    int h = 0;
    try {
        w = static_cast<int>(src->Getwidth());
        h = static_cast<int>(src->Getheight());
    } catch (...) {
        return;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    const int cx = (kDiceHitRect.left + kDiceHitRect.right) / 2;
    const int cy = (kDiceHitRect.top + kDiceHitRect.bottom) / 2;
    const int dx = cx - w / 2;
    const int dy = cy - h / 2;
    try {
        canvas->CopyEx(dx, dy, src, CANVAS_ALPHATYPE::CA_OVERWRITE, w, h, 0, 0, w, h);
    } catch (...) {
    }
}

void PaintDiceUi(void* wnd) {
    if (!wnd || !Client::enableNativeAdventurerDice) {
        return;
    }
    EnsureRoll();
    if (!LoadAssets()) {
        return;
    }

    IWzCanvasPtr canvas;
    try {
        canvas = reinterpret_cast<CWnd*>(wnd)->GetCanvas();
    } catch (...) {
        return;
    }
    if (!canvas) {
        return;
    }

    int frame = 0;
    if (g_animating) {
        const DWORD elapsed = GetTickCount() - g_animStartTick;
        if (elapsed >= kAnimDurationMs) {
            g_animating = false;
            frame = 0;
        } else {
            frame = static_cast<int>((elapsed / kAnimFrameMs) % 4);
        }
    }
    DrawDiceFrame(canvas, frame);
    if (!g_animating && g_has_roll) {
        DrawValue(canvas, 70, 161, g_stats.str);
        DrawValue(canvas, 70, 180, g_stats.dex);
        DrawValue(canvas, 70, 199, g_stats.intelligence);
        DrawValue(canvas, 70, 218, g_stats.luk);
    }
}

void InvalidateNameWnd(void* wnd) {
    if (!wnd) {
        return;
    }
    reinterpret_cast<InvalidateFn>(kInvalidateRect)(wnd, nullptr);
}

void __fastcall Hook_Draw(void* wnd, void* /*edx*/, const RECT* rect) {
    if (g_OrigDraw) {
        g_OrigDraw(wnd, rect);
    }
    if (wnd && wnd == GetActiveNameWnd()) {
        PaintDiceUi(wnd);
    }
}

void __fastcall Hook_OnMouseButton(void* handler, void* /*edx*/, unsigned int msg, unsigned int wParam, int rx, int ry) {
    void* wnd = handler ? reinterpret_cast<char*>(handler) - 4 : nullptr;
    if (Client::enableNativeAdventurerDice && wnd && wnd == GetActiveNameWnd() && msg == WM_LBUTTONDOWN) {
        if (rx >= kDiceHitRect.left && rx < kDiceHitRect.right &&
            ry >= kDiceHitRect.top && ry < kDiceHitRect.bottom) {
            RerollWithAnim();
            InvalidateNameWnd(wnd);
            return;
        }
    }
    if (g_OrigMouse && reinterpret_cast<uintptr_t>(g_OrigMouse) != 0x00424443) {
        g_OrigMouse(handler, msg, wParam, rx, ry);
    }
}

void __fastcall Hook_SendCreateChar(void* uiThis, void* /*edx*/, void* packet) {
    if (Client::enableNativeAdventurerDice && uiThis && packet) {
        const int job = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(uiThis) + kJobOffset);
        if (job == 1) {
            EnsureRoll();
            if (g_has_roll && g_stats.IsValid()) {
                g_Encode1(packet, g_stats.str);
                g_Encode1(packet, g_stats.dex);
                g_Encode1(packet, g_stats.intelligence);
                g_Encode1(packet, g_stats.luk);
                DiceLog("append dice STR=%u DEX=%u INT=%u LUK=%u",
                        g_stats.str, g_stats.dex, g_stats.intelligence, g_stats.luk);
            } else {
                DiceLog("WARN create adventurer but no valid roll");
            }
        }
    }
    g_OrigSendCreate(uiThis, packet);
}

void ApplyStarterApPatches() {
    if (g_starter_ap_applied) {
        return;
    }

    if (ExpectBytes(kStarterApLevelGate, kOriginalLevelGate.data(), kOriginalLevelGate.size())) {
        PatchMemory(kStarterApLevelGate, kPatchedLevelGate.data(), kPatchedLevelGate.size());
        DiceLog("OK starter_ap level_gate");
    } else if (ExpectBytes(kStarterApLevelGate, kPatchedLevelGate.data(), kPatchedLevelGate.size())) {
        DiceLog("SKIP starter_ap level_gate already patched");
    } else {
        DiceLog("SKIP starter_ap level_gate signature mismatch");
    }

    unsigned char* button_gate = reinterpret_cast<unsigned char*>(kStarterApButtonGate);
    if (*button_gate == kOriginalButtonGate) {
        Patch1(kStarterApButtonGate, kPatchedButtonGate);
        DiceLog("OK starter_ap button_gate");
    } else if (*button_gate != kPatchedButtonGate) {
        DiceLog("SKIP starter_ap button_gate mismatch 0x%02X", *button_gate);
    }

    unsigned char* overlay_gate = reinterpret_cast<unsigned char*>(kStarterApOverlayGate);
    if (*overlay_gate == kOriginalOverlayGate) {
        Patch1(kStarterApOverlayGate, kPatchedOverlayGate);
        DiceLog("OK starter_ap overlay_gate");
    } else if (*overlay_gate != kPatchedOverlayGate) {
        DiceLog("SKIP starter_ap overlay_gate mismatch 0x%02X", *overlay_gate);
    }

    g_starter_ap_applied = true;
}

void ApplyCreateCharDiceHook() {
    if (g_create_hook_applied || !Client::enableNativeAdventurerDice) {
        return;
    }
    if (!ExpectBytes(kCreateCharSendCall, kOriginalCreateSendCall.data(), kOriginalCreateSendCall.size())) {
        DiceLog("SKIP create_char send hook signature mismatch");
        return;
    }
    PatchCall(kCreateCharSendCall, &Hook_SendCreateChar);
    EnsureRoll();
    DiceLog("OK create_char send hook initial STR=%u DEX=%u INT=%u LUK=%u",
            g_stats.str, g_stats.dex, g_stats.intelligence, g_stats.luk);
    g_create_hook_applied = true;
}

void ApplyNameWindowLayoutPatches() {
    if (g_layout_patched || !Client::enableNativeAdventurerDice) {
        return;
    }
    PatchImm32(kCreateWndHeightImm, 0xE0, static_cast<unsigned>(kWindowHeight), "name_wnd_height");
    PatchImm32(kBtnConfirmYImm, 0xB2, static_cast<unsigned>(kButtonY), "btn_confirm_y");
    PatchImm32(kBtnCancelYImm, 0xB2, static_cast<unsigned>(kButtonY), "btn_cancel_y");

    unsigned char* confirmX = reinterpret_cast<unsigned char*>(kBtnConfirmXOpcode);
    if (confirmX[0] == 0x6A && confirmX[1] == 0x1B) {
        Patch1(kBtnConfirmXOpcode + 1, static_cast<unsigned char>(kConfirmButtonX));
        DiceLog("OK btn_confirm_x 27 -> %d", kConfirmButtonX);
    } else if (confirmX[0] == 0x6A && confirmX[1] == static_cast<unsigned char>(kConfirmButtonX)) {
        DiceLog("SKIP btn_confirm_x already patched");
    } else {
        DiceLog("SKIP btn_confirm_x mismatch %02X %02X", confirmX[0], confirmX[1]);
    }
    g_layout_patched = true;
}

bool PatchVtableSlot(uintptr_t slotAddr, void* detour, void** outOrig, const char* label) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(slotAddr), sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        DiceLog("FAIL VirtualProtect %s", label);
        return false;
    }
    void* before = *reinterpret_cast<void**>(slotAddr);
    if (outOrig && !*outOrig) {
        *outOrig = before;
    }
    if (before == detour) {
        VirtualProtect(reinterpret_cast<void*>(slotAddr), sizeof(void*), oldProtect, &oldProtect);
        DiceLog("SKIP %s already hooked", label);
        return true;
    }
    *reinterpret_cast<void**>(slotAddr) = detour;
    VirtualProtect(reinterpret_cast<void*>(slotAddr), sizeof(void*), oldProtect, &oldProtect);
    DiceLog("OK %s vtable %p -> %p", label, before, detour);
    return true;
}

void ApplyUiHooks() {
    if (g_ui_hooks_applied || !Client::enableNativeAdventurerDice) {
        return;
    }
    ApplyNameWindowLayoutPatches();
    PatchVtableSlot(kVtableCWnd + kDrawSlot, reinterpret_cast<void*>(&Hook_Draw),
                    reinterpret_cast<void**>(&g_OrigDraw), "name_wnd_Draw");
    PatchVtableSlot(kVtableMsg + kMouseSlot, reinterpret_cast<void*>(&Hook_OnMouseButton),
                    reinterpret_cast<void**>(&g_OrigMouse), "name_wnd_OnMouseButton");
    g_ui_hooks_applied = true;
}

} // namespace

bool NewCharDice::IsEnabled() {
    return Client::enableNativeAdventurerDice;
}

void NewCharDice::ApplyPatches() {
    ApplyStarterApPatches();
    ApplyCreateCharDiceHook();
    ApplyUiHooks();
}

void NewCharDice::OnClientTick() {
    if (!Client::enableNativeAdventurerDice) {
        return;
    }
    void* wnd = GetActiveNameWnd();
    if (wnd && wnd != g_lastNameWnd) {
        g_lastNameWnd = wnd;
        EnsureRoll();
        LoadAssets();
        if (!g_animating) {
            // brief spin so the player sees dice on first open
            g_animStartTick = GetTickCount();
            g_animating = true;
        }
        InvalidateNameWnd(wnd);
        DiceLog("name wnd open %p stats %u/%u/%u/%u", wnd,
                g_stats.str, g_stats.dex, g_stats.intelligence, g_stats.luk);
    } else if (!wnd && g_lastNameWnd) {
        DiceLog("name wnd close");
        g_lastNameWnd = nullptr;
        g_animating = false;
    } else if (wnd && g_animating) {
        InvalidateNameWnd(wnd);
    }
}

void AttachNewCharDiceMod() {
    NewCharDice::ApplyPatches();
}
