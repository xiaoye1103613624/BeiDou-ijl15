// ============================================================
// equipaddon.cpp — 附加装备栏 as IWzGr2DLayer (Equip overlay path).
//
// HARD RULE 2026-08-04 ~20:54（用户）:
//   「不要改原版地址，自定义的可以改」
//   FORBIDDEN: patching vanilla BeiDou.exe CharacterData field offsets
//   (skill-cool +0x5FF/+0x4CB, cash lea, SAFEIMM displ×N, CTOR_FIELDS, …).
//   REQUIRED (PLUGIN_ONLY stopgap): −54…−62 via THIS plugin (sidecar/caves).
//   CD64_TEST lab pair: EXE ACF10F63 (vanilla layout) + DLL FA26021C family.
//   DetectCd64 / native Route A = SUSPENDED while EXE grow is forbidden.
//
// 2026-08-07 product intent — source-driven COMPOSE+APPEND54 (no bak roulette):
//   Vanilla ACF EXE + plugin sidecar −54…−62. kEnableAddon* ON for Layer dock +
//   Apply/GetSet caves (54–62). ExtraRing −52/−53 = shoulders HT. Origin skip =
//   rs.cpp follow_login/!grChanged (no PE OriginJe NOP). Forbidden: LoginPersist
//   in DllMain, ForceDrawLoop, stub-smash grown ship, FA bak as deliverable.
//
// Stamp (source): ADDON_GETSET_UIHOOKS_20260808
//   Prefer frozen-obj link + stub-safe; full MSBuild OK only if stub@AB30C=53 E8.
//   Docs: docs/IDA_BRIEF_COMPOSE_* / ADDON_APPEND_ONLY_EXTEND.md
//   2026-08-08b: BAAD6396 blackscreen = all-flags + polluted dllmain Early.
//   2026-08-08c: GetSet+ApplyCaves green (4F11795E). Open UiHooks only (anti-pierce /
//   bag-dbl totemx4). Layer+OnTick deferred — full ON → ~1500672 ≈ BAAD size family.
//   Park OFF. No LoginPersist Early. ExtraRing −52/−53 stays shoulders.
// ============================================================

#include "stdafx.h"
#include "EquipAddonApi.h"
#include "ShoulderApi.h"
#include "compat/hook.h"
#include "Memory.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/field.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include "compat/wvs/statusbar.h"
#include "compat/ztl/ztl.h"
#include "compat/ztl/zcom.h"
#include "sidetoolbar/SideToolbarApi.h"
#include <windows.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>

// File-scope trampoline so shoulders can link EquipAddon_SidecarZRefForBp.
static void* (__cdecl* g_sidecarZRefFn)(int) = nullptr;

namespace {

// ENTERSAFE: deploy ONLY from green-obj frozen set / zero-growth JG_ONLY lineage.
// TOTEM2 / EQUIP_TIP_* / SENDBUSY full-chain rebuilds → select→enter 0xC0000409.
// BP62 peer caves MUST be PIC (dll_base+RVA) or HIGHLOW-reloc'd — never bare 0x10xxxxxx.
// Forbidden redeploy: ENTER-RED 0F244A5D / 73559AC5 / incomplete PEER_COMPLETE EADEF4A8.
// PEER_COMPLETE remapped aux imm but left ApplyEquip at 56–62 + wear_fn → native
// aEquipped[54/55] aliases cash face/eye (−102/−103); aEquipped[62] OOB multi-ghost.
constexpr const char* kStamp = "ADDON_GETSET_UIHOOKS_20260808";
// Feature gates (must be near top — used before EnsureLayer/Teardown).
// 2026-08-08c: GetSet+ApplyCaves kept; UiHooks ON (anti-pierce / bag dbl / totem full).
// Layer+OnTick OFF this ship — linking them pulled ~1500672 (BAAD-adjacent). Park OFF.
constexpr bool kEnableAddonLayer = false;
constexpr bool kEnableAddonOnTick = false;
constexpr bool kEnableAddonPark = false;
constexpr bool kEnableAddonUiHooks = true;
constexpr bool kEnableAddonGetSetHooks = true;
constexpr bool kEnableAddonApplyCaves = true;
// Keep ROW3_WEAR_UNEQUIP marker for static enter-risk gate (forbid regress).
constexpr const char* kRow3WearMarker = "ADDON_ROW3_WEAR_UNEQUIP_20260802";
// Survives /OPT:REF — proves GetItem normal-only path is linked.
constexpr const char* kGetItemNormalMarker = "SidecarBpFromSlotNormal";
// Survives /OPT:REF — proves logout UI teardown hook is linked.
constexpr const char* kLogoutUiMarker = "DestroyGameUI_A041FF";
// Survives /OPT:REF — CD64 native inventory + reused Addon dock (paint/HitTest).
constexpr const char* kNativeUiMarker = "NATIVE_CD64_ADDON_UI_REUSE";
// Survives /OPT:REF — bodypart gate + no classic HT cash smash.
constexpr const char* kWearFixMarker = "WEAR_FIX_BODYPART_HT50";
// Survives /OPT:REF — cash −(bp+100) paint/PacketSlot append (not alias).
constexpr const char* kCashAppendMarker = "ADDON_CASH_APPEND_BP100";
// Hooked is_correct_bodypart @460358 (shoulders Detours) — type↔seat gate.
using IsCorrectBodypartFn = int(__cdecl*)(int nItemID, int nBodyPart, int nGender);
auto IsCorrectBodypart =
        reinterpret_cast<IsCorrectBodypartFn>(0x00460358);
// CWvsContext::DestroyGameUI — set_stage→login/charselect (LABEL_16) @0xA041FF.
constexpr uintptr_t kAddr_DestroyGameUi = 0x00A041FF;
// Known leftover singletons if set_stage skipped A041FF (CharData already null).
constexpr uintptr_t kCUIItemSingleton = 0x00BED654;     // bag
constexpr uintptr_t kCUIStatusBarSingleton = 0x00BEC208;
constexpr uintptr_t kCUIMiniMapSingleton = 0x00BED788;
constexpr uintptr_t kStageSingletonPtr = 0x00BEDED4; // ZRef<CStage>.p
constexpr uintptr_t kRtti_CField = 0x00BED758;
constexpr uintptr_t kRtti_CInterStage = 0x00BED874;
constexpr uintptr_t kRtti_CashShop = 0x00BEC310;
constexpr uintptr_t kRtti_AltField = 0x00BED87C;
// Route A: when CD64 EXE + Shoulder_UseNativeCd64Slots, sidecar is UI-only (no inventory).
static bool NativeCd64Inventory() {
    return Shoulder_UseNativeCd64Slots();
}
static bool DetectCd64ExeLite() {
    auto* p = reinterpret_cast<const unsigned char*>(0x00778F02);
    return p[0] == 0x68 && p[1] == 0x00 && p[2] == 0x07 && p[3] == 0x00 && p[4] == 0x00;
}
constexpr int kEquipUIType = 1; // CUIEquip ctor sub_7FDE7C: CUIWnd(nUIType=1, …)
// SendBusy watchdog: never leave ctx[2089] stuck if enableActions is missed.
// Age uses GetTickCount (not game GetUpdateTime) — game clock AV/GS before field.
// Narrow unstick only — do NOT clear mid-flight inventory ops (was 50/80ms).
constexpr DWORD kSendBusyUnstickAgeMs = 400u;
constexpr ULONGLONG kSendBusyWatchdogMs = 1500ull;
constexpr int kKeyConfigUIType = 5; // CUIKeyConfig — never dock
constexpr uintptr_t kAddr_CUIStatusBar_Chat = 0x008DB070;
constexpr uintptr_t kAddr_DraggableItem_OnDropped = 0x004EF140;
constexpr uintptr_t kAddr_OnDoubleClicked = 0x004EFD25;
constexpr uintptr_t kAddr_WearFromDraggable = 0x004F1C2D;
constexpr uintptr_t kAddr_CInputSystem_GetCursorPos = 0x0059A388;
constexpr uintptr_t kAddr_GameHwnd = 0x00BD0234;
constexpr uintptr_t kAddr_CharacterData_SetItem = 0x0047B05A;
constexpr uintptr_t kAddr_ZRef_Assign = 0x00428285;
constexpr uintptr_t kAddr_ApplyEquipZRefLea = 0x004E5D55; // 7-byte lea → ZRef assign
constexpr uintptr_t kAddr_ApplyEquipZRefCont = 0x004E5D5C;
// Apply slot-max sites — READ ONLY for diagnostics. Never raise past 55 here.
constexpr uintptr_t kAddr_ApplySlotMaxA = 0x004E5D0C; // cmp esi, imm8 (login decode)
constexpr uintptr_t kAddr_ApplySlotMaxB = 0x004E5DCC; // cash path cmp esi, imm8
// Login decode: cmp esi,max / jg skip — allow 1..53 native/shadow + 54..62 sidecar.
constexpr uintptr_t kAddr_LoginAllowA = 0x004E5D0C; // 5B: cmp+jg → cave
constexpr uintptr_t kAddr_LoginAllowA_Cont = 0x004E5D11;
constexpr uintptr_t kAddr_LoginAllowA_Skip = 0x004E5D61;
constexpr uintptr_t kAddr_LoginAllowB = 0x004E5DCC; // cash path
constexpr uintptr_t kAddr_LoginAllowB_Cont = 0x004E5DD1;
constexpr uintptr_t kAddr_LoginAllowB_Skip = 0x004E5E24;
constexpr uintptr_t kAddr_CashApplyZRefLea = 0x004E5E18; // CD64 EXE:+0x2E7; vanilla:+0x287
constexpr uintptr_t kAddr_CashApplyZRefCont = 0x004E5E1F; // call ZRef_Assign
constexpr uintptr_t kAddr_LoginClearEquip = 0x004E5CC0; // push 34h — clear aEquipped
constexpr uintptr_t kAddr_LoginClearEquipCont = 0x004E5CC9; // after pop ebx
constexpr int kChatHintType = 0xC; // CHAT_TYPE_SYSTEM (slotlock)
// CWvsContext DWORD indices (Hex-Rays this+_DWORD*): busy + last-send tick.
constexpr int kCtxDw_SendBusy = 2089;
constexpr int kCtxDw_LastSendTick = 2090;
// Pocket −33 + Aux −62 on Addon row3 (true split from shield −10).
constexpr int kPocketBp = 33;   // 116xxxx Po → Addon row3
// Aux 134/135 → BP62/−62 (true split from shield −10). Tip restored 2026-08-04.
constexpr int kSubWeaponBp = 62; // 134/135 aux — sidecar with BP56–61
constexpr int kAndroidBp = 60;  // 166xxxx Dr — sidecar (NOT pet BP21)
constexpr int kHeartBp = 61;    // 167xxxx Ht — sidecar (NOT pet BP22)
// BP54/55 MUST be sidecar on vanilla 52-slot aEquipped: native −55 aliases cash eye (−103).
constexpr int kSidecarBpMin = 54; // badge + totem1… + sidecar … + aux
constexpr int kSidecarBpMax = 62; // incl. aux −62; jg-allow/apply caves must match
// Legacy red9/10 coords kept only for dead-code path removal / probe logs.
constexpr int kRed9X = 104;
constexpr int kRed9Y = 200;
constexpr int kRed10X = 137;
constexpr int kRed10Y = 200;
// GBK: "物品与槽位不符"
static const char kMismatchSeatMsg[] = {
    '\xCE', '\xEF', '\xC6', '\xB7', '\xD3', '\xEB', '\xB2', '\xDB', '\xCE', '\xBB',
    '\xB2', '\xBB', '\xB7', '\xFB', '\0'
};
// GBK: "图腾栏已满（最多4件）"
static const char kTotemFullMsg[] = {
    '\xCD', '\xBC', '\xCC', '\xA4', '\xC0', '\xB8', '\xD2', '\xD1', '\xC2', '\xFA',
    '\xA3', '\xA8', '\xD7', '\xEE', '\xB6', '\xE0', '4', '\xBC', '\xFE', '\xA3', '\xA9',
    '\0'
};
// GBK: "该栏位已有装备，请先卸下再穿戴"
static const char kOccupySeatMsg[] = {
    '\xB8', '\xC3', '\xC0', '\xB8', '\xCE', '\xBB', '\xD2', '\xD1', '\xD3', '\xD0',
    '\xD7', '\xB0', '\xB1', '\xB8', '\xA3', '\xAC', '\xC7', '\xEB', '\xCF', '\xC8',
    '\xD0', '\xB6', '\xCF', '\xC2', '\xD4', '\xD9', '\xB4', '\xA9', '\xB4', '\xF7',
    '\0'
};

// Real CUIEquip (NOT KeyConfig BED650). PetEquip = BED648 (177×181 @ Equip+172).
constexpr uintptr_t kCUIEquipSingleton = 0x00BED64C;
constexpr uintptr_t kCUIKeyConfigSingleton = 0x00BED650;
constexpr uintptr_t kCUIPetEquipSingleton = 0x00BED648;
constexpr uintptr_t kCWvsContextSingleton = 0x00BE7918;
constexpr uintptr_t kOff_CharData_InCtx = 0x20B8;
constexpr uintptr_t kOff_CWvs_UIEquip_Ptr = 0x35C0;
constexpr uintptr_t kCUIEquip_VTable = 0x00B389E8;
constexpr uintptr_t kCUIKeyConfig_VTable = 0x00B397B8;
constexpr uintptr_t kAddr_CharacterData_GetItem = 0x004282F7;
constexpr uintptr_t kAddr_SendChangeSlotPosition = 0x00A0900A;
constexpr uintptr_t kAddr_TSecType_long_GetData = 0x0042873D;
constexpr uintptr_t kAddr_ShowItemToolTip = 0x008F5B20;
constexpr uintptr_t kAddr_InputSystem = 0x00BEC33C;

// Main Equip slot tables — park Addon-owned BPs off-panel (red 9/10 = BP54/55).
constexpr uintptr_t kGetSlotXyTable = 0x00BE2580; // index = BP-1
constexpr uintptr_t kClassicHitTestTable = 0x00BE2260; // index = BP-1 (PE; ≤49 only)
constexpr uintptr_t kBe27Table = 0x00BE27E0;      // index = BP
constexpr int kOffPanelX = -2000;
constexpr int kOffPanelY = -2000;

constexpr int kPanelW = 141;
constexpr int kPanelH = 204; // 168 + row3 (y=147 + icon + margin)
constexpr int kIcon = 32;
// PetEquip CreateWnd z=10; keep Addon well above within Equip overlay tree.
constexpr int kLayerZ = 3000;

struct Seat {
    int bp;
    int sx;
    int sy;
};

// Addon dock 2×4 + row3 pocket/aux (right two cells empty).
constexpr Seat kSeats[] = {
    {55, 6, 33},
    {56, 39, 33},
    {57, 72, 33},
    {58, 105, 33},
    {59, 6, 90},
    {60, 39, 90},
    {61, 72, 90},
    {54, 105, 90},
    {33, 6, 147},   // Pocket 116 → −33
    {62, 39, 147},  // Aux 134|135 → −62 (never 109 / never −10)
};

IWzGr2DLayerPtr g_layer;
IWzCanvasPtr g_canvas;
bool g_visible = false;
int g_lastEqX = INT_MIN;
int g_lastEqY = INT_MIN;
int g_hoverBp = 0;
bool g_tipOn = false;
bool g_loggedCreate = false;
bool g_initLogged = false;
ULONGLONG g_lastDiag = 0;
ULONGLONG g_lastKeyLog = 0;
ULONGLONG g_ePressedAt = 0;
bool g_eWasDown = false;
int g_addonScreenX = 0;
int g_addonScreenY = 0;
int g_localDockX = 0; // RelMove relative to Equip.lt after Putoverlay
int g_localDockY = 0;
void* g_cachedEquip = nullptr;
const char* g_cacheSrc = "none";
alignas(8) unsigned char g_tipBuf[0xA00]{};
bool g_tipReady = false;
using TTFn = void(__thiscall*)(void*);
auto TT_Ctor = reinterpret_cast<TTFn>(0x008E49B5);
auto TT_Clear = reinterpret_cast<TTFn>(0x008E6E23);

// Sidecar ZRef storage for BP56–62 (never touches aEquipped[56+]).
alignas(8) void* g_sidecarZRef[63][2]{}; // [bp][0]=pad, [bp][1]=pItem; bp0..62 (aux −62)
// Zero-growth JG_ONLY binary has [62][2] only — ASLR peer parks bp62 ZRef in .data BSS
// (RVA 0x14B930) via PIC cave; full rebuild uses this array directly.
bool g_getItemHooked = false;
bool g_setItemHooked = false;
bool g_applyCaveDone = false;
bool g_loginAllowDone = false;
bool g_cashLeaDone = false;
bool g_loginClearDone = false;
void* g_prevApplyLea = nullptr; // prior cave (shoulders rings 52–53) or nullptr
void* g_prevCashLea = nullptr;

using OnDroppedFn = int(__thiscall*)(void* pThis, void* pFrom, void* pTo, int rx, int ry);
OnDroppedFn g_OnDroppedOrig =
        reinterpret_cast<OnDroppedFn>(kAddr_DraggableItem_OnDropped);
bool g_dropHooked = false;

using DestroyGameUiFn = int(__thiscall*)(void* pCtx);
DestroyGameUiFn g_DestroyGameUiOrig =
        reinterpret_cast<DestroyGameUiFn>(kAddr_DestroyGameUi);
bool g_logoutUiHooked = false;
bool g_forcedLogoutUiOnce = false;

using OnDoubleClickedFn = int(__thiscall*)(void* pDraggable);
OnDoubleClickedFn g_OnDblOrig =
        reinterpret_cast<OnDoubleClickedFn>(kAddr_OnDoubleClicked);
bool g_dblHooked = false;
bool g_parkDone = false;

struct ZRefItemOut {
    void* unused;
    void* pItem;
};

using CharacterData_GetItemFn =
        void*(__thiscall*)(void* pCharData, ZRefItemOut* outRef, int nTI, int nPOS);
CharacterData_GetItemFn g_GetItemOrig =
        reinterpret_cast<CharacterData_GetItemFn>(kAddr_CharacterData_GetItem);

using CharacterData_SetItemFn =
        int(__thiscall*)(void* pCharData, int nTI, int nPOS, void* z0, void* pItem);
CharacterData_SetItemFn g_SetItemOrig =
        reinterpret_cast<CharacterData_SetItemFn>(kAddr_CharacterData_SetItem);

using ZRefAssignFn = void(__thiscall*)(void* pDestZRef, void* pSrcZRef);
auto ZRef_Assign = reinterpret_cast<ZRefAssignFn>(kAddr_ZRef_Assign);

using SendChangeSlotPositionFn =
        void(__thiscall*)(void* pCtx, int nTI, int nOldPos, int nNewPos, int nCount);
auto SendChangeSlotPosition =
        reinterpret_cast<SendChangeSlotPositionFn>(kAddr_SendChangeSlotPosition);

// Native wear@4F1C2D(this, bagOrEquipSlot, bodyPart) → SendChangeSlotPosition(..., -1).
using WearFromDraggableFn = int(__thiscall*)(void* pDrag, int nSlot, int nBodyPart);
WearFromDraggableFn g_WearOrig =
        reinterpret_cast<WearFromDraggableFn>(kAddr_WearFromDraggable);
bool g_wearHooked = false;

using TSecGetFn = int(__thiscall*)(const void*);
auto TSecType_long_GetData = reinterpret_cast<TSecGetFn>(kAddr_TSecType_long_GetData);

using GetCursorPosFn = BOOL(__thiscall*)(void* pInput, POINT* out);
auto CInputSystem_GetCursorPos =
        reinterpret_cast<GetCursorPosFn>(kAddr_CInputSystem_GetCursorPos);

using ShowItemToolTipFn =
        void(__thiscall*)(void*, int, int, void*, void*, void*, int, int, int);
auto ShowItemToolTip = reinterpret_cast<ShowItemToolTipFn>(kAddr_ShowItemToolTip);

std::mutex& LogMu() {
    static std::mutex m;
    return m;
}

// File I/O under DllMain loader lock correlated with SENDBUSY boot 0xC0000409.
bool g_dbgFileOk = false;

void Dbg(const char* msg) {
    if (!g_dbgFileOk || !msg) {
        return;
    }
    std::lock_guard<std::mutex> lock(LogMu());
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strcat_s(path, "equipaddon_debug.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            msg);
    fclose(f);
}

void* SafeReadPtr(uintptr_t addr) {
    void* p = nullptr;
    __try {
        p = *reinterpret_cast<void**>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p = nullptr;
    }
    return p;
}

DWORD SafeReadDword(uintptr_t addr) {
    DWORD v = 0;
    __try {
        v = *reinterpret_cast<DWORD*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = 0;
    }
    return v;
}

bool IsCUIEquipPtr(void* p) {
    if (!p) {
        return false;
    }
    __try {
        return *reinterpret_cast<uintptr_t*>(p) == kCUIEquip_VTable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void NoteEquipUi(void* p, const char* src) {
    if (!p || !IsCUIEquipPtr(p)) {
        return;
    }
    if (g_cachedEquip != p) {
        char buf[176];
        sprintf_s(buf, "Equip DETECTED via %s ptr=%p vtOK=1 BED64C=%p", src, p,
                  SafeReadPtr(kCUIEquipSingleton));
        Dbg(buf);
    }
    g_cachedEquip = p;
    g_cacheSrc = src;
}

void* GetEquipUiSingleton() {
    // dword_BED64C is authoritative CUIEquip* while Equip is open.
    return SafeReadPtr(kCUIEquipSingleton);
}

void* GetEquipUiFromCtx() {
    void* pCtx = SafeReadPtr(kCWvsContextSingleton);
    if (!pCtx) {
        return nullptr;
    }
    return SafeReadPtr(reinterpret_cast<uintptr_t>(pCtx) + kOff_CWvs_UIEquip_Ptr);
}

void* GetEquipUi() {
    // ENTER-SAFE: never return raw non-vtable (mid-ctor / select→enter race).
    // P12_POCKET_PAINT wire+paint could run on BED64C-raw and GS-fail enter.
    if (void* a = GetEquipUiSingleton()) {
        if (IsCUIEquipPtr(a)) {
            NoteEquipUi(a, "BED64C");
            return a;
        }
    }
    if (void* b = GetEquipUiFromCtx()) {
        if (IsCUIEquipPtr(b)) {
            NoteEquipUi(b, "ctxZRef");
            return b;
        }
    }
    // No call-site cache: if both live sources are null/invalid, Equip is closed.
    g_cachedEquip = nullptr;
    g_cacheSrc = "none";
    return nullptr;
}

CWnd* AsWnd(void* p) {
    return reinterpret_cast<CWnd*>(p);
}

bool EnsureLayer(CWnd* eq); // defined below

void* g_lastCharData = nullptr;
void ClearSidecarShadows(); // defined with sidecar helpers below

void* GetLocalCharacterData() {
    void* pCtx = SafeReadPtr(kCWvsContextSingleton);
    if (!pCtx) {
        return nullptr;
    }
    void* pChar = nullptr;
    __try {
        pChar = *reinterpret_cast<void**>(reinterpret_cast<char*>(pCtx) + kOff_CharData_InCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pChar = nullptr;
    }
    // NEVER ClearSidecar here — map/enter briefly nulls CharData and wiped
    // BP56–61 after login apply (looks like “Addon stripped on relog”).
    // Char-switch clear is owned by LoginClearSidecar_cave @ push-34h.
    if (!pChar) {
        g_lastCharData = nullptr;
        return nullptr;
    }
    g_lastCharData = pChar;
    return pChar;
}

bool CtxReadyForSendBusy(void* pCtx) {
    return pCtx != nullptr && GetLocalCharacterData() != nullptr;
}

// Normal −56..−62 only. Do NOT map cash −156..−162 here for GetItem:
// CUIEquip@7FE9BB fetches both −bp and −(100+bp); cash alias → same ZRef for both
// → dual tip sub_8EBA5B AV read@0x500 (live 2026-08-03 ~22:02/22:04 hover 点装).
int SidecarBpFromSlotNormal(int nPOS) {
    if (nPOS <= -kSidecarBpMin && nPOS >= -kSidecarBpMax) {
        return -nPOS;
    }
    return 0;
}

// SetItem / apply still accept cash −156..−161 → same sidecar ZRef (one seat).
int SidecarBpFromSlot(int nPOS) {
    if (const int bp = SidecarBpFromSlotNormal(nPOS)) {
        return bp;
    }
    if (nPOS <= -(kSidecarBpMin + 100) && nPOS >= -(kSidecarBpMax + 100)) {
        return -(nPOS + 100);
    }
    return 0;
}

bool IsSidecarBp(int bp) {
    // Sidecar range 54–62: badge/totem1…aux. Never native aEquipped (52-slot).
    return bp >= kSidecarBpMin && bp <= kSidecarBpMax;
}

void* SidecarZRefBase(int bp) {
    if (!IsSidecarBp(bp)) {
        return nullptr;
    }
    return &g_sidecarZRef[bp][0];
}

void* __cdecl SidecarZRefForBp(int bp) {
    return SidecarZRefBase(bp);
}

struct SidecarExportInit {
    SidecarExportInit() { g_sidecarZRefFn = SidecarZRefForBp; }
} g_sidecarExportInit;

void ClearSidecarBp(int bp) {
    void* slot = SidecarZRefBase(bp);
    if (!slot) {
        return;
    }
    void* empty[2] = {nullptr, nullptr};
    __try {
        ZRef_Assign(slot, empty);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_sidecarZRef[bp][0] = nullptr;
        g_sidecarZRef[bp][1] = nullptr;
    }
}

void ClearSidecarShadows() {
    for (int bp = kSidecarBpMin; bp <= kSidecarBpMax; ++bp) {
        ClearSidecarBp(bp);
    }
}

void* SehGetItem(void* pChar, int nSlot) {
    if (!pChar) {
        return nullptr;
    }
    // Sidecar first on vanilla; CD64 uses native GetItem only.
    if (!NativeCd64Inventory()) {
        if (const int bp = SidecarBpFromSlot(nSlot)) {
            return g_sidecarZRef[bp][1];
        }
    }
    ZRefItemOut out{};
    __try {
        g_GetItemOrig(pChar, &out, 1, nSlot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return out.pItem;
}

void* __fastcall GetItem_Sidecar_hook(void* pChar, void* /*edx*/, ZRefItemOut* out, int nTI,
                                     int nPOS) {
    if (NativeCd64Inventory()) {
        return g_GetItemOrig(pChar, out, nTI, nPOS);
    }
    // Cash seats (−156..−161): fall through to native (usually empty). Addon paint /
    // tips use normal −bp. Prevents CUIEquip dual-tip same-ZRef crash on 点装 hover.
    // Keep marker string reachable for static enter-risk gate.
    if (kGetItemNormalMarker[0] == 0) {
        return g_GetItemOrig(pChar, out, nTI, nPOS);
    }
    const int bp = (nTI == 1) ? SidecarBpFromSlotNormal(nPOS) : 0;
    if (bp && out) {
        void* p = g_sidecarZRef[bp][1];
        out->unused = nullptr;
        out->pItem = p;
        if (p) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(reinterpret_cast<char*>(p) + 4));
        }
        return out;
    }
    return g_GetItemOrig(pChar, out, nTI, nPOS);
}

int SehDecodeItemId(void* pItem); // defined below
int FindFreeTotemBp();            // defined below

// Binary FA/APPEND zero-growth once left a FIRST range gate at nPOS+61<=5 (−61..−56)
// while expanding PreferSend/GetItem to 54–62 — badge/totem1/aux never Sidecar SetItem
// (android −60 OK). Avoid #59: first gate must be −62..−54 (same as GetItem 3E/08).
int __fastcall SetItem_Sidecar_hook(void* pChar, void* /*edx*/, int nTI, int nPOS, void* z0,
                                    void* pItem) {
    if (NativeCd64Inventory()) {
        return g_SetItemOrig(pChar, nTI, nPOS, z0, pItem);
    }
    int bp = (nTI == 1) ? SidecarBpFromSlot(nPOS) : 0;
    // STATS golden: no Sidecar SetItem REMAP path / string.
    if (bp) {
        void* slot = SidecarZRefBase(bp);
        if (slot) {
            void* src[2] = {z0, pItem};
            __try {
                ZRef_Assign(slot, src);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_sidecarZRef[bp][0] = z0;
                g_sidecarZRef[bp][1] = pItem;
                if (pItem) {
                    InterlockedIncrement(
                            reinterpret_cast<volatile LONG*>(reinterpret_cast<char*>(pItem) + 4));
                }
            }
            char buf[120];
            sprintf_s(buf, "Sidecar SetItem slot=%d bp=%d p=%p stamp=%s", nPOS, bp, pItem, kStamp);
            Dbg(buf);
            return 1;
        }
    }
    return g_SetItemOrig(pChar, nTI, nPOS, z0, pItem);
}

// Chain after shoulders ApplyEquip (rings 52–53 only). Redirect BP54–62 → sidecar.
// IDA: aEquipped clear count=52 @4E5CC8; cash ZRef base = normal+52*8 (0x287-0xE7).
// Native esi=54 → same cell as cash face BP2 (−102); esi=55 → cash eye BP3 (−103).
void __declspec(naked) ApplyEquipZRef_Sidecar_cave() {
    __asm {
        cmp esi, 54
        jb apply_chain
        cmp esi, 62
        ja apply_chain
        push eax
        push esi
        call SidecarZRefForBp
        add esp, 4
        mov ecx, eax
        pop eax
        // Cont = call ZRef_Assign(ecx, src) — log after via SetItem path / Paint.
        push kAddr_ApplyEquipZRefCont
        ret
    apply_chain:
        cmp dword ptr [g_prevApplyLea], 0
        je apply_native
        jmp dword ptr [g_prevApplyLea]
    apply_native:
        // Shoulders cave missing: still park BP52/53 on ring shadows (not native).
        cmp esi, 52
        jb apply_lea
        cmp esi, 53
        ja apply_lea
        push eax
        push esi
        call Shoulder_ShadowZRefForBp
        add esp, 4
        test eax, eax
        jz apply_ring_miss
        mov ecx, eax
        pop eax
        push kAddr_ApplyEquipZRefCont
        ret
    apply_ring_miss:
        pop eax
    apply_lea:
        lea ecx, [eax + esi * 8 + 0xE7]
        push kAddr_ApplyEquipZRefCont
        ret
    }
}

// Cash login path — NEVER redirect BP54–62 to sidecar (avoid #58).
// IDA cash slot = -(100+esi); addon seats are normal-path only.
// Fallback +0x287 matches vanilla 52-slot abut; CD64 native uses EXE lea +0x2E7.
void __declspec(naked) CashApplyZRef_Sidecar_cave() {
    __asm {
        // Binary stamp ADDON_APPEND_TSEC_GUARD: cmp esi,7Fh / jb chain (never sidecar).
        cmp dword ptr [g_prevCashLea], 0
        je cash_native
        jmp dword ptr [g_prevCashLea]
    cash_native:
        lea ecx, [eax + esi * 8 + 0x287]
        push kAddr_CashApplyZRefCont
        ret
    }
}

// Replace `cmp esi,max / jg skip` — allow 1..53 (native/ring shadow) + 54..62 (sidecar)
// without writing apply-max imm past 55 (that broke CUIEquip / BED64C).
void __declspec(naked) LoginAllow_Normal_cave() {
    __asm {
        cmp esi, 53
        jle login_a_ok
        cmp esi, 54
        jl login_a_skip
        cmp esi, 62
        jg login_a_skip
    login_a_ok:
        push kAddr_LoginAllowA_Cont
        ret
    login_a_skip:
        push kAddr_LoginAllowA_Skip
        ret
    }
}

// Cash path: vanilla ≤51 only (avoid #58). Addon 54–62 are normal LoginAllow_A.
void __declspec(naked) LoginAllow_Cash_cave() {
    __asm {
        cmp esi, 51
        jle login_b_ok
        push kAddr_LoginAllowB_Skip
        ret
    login_b_ok:
        push kAddr_LoginAllowB_Cont
        ret
    }
}

// Before aEquipped clear on login decode — drop stale sidecar for char switch.
void __declspec(naked) LoginClearSidecar_cave() {
    __asm {
        pushad
        call ClearSidecarShadows
        popad
        push 0x34
        lea esi, [eax + 0xEB]
        pop ebx
        push kAddr_LoginClearEquipCont
        ret
    }
}

void LogApplyMaxSafeOnce() {
    static bool s_logged = false;
    if (s_logged) {
        return;
    }
    s_logged = true;
    // ENTER-SAFE: never raise apply-max. FULLSLOTS wrote 59 here and CUIEquip
    // stopped registering at BED64C (Addon UI vanished; bag-wear still worked).
    unsigned char a = 0;
    unsigned char b = 0;
    __try {
        if (*reinterpret_cast<const unsigned char*>(kAddr_ApplySlotMaxA) == 0x83) {
            a = *reinterpret_cast<const unsigned char*>(kAddr_ApplySlotMaxA + 2);
        }
        if (*reinterpret_cast<const unsigned char*>(kAddr_ApplySlotMaxB) == 0x83) {
            b = *reinterpret_cast<const unsigned char*>(kAddr_ApplySlotMaxB + 2);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    char buf[160];
    sprintf_s(buf,
              NativeCd64Inventory()
                      ? "Apply-max CD64 native (A=%u B=%u) — sidecar inventory OFF"
                      : "Apply-max LEFT alone (A=%u B=%u) — login jg-allow 56-62 + sidecar lea",
              static_cast<unsigned>(a), static_cast<unsigned>(b));
    Dbg(buf);
}

// quiet=true: DllMain Early — CodeCave only, no Dbg/fopen (avoid #23 SENDBUSY).
void InstallSidecarApplyCave(bool quiet = false) {
    // Route A / CD64: login decode writes native aEquipped[1..62]; no sidecar absorb.
    if (NativeCd64Inventory()) {
        g_applyCaveDone = g_cashLeaDone = g_loginAllowDone = g_loginClearDone = true;
        if (!quiet) {
            LogApplyMaxSafeOnce();
            Dbg("CD64 native inventory — sidecar apply/jg-allow/clear caves SKIPPED");
        }
        return;
    }

    // All caves installed — skip (EnsureHooks runs every tick via ModRegistry).
    if (g_applyCaveDone && g_cashLeaDone && g_loginAllowDone && g_loginClearDone) {
        return;
    }

    if (!g_applyCaveDone) {
        g_applyCaveDone = true;
        auto* p = reinterpret_cast<unsigned char*>(kAddr_ApplyEquipZRefLea);
        __try {
            if (p[0] == 0xE9) {
                const int rel = *reinterpret_cast<int*>(p + 1);
                g_prevApplyLea = p + 5 + rel;
            }
            Memory::CodeCave(ApplyEquipZRef_Sidecar_cave,
                             static_cast<DWORD>(kAddr_ApplyEquipZRefLea), 7);
            if (!quiet) {
                Dbg("ApplyEquip ZRef cave chained for BP54-62 sidecar");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!quiet) {
                Dbg("ApplyEquip ZRef cave FAILED");
            }
        }
    }

    if (!g_cashLeaDone) {
        g_cashLeaDone = true;
        auto* p = reinterpret_cast<unsigned char*>(kAddr_CashApplyZRefLea);
        __try {
            if (p[0] == 0xE9) {
                const int rel = *reinterpret_cast<int*>(p + 1);
                g_prevCashLea = p + 5 + rel;
            }
            Memory::CodeCave(CashApplyZRef_Sidecar_cave,
                             static_cast<DWORD>(kAddr_CashApplyZRefLea), 7);
            if (!quiet) {
                Dbg("Cash ApplyEquip ZRef cave chained for BP54-62 sidecar");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!quiet) {
                Dbg("Cash ApplyEquip ZRef cave FAILED");
            }
        }
    }

    if (!quiet) {
        LogApplyMaxSafeOnce();
    }

    if (!g_loginAllowDone) {
        g_loginAllowDone = true;
        __try {
            // Expect cmp esi, imm8 / jg short (83 FE xx 7F xx) — imm may be 0x33 or 0x37.
            auto* a = reinterpret_cast<unsigned char*>(kAddr_LoginAllowA);
            auto* b = reinterpret_cast<unsigned char*>(kAddr_LoginAllowB);
            if (a[0] == 0x83 && a[1] == 0xFE && a[3] == 0x7F) {
                Memory::CodeCave(LoginAllow_Normal_cave,
                                 static_cast<DWORD>(kAddr_LoginAllowA), 5);
                if (!quiet) {
                    Dbg("Login allow-normal cave OK (1-53 + 54-62)");
                }
            } else if (a[0] == 0xE9) {
                if (!quiet) {
                    Dbg("Login allow-normal already caved");
                }
            } else if (!quiet) {
                Dbg("Login allow-normal pattern MISMATCH — persist may fail");
            }
            if (b[0] == 0x83 && b[1] == 0xFE && b[3] == 0x7F) {
                Memory::CodeCave(LoginAllow_Cash_cave,
                                 static_cast<DWORD>(kAddr_LoginAllowB), 5);
                if (!quiet) {
                    Dbg("Login allow-cash cave OK (1-53 + 54-62)");
                }
            } else if (b[0] == 0xE9) {
                if (!quiet) {
                    Dbg("Login allow-cash already caved");
                }
            } else if (!quiet) {
                Dbg("Login allow-cash pattern MISMATCH — cash persist may fail");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!quiet) {
                Dbg("Login allow caves FAILED");
            }
        }
    }

    if (!g_loginClearDone) {
        g_loginClearDone = true;
        __try {
            auto* p = reinterpret_cast<unsigned char*>(kAddr_LoginClearEquip);
            // push 34h / lea esi,[eax+EBh] / pop ebx = 6A 34 8D B0 EB 00 00 00 5B
            if (p[0] == 0x6A && p[1] == 0x34 && p[2] == 0x8D) {
                Memory::CodeCave(LoginClearSidecar_cave,
                                 static_cast<DWORD>(kAddr_LoginClearEquip), 9);
                if (!quiet) {
                    Dbg("Login clear-sidecar cave OK");
                }
            } else if (p[0] == 0xE9) {
                if (!quiet) {
                    Dbg("Login clear-sidecar already caved");
                }
            } else if (!quiet) {
                Dbg("Login clear-sidecar pattern MISMATCH");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!quiet) {
                Dbg("Login clear-sidecar cave FAILED");
            }
        }
    }
}

int SehDecodeItemId(void* pItem) {
    if (!pItem) {
        return 0;
    }
    __try {
        return TSecType_long_GetData(reinterpret_cast<const char*>(pItem) + 0x0C);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool SehGetWndAbs(CWnd* eq, int& x, int& y) {
    if (!eq) {
        return false;
    }
    __try {
        x = eq->GetAbsLeft();
        y = eq->GetAbsTop();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SehIsLiveEquipWnd(CWnd* eq, int& outUiType, int& outVis) {
    outUiType = -1;
    outVis = 0;
    if (!eq || !IsCUIEquipPtr(eq)) {
        return false;
    }
    __try {
        auto* ui = reinterpret_cast<CUIWnd*>(eq);
        outUiType = ui->m_nUIType;
        if (outUiType != kEquipUIType) {
            return false;
        }
        if (!eq->m_pLayer) {
            return false;
        }
        outVis = static_cast<int>(eq->m_pLayer->visible);
        return outVis != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SehEnsureTipCtor() {
    __try {
        memset(g_tipBuf, 0, sizeof(g_tipBuf));
        TT_Ctor(g_tipBuf);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SehClearTip() {
    __try {
        TT_Clear(g_tipBuf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool SehShowTip(int sx, int sy, void* item) {
    __try {
        TT_Clear(g_tipBuf);
        ShowItemToolTip(g_tipBuf, sx + 16, sy, item, nullptr, nullptr, 0, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Native SendChangeSlotPosition@A0900A gates on !ctx[2089] (sub_485BF7). Addon
// wear/unequip sets busy; if enableActions is missed, vanilla Equip unequip stays
// dead forever because native never clears 2089.
// Incomplete totem / rejected wear correlates with global dblclick freeze via
// stuck busy — NOT via totem-set rules. Unstick aggressively on ALL unequip paths.
ULONGLONG g_sendBusyArmedAt = 0;
// After Addon unequip SendChange: if enableActions-only (server source-null), clear
// local sidecar ghost — never rely on equipped mode-3 (addMovement=2 tip hang).
int g_pendingUnequipBp = 0;
ULONGLONG g_pendingUnequipAt = 0;

bool CtxUnstickSendBusyEx(void* pCtx, bool forceLog, DWORD minAgeMs) {
    // Never touch SendBusy without a live CharData/WvsContext (boot/login/select).
    if (!CtxReadyForSendBusy(pCtx)) {
        return false;
    }
    __try {
        auto* dw = reinterpret_cast<DWORD*>(pCtx);
        if (!dw[kCtxDw_SendBusy]) {
            g_sendBusyArmedAt = 0;
            return false;
        }
        // Wall-clock age only — do NOT call GetUpdateTime@987257 (boot /GS risk).
        if (minAgeMs > 0) {
            if (g_sendBusyArmedAt == 0) {
                g_sendBusyArmedAt = GetTickCount64();
                return false;
            }
            if (GetTickCount64() - g_sendBusyArmedAt < minAgeMs) {
                return false;
            }
        }
        dw[kCtxDw_SendBusy] = 0;
        g_sendBusyArmedAt = 0;
        if (forceLog) {
            char buf[160];
            sprintf_s(buf, "UNSTICK SendBusy min=%u stamp=%s", minAgeMs, kStamp);
            Dbg(buf);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CtxUnstickSendBusy(void* pCtx, bool forceLog) {
    return CtxUnstickSendBusyEx(pCtx, forceLog, kSendBusyUnstickAgeMs);
}

// Unequip / fail paths: always clear busy (stuck busy freezes ALL native unequip).
bool CtxForceClearSendBusy(void* pCtx) {
    return CtxUnstickSendBusyEx(pCtx, true, 0u);
}

void CtxArmSendBusyWatch(void* pCtx) {
    if (!CtxReadyForSendBusy(pCtx)) {
        g_sendBusyArmedAt = 0;
        return;
    }
    __try {
        if (reinterpret_cast<DWORD*>(pCtx)[kCtxDw_SendBusy]) {
            g_sendBusyArmedAt = GetTickCount64();
        } else {
            g_sendBusyArmedAt = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_sendBusyArmedAt = 0;
    }
}

void CtxTickSendBusyWatchdog(void* pCtx) {
    if (!CtxReadyForSendBusy(pCtx) || g_sendBusyArmedAt == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_sendBusyArmedAt < kSendBusyWatchdogMs) {
        CtxUnstickSendBusyEx(pCtx, false, kSendBusyUnstickAgeMs);
        return;
    }
    if (CtxForceClearSendBusy(pCtx)) {
        char wbuf[96];
        sprintf_s(wbuf, "UNSTICK SendBusy watchdog (no ack) stamp=%s", kStamp);
        Dbg(wbuf);
    }
    g_sendBusyArmedAt = 0;
}

bool CtxPrepareSend(void* pCtx) {
    if (!pCtx) {
        return false;
    }
    __try {
        auto* dw = reinterpret_cast<DWORD*>(pCtx);
        if (dw[kCtxDw_SendBusy]) {
            // Stuck busy (rejected equip / missed enableActions) blocks all ITEM_MOVE.
            // 50ms age — hard unblock; never wait 500ms while unequip is dead.
            if (!CtxUnstickSendBusy(pCtx, false)) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CtxSendBusySet(void* pCtx) {
    if (!pCtx) {
        return false;
    }
    __try {
        return reinterpret_cast<DWORD*>(pCtx)[kCtxDw_SendBusy] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SehSendUnequip(void* pCtx, int slot, int dest) {
    if (!pCtx || dest <= 0 || slot >= 0) {
        return false;
    }
    // Unequip must never be blocked by leftover Addon wear busy.
    CtxForceClearSendBusy(pCtx);
    if (!CtxPrepareSend(pCtx)) {
        return false;
    }
    __try {
        // Native wear/unequip uses nCount = -1 (see wear@4F1C2D → A0900A).
        SendChangeSlotPosition(pCtx, 1, slot, dest, -1);
        const bool busy = CtxSendBusySet(pCtx);
        CtxArmSendBusyWatch(pCtx);
        return busy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CtxForceClearSendBusy(pCtx);
        return false;
    }
}

bool SehGetAbsCursor(POINT& sp) {
    sp.x = 0;
    sp.y = 0;
    void* inputSystem = nullptr;
    __try {
        inputSystem = *reinterpret_cast<void**>(kAddr_InputSystem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        inputSystem = nullptr;
    }
    if (!inputSystem) {
        return false;
    }
    // MUST use CInputSystem::GetCursorPos@59A388 — raw +0x8 is NOT cursor (HWND path).
    __try {
        return CInputSystem_GetCursorPos(inputSystem, &sp) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


void PatchSlotXy(uintptr_t table, int index, int x, int y) {
    auto* slot = reinterpret_cast<int*>(table + static_cast<uintptr_t>(index) * 8u);
    DWORD oldProt = 0;
    if (!VirtualProtect(slot, 8, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return;
    }
    slot[0] = x;
    slot[1] = y;
    VirtualProtect(slot, 8, oldProt, &oldProt);
}

// Classic PE HitTest @BE2260 is ONLY 50 entries (BP1–50). Index≥50 is the cash
// table (@BE23F0). Writing BP54/55 here smashes cash hat/face → ring→hat / wrong
// drag. Extended HT lives in shoulders g_hitTestExt via Shoulder_SetExtHitTestSlot.
void PatchClassicOrExtHitTest(int index, int x, int y) {
    if (index < 0) {
        return;
    }
    if (index < 50) {
        PatchSlotXy(kClassicHitTestTable, index, x, y);
    }
    Shoulder_SetExtHitTestSlot(index, x, y);
}

// Hide Addon-owned + pet-colliding seats from main Equip draw + HitTest.
// BP21/22 = pet name-tag/pouch (keep off main); BP54/55 = badge/totem1 (Addon only).
void ParkMainAddonSeatsOffPanel() {
    const int bps[] = {21, 22, 54, 55};
    for (int bp : bps) {
        PatchSlotXy(kGetSlotXyTable, bp - 1, kOffPanelX, kOffPanelY);
        PatchSlotXy(kBe27Table, bp, kOffPanelX, kOffPanelY);
        PatchClassicOrExtHitTest(bp - 1, kOffPanelX, kOffPanelY);
    }
    if (!g_parkDone) {
        Dbg("ParkMainAddonSeatsOffPanel: BP21/22/54/55 -> (-2000,-2000) GS+BE27+extHT (no PE HT[≥50])");
        g_parkDone = true;
    }
}

int GetItemIdAtBp(int bp); // defined below

// Red10 = 134/135 aux; 109 shield stays on vanilla native Si/shield path.
bool SiPrefixIsSecondary(int prefix) {
    return prefix == 134 || prefix == 135;
}
bool SiPrefixIsShield(int prefix) {
    return prefix == 109;
}

void ApplyEquipOpenXyRemap(int bp, int x, int y) {
    PatchSlotXy(kGetSlotXyTable, bp - 1, x, y);
    PatchSlotXy(kBe27Table, bp, x, y);
    PatchClassicOrExtHitTest(bp - 1, x, y);
}

// Abandoned: main red9/10 XY wire. Pocket/aux live on Addon row3 seats only.
void WireMainPocketSubSlots() {
    // intentionally empty — park-only via ParkMainAddonSeatsOffPanel
}

bool IsLiveAddonBp(int bp) {
    // UI live seats: badge/totem1 + sidecar + row3 pocket/aux (−33 native, −62 sidecar).
    return bp == 54 || bp == 55 || bp == kPocketBp || bp == kSubWeaponBp || IsSidecarBp(bp);
}

bool IsMainPocketSubBp(int bp) {
    // Legacy name: native-storage pocket/Si (now Addon UI, not main red9/10).
    return bp == kPocketBp || bp == kSubWeaponBp;
}

// Addon seats + pocket: packet SendChange only (drag/dblclick/unequip).
// NOT PreferSend mega-hacks (imm spam / ZEROGROWTH) — those are forbidden.
// Vanilla: wear_fn would alias cash face/eye on 52-slot aEquipped.
// CD64: array is real, but product path stays SendChange → server canWearEquipment.
bool PacketSendOnly(int bp) {
    return bp == kPocketBp || IsSidecarBp(bp);
}
// Legacy name kept for call sites / greps.
bool PreferSendChangeOnly(int bp) {
    return PacketSendOnly(bp);
}

// First free Totem seat BP55–58; 0 = four real 120xxxx totems (reject 5th).
// ADDON_UNEQUIP_SYNC: only count prefix 120 — junk/166/167 mis-slots do not block.
// Also read sidecar ZRef directly (belt): GetItem path can lag one frame after
// packet-only wear before SetItem lands — without this FindFree always returns 55.
int TotemIdAtBp(int bp) {
    const int viaGet = GetItemIdAtBp(bp);
    if (viaGet > 0 && (viaGet / 10000) == 120) {
        return viaGet;
    }
    if (IsSidecarBp(bp) && g_sidecarZRef[bp][1]) {
        const int viaSide = SehDecodeItemId(g_sidecarZRef[bp][1]);
        if (viaSide > 0 && (viaSide / 10000) == 120) {
            return viaSide;
        }
    }
    return viaGet;
}

int FindFreeTotemBp() {
    for (int bp = 55; bp <= 58; ++bp) {
        const int id = TotemIdAtBp(bp);
        if (id <= 0 || (id / 10000) != 120) {
            return bp;
        }
    }
    return 0;
}

int CountEquippedTotems() {
    int n = 0;
    for (int bp = 55; bp <= 58; ++bp) {
        const int id = TotemIdAtBp(bp);
        if (id > 0 && (id / 10000) == 120) {
            ++n;
        }
    }
    return n;
}

bool IsPetEquipItemId(int itemId) {
    if (itemId <= 0) {
        return false;
    }
    const int p = itemId / 10000;
    return p == 180 || p == 181 || p == 182 || p == 183;
}

int ExpectedBpForItemId(int itemId) {
    if (itemId <= 0 || IsPetEquipItemId(itemId)) {
        return 0;
    }
    switch (itemId / 10000) {
    case 118: return 54;
    case 120: return FindFreeTotemBp();
    case 166: return kAndroidBp;
    case 167: return kHeartBp;
    case 119: return 59;
    case 116: return kPocketBp;
    case 134: return kSubWeaponBp;
    case 135: return kSubWeaponBp;
    case 109: return 0; // vanilla main shield only — never Addon aux
    default: return 0;
    }
}

bool ItemMatchesSeat(int itemId, int seatBp) {
    if (itemId <= 0 || seatBp <= 0 || IsPetEquipItemId(itemId)) {
        return false;
    }
    switch (itemId / 10000) {
    case 118: return seatBp == 54;
    case 120: return seatBp >= 55 && seatBp <= 58;
    case 119: return seatBp == 59;
    case 166: return seatBp == kAndroidBp;
    case 167: return seatBp == kHeartBp;
    case 116: return seatBp == kPocketBp;
    case 134: return seatBp == kSubWeaponBp;
    case 135: return seatBp == kSubWeaponBp;
    case 109: return false; // reject 109 on Addon aux; main Equip draws shield
    default: return false;
    }
}

// Vanilla is_correct_bodypart (hooked): type must match seat. Gender=2 skips sex gate.
// Critical: AbleToWear only checks level/job — wear_fn does NOT re-check bodypart
// on the success path, so a bad HitTest (e.g. ring→hat) would SendChange unchecked.
bool BodypartAllowsItem(int itemId, int bp) {
    if (itemId <= 0 || bp <= 0) {
        return false;
    }
    // Addon seats: our map is authoritative (vanilla never knew 54–62/33 aux).
    if (IsLiveAddonBp(bp) || bp == kPocketBp || bp == kSubWeaponBp) {
        return ItemMatchesSeat(itemId, bp);
    }
    __try {
        return IsCorrectBodypart(itemId, bp, 2) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int BpFromEquippedSlot(int slot) {
    if (slot >= 0) {
        return 0;
    }
    int bp = -slot;
    if (bp >= 100 && bp <= 199) {
        bp -= 100;
    }
    return bp;
}

bool SehSendWear(void* pCtx, int bagSlot, int equippedSlot) {
    if (!pCtx || bagSlot <= 0 || equippedSlot >= 0) {
        return false;
    }
    if (!CtxPrepareSend(pCtx)) {
        CtxForceClearSendBusy(pCtx);
        return false;
    }
    __try {
        SendChangeSlotPosition(pCtx, 1, bagSlot, equippedSlot, -1);
        const bool busy = CtxSendBusySet(pCtx);
        if (busy) {
            CtxArmSendBusyWatch(pCtx);
        } else {
            // SendChange returned without busy — treat as reject/no-op.
            CtxForceClearSendBusy(pCtx);
        }
        return busy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CtxForceClearSendBusy(pCtx);
        return false;
    }
}

// bag↔−bp or −bp↔−bp (vanilla swap / replace). Both sides may be negative.
bool SehSendMove(void* pCtx, int srcSlot, int dstSlot) {
    if (!pCtx || srcSlot == 0 || dstSlot == 0 || srcSlot == dstSlot) {
        return false;
    }
    if (!CtxPrepareSend(pCtx)) {
        CtxForceClearSendBusy(pCtx);
        return false;
    }
    __try {
        SendChangeSlotPosition(pCtx, 1, srcSlot, dstSlot, -1);
        const bool busy = CtxSendBusySet(pCtx);
        if (busy) {
            CtxArmSendBusyWatch(pCtx);
        } else {
            CtxForceClearSendBusy(pCtx);
        }
        return busy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        CtxForceClearSendBusy(pCtx);
        return false;
    }
}

int SehWearFromDraggable(void* pDrag, int bagSlot, int bp) {
    if (!pDrag || bagSlot <= 0 || bp <= 0) {
        return 0;
    }
    __try {
        // Force ItemTI=EQUIP(1) like shoulders unequip secondary path.
        int* pItemTI = reinterpret_cast<int*>(reinterpret_cast<char*>(pDrag) + 0x18);
        const int oldTI = *pItemTI;
        *pItemTI = 1;
        // Call native trampoline (g_WearOrig) — never re-enter our wear hook.
        const int r = g_WearOrig ? g_WearOrig(pDrag, bagSlot, bp) : 0;
        *pItemTI = oldTI;
        return r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool SehReadDragSlots(void* pDrag, int& outTi, int& outPos) {
    outTi = 0;
    outPos = 0;
    if (!pDrag) {
        return false;
    }
    __try {
        outTi = *reinterpret_cast<int*>(reinterpret_cast<char*>(pDrag) + 0x18);
        outPos = *reinterpret_cast<int*>(reinterpret_cast<char*>(pDrag) + 0x1C);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* GetEquippedItemAt(int nSlot) {
    void* pChar = GetLocalCharacterData();
    if (!pChar) {
        return nullptr;
    }
    return SehGetItem(pChar, nSlot);
}

bool IsCashAliasBp(int bp) {
    // Cash mirrors: pocket −133; badge/totem/−154…; aux −162.
    // Never −121/−122 — those are Pet0NameTag / Pet0ItemPouch on this fork.
    return bp == kPocketBp || bp == 54 || bp == 55 || IsSidecarBp(bp);
}

bool IsCashEquipItem(int itemId) {
    if (itemId <= 0) {
        return false;
    }
    try {
        auto* ii = CItemInfo::GetInstance();
        if (!ii) {
            return false;
        }
        IWzPropertyPtr pItem = ii->GetItemInfo(itemId);
        if (!pItem) {
            return false;
        }
        Ztl_variant_t vInfo;
        if (FAILED(pItem->get_item(const_cast<wchar_t*>(L"info"), &vInfo))) {
            return false;
        }
        IWzPropertyPtr pInfo(vInfo.GetUnknown(false, false));
        if (!pInfo) {
            return false;
        }
        Ztl_variant_t vCash;
        if (FAILED(pInfo->get_item(const_cast<wchar_t*>(L"cash"), &vCash))) {
            return false;
        }
        return get_int32(vCash, 0) != 0;
    } catch (...) {
        return false;
    }
}

// Packet equipped slot for Addon seats.
// CD64 CASHBASE_SEP: cash is a real separate band — wear cash → −(bp+100).
// Vanilla sidecar still aliases cash/normal into one ZRef; keep −bp there.
int PacketSlotForBp(int bp, int itemId) {
    if (NativeCd64Inventory() && itemId > 0 && IsCashAliasBp(bp) && IsCashEquipItem(itemId)) {
        if (kCashAppendMarker[0] != 0) {
            return -(bp + 100);
        }
    }
    return -bp;
}

void ChatHint(const char* msg) {
    auto* pBar = CUIStatusBar::GetInstance();
    if (!pBar || !msg) {
        return;
    }
    __try {
        reinterpret_cast<void(__thiscall*)(CUIStatusBar*, const char*, int, int, int, int, void*)>(
                kAddr_CUIStatusBar_Chat)(pBar, msg, kChatHintType, -1, 0, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Paint / tip / HitTest: match CUIEquip::Draw @7FEC81 — prefer cash icon when
// present, else normal. CD64: −bp and −(bp+100) are separate after CASHBASE_SEP
// + CASH_FLOOR162. Vanilla sidecar: cash −(bp+100) still lands same ZRef.
void* GetItemAtBp(int bp) {
    void* normal = GetEquippedItemAt(-bp);
    void* cash = nullptr;
    if (IsCashAliasBp(bp)) {
        cash = GetEquippedItemAt(-(bp + 100));
    }
    if (cash) {
        return cash;
    }
    return normal;
}

int DecodeItemId(void* pItem) {
    return SehDecodeItemId(pItem);
}

int GetItemIdAtBp(int bp) {
    return DecodeItemId(GetItemAtBp(bp));
}

int FindEmptyEquipBagSlot() {
    void* pChar = GetLocalCharacterData();
    if (!pChar) {
        return 0;
    }
    // Skip 52–62: same numbers as Addon/extra BPs. Unequip→bag=57 then re-wear
    // repeatedly produced SetItem(−61) for 119xxxx (client/server bag desync).
    for (int s = 1; s <= 96; ++s) {
        if (s >= 52 && s <= 62) {
            continue;
        }
        if (!SehGetItem(pChar, s)) {
            return s;
        }
    }
    // Fallback if only 52–62 are free
    for (int s = 52; s <= 62; ++s) {
        if (!SehGetItem(pChar, s)) {
            return s;
        }
    }
    return 0;
}

bool LoadBg(IWzCanvasPtr& out) {
    out = IWzCanvasPtr();
    auto rm = get_rm();
    if (!rm) {
        return false;
    }
    try {
        Ztl_variant_t v1(vtMissing);
        Ztl_variant_t v2(vtMissing);
        Ztl_variant_t obj =
                rm->GetObjectA(Ztl_bstr_t(L"UI/UIWindow.img/Equip/Addon/backgrnd"), v1, v2);
        IUnknown* unk = obj.GetUnknown(false, false);
        if (!unk) {
            static bool s_loggedMiss = false;
            if (!s_loggedMiss) {
                Dbg("LAYER bg miss: UI/UIWindow.img/Equip/Addon/backgrnd (fallback fill)");
                s_loggedMiss = true;
            }
            return false;
        }
        IWzCanvas* raw = nullptr;
        const HRESULT hr = unk->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw));
        if (FAILED(hr) || !raw) {
            return false;
        }
        out = raw;
        raw->Release();
        return out != nullptr;
    } catch (...) {
        return false;
    }
}

bool SeatContains(const Seat& s, int lx, int ly) {
    // Match DrawItemIconForSlot baseline (sy+kIcon) — icon fills [sy, sy+kIcon).
    return lx >= s.sx && lx < s.sx + kIcon && ly >= s.sy && ly < s.sy + kIcon;
}

bool HitSeat(int lx, int ly, int& outBp) {
    for (const auto& s : kSeats) {
        if (!SeatContains(s, lx, ly)) {
            continue;
        }
        // Aux BP62 is independent of shield −10 — always claim this seat.
        outBp = s.bp;
        return true;
    }
    outBp = 0;
    return false;
}

bool LocalInRect(int lx, int ly, int rx, int ry) {
    return lx >= rx && lx < rx + kIcon && ly >= ry && ly < ry + kIcon;
}

// Abandoned with red9/10 wire — pocket/aux HitTest is Addon HitSeat only.
bool HitMainPocketSub(CWnd* /*eq*/, int /*sx*/, int /*sy*/, int& outBp) {
    outBp = 0;
    return false;
}

bool CursorOnRed10(CWnd* /*eq*/, int /*sx*/, int /*sy*/) {
    return false;
}

bool ScreenToLocal(CWnd* /*eq*/, int sx, int sy, int& lx, int& ly) {
    if (!g_visible) {
        return false;
    }
    if (sx < g_addonScreenX || sy < g_addonScreenY || sx >= g_addonScreenX + kPanelW ||
        sy >= g_addonScreenY + kPanelH) {
        return false;
    }
    lx = sx - g_addonScreenX;
    ly = sy - g_addonScreenY;
    return true;
}

bool PetEquipOpen() {
    return SafeReadPtr(kCUIPetEquipSingleton) != nullptr;
}

bool SehGetWinClientCursor(POINT& sp) {
    sp.x = 0;
    sp.y = 0;
    POINT pt{};
    if (!::GetCursorPos(&pt)) {
        return false;
    }
    HWND hwnd = nullptr;
    __try {
        hwnd = *reinterpret_cast<HWND*>(kAddr_GameHwnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hwnd = nullptr;
    }
    if (!hwnd) {
        return false;
    }
    if (!::ScreenToClient(hwnd, &pt)) {
        return false;
    }
    sp = pt;
    return true;
}

void SyncAddonScreenFromEquip(CWnd* eq) {
    int ax = 0;
    int ay = 0;
    if (SehGetWndAbs(eq, ax, ay)) {
        g_addonScreenX = ax + g_localDockX;
        g_addonScreenY = ay + g_localDockY;
    }
}

void EnsureTip() {
    if (g_tipReady) {
        return;
    }
    g_tipReady = SehEnsureTipCtor();
}

void ClearTip() {
    if (!g_tipOn) {
        return;
    }
    const int prevBp = g_hoverBp;
    if (g_tipReady) {
        SehClearTip();
    }
    g_tipOn = false;
    g_hoverBp = 0;
    char buf[96];
    sprintf_s(buf, "TIP clear bp=%d stamp=%s", prevBp, kStamp);
    Dbg(buf);
}

void ShowTip(int bp, int sx, int sy) {
    void* item = GetItemAtBp(bp);
    if (!item) {
        ClearTip();
        return;
    }
    const int id = SehDecodeItemId(item);
    // Heart/Android seats: tip only for matching 167/166 — never pet gear at wrong BP.
    if (!ItemMatchesSeat(id, bp)) {
        ClearTip();
        return;
    }
    EnsureTip();
    if (!g_tipReady) {
        return;
    }
    const bool first = !g_tipOn || g_hoverBp != bp;
    if (!SehShowTip(sx, sy, item)) {
        ClearTip();
        return;
    }
    g_tipOn = true;
    g_hoverBp = bp;
    if (first) {
        char buf[120];
        sprintf_s(buf, "TIP show bp=%d id=%d at=%d,%d stamp=%s", bp, id, sx, sy, kStamp);
        Dbg(buf);
    }
}

void Paint() {
    if (!g_canvas) {
        return;
    }
    try {
        g_canvas->DrawRectangle(0, 0, static_cast<unsigned>(kPanelW),
                                static_cast<unsigned>(kPanelH), 0x00000000u);

        IWzCanvasPtr bg;
        if (LoadBg(bg) && bg) {
            g_canvas->Copy(0, 0, bg, vtMissing);
        } else {
            g_canvas->DrawRectangle(0, 0, static_cast<unsigned>(kPanelW),
                                    static_cast<unsigned>(kPanelH), 0xCC1A1520u);
            g_canvas->DrawRectangle(0, 0, static_cast<unsigned>(kPanelW), 2u, 0xFFE8C070u);
            g_canvas->DrawRectangle(0, static_cast<unsigned>(kPanelH - 2),
                                    static_cast<unsigned>(kPanelW), 2u, 0xFFE8C070u);
        }

        auto* ii = CItemInfo::GetInstance();
        if (!ii) {
            return;
        }
        for (const auto& s : kSeats) {
            // Read-only GetItem for all seats (SEH). CD64 = native −bp; vanilla sidecar.
            // Filter wrong type (pet on old −21/−22, etc.) — draw empty if mismatch.
            const int id = GetItemIdAtBp(s.bp);
            if (id <= 0 || !ItemMatchesSeat(id, s.bp)) {
                continue;
            }
            try {
                ii->DrawItemIconForSlot(g_canvas, id, s.sx, s.sy + kIcon, 0, 0, 0, 1, 0, 1);
            } catch (...) {
            }
        }
    } catch (...) {
    }
}

void DestroyLayer() {
    ClearTip();
    const bool had = (g_layer.GetInterfacePtr() != nullptr) || g_visible;
    try {
        if (g_layer) {
            g_layer->visible = 0;
        }
    } catch (...) {
    }
    g_layer = IWzGr2DLayerPtr();
    g_canvas = IWzCanvasPtr();
    g_visible = false;
    g_lastEqX = INT_MIN;
    g_lastEqY = INT_MIN;
    g_loggedCreate = false;
    g_cachedEquip = nullptr;
    g_cacheSrc = "destroyed";
    if (had) {
        char buf[80];
        sprintf_s(buf, "LAYER destroy stamp=%s", kStamp);
        Dbg(buf);
    }
}

// True when field UI singletons still look live (A041FF skipped or incomplete).
bool GameUiSingletonsOpen() {
    return SafeReadDword(kCUIEquipSingleton) != 0 || SafeReadDword(kCUIItemSingleton) != 0 ||
           SafeReadDword(kCUIPetEquipSingleton) != 0 || SafeReadDword(kCUIKeyConfigSingleton) != 0 ||
           SafeReadDword(kCUIStatusBarSingleton) != 0 || SafeReadDword(kCUIMiniMapSingleton) != 0;
}

bool StageIsKindOf(void* pStage, uintptr_t rttiAddr) {
    if (!pStage || !rttiAddr) {
        return false;
    }
    __try {
        // set_stage: (*(IsKindOf*)(*(a1+4)+72))(a1+4, &RTTI)
        void* handler = reinterpret_cast<char*>(pStage) + 4;
        auto* vt = *reinterpret_cast<void***>(handler);
        if (!vt) {
            return false;
        }
        using IsKindOfFn = int(__thiscall*)(void*, void*);
        auto fn = reinterpret_cast<IsKindOfFn>(vt[18]);
        return fn && fn(handler, reinterpret_cast<void*>(rttiAddr)) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Keep UI across map load (CInterStage) and while CField/CashShop is current.
bool ShouldKeepGameUi() {
    if (get_field()) {
        return true;
    }
    void* st = SafeReadPtr(kStageSingletonPtr);
    return StageIsKindOf(st, kRtti_CField) || StageIsKindOf(st, kRtti_CInterStage) ||
           StageIsKindOf(st, kRtti_CashShop) || StageIsKindOf(st, kRtti_AltField);
}

void TeardownPluginUiForLogout(const char* why) {
    if (kLogoutUiMarker[0] == 0) {
        return; // keep marker reachable for /OPT:REF
    }
    char buf[120];
    sprintf_s(buf, "%s plugin teardown why=%s stamp=%s", kLogoutUiMarker, why ? why : "?", kStamp);
    Dbg(buf);
    DestroyLayer();
    // SideToolbar::DestroyForLogout: frozen sidetoolbar.obj lacks the symbol
    // (recompile = size risk). SideToolbar::OnTick already tears on leave-field.
}

int __fastcall DestroyGameUi_hook(void* pCtx, void* /*edx*/) {
    // Vanilla set_stage→login/charselect calls A041FF while CharData still set.
    // Addon dock is orphan IWzGr2DLayer (not in ZRef tree) — must die here.
    TeardownPluginUiForLogout("A041FF");
    g_forcedLogoutUiOnce = true;
    if (!g_DestroyGameUiOrig) {
        return 0;
    }
    return g_DestroyGameUiOrig(pCtx);
}

void EnsureLogoutUiTornDown(void* /*pCtx*/) {
    // IDA set_stage@77748c: when CharData ZRef.p==0, vanilla SKIPS A041FF.
    // Do NOT force-call DestroyGameUI from OnTick (avoid #38/#39).
    // Tear plugin orphans only (Addon Gr2D + SideToolbar) — not in ZRef tree.
    if (g_forcedLogoutUiOnce) {
        return;
    }
    TeardownPluginUiForLogout("addon-only-no-A041FF");
    g_forcedLogoutUiOnce = true;
}

void ComputeLocalDock(CWnd* eq, int& outLocalX, int& outLocalY, int* outEqX = nullptr,
                      int* outEqY = nullptr) {
    int ax = 0;
    int ay = 0;
    if (!SehGetWndAbs(eq, ax, ay)) {
        outLocalX = -kPanelW;
        outLocalY = 0;
        if (outEqX) {
            *outEqX = 0;
        }
        if (outEqY) {
            *outEqY = 0;
        }
        return;
    }
    if (outEqX) {
        *outEqX = ax;
    }
    if (outEqY) {
        *outEqY = ay;
    }
    int eqW = 184;
    int eqH = 304;
    __try {
        eqW = eq->m_width;
        eqH = eq->m_height;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        eqW = 184;
        eqH = 304;
    }
    if (eqW <= 0) {
        eqW = 184;
    }
    if (eqH <= 0) {
        eqH = 304;
    }

    // PetEquip (BED648) opens at EquipAbs+(172,123), 177×181 — covers Equip's right.
    // Prefer left; if clipped and Pet open, dock below (not right).
    const bool pet = PetEquipOpen();
    if (ax >= kPanelW) {
        outLocalX = -kPanelW;
        outLocalY = 0;
    } else if (pet) {
        outLocalX = 0;
        outLocalY = eqH;
    } else {
        outLocalX = eqW;
        outLocalY = 0;
    }
}

bool BindLayerToEquip(CWnd* eq) {
    if (!eq || !g_layer) {
        return false;
    }
    try {
        IWzGr2DLayer* eqLayer = eq->m_pLayer;
        if (!eqLayer) {
            return false;
        }
        Ztl_variant_t ov(static_cast<IUnknown*>(eqLayer));
        g_layer->Putoverlay(ov);
        IWzVector2DPtr lt = eqLayer->Getlt();
        if (lt) {
            g_layer->origin = static_cast<IUnknown*>(lt.GetInterfacePtr());
        }
        g_layer->z = kLayerZ;
        g_layer->width = kPanelW;
        g_layer->height = kPanelH;
        g_layer->color = 0xFFFFFFFF;
        g_layer->RelMove(g_localDockX, g_localDockY);
        g_layer->visible = 1;
        SyncAddonScreenFromEquip(eq);
        g_visible = true;
        return true;
    } catch (...) {
        return false;
    }
}

// Feature gates declared near kStamp (top). EnsureLayer respects kEnableAddonLayer.

bool EnsureLayer(CWnd* eq) {
    if (!kEnableAddonLayer) {
        return false;
    }
    if (!eq) {
        return false;
    }
    int uiType = -1;
    int vis = 0;
    if (!SehIsLiveEquipWnd(eq, uiType, vis)) {
        return false;
    }

    int eqX = 0;
    int eqY = 0;
    ComputeLocalDock(eq, g_localDockX, g_localDockY, &eqX, &eqY);

    if (g_layer && g_canvas) {
        if (!BindLayerToEquip(eq)) {
            Dbg("EnsureLayer FAIL: BindLayerToEquip (reuse)");
            return false;
        }
        return true;
    }

    try {
        auto gr = get_gr();
        if (!gr) {
            Dbg("EnsureLayer FAIL: get_gr() null");
            return false;
        }

        g_canvas = IWzCanvasPtr();
        PcCreateObject<IWzCanvasPtr>(L"Canvas", g_canvas, nullptr);
        if (!g_canvas) {
            Dbg("EnsureLayer FAIL: PcCreateObject Canvas");
            return false;
        }
        g_canvas->Create(kPanelW, kPanelH, vtMissing, vtMissing);

        g_layer = gr->CreateLayer(0, 0, kPanelW, kPanelH, kLayerZ,
                                  static_cast<IUnknown*>(g_canvas.GetInterfacePtr()), vtMissing);
        if (!g_layer) {
            g_canvas = IWzCanvasPtr();
            Dbg("EnsureLayer FAIL: CreateLayer null");
            return false;
        }

        if (!BindLayerToEquip(eq)) {
            Dbg("EnsureLayer FAIL: BindLayerToEquip (new)");
            DestroyLayer();
            return false;
        }

        Paint();

        if (!g_loggedCreate) {
            char buf[300];
            sprintf_s(buf,
                      "Addon LAYER created equip-overlay local=%d,%d screen=%d,%d equipAbs=%d,%d "
                      "pet=%d uiType=%d layer=%p stamp=%s",
                      g_localDockX, g_localDockY, g_addonScreenX, g_addonScreenY, eqX, eqY,
                      PetEquipOpen() ? 1 : 0, uiType, static_cast<void*>(g_layer.GetInterfacePtr()),
                      kStamp);
            Dbg(buf);
            g_loggedCreate = true;
        }
        return true;
    } catch (...) {
        Dbg("EnsureLayer FAIL: exception");
        DestroyLayer();
        return false;
    }
}

void SyncLayer(CWnd* eq) {
    if (!eq || !g_layer) {
        return;
    }
    int uiType = -1;
    int vis = 0;
    if (!SehIsLiveEquipWnd(eq, uiType, vis)) {
        return;
    }
    try {
        int eqX = 0;
        int eqY = 0;
        const int prevLX = g_localDockX;
        const int prevLY = g_localDockY;
        ComputeLocalDock(eq, g_localDockX, g_localDockY, &eqX, &eqY);
        if (!BindLayerToEquip(eq)) {
            return;
        }

        if (g_localDockX != prevLX || g_localDockY != prevLY || g_addonScreenX != g_lastEqX ||
            g_addonScreenY != g_lastEqY) {
            char buf[240];
            sprintf_s(buf,
                      "Addon dock sync local=%d,%d screen=%d,%d equipAbs=%d,%d pet=%d uiType=%d",
                      g_localDockX, g_localDockY, g_addonScreenX, g_addonScreenY, eqX, eqY,
                      PetEquipOpen() ? 1 : 0, uiType);
            Dbg(buf);
            g_lastEqX = g_addonScreenX;
            g_lastEqY = g_addonScreenY;
            Paint();
        } else {
            static ULONGLONG s_lastPaint = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastPaint > 200) {
                Paint();
                s_lastPaint = now;
            }
        }
    } catch (...) {
    }
}

bool GetAbsCursor(POINT& sp) {
    return SehGetAbsCursor(sp);
}


bool WearBagToBp(int bagPos, int bp, const char* via, void* pDrag) {
    if (bagPos <= 0 || bp <= 0) {
        return false;
    }
    void* pChar = GetLocalCharacterData();
    const int itemId = SehDecodeItemId(SehGetItem(pChar, bagPos));
    const int prefix = itemId / 10000;
    // Aux 134/135 → only BP62 (never invent other dst; never share shield −10).
    if (prefix == 134 || prefix == 135) {
        bp = kSubWeaponBp;
    }
    const bool addonLive = IsLiveAddonBp(bp);
    const bool mainPs = IsMainPocketSubBp(bp);
    if (!addonLive && !mainPs) {
        char buf[100];
        sprintf_s(buf, "WEAR reject via=%s bag=%d bp=%d (not addon/mainPS)", via ? via : "?",
                  bagPos, bp);
        Dbg(buf);
        return false;
    }
    if (!BodypartAllowsItem(itemId, bp)) {
        ChatHint(kMismatchSeatMsg);
        char buf[120];
        sprintf_s(buf, "WEAR reject bodypart via=%s id=%d bp=%d", via ? via : "?", itemId, bp);
        Dbg(buf);
        return false;
    }
    // VANILLA PARITY: Addon PacketSendOnly seats replace via mode-2 like classic.
    // Hard client occupy-block + server occupy-reject caused blank UI + "戴不上"
    // while EQUIPPED still held items (out.log 23:05). Still hint on non-replace
    // paths so users know the seat was occupied (optional UX).
    if (GetItemAtBp(bp) != nullptr && !PreferSendChangeOnly(bp)) {
        const bool allowReplace = via && (strstr(via, "replace") != nullptr ||
                                          strstr(via, "drag_totem_replace") != nullptr ||
                                          strstr(via, "drag_equip") != nullptr);
        if (!allowReplace) {
            ChatHint(kOccupySeatMsg);
            char buf[160];
            sprintf_s(buf, "WEAR reject occupy via=%s bag=%d bp=%d id=%d stamp=%s",
                      via ? via : "?", bagPos, bp, itemId, kStamp);
            Dbg(buf);
            void* pCtxBusy = SafeReadPtr(kCWvsContextSingleton);
            CtxForceClearSendBusy(pCtxBusy);
            return false;
        }
    } else if (GetItemAtBp(bp) != nullptr && PreferSendChangeOnly(bp)) {
        char buf[160];
        sprintf_s(buf, "WEAR replace-occupied via=%s bag=%d bp=%d id=%d (vanilla mode-2)",
                  via ? via : "?", bagPos, bp, itemId);
        Dbg(buf);
    }
    const int dstSlot = PacketSlotForBp(bp, itemId);
    // PacketSendOnly for Addon/pocket (not PreferSend mega). Never wear_fn on
    // vanilla 52-slot — aliases cash face/eye. CD64 keeps SendChange product path.
    // Occupied dest: SendChange bag↔−bp swaps like vanilla (replace).
    if (pDrag && !PreferSendChangeOnly(bp) && dstSlot == -bp) {
        const int wr = SehWearFromDraggable(pDrag, bagPos, bp);
        if (wr != 0) {
            char buf[180];
            sprintf_s(buf, "WEAR ok via=%s/wear_fn bag=%d -> %d stamp=%s",
                      via ? via : "?", bagPos, dstSlot, kStamp);
            Dbg(buf);
            ClearTip();
            Paint();
            return true;
        }
        char buf[140];
        sprintf_s(buf, "wear_fn miss via=%s bag=%d bp=%d — fallback SendChange",
                  via ? via : "?", bagPos, bp);
        Dbg(buf);
    } else if (PreferSendChangeOnly(bp) || dstSlot != -bp) {
        char buf[140];
        sprintf_s(buf, "WEAR send-only via=%s bag=%d bp=%d dst=%d cash=%d (no wear_fn)",
                  via ? via : "?", bagPos, bp, dstSlot, (dstSlot != -bp) ? 1 : 0);
        Dbg(buf);
    }
    void* pCtx = SafeReadPtr(kCWvsContextSingleton);
    if (!SehSendWear(pCtx, bagPos, dstSlot)) {
        // Incomplete Addon / totem reject / missed ack must NEVER leave SendBusy.
        CtxForceClearSendBusy(pCtx);
        char buf[140];
        sprintf_s(buf, "WEAR fail via=%s bag=%d bp=%d dst=%d (busy/cooldown/seh) — force clear",
                  via ? via : "?", bagPos, bp, dstSlot);
        Dbg(buf);
        return false;
    }
    // Packet-only: do NOT optimistic-SetItem sidecar. Mode-2 move swaps via GetItem;
    // pre-filling dest with the bag pointer leaves bag uncleared (ghost + Addon).
    char buf[180];
    sprintf_s(buf, "WEAR ok via=%s/send bag=%d -> %d (packet-only, no optimistic) stamp=%s",
              via ? via : "?", bagPos, dstSlot, kStamp);
    Dbg(buf);
    ClearTip();
    Paint();
    return true;
}

bool ResolveCursor(POINT& pt) {
    // Prefer a cursor sample that lands in the Addon panel (RS can desync one path).
    POINT samples[2]{};
    int n = 0;
    POINT cis{};
    if (GetAbsCursor(cis)) {
        samples[n++] = cis;
    }
    POINT win{};
    if (SehGetWinClientCursor(win)) {
        if (n == 0 || win.x != samples[0].x || win.y != samples[0].y) {
            samples[n++] = win;
        }
    }
    if (n == 0) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        int lx = 0;
        int ly = 0;
        if (ScreenToLocal(nullptr, samples[i].x, samples[i].y, lx, ly)) {
            pt = samples[i];
            return true;
        }
    }
    pt = samples[0];
    return true;
}

bool IsStrictAddonPrefix(int prefix) {
    return prefix == 118 || prefix == 119 || prefix == 120 || prefix == 166
            || prefix == 167;
}

// Drag-wear gate: Addon seat rect only (row3 pocket/aux included; never bag shuffle).
bool CursorOnWearSeat(int& outBp, const char*& outReason) {
    outBp = 0;
    outReason = "rejected-not-on-seat";
    POINT pt{};
    if (!ResolveCursor(pt)) {
        return false;
    }
    if (CWnd* eq = AsWnd(GetEquipUi())) {
        SyncAddonScreenFromEquip(eq);
    }
    if (!g_visible) {
        return false;
    }
    if (pt.x < g_addonScreenX || pt.y < g_addonScreenY
        || pt.x >= g_addonScreenX + kPanelW || pt.y >= g_addonScreenY + kPanelH) {
        return false;
    }
    const int lx = pt.x - g_addonScreenX;
    const int ly = pt.y - g_addonScreenY;
    if (!HitSeat(lx, ly, outBp)) {
        return false;
    }
    outReason = "addon-seat";
    return true;
}

// True if cursor is over Addon dock or main Equip window (wear targets).
// Bag shuffle must NOT hit StrictAddon swallow — that froze bag moves for 118+.
bool CursorOverEquipOrAddonWearZone() {
    POINT pt{};
    if (!ResolveCursor(pt)) {
        return false;
    }
    if (CWnd* eq = AsWnd(GetEquipUi())) {
        SyncAddonScreenFromEquip(eq);
        __try {
            // CWnd layout: +0x04 abs X, +0x08 abs Y, +0x0C width?, use layer/getrect
            // Prefer known Equip abs via Sync; also check Addon panel.
            (void)eq;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (g_visible) {
        constexpr int kMargin = 8;
        if (pt.x >= g_addonScreenX - kMargin && pt.y >= g_addonScreenY - kMargin
            && pt.x < g_addonScreenX + kPanelW + kMargin
            && pt.y < g_addonScreenY + kPanelH + kMargin) {
            return true;
        }
    }
    // Main Equip window screen box (from synced dock origin: Addon is left of Equip).
    // Equip singleton abs ≈ Addon right edge when docked; use CWnd+0x04/+0x08/+0x1C/+0x20
    // when available (v083 CWnd: m_width @+0x1C, m_height @+0x20 — verified via prior UI).
    if (void* peq = GetEquipUi()) {
        __try {
            const int ex = *reinterpret_cast<int*>(reinterpret_cast<char*>(peq) + 0x04);
            const int ey = *reinterpret_cast<int*>(reinterpret_cast<char*>(peq) + 0x08);
            const int ew = *reinterpret_cast<int*>(reinterpret_cast<char*>(peq) + 0x1C);
            const int eh = *reinterpret_cast<int*>(reinterpret_cast<char*>(peq) + 0x20);
            if (ew > 0 && ew < 2000 && eh > 0 && eh < 2000) {
                if (pt.x >= ex && pt.y >= ey && pt.x < ex + ew && pt.y < ey + eh) {
                    return true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return false;
}

// Off-seat drop of 118/119/120/166/167 onto Equip/Addon: swallow so native Si/−10
// never wins. NEVER swallow bag→bag (cursor on inventory) — that blocked moves.
bool TryBlockStrictAddonNativeWear(void* pDrag) {
    if (!pDrag) {
        return false;
    }
    int nTI = 0;
    int nPos = 0;
    if (!SehReadDragSlots(pDrag, nTI, nPos) || nTI != 1 || nPos <= 0) {
        return false;
    }
    void* pChar = GetLocalCharacterData();
    const int itemId = SehDecodeItemId(SehGetItem(pChar, nPos));
    const int prefix = itemId / 10000;
    if (!IsStrictAddonPrefix(prefix)) {
        return false;
    }
    int seatBp = 0;
    const char* reason = "rejected-not-on-seat";
    if (CursorOnWearSeat(seatBp, reason)) {
        // Seat path already handled by TryAddonWearDrop; do not double-wear.
        return false;
    }
    // Bag rearrange / drop on field: let native OnDropped run.
    if (!CursorOverEquipOrAddonWearZone()) {
        return false;
    }
    char buf[160];
    sprintf_s(buf,
              "WEAR intercept reason=%s id=%d bag=%d (block native Si on Equip/Addon) stamp=%s",
              reason, itemId, nPos, kStamp);
    Dbg(buf);
    return true; // swallow only when over wear UI
}

bool TryAddonWearDrop(void* pDrag) {
    if (!pDrag) {
        return false;
    }
    if (!g_visible) {
        return false;
    }
    if (CWnd* eq = AsWnd(GetEquipUi())) {
        SyncAddonScreenFromEquip(eq);
    }
    POINT pt{};
    if (!ResolveCursor(pt)) {
        return false;
    }
    if (pt.x < g_addonScreenX || pt.y < g_addonScreenY ||
        pt.x >= g_addonScreenX + kPanelW || pt.y >= g_addonScreenY + kPanelH) {
        return false;
    }
    const int lx = pt.x - g_addonScreenX;
    const int ly = pt.y - g_addonScreenY;
    int bp = 0;
    if (!HitSeat(lx, ly, bp)) {
        char buf[120];
        sprintf_s(buf, "Addon drop in panel but miss seat lx=%d ly=%d cur=%d,%d", lx, ly, pt.x,
                  pt.y);
        Dbg(buf);
        return false;
    }
    int nTI = 0;
    int nPos = 0;
    if (!SehReadDragSlots(pDrag, nTI, nPos)) {
        Dbg("Addon wear drop: read drag slots fail");
        return true;
    }
    // Equipped → Addon seat: −bp↔−bp swap / replace (vanilla SendChange).
    if (nTI == 1 && nPos < 0) {
        const int srcBp = BpFromEquippedSlot(nPos);
        if (srcBp <= 0 || !IsLiveAddonBp(srcBp)) {
            // Classic equipped drag over Addon panel — do not invent dst.
            return false;
        }
        void* pCtx = SafeReadPtr(kCWvsContextSingleton);
        const int dstSlot = PacketSlotForBp(bp, GetItemIdAtBp(srcBp));
        if (nPos == dstSlot) {
            return true; // same seat no-op
        }
        // Source item must be legal on dest seat (type check).
        const int srcId = GetItemIdAtBp(srcBp);
        if (!BodypartAllowsItem(srcId, bp)) {
            ChatHint(kMismatchSeatMsg);
            Dbg("Addon equip↔equip mismatch");
            return true;
        }
        CtxForceClearSendBusy(pCtx);
        if (!SehSendMove(pCtx, nPos, dstSlot)) {
            CtxForceClearSendBusy(pCtx);
            Dbg("Addon equip↔equip SendChange fail");
            return true;
        }
        char buf[140];
        sprintf_s(buf, "WEAR swap via=drag_equip src=%d -> %d stamp=%s", nPos, dstSlot, kStamp);
        Dbg(buf);
        ClearTip();
        Paint();
        return true;
    }
    if (nTI != 1 || nPos <= 0) {
        char buf[100];
        sprintf_s(buf, "Addon wear drop ignore TI=%d pos=%d", nTI, nPos);
        Dbg(buf);
        return false;
    }
    void* pChar = GetLocalCharacterData();
    void* pItem = SehGetItem(pChar, nPos);
    const int itemId = SehDecodeItemId(pItem);
    char bufHit[140];
    sprintf_s(bufHit, "Addon wear drop attempt id=%d seatBP=%d bag=%d cur=%d,%d", itemId, bp, nPos,
              pt.x, pt.y);
    Dbg(bufHit);
    if (bp == kSubWeaponBp && SiPrefixIsShield(itemId / 10000)) {
        ChatHint(kMismatchSeatMsg);
        Dbg("Addon aux reject: 109 shield (use vanilla main Si)");
        return true;
    }
    // Wrong type on Addon seat (e.g. ring on hat-looking cell) — swallow, never native.
    if (!ItemMatchesSeat(itemId, bp)) {
        ChatHint(kMismatchSeatMsg);
        char buf[120];
        sprintf_s(buf, "Addon wear drop mismatch id=%d seat=%d", itemId, bp);
        Dbg(buf);
        return true;
    }
    // Totem: drop on a specific seat → REPLACE that seat (vanilla). Do NOT redirect
    // to a free seat — that blocked drag-replace on occupied Addon totem cells.
    if ((itemId / 10000) == 120) {
        const int atSeat = GetItemIdAtBp(bp);
        const bool seatHasTotem = atSeat > 0 && (atSeat / 10000) == 120;
        if (seatHasTotem) {
            // Replace occupied totem at hit seat.
            char bufTotem[140];
            sprintf_s(bufTotem,
                      "WEAR intercept reason=addon-seat via=drag_totem_replace id=%d bp=%d "
                      "bag=%d stamp=%s",
                      itemId, bp, nPos, kStamp);
            Dbg(bufTotem);
            WearBagToBp(nPos, bp, "drag_totem_replace", pDrag);
            return true;
        }
        // Empty / junk on seat: still wear here. Full-4 is only for dblclick fill.
    }
    char bufWear[140];
    sprintf_s(bufWear, "WEAR intercept reason=addon-seat via=drag id=%d bp=%d bag=%d stamp=%s",
              itemId, bp, nPos, kStamp);
    Dbg(bufWear);
    WearBagToBp(nPos, bp, "drag", pDrag);
    return true;
}

int __fastcall OnDropped_Addon_hook(void* pThis, void* /*edx*/, void* pFrom, void* pTo,
                                    int rx, int ry) {
    // Bag-source + Addon-equipped source: intercept when cursor on Addon seats.
    // Classic equipped↔classic must fall through to native.
    int nTI = 0;
    int nPos = 0;
    const bool haveSlots = SehReadDragSlots(pThis, nTI, nPos);
    const bool bagSrc = haveSlots && nTI == 1 && nPos > 0;
    const bool equippedSrc = haveSlots && nTI == 1 && nPos < 0;
    if (bagSrc || equippedSrc) {
        // Addon is not a CWnd drop target; pTo is often the field. Cursor wins.
        if (TryAddonWearDrop(pThis)) {
            return 1;
        }
    }
    if (bagSrc) {
        // Strict Addon IDs off-seat: swallow only (no force-wear / no native Si).
        if (TryBlockStrictAddonNativeWear(pThis)) {
            CtxForceClearSendBusy(SafeReadPtr(kCWvsContextSingleton));
            return 1;
        }
        // Wrong type over Addon panel (miss seat): swallow so native HitTest cannot
        // invent hat/other BP (cash-HT smash family).
        if (g_visible) {
            POINT pt{};
            if (ResolveCursor(pt)) {
                const int dx = pt.x - g_addonScreenX;
                const int dy = pt.y - g_addonScreenY;
                if (dx >= 0 && dx < kPanelW && dy >= 0 && dy < kPanelH) {
                    void* pChar = GetLocalCharacterData();
                    const int itemId = SehDecodeItemId(SehGetItem(pChar, nPos));
                    const int prefix = itemId / 10000;
                    // Non-Addon items dropped on Addon chrome → reject (no native).
                    if (ExpectedBpForItemId(itemId) == 0 && !IsStrictAddonPrefix(prefix)
                        && prefix != 116 && prefix != 134 && prefix != 135) {
                        ChatHint(kMismatchSeatMsg);
                        CtxForceClearSendBusy(SafeReadPtr(kCWvsContextSingleton));
                        char buf[160];
                        sprintf_s(buf,
                                  "OnDropped swallow wrong-type on Addon panel id=%d stamp=%s",
                                  itemId, kStamp);
                        Dbg(buf);
                        return 1;
                    }
                    char bufMiss[160];
                    sprintf_s(bufMiss,
                              "OnDropped near Addon but miss cur=%d,%d screen=%d,%d stamp=%s",
                              pt.x, pt.y, g_addonScreenX, g_addonScreenY, kStamp);
                    Dbg(bufMiss);
                }
            }
        }
    }
    if (equippedSrc) {
        CtxForceClearSendBusy(SafeReadPtr(kCWvsContextSingleton));
    }
    if (g_OnDroppedOrig) {
        return g_OnDroppedOrig(pThis, pFrom, pTo, rx, ry);
    }
    return 0;
}

int __fastcall WearFromDraggable_Addon_hook(void* pDrag, void* /*edx*/, int nSlot, int nBodyPart) {
    // Equipped→bag (nSlot<0): always native — classic unequip / rearrange.
    // Force-clear SendBusy so incomplete Addon wear never blocks vanilla unequip.
    if (pDrag && nSlot < 0) {
        CtxForceClearSendBusy(SafeReadPtr(kCWvsContextSingleton));
        return g_WearOrig ? g_WearOrig(pDrag, nSlot, nBodyPart) : 0;
    }
    // Global type↔seat gate (classic + Addon). wear_fn success path never calls
    // is_correct_bodypart; AbleToWear only checks level/job. Bad HitTest (ring→hat)
    // must be rejected here — do not invent/remap dst.
    if (pDrag && nSlot > 0 && nBodyPart > 0 && nBodyPart < 1000) {
        void* pCharGate = GetLocalCharacterData();
        const int idGate = SehDecodeItemId(SehGetItem(pCharGate, nSlot));
        const int prefixGate = idGate / 10000;
        // Aux: only BP62 — reject weapon/shield HitTest targets.
        if ((prefixGate == 134 || prefixGate == 135) && nBodyPart != kSubWeaponBp) {
            // If cursor is on Addon aux seat, force correct bp via WearBagToBp.
            int seatBp = 0;
            const char* reason = "rejected-not-on-seat";
            if (CursorOnWearSeat(seatBp, reason) && seatBp == kSubWeaponBp) {
                return WearBagToBp(nSlot, kSubWeaponBp, "wear_fn_aux62", pDrag) ? 1 : 0;
            }
            ChatHint(kMismatchSeatMsg);
            Dbg("WEAR reject aux on non-62 via=wear_fn");
            return 1;
        }
        if (!BodypartAllowsItem(idGate, nBodyPart)) {
            // Addon-owned items off their seat: already handled below; classic wrong
            // seat (ring on hat) → swallow.
            if (!IsStrictAddonPrefix(prefixGate) && prefixGate != 116 && prefixGate != 134
                && prefixGate != 135 && prefixGate != 118 && prefixGate != 119
                && prefixGate != 120 && prefixGate != 166 && prefixGate != 167) {
                ChatHint(kMismatchSeatMsg);
                char buf[140];
                sprintf_s(buf, "WEAR reject bodypart via=wear_fn id=%d bp=%d stamp=%s",
                          idGate, nBodyPart, kStamp);
                Dbg(buf);
                return 1;
            }
        }
    }
    // 109 → always native main shield. Never force Addon aux via wear_fn.
    if (pDrag && nSlot > 0) {
        void* pChar109 = GetLocalCharacterData();
        const int id109 = SehDecodeItemId(SehGetItem(pChar109, nSlot));
        if (SiPrefixIsShield(id109 / 10000)) {
            int seatBp = 0;
            const char* reason = "rejected-not-on-seat";
            if (CursorOnWearSeat(seatBp, reason) && seatBp == kSubWeaponBp) {
                Dbg("WEAR reject 109 on Addon aux via=wear_fn");
                return 1;
            }
            return g_WearOrig ? g_WearOrig(pDrag, nSlot, nBodyPart) : 0;
        }
    }
    // Strict Addon IDs: never let native Si/−10 win. Force-wear only on seat.
    if (pDrag && nSlot > 0) {
        void* pChar = GetLocalCharacterData();
        const int itemId = SehDecodeItemId(SehGetItem(pChar, nSlot));
        const int prefix = itemId / 10000;
        if (IsStrictAddonPrefix(prefix)) {
            int bp = ExpectedBpForItemId(itemId);
            if (prefix == 119) {
                bp = 59;
            }
            const bool nativeSiOrPocket =
                    (nBodyPart == 10 || nBodyPart == kPocketBp || prefix == 119);
            int seatBp = 0;
            const char* reason = "rejected-not-on-seat";
            const bool onSeat = CursorOnWearSeat(seatBp, reason);
            if (onSeat && bp > 0 && IsLiveAddonBp(bp)) {
                if (ItemMatchesSeat(itemId, seatBp)) {
                    bp = seatBp;
                }
                char buf[160];
                sprintf_s(buf,
                          "WEAR intercept reason=%s via=wear_fn id=%d bp=%d bag=%d stamp=%s",
                          reason, itemId, bp, nSlot, kStamp);
                Dbg(buf);
                return WearBagToBp(nSlot, bp, "wear_fn_seat", pDrag) ? 1 : 0;
            }
            if (nativeSiOrPocket || !onSeat) {
                char buf[160];
                sprintf_s(buf,
                          "WEAR intercept reason=rejected-not-on-seat via=wear_fn id=%d "
                          "nativeBp=%d bag=%d (no force-wear) stamp=%s",
                          itemId, nBodyPart, nSlot, kStamp);
                Dbg(buf);
                return 1; // swallow — do not equip off-seat / Si
            }
        }
    }
    return g_WearOrig ? g_WearOrig(pDrag, nSlot, nBodyPart) : 0;
}

bool TryUnequipBp(int bp, const char* via); // defined below

bool TryAddonBagDblWear(void* pDrag) {
    if (!pDrag) {
        return false;
    }
    int nTI = 0;
    int nPos = 0;
    if (!SehReadDragSlots(pDrag, nTI, nPos)) {
        return false;
    }
    if (nTI != 1 || nPos <= 0) {
        return false;
    }
    void* pChar = GetLocalCharacterData();
    void* pItem = SehGetItem(pChar, nPos);
    const int itemId = SehDecodeItemId(pItem);
    const int prefix = itemId / 10000;
    // Totem full → reject (do not fall through to native replace/wrong seat).
    // Strict prefix==120 only — android/heart must never see this toast.
    if (prefix == 120 && CountEquippedTotems() >= 4) {
        ChatHint(kTotemFullMsg);
        Dbg("Addon bag dblclick totem FULL — reject 5th");
        return true;
    }
    // Emblem/badge/totem/android/heart must never reach native red-10 Si path.
    if (prefix == 118 || prefix == 119 || prefix == 120 || prefix == 166 || prefix == 167) {
        int bp = ExpectedBpForItemId(itemId);
        if (prefix == 166) {
            bp = kAndroidBp;
        } else if (prefix == 167) {
            bp = kHeartBp;
        } else if (prefix == 119) {
            bp = 59;
        } else if (prefix == 118) {
            bp = 54;
        }
        if (bp <= 0 || !IsLiveAddonBp(bp)) {
            ChatHint(kMismatchSeatMsg);
            return true;
        }
        char buf[120];
        sprintf_s(buf, "Addon bag dblclick id=%d -> bp=%d", itemId, bp);
        Dbg(buf);
        WearBagToBp(nPos, bp, "bag_dbl", pDrag);
        return true;
    }
    const int bp = ExpectedBpForItemId(itemId);
    if (bp == 0) {
        return false; // not Addon / pocket / Si(109/134/135) — leave to native
    }
    if (IsMainPocketSubBp(bp)) {
        WearBagToBp(nPos, bp, "bag_dbl_main", pDrag);
        return true;
    }
    if (!IsLiveAddonBp(bp)) {
        return false;
    }
    char buf[120];
    sprintf_s(buf, "Addon bag dblclick id=%d -> bp=%d", itemId, bp);
    Dbg(buf);
    // Intercept: force −BP via wear_fn/SendChange — do NOT let native use parked GetSlotXY.
    WearBagToBp(nPos, bp, "bag_dbl", pDrag);
    return true;
}

int __fastcall OnDoubleClicked_Addon_hook(void* pThis, void* /*edx*/) {
    int nTI = 0;
    int nPos = 0;
    if (SehReadDragSlots(pThis, nTI, nPos) && nTI == 1 && nPos < 0) {
        // Equipped on main Equip: FORCE clear busy then native/shoulders only.
        // Stuck busy (rejected totem/Addon wear) froze ALL classic dblclick unequip.
        // Do NOT SendChange-fallback classic seats. Addon seats unequip via HandleInput.
        void* pCtx = SafeReadPtr(kCWvsContextSingleton);
        CtxForceClearSendBusy(pCtx);
        return g_OnDblOrig ? g_OnDblOrig(pThis) : 0;
    }
    if (TryAddonBagDblWear(pThis)) {
        return 1;
    }
    return g_OnDblOrig ? g_OnDblOrig(pThis) : 0;
}

bool TryUnequipBp(int bp, const char* via) {
    void* item = GetItemAtBp(bp);
    if (!item) {
        return false;
    }
    const int itemId = DecodeItemId(item);
    // CD64 cash → −(bp+100); vanilla sidecar still −bp (aliased ZRef).
    int slot = PacketSlotForBp(bp, itemId);
    const int dest = FindEmptyEquipBagSlot();
    void* pCtx = SafeReadPtr(kCWvsContextSingleton);
    CtxForceClearSendBusy(pCtx);
    if (dest <= 0) {
        Dbg("UNEQUIP fail: bag full");
        return false;
    }
    if (!SehSendUnequip(pCtx, slot, dest)) {
        char buf[120];
        sprintf_s(buf, "UNEQUIP fail via=%s slot=%d dest=%d (busy/seh)", via ? via : "?", slot,
                  dest);
        Dbg(buf);
        return false;
    }
    // Packet-only: do NOT clear-only sidecar and do NOT optimistic bag SetItem.
    // Mode-2 unequip swaps GetItem(dest)↔GetItem(slot). Ghost (server empty): server
    // enableActions only — OnTick clears sidecar when busy drops and seat still painted.
    g_pendingUnequipBp = bp;
    g_pendingUnequipAt = GetTickCount64();
    char buf[200];
    sprintf_s(buf,
              "UNEQUIP ok via=%s slot=%d bp=%d id=%d -> bag=%d (packet-only, pending-ghost-heal) stamp=%s",
              via ? via : "?", slot, bp, itemId, dest, kStamp);
    Dbg(buf);
    ClearTip();
    Paint();
    return true;
}

void HandleInput(CWnd* eq) {
    if (!eq || !g_visible) {
        ClearTip();
        return;
    }
    SyncAddonScreenFromEquip(eq);

    POINT pt{};
    if (!ResolveCursor(pt)) {
        ClearTip();
        return;
    }

    int lx = 0;
    int ly = 0;
    const bool inPanel = ScreenToLocal(eq, pt.x, pt.y, lx, ly);

    // Probe: when near panel/equip, log coords so miss hits are diagnosable.
    static ULONGLONG s_lastProbe = 0;
    const ULONGLONG nowProbe = GetTickCount64();
    if (nowProbe - s_lastProbe > 500) {
        const int nearDx = pt.x - g_addonScreenX;
        const int nearDy = pt.y - g_addonScreenY;
        if (nearDx > -80 && nearDx < kPanelW + 80 && nearDy > -80 && nearDy < kPanelH + 80) {
            s_lastProbe = nowProbe;
            char buf[220];
            sprintf_s(buf,
                      "PROBE cur=%d,%d screen=%d,%d localDock=%d,%d in=%d pet=%d stamp=%s",
                      pt.x, pt.y, g_addonScreenX, g_addonScreenY, g_localDockX, g_localDockY,
                      inPanel ? 1 : 0, PetEquipOpen() ? 1 : 0, kStamp);
            Dbg(buf);
        }
    }

    // Drag-off unequip: press on occupied live seat, release outside panel.
    static bool s_dragArmed = false;
    static int s_dragBp = 0;
    static int s_dragX = 0;
    static int s_dragY = 0;
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    static bool s_wasDown = false;

    if (!inPanel) {
        if (s_dragArmed && !down && s_wasDown) {
            const int dx = pt.x - s_dragX;
            const int dy = pt.y - s_dragY;
            if (dx * dx + dy * dy >= 36) {
                TryUnequipBp(s_dragBp, "drag_off");
            }
        }
        if (!down) {
            s_dragArmed = false;
            s_dragBp = 0;
        }
        s_wasDown = down;
        ClearTip();
        return;
    }

    int bp = 0;
    if (!HitSeat(lx, ly, bp)) {
        if (!down) {
            s_dragArmed = false;
        }
        s_wasDown = down;
        ClearTip();
        return;
    }

    const int itemId = GetItemIdAtBp(bp);
    if (down && !s_wasDown) {
        char buf[180];
        sprintf_s(buf, "Addon CLICK lx=%d ly=%d bp=%d id=%d cur=%d,%d screen=%d,%d stamp=%s", lx,
                  ly, bp, itemId, pt.x, pt.y, g_addonScreenX, g_addonScreenY, kStamp);
        Dbg(buf);
    }

    // Wrong-type (e.g. pet at old heart seat) → treat as empty.
    if (itemId <= 0 || !ItemMatchesSeat(itemId, bp)) {
        s_wasDown = down;
        s_dragArmed = false;
        ClearTip();
        return;
    }
    // Refresh tip every tick while hovering (tooltips clear if not re-shown).
    ShowTip(bp, pt.x, pt.y);

    // Arm drag-off + dblclick unequip (layer is not a CWnd — poll LBUTTON).
    static ULONGLONG s_lastClick = 0;
    static int s_lastBp = 0;
    if (down && !s_wasDown) {
        s_dragArmed = true;
        s_dragBp = bp;
        s_dragX = pt.x;
        s_dragY = pt.y;
        const ULONGLONG now = GetTickCount64();
        if (bp == s_lastBp && now - s_lastClick < 550) {
            char buf[120];
            sprintf_s(buf, "UNEQUIP attempt via=dblclick bp=%d id=%d stamp=%s", bp, itemId,
                      kStamp);
            Dbg(buf);
            TryUnequipBp(bp, "dblclick");
            s_lastClick = 0;
            s_lastBp = 0;
            s_dragArmed = false;
        } else {
            s_lastClick = now;
            s_lastBp = bp;
        }
    }
    if (!down) {
        s_dragArmed = false;
    }
    s_wasDown = down;
}

void LogInitOnce() {
    g_dbgFileOk = true; // past DllMain — safe to fopen equipaddon_debug.log
    if (g_initLogged) {
        return;
    }
    g_initLogged = true;
    char buf[512];
    sprintf_s(buf,
              "=== EquipAddon Init %s (poll BED64C+ctx+35C0, dock CUIEquip UIType=1, "
              "LAYER Equip-overlay z=%d, TIP hover, WEAR drag/bag_dbl, UNEQUIP dbl+drag_off, "
              "PetEquip BED648 avoid, Totemx4+Emblem+Android60+Heart61+Aux62, "
              "CD64=%d native-inv=%d (%s), "
              "Paint=native −bp / cash −(bp+100) prefer-cash (CUIEquip Draw), "
              "CD64 cash PacketSlot −(bp+100), "
              "33/54-62 PacketSendOnly (no PreferSend mega), "
              "park54/55 every tick, row3 pocket33+aux62 NO red9/10 wire, "
              "login keep-slots (no cash/normal swap), SendBusy watchdog gated CharData, %s %s %s) ===",
              kStamp, kLayerZ, DetectCd64ExeLite(), NativeCd64Inventory() ? 1 : 0,
              NativeCd64Inventory() ? kNativeUiMarker : "sidecar-inv-ON",
              kCashAppendMarker, kRow3WearMarker, kWearFixMarker);
    Dbg(buf);
}

// Full persist (Park + Get/Set + caves). Call from EnsureHooks / field — NOT DllMain.
void InstallLoginPersistOnce() {
    if (kEnableAddonPark) {
        ParkMainAddonSeatsOffPanel();
    }
    if (NativeCd64Inventory()) {
        g_applyCaveDone = g_cashLeaDone = g_loginAllowDone = g_loginClearDone = true;
        g_getItemHooked = g_setItemHooked = true;
        Dbg("CD64 native — Addon UI-only (no sidecar Get/Set)");
        return;
    }
    if (kEnableAddonApplyCaves) {
        InstallSidecarApplyCave();
    } else {
        g_applyCaveDone = g_cashLeaDone = g_loginAllowDone = g_loginClearDone = true;
        Dbg("Addon ApplyEquip/login caves OFF — classic equip safe");
    }
    if (!kEnableAddonGetSetHooks) {
        g_getItemHooked = g_setItemHooked = true;
        Dbg("Addon Get/Set Detours OFF — classic bag/equip safe");
        return;
    }
    if (!g_getItemHooked) {
        g_getItemHooked = true;
        if (!ATTACH_HOOK(g_GetItemOrig, GetItem_Sidecar_hook)) {
            Dbg("Sidecar GetItem hook FAILED");
        } else {
            Dbg("Sidecar GetItem hook OK (BP54-62)");
        }
    }
    if (!g_setItemHooked) {
        g_setItemHooked = true;
        if (!ATTACH_HOOK(g_SetItemOrig, SetItem_Sidecar_hook)) {
            Dbg("Sidecar SetItem hook FAILED");
        } else {
            Dbg("Sidecar SetItem hook OK (BP54-62)");
        }
    }
}

void InstallUiHooksOnce() {
    if (!kEnableAddonUiHooks) {
        Dbg("Addon UI Detours OFF (Drop/Wear/Dbl/Logout) — classic equip safe");
        return;
    }
    if (!g_logoutUiHooked) {
        g_logoutUiHooked = true;
        if (!ATTACH_HOOK(g_DestroyGameUiOrig, DestroyGameUi_hook)) {
            Dbg("DestroyGameUI A041FF hook FAILED — login UI leak risk");
        } else {
            char buf[96];
            sprintf_s(buf, "%s hook OK stamp=%s", kLogoutUiMarker, kStamp);
            Dbg(buf);
        }
    }
    if (!g_dropHooked) {
        g_dropHooked = true;
        if (!ATTACH_HOOK(g_OnDroppedOrig, OnDropped_Addon_hook)) {
            Dbg("Addon OnDropped hook FAILED — drag-wear disabled");
        } else {
            Dbg("Addon OnDropped wear hook OK (seat-only + reject-off-seat strict)");
        }
    }
    if (!g_wearHooked) {
        g_wearHooked = true;
        if (!ATTACH_HOOK(g_WearOrig, WearFromDraggable_Addon_hook)) {
            Dbg("Addon WearFromDraggable hook FAILED — emblem may hit Si");
        } else {
            Dbg("Addon WearFromDraggable hook OK (seat-gate + equipped callthrough)");
        }
    }
    if (!g_dblHooked) {
        g_dblHooked = true;
        if (!ATTACH_HOOK(g_OnDblOrig, OnDoubleClicked_Addon_hook)) {
            Dbg("Addon OnDoubleClicked hook FAILED — bag dbl uses shoulders path");
        } else {
            Dbg("Addon OnDoubleClicked OK (bag Addon wear + equipped unequip callthrough)");
        }
    }
}

void InstallHooksOnce() {
    InstallLoginPersistOnce();
    InstallUiHooksOnce();
}

} // namespace

extern "C" void* __cdecl EquipAddon_SidecarZRefForBp(int bp) {
    return g_sidecarZRefFn ? g_sidecarZRefFn(bp) : nullptr;
}

void AttachEquipAddonMod() {
    LogInitOnce();
    InstallHooksOnce();
}

namespace EquipAddon {

void InstallLoginPersistEarly() {
    // FORBIDDEN in DllMain / boot (avoid #35/#38). Keep as no-op stub so any
    // accidental call cannot install caves/GetSet under loader lock.
    // Quiet caves + Get/Set attach only via EnsureHooks → InstallLoginPersistOnce.
    (void)0;
}

void EnsureHooks() {
    LogInitOnce();
    InstallHooksOnce();
}

void WirePocketAndSiSlots() {
    // ROW3: park54/55 only — red9/10 wire abandoned (pocket/aux on Addon seats).
    if (kEnableAddonPark) {
        ParkMainAddonSeatsOffPanel();
    }
    WireMainPocketSubSlots(); // no-op
}

void OnTick() {
    if (!kEnableAddonOnTick) {
        return;
    }
    void* pCtx = SafeReadPtr(kCWvsContextSingleton);
    // Login / char-select / logo: tear down. Keep UI on CField + CInterStage (map load).
    if (!ShouldKeepGameUi()) {
        EnsureLogoutUiTornDown(pCtx);
        return;
    }
    g_forcedLogoutUiOnce = false;

    // Gate remaining Addon tick work until in-character.
    if (!CtxReadyForSendBusy(pCtx)) {
        if (g_layer || g_visible) {
            Dbg("in-field but no CharData -> destroy Addon layer");
            DestroyLayer();
        }
        return;
    }
    LogInitOnce();

    void* eqSing = GetEquipUiSingleton();
    void* eqCtx = GetEquipUiFromCtx();
    void* eq = GetEquipUi();
    const DWORD rawBed64C = SafeReadDword(kCUIEquipSingleton);
    const DWORD rawBed650 = SafeReadDword(kCUIKeyConfigSingleton);
    const DWORD rawZ = SafeReadDword(reinterpret_cast<uintptr_t>(pCtx) + kOff_CWvs_UIEquip_Ptr);
    const ULONGLONG now = GetTickCount64();

    // ENTER_FIX3: WirePocketAndSiSlots no-ops unless Equip UI resolved below.

    const bool eDown = (GetAsyncKeyState('E') & 0x8000) != 0;
    if (eDown && !g_eWasDown) {
        g_ePressedAt = now;
        char buf[280];
        sprintf_s(buf,
                  "key E edge rawBED64C=0x%08X rawBED650=0x%08X ctx=%p rawZ@+35C0=0x%08X "
                  "sing=%p ctxZ=%p cache=%p/%s resolved=%p addon=%p",
                  rawBed64C, rawBed650, pCtx, rawZ, eqSing, eqCtx, g_cachedEquip, g_cacheSrc, eq,
                  static_cast<void*>(g_layer.GetInterfacePtr()));
        Dbg(buf);
    }
    if (eDown && now - g_lastKeyLog > 500) {
        g_lastKeyLog = now;
        char buf[280];
        sprintf_s(buf,
                  "key E down rawBED64C=0x%08X rawBED650=0x%08X ctx=%p rawZ@+35C0=0x%08X "
                  "sing=%p ctxZ=%p cache=%p/%s resolved=%p addon=%p",
                  rawBed64C, rawBed650, pCtx, rawZ, eqSing, eqCtx, g_cachedEquip, g_cacheSrc, eq,
                  static_cast<void*>(g_layer.GetInterfacePtr()));
        Dbg(buf);
    }
    if (!eDown && g_eWasDown && g_ePressedAt != 0 && now - g_ePressedAt > 80 && !eq) {
        Dbg("key E released but Equip still null — open path may have failed");
    }
    g_eWasDown = eDown;

    if (now - g_lastDiag > 3000) {
        g_lastDiag = now;
        char buf[320];
        sprintf_s(buf,
                  "tick rawBED64C=0x%08X rawBED650=0x%08X ctx=%p rawZ@+35C0=0x%08X sing=%p "
                  "ctxZ=%p cache=%p/%s resolved=%p layer=%p vis=%d stamp=%s",
                  rawBed64C, rawBed650, pCtx, rawZ, eqSing, eqCtx, g_cachedEquip, g_cacheSrc, eq,
                  static_cast<void*>(g_layer.GetInterfacePtr()), g_visible ? 1 : 0, kStamp);
        Dbg(buf);
    }

    if (!eq) {
        if (g_layer || g_visible) {
            Dbg("Equip closed -> destroy Addon layer");
            DestroyLayer();
        }
        // Age-gated unstick + watchdog so bag→Equip unequip works with Equip closed.
        CtxTickSendBusyWatchdog(pCtx);
        CtxUnstickSendBusy(pCtx, false);
        return;
    }

    CWnd* wnd = AsWnd(eq);
    int uiType = -1;
    int vis = 0;
    if (!SehIsLiveEquipWnd(wnd, uiType, vis)) {
        if (g_layer || g_visible) {
            char buf[160];
            sprintf_s(buf, "Equip not live (uiType=%d vis=%d) -> destroy Addon", uiType, vis);
            Dbg(buf);
            DestroyLayer();
        }
        return;
    }
    if (!GetLocalCharacterData()) {
        return; // set_stage / select→enter — zero pocket calls
    }

    // Unblock native Equip unequip if Addon wear left SendBusy stuck.
    // Watchdog <100ms + 50ms age-gate — never wait 500ms while unequip is dead.
    static ULONGLONG s_lastBusyUnstick = 0;
    if (now - s_lastBusyUnstick > 50) {
        s_lastBusyUnstick = now;
        CtxTickSendBusyWatchdog(pCtx);
        CtxUnstickSendBusyEx(pCtx, false, kSendBusyUnstickAgeMs);
    }

    // Ghost heal: unequip sent but server source-null (enableActions only, no mode-2).
    // After busy clears (≥80ms), if seat still painted → local ClearSidecarBp.
    // Real mode-2 already nulls GetItem — no-op. Never send equipped mode-3.
    // ALSO: if busy never latched (enableActions race), clear after 80ms anyway.
    if (g_pendingUnequipBp >= kSidecarBpMin && g_pendingUnequipBp <= kSidecarBpMax
        && g_pendingUnequipAt != 0 && now - g_pendingUnequipAt > 80) {
        const bool busy = CtxSendBusySet(pCtx);
        if (!busy) {
            const int bp = g_pendingUnequipBp;
            if (GetItemAtBp(bp) != nullptr) {
                ClearSidecarBp(bp);
                char buf[120];
                sprintf_s(buf, "GHOST heal ClearSidecar bp=%d stamp=%s", bp, kStamp);
                Dbg(buf);
                Paint();
            }
            g_pendingUnequipBp = 0;
            g_pendingUnequipAt = 0;
        } else if (now - g_pendingUnequipAt > 2000) {
            // Stuck busy: still clear ghost paint so organize is not fed a desynced UI.
            const int bp = g_pendingUnequipBp;
            if (GetItemAtBp(bp) != nullptr) {
                ClearSidecarBp(bp);
                Dbg("GHOST heal ClearSidecar (busy-stuck timeout)");
                Paint();
            }
            CtxForceClearSendBusy(pCtx);
            g_pendingUnequipBp = 0;
            g_pendingUnequipAt = 0;
        }
    }

    // park54/55 every live Equip tick (no red9/10 XY mutation).
    WirePocketAndSiSlots();

    static ULONGLONG s_lastPocketProbe = 0;
    if (now - s_lastPocketProbe > 5000) {
        s_lastPocketProbe = now;
        const int id33 = DecodeItemId(GetEquippedItemAt(-kPocketBp));
        const int id133 = DecodeItemId(GetEquippedItemAt(-(kPocketBp + 100)));
        const int id62 = DecodeItemId(GetEquippedItemAt(-kSubWeaponBp));
        const int id162 = DecodeItemId(GetEquippedItemAt(-(kSubWeaponBp + 100)));
        if (id33 > 0 || id133 > 0 || id62 > 0 || id162 > 0) {
            char buf[220];
            sprintf_s(buf,
                      "ROW3 probe id-33=%d id-133=%d id-62=%d id-162=%d addon=(6|39,147) stamp=%s",
                      id33, id133, id62, id162, kStamp);
            Dbg(buf);
        }
    }

    if (!EnsureLayer(wnd)) {
        return;
    }
    SyncLayer(wnd);
    HandleInput(wnd);
}

} // namespace EquipAddon
