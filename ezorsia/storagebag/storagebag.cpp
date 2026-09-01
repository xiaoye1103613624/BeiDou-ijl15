// ============================================================
// storagebag.cpp  —  the STORAGE BAG window (inventory BAG button): Ore / Scroll / Chair / Cash bags in one window
//
// A client-DLL CWnd window that gives the player extra, server-backed storage split
// across four "bag" kinds (Ore / Scroll / Chair / Cash). ONE CWnd subclass
// (CUIBagWindow), a SINGLE instance, shows one bag kind at a time. The window chrome is
// the "STORAGE BAG" art (UI/UIWindow.img/Bag/backgrnd, 207x248, a 5x5 grid). The four bag
// kinds are selected by clickable tab labels (ORE/SCROLL/CHAIR/CASH) drawn in vanilla
// item-inventory style — a pill behind the active kind, grey behind the rest — with the
// white label centered on each. The body is a 5x5 scrollable item grid with native hover
// tooltips and native drag (drag OUT -> withdraw to inventory; drag a valid item IN from
// the player inventory -> deposit into the active bag). Server-backed: each bag is
// DB-persistent and arrives as a RESP_SNAPSHOT, and every transfer is a server request
// that re-snapshots. Also injects a "BAG" open-button onto the vanilla item inventory
// window (see "Inventory-window bag button" near the bottom).
//
// Title bar (right side): [ sort/merge (red BtSort = consolidate stacks) ] [ close
// (Bag/BtClose) ]. The scrollbar is the vanilla blue VScr4 (Basic.img) in the right
// margin. The search field along the bottom is baked into the art; the typed text /
// caret / hint / result-count are drawn ON TOP of it.
//
// -------------------------------------------------------------------------------------
// WARNING — CLIENT-BUILD-SPECIFIC ADDRESSES:
// EVERY hard-coded address in this file (0x00XXXXXX, image base 0x400000) — the window
// and hook addresses, the singletons, the engine call targets, e.g. the CUIItem
// TSingleton @0x00BED654 and the CUIItem::OnCreate / CUIItem::OnChildNotify hooks — is
// specific to ONE particular v83 client build. They are NOT portable: on any other
// client (different build, patch, or localization) every one of these addresses MUST be
// RE-FOUND (re-reverse-engineered) or the DLL will crash or misbehave. Treat all the
// 0x00... constants below as tied to this exact executable.
// ============================================================

// BeiDou v083 — IDA MCP verified 2026-07-10 (image base 0x400000)
// CUIItem singleton 0x00BED654, OnCreate 0x0081C6C9, Draw 0x0081DC20, OnMouseButton 0x0081D42E
// SendPacket 0x0049637B, ClientSocket 0x00BE7914 — see compat/ClientAddresses.h
// Recv opcode 0x3725 via PacketDispatcher (NOT ProcessPacket detour).

#include "stdafx.h"
#include "StorageBagApi.h"
#include "ClientAddresses.h"
#include "compat/hook.h"
#include "wvs/Packet.h"
#include "wvs/packet_legacy.h"
#include "wvs/wnd.h"
#include "wvs/iteminfo.h"
#include "wvs/util.h"
#include "wvs/wndman.h"
#include "wvs/ctrlwnd.h"
#include "wvs/field.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <cctype>

namespace BagWindow {

// ---------------------------------------------------------------------------
// Item-name lookup (for the search bar) — reads the display name out of String.wz
// via the resource manager (same get_rm() pattern getwzinfo.cpp uses), lowercased
// + cached so a keystroke filter never re-hits the WZ for the same item.
//
// Names live in per-category top-level images: Consume.img (scrolls/Use), Etc.img
// (ores), Ins.img, Cash.img, Eqp.img (equips — incl. mounts 1902xxx/1912xxx).
// Consume/Ins/Cash are FLAT (<id>/name); Etc.img nests under an "Etc" wrapper
// (Etc.img/Etc/<id>/name); Eqp.img nests TWO levels — Eqp/<category>/<id>/name
// (mounts are under "Taming") — so equips probe the known categories by id.
// ---------------------------------------------------------------------------
static const wchar_t* ItemCatImg(int id) {
    switch (id / 1000000) {
        case 1:  return L"Eqp";       // equips (mounts 1902xxx/1912xxx) — nested Eqp/<category>/<id>
        case 2:  return L"Consume";   // Use — scrolls (204xxxx) + White Scroll (2340000)
        case 3:  return L"Ins";
        case 4:  return L"Etc";       // ores / maker materials (wrapped under "Etc")
        case 5:  return L"Cash";
        default: return nullptr;
    }
}
// Lowercase + strip Latin-1/CP1252 accents so the search bar is accent-INSENSITIVE: the item name
// "Minério" normalizes to "minerio", so typing plain "minerio" (no dead-key accent) finds it.
static char NormalizeChar(unsigned char uc) {
    if (uc >= 'A' && uc <= 'Z') return (char)(uc - 'A' + 'a');
    switch (uc) {   // accented letters (upper + lower, CP1252) -> ASCII base
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
        case 0xC7: case 0xE7: return 'c';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
        case 0xD1: case 0xF1: return 'n';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6:
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC:
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    }
    return (char)uc;   // ascii letters/digits/space/punctuation kept as-is
}
// Combine a spacing dead-accent (CP1252) with a lowercase base letter -> the accented CP1252 char, or 0 if
// the pair doesn't combine. Covers the pt-BR set (accented vowels + n-tilde). acc is the spacing accent
// byte (0xB4 acute, 0x60 grave, 0x5E circumflex, 0x7E tilde, 0xA8 diaeresis).
static char ComposeAccent(char acc, char base) {
    switch ((unsigned char)acc) {
        case 0xB4: switch (base) { case 'a': return (char)0xE1; case 'e': return (char)0xE9; case 'i': return (char)0xED; case 'o': return (char)0xF3; case 'u': return (char)0xFA; } break;
        case 0x60: switch (base) { case 'a': return (char)0xE0; case 'e': return (char)0xE8; case 'i': return (char)0xEC; case 'o': return (char)0xF2; case 'u': return (char)0xF9; } break;
        case 0x5E: switch (base) { case 'a': return (char)0xE2; case 'e': return (char)0xEA; case 'i': return (char)0xEE; case 'o': return (char)0xF4; case 'u': return (char)0xFB; } break;
        case 0x7E: switch (base) { case 'a': return (char)0xE3; case 'o': return (char)0xF5; case 'n': return (char)0xF1; } break;
        case 0xA8: switch (base) { case 'a': return (char)0xE4; case 'e': return (char)0xEB; case 'i': return (char)0xEF; case 'o': return (char)0xF6; case 'u': return (char)0xFC; } break;
    }
    return 0;
}
static const std::string& GetItemNameLower(int id) {
    static std::unordered_map<int, std::string> s_cache;
    auto it = s_cache.find(id);
    if (it != s_cache.end()) return it->second;
    std::string name;
    const wchar_t* img = ItemCatImg(id);
    if (img) {
        try {
            std::wstring path = std::wstring(L"String/") + img + L".img";
            IWzPropertyPtr root = get_rm()->GetObjectA(path.c_str()).GetUnknown();
            if (root) {
                Ztl_bstr_t sId = std::to_wstring(id).c_str();
                IWzPropertyPtr node;
                if (id / 1000000 == 1) {
                    // Equips: String/Eqp.img/Eqp/<category>/<id>/name (mounts live under "Taming").
                    // The sub-category isn't known up front, so probe the known equip categories by
                    // direct id-index (each is a hash lookup, not a scan) — first hit wins, then cached.
                    IWzPropertyPtr eqp = root->item[img].GetUnknown();     // the "Eqp" wrapper
                    if (eqp) {
                        static const wchar_t* kEqpCats[] = {
                            L"Taming", L"Weapon", L"Cap", L"Accessory", L"Coat", L"Longcoat", L"Pants",
                            L"Shoes", L"Glove", L"Shield", L"Cape", L"Ring", L"Face", L"Hair", L"PetEquip", L"Dragon" };
                        for (const wchar_t* c : kEqpCats) {
                            IWzPropertyPtr cat = eqp->item[c].GetUnknown();
                            if (cat) { node = cat->item[sId].GetUnknown(); if (node) break; }
                        }
                    }
                } else {
                    node = root->item[sId].GetUnknown();                   // flat (Consume/Ins/Cash)
                    if (!node) {
                        IWzPropertyPtr wrap = root->item[img].GetUnknown(); // wrapper (Etc.img/Etc)
                        if (wrap) node = wrap->item[sId].GetUnknown();
                    }
                }
                if (node) {
                    Ztl_variant_t v = node->item[L"name"];
                    if (v.vt == VT_BSTR) name = (const char*)_bstr_t(v);
                }
            }
        } catch (...) {}
    }
    for (char& c : name) c = NormalizeChar((unsigned char)c);
    return s_cache.emplace(id, std::move(name)).first->second;
}

// ---------------------------------------------------------------------------
// Addresses & thin wrappers
// ---------------------------------------------------------------------------
static constexpr uintptr_t kAddr_CWvsContext_Instance  = 0x00BE7918;
static constexpr uintptr_t kAddr_TSecType_long_GetData = 0x0042873D; // item+0xC plaintext itemID
static constexpr uintptr_t kAddr_play_ui_sound         = 0x00989588;
static constexpr uintptr_t kAddr_get_basic_font        = 0x0098A707;
static constexpr uintptr_t kAddr_CField_OnKey          = 0x00529968;
static constexpr uintptr_t kAddr_ProcessBasicUIKey     = 0x00A07431;
static constexpr uintptr_t kAddr_SetFont               = 0x0046341A; // IWzFont::SetFont (name,size,color,..)
static constexpr uintptr_t kAddr_TT_Ctor               = 0x008E49B5;
static constexpr uintptr_t kAddr_TT_Dtor               = 0x008E6BA3;
static constexpr uintptr_t kAddr_TT_Clear              = 0x008E6E23;
static constexpr uintptr_t kAddr_ShowItemToolTip       = 0x008F5B20;

static auto play_ui_sound  = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto get_basic_font = reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

typedef void(__thiscall* t_TT)(void*);
static auto TT_Ctor  = reinterpret_cast<t_TT>(kAddr_TT_Ctor);
static auto TT_Dtor  = reinterpret_cast<t_TT>(kAddr_TT_Dtor);
static auto TT_Clear = reinterpret_cast<t_TT>(kAddr_TT_Clear);
typedef void(__thiscall* t_ShowItemToolTip)(void*, int, int, void*, void*, void*, int, int, int);
static auto ShowItemToolTip = reinterpret_cast<t_ShowItemToolTip>(kAddr_ShowItemToolTip);

typedef int(__thiscall* t_GetData)(const void*);
static auto TSecType_long_GetData = reinterpret_cast<t_GetData>(kAddr_TSecType_long_GetData);

static void* GetWvsContext() { return *reinterpret_cast<void**>(kAddr_CWvsContext_Instance); }

// Absolute (screen) cursor position from the engine input system — the same source the window
// drag uses, so the BAG button hit-test and the drag ghost all agree on where the cursor is.
// Returns false (and leaves sp at 0,0) if the input system isn't available.
static bool GetAbsCursor(POINT& sp) {
    sp.x = 0; sp.y = 0;
    void* inputSystem = *reinterpret_cast<void**>(0x00BEC33C);
    if (!inputSystem) return false;
    reinterpret_cast<void(__thiscall*)(void*, POINT*)>(0x0059A388)(inputSystem, &sp);
    return true;
}

// Set the engine cursor sprite state (CInputSystem::SetCursorState @0x0059A6D9). States (from
// UI/Basic.img/Cursor, confirmed by exporting the sprites):
//   0  = arrow
//   1  = finger + red mouse-click badge — the "click me" pointer; shown ONLY while HOVERING the BAG button
//   5  = open/close hand — shown while hovering an item (the "you can grab this" cue)
//   7  = ANIMATED up/down arrows (4-frame, bounces) — the vertical-scroll HOVER cue, over the scrollbar
//   9  = STATIC up/down arrows — the vertical-scroll GRAB cursor, while dragging the scrollbar thumb
//   11 (0xB) = closed grab hand — shown while a bag item is held on the cursor (the drag)
//   12 = plain "pressing finger" — shown at the moment a button is CLICKED/held (BAG + the bag's X/sort/auto)
// -1 restores the previous state. We drive these ourselves because our custom window + the neutralized
// BAG-button click never reach the engine's own cursor logic (and the engine resets the sprite each frame).
static const int kCursor_Arrow = 0, kCursor_AskClick = 1, kCursor_ItemHand = 5, kCursor_ScrollHover = 7,
                 kCursor_ScrollV = 9, kCursor_ItemGrab = 11, kCursor_ButtonPress = 12, kCursor_Restore = -1;
static void SetCursorState(int state) {
    void* inputSystem = *reinterpret_cast<void**>(0x00BEC33C);
    if (inputSystem) reinterpret_cast<void(__thiscall*)(void*, int)>(0x0059A6D9)(inputSystem, state);
}

// Plaintext itemID from a GW_ItemSlotBase* (TSecType<long> at +0xC).
static int DecodeItemID(void* pItem) {
    if (!pItem) return 0;
    int id = 0;
    __try { id = TSecType_long_GetData(reinterpret_cast<const char*>(pItem) + 0x0C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { id = 0; }
    return id;
}

// ---------------------------------------------------------------------------
// Native drag SOURCE — every address here is VERIFIED by disassembling the stock item
// inventory's own drag-start in CUIItem::OnMouseButton (@0x0081D7D6) in THIS client:
//   ZAlloc(0xBF0B00,0x28)@0x403065 ; CDraggableItem::Init@0x6FFDA3 ; vtable 0xAF34D8 ;
//   CWndMan(0xBEC20C)::BeginDragDrop@0x9E353D  (BeginDragDrop also sets cursor state 0xB = grab-hand).
// ---------------------------------------------------------------------------
static constexpr int kDragIconW = 44, kDragIconH = 44, kDragIconZ = 9999;
typedef void(__thiscall* t_BeginDragDrop)(void* wndMan, void* src, void* drag);
static auto BeginDragDrop = reinterpret_cast<t_BeginDragDrop>(0x009E353D);
// CWndMan::EndDragDrop(rx, ry, bDblClk) — the engine's "finish the active drag" entry. Retained
// for reference but NOT called: this client ends a drag on the NEXT click on its own (native
// "click to pick up, click to drop"), which is exactly the interaction we want.
typedef void(__thiscall* t_EndDragDrop)(void* wndMan, int rx, int ry, int bDblClk);
static auto EndDragDrop = reinterpret_cast<t_EndDragDrop>(0x009E37C2);
// CDraggableItem::Init(self, iconLayer) — stores the drag icon layer at self+0xc (and AddRefs it),
// zeroes the base, installs the base vtable. The caller then overwrites the vtable with 0xAF34D8.
typedef void*(__thiscall* t_IDraggable_Init)(void* self, void* icon);
static auto IDraggable_Init = reinterpret_cast<t_IDraggable_Init>(0x006FFDA3);
static constexpr uintptr_t kCDraggableItem_Vtable = 0x00AF34D8;
// The CDraggableItem MUST come from the GAME's ZAlloc so the engine drag manager recognizes it.
// A C++ reimplementation of the allocator produces an object the drag manager rejects
// (drag never starts -> no ghost icon). Call the real allocator: Alloc(this=instance, size).
typedef void*(__thiscall* t_ZAlloc_Alloc)(void* self, size_t size);
static auto ZAlloc_Alloc = reinterpret_cast<t_ZAlloc_Alloc>(0x00403065);
static constexpr uintptr_t kZAlloc_Instance = 0x00BF0B00;

// pOrigin: HUD origin borrowed from a REAL CWnd (the bag window's own layer). We do
// NOT call CWndMan::GetOrgWindowEx() here — it is dead in this client (ResetOrgWindow
// is never called) and THREW, which aborted this whole function to its catch and
// returned null, so BeginDragDrop never ran and no drag ever started. Borrowing the
// window's live origin (and letting BeginDragDrop overwrite it anyway) fixes that.
static IWzGr2DLayer* CreateDragIconLayer(int itemID, int hudX, int hudY, IUnknown* pOrigin) {
    if (!itemID) return nullptr;
    try {
        IWzGr2DPtr& gr = get_gr();
        if (!gr) return nullptr;
        CItemInfo* pItemInfo = CItemInfo::GetInstance();
        if (!pItemInfo) return nullptr;
        IWzCanvasPtr canvas;
        PcCreateObject<IWzCanvasPtr>(L"Canvas", canvas, nullptr);
        if (!canvas) return nullptr;
        canvas->Create(kDragIconW, kDragIconH, vtMissing, vtMissing);
        pItemInfo->DrawItemIconForSlot(canvas, itemID, 6, kDragIconH - 6, 0, 0, 0, 0, 0, 0);
        IWzGr2DLayerPtr layer = gr->CreateLayer(0, 0, kDragIconW, kDragIconH, kDragIconZ,
                                                static_cast<IUnknown*>(canvas), vtMissing);
        if (!layer) return nullptr;
        if (pOrigin) { try { layer->origin = pOrigin; } catch (...) {} }
        layer->width = kDragIconW; layer->height = kDragIconH; layer->color = 0xFFFFFFFF;
        layer->RelMove(hudX - kDragIconW / 2, hudY - kDragIconH / 2);
        layer->visible = 1;
        IWzGr2DLayer* raw = layer; raw->AddRef();
        return raw;
    } catch (...) { return nullptr; }
}

static IWzGr2DLayer* s_pDragIcon = nullptr;   // our ref to the drag icon layer (engine holds its own via draggable+0xc)
// True while a NATIVE engine item-drag started FROM the bag is in flight. Set in BeginItemDrag
// (the verified CUIItem drag sequence) and cleared on drop by the CDraggableItem::OnDropped hook
// / EndDragIcon. Suppresses grid scroll while an item is held on the cursor.
static bool s_bItemDragging = false;
static int  s_manualDragSlot = -1;   // bag slot being dragged (informational; also lives in draggable+0x1c)
static void EndDragIcon() {
    s_bItemDragging = false;
    s_manualDragSlot = -1;
    if (!s_pDragIcon) return;
    try { s_pDragIcon->visible = 0; } catch (...) {}
    s_pDragIcon->Release();
    s_pDragIcon = nullptr;
}

// True while the engine has ANY drag-drop in flight, no matter where it started —
// a bag item being dragged out, OR a player-inventory (or other window) item being
// dragged IN/over the bag. CWndMan::BeginDragDrop stores the drag source handler at
// CWndMan+0x90 and ClearDragContext zeroes it on drop, so a non-null value there is
// the global "a drag is happening" signal. (Grabbing our own scrollbar thumb does
// NOT call BeginDragDrop, so this stays false then — legitimate scrolling is fine.)
static bool IsEngineDragActive() {
    if (!CWndMan::IsInstantiated()) return false;
    CWndMan* wm = CWndMan::GetInstance();
    if (!wm) return false;
    void* src = nullptr;
    __try { src = *reinterpret_cast<void**>(reinterpret_cast<char*>(wm) + 0x90); }
    __except (EXCEPTION_EXECUTE_HANDLER) { src = nullptr; }
    return src != nullptr;
}
// The scrollbar (wheel, arrows, thumb grab) is locked while any item drag is in
// flight: our own bag drag-out (s_bItemDragging) or an engine drag in/over the
// window (IsEngineDragActive). Only deliberate scrolling — wheel or a thumb grab
// with no drag active — moves the grid.
static bool ScrollLocked() { return s_bItemDragging || IsEngineDragActive(); }

// ---------------------------------------------------------------------------
// GW_ItemSlotBase decode (for native tooltips).
// ---------------------------------------------------------------------------
struct GW_ItemSlotBase : public ZRefCounted {
    virtual ~GW_ItemSlotBase() = 0;
    virtual int IsProtectedItem() = 0;
    virtual int IsPreventSlipItem() = 0;
    virtual int IsSupportWarmItem() = 0;
    virtual int IsBindedItem() = 0;
    virtual int IsPossibleTradingItem() = 0;
    virtual int GetType() = 0;
};
struct ZRefOut { void* unused; void* item; };
static auto GW_ItemSlotBase_Decode =
    reinterpret_cast<int(__cdecl*)(void* /*ZRefOut*/, CInPacket*)>(0x004E33F9);

static GW_ItemSlotBase* RawDecodeItem(CInPacket* pkt) {
    ZRefOut out = {};
    __try { GW_ItemSlotBase_Decode(&out, pkt); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.item = nullptr; }
    return reinterpret_cast<GW_ItemSlotBase*>(out.item);
}
static void StoreDecoded(ZRef<GW_ItemSlotBase>& dst, GW_ItemSlotBase* p) {
    if (p) dst = ZRef<GW_ItemSlotBase>(p, false);
    else   dst = ZRef<GW_ItemSlotBase>();
}
static void DropRef(GW_ItemSlotBase* p) { if (p) { ZRef<GW_ItemSlotBase> t(p, false); } }

// ---------------------------------------------------------------------------
// Protocol (must match the server's RecvOpcode/SendOpcode.BAG_WINDOW).
// ---------------------------------------------------------------------------
static constexpr int kOpcode_Bag_Send = 0x3724;   // CP_BagWindow (client -> server) - BBrStory custom opcode
static constexpr int kOpcode_Bag_Recv = 0x3725;   // LP_BagWindow (server -> client) - BBrStory custom opcode
static constexpr int kReq_Open = 0, kReq_Withdraw = 1, kReq_Deposit = 2, kReq_Merge = 3, kReq_SetAuto = 4, kReq_Move = 5;
static constexpr int kResp_Snapshot = 1;
static constexpr int kKindOre = 0, kKindScroll = 1, kKindChair = 2, kKindCash = 3;
static constexpr int kKindCount = 4;

// GBK tab labels (client Dotum font expects ANSI/GBK on CN client).
static const char* BagTabLabel(int kind) {
    switch (kind) {
    case kKindScroll: return "\xBE\xED\xD6\xE1\xB0\xFC"; // 卷轴包
    case kKindChair:  return "\xD2\xCE\xD7\xD3\xB0\xFC"; // 椅子包
    case kKindCash:   return "\xD7\xF8\xC6\xEF\xB0\xFC"; // 坐骑包
    default:          return "\xBF\xF3\xCA\xAF\xB0\xFC"; // 矿石包
    }
}

static const char* BagSearchHint(int kind) {
    switch (kind) {
    case kKindScroll: return "\xCB\xD1\xCB\xF7\xBE\xED\xD6\xE1\x2E\x2E\x2E"; // 搜索卷轴...
    case kKindChair:  return "\xCB\xD1\xCB\xF7\xD2\xCE\xD7\xD3\x2E\x2E\x2E"; // 搜索椅子...
    case kKindCash:   return "\xCB\xD1\xCB\xF7\xD7\xF8\xC6\xEF\x2E\x2E\x2E"; // 搜索坐骑...
    default:          return "\xCB\xD1\xCB\xF7\xBF\xF3\xCA\xAF\x2E\x2E\x2E"; // 搜索矿石...
    }
}

// ---------------------------------------------------------------------------
// Geometry — matches the storage-bag art (UI/UIWindow.img/Bag/backgrnd, 207x248):
// a shared background with a 5x5 grid. The four bag kinds are selected by clickable
// tab labels (ORE/SCROLL/CHAIR/CASH) in the gray strip under the title, vanilla-style.
// ---------------------------------------------------------------------------
static constexpr int kInvCols     = 5;
static constexpr int kInvVisRows  = 5;
static constexpr int kInvVisCount = kInvCols * kInvVisRows;   // 25
static constexpr int kInvCap      = 200;                      // bag slot capacity (matches server)
static constexpr int kInvTotRows  = kInvCap / kInvCols;       // 40

static constexpr int kWndW = 207;
static constexpr int kWndH = 248;
static constexpr int kTitleH = 20;            // title bar (drag region); tabs sit below it at y~22..40

// Grid (cell wells measured off the art: col0 x=8, 36px pitch; row0 y=50, 34px pitch; cell ~31px).
static constexpr int kCell     = 31;
static constexpr int kColW     = 36;
static constexpr int kRowH     = 34;
static constexpr int kGridLeft = 8;
static constexpr int kGridTop  = 50;
static constexpr int kIconDX   = 0;
static constexpr int kIconBY   = kCell - 1;

// Scrollbar — vanilla blue Basic.img/VScr4, overlaid in the right margin (col5 ends at x=183).
static constexpr int kScrollW   = 15;
static constexpr int kScrollX   = 186;
static constexpr int kSbArrowH  = 13;
static constexpr int kScrollTop = kGridTop;                                  // 50
static constexpr int kScrollBot = kGridTop + (kInvVisRows - 1) * kRowH + kCell; // 217
static constexpr int kThumbMinH = 20;

// Search field — baked recessed white box near the bottom; we draw text/caret/hint/count on top.
static constexpr int kSearchBoxL   = 8;
static constexpr int kSearchBoxR   = 199;
static constexpr int kSearchBoxTop = 222;
static constexpr int kSearchBoxBot = 242;
static constexpr int kSearchTextX  = 6;
static constexpr int kSearchTextY  = 227;
static constexpr int kSearchMax    = 32;

// Title-bar buttons overlaid on the RIGHT (the art bakes no button wells): sort/merge + close.
static constexpr int kBtMergeX  = 173, kBtMergeY  = 5, kBtMergeW  = 12, kBtMergeH  = 12;
static constexpr int kBtCloseX  = 187, kBtCloseY  = 5, kBtCloseW  = 12, kBtCloseH  = 12;
// Auto-collect toggle button (copied from DamageRank/BtAuto, 21x12): sits left of the merge button.
static constexpr int kBtAutoX   = 150, kBtAutoY   = 5, kBtAutoW   = 21, kBtAutoH   = 12;   // X nudged +1px right

// Tab bar — four clickable tab labels in the gray strip below the title. Each tab's hit-rect is
// one quarter of the label row; the label art (17/33/25/23 px wide) is centered within its slot.
static constexpr int kTabTop = 22, kTabBot = 40;
static constexpr int kTabHitLeft[kKindCount]  = {   8,  56, 104, 152 };  // per-tab slot left
static constexpr int kTabHitRight[kKindCount] = {  56, 104, 152, 200 };  // per-tab slot right
static constexpr int kTabLabelX[kKindCount]   = {  13,  62, 109, 156 };  // per-tab label blit X (0=ORE 1=SCROLL 2=CHAIR 3=CASH) — TUNABLE individually
// Vanilla-style tab pill (shared Basic.img/Tab2 9-slice) drawn behind each label: pink when active,
// grey otherwise — matching the item inventory tabs. Bottom meets the red separator line.
static constexpr int kPillTop = 21;    // pill top y
static constexpr int kPillH   = 19;    // Basic.img/Tab2 fill height
static constexpr int kPillPad = 2;     // inset of the pill within its tab slot
static constexpr int kTabLabelY = kPillTop + (kPillH - 5) / 2;   // 28: center the 5px label on the pill

// Decoded bag contents, shared cache (the window is a thin view over the active one).
struct BagStore {
    ZRef<GW_ItemSlotBase> obj[kInvCap];
    int  id[kInvCap];
    int  qty[kInvCap];     // stack count per slot (from the snapshot's trailing block; 0 if unknown)
    bool isNew[kInvCap];   // "just arrived via auto-collect" -> pulsing glow until the player hovers/grabs it
    int  count;
    bool ready;
};
static BagStore g_bag[kKindCount];   // indexed by kind (ore/scroll/chair/cash)

// True from the moment a transfer request (withdraw/deposit/move) is sent until the next snapshot
// lands. A transfer can shift slots, so the slot indices the client learned from the last snapshot
// may be stale while a transfer is in flight; we freeze grid interaction until the reply so a second
// rapid gesture can't act on a stale index. Also used to distinguish a user-action reply from an
// unsolicited auto-collect push (the latter drives the new-item glow). Cleared in HandleBagSnapshot.
static bool s_bAwaitingSnapshot = false;

// Remembered window placement, so reopening lands where it was last closed and on
// the bag last viewed.
static bool s_bSavedPos = false;
static int  s_savedX = 0, s_savedY = 0;
static int  s_savedKind = kKindOre;

// Per-kind Auto-collect state, mirrored from each snapshot's trailing byte; drives the Auto button art.
static bool s_bagAuto[kKindCount] = { false, false, false, false };

// Tick of the item inventory's last Draw (set by the CUIItem::Draw hook), kept for the button rect.
static DWORD s_invBagLastDrawTick = 0;

// SEH-isolated read of the item inventory singleton (0x00BED654) + its render layer (CWnd::m_pLayer
// @ +0x18). Returns: 0 = couldn't read (uncertain), 1 = singleton is NULL (inventory destroyed =
// closed), 2 = got the layer into *outLayer (inventory exists; check its visible flag).
static int SehReadInv(void** outLayer) {
    int r = 0; *outLayer = nullptr;
    __try {
        void* inv = *reinterpret_cast<void**>(0x00BED654);
        if (!inv) { r = 1; }
        else { *outLayer = *reinterpret_cast<void**>(reinterpret_cast<char*>(inv) + 0x18); r = 2; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { r = 0; }
    return r;
}
// Is the item inventory currently shown? Closing it (its X button, the inventory hotkey, or ESC)
// DESTROYS the CUIItem window -> the singleton goes NULL; a still-open-but-hidden inventory instead
// clears its layer's `visible` flag. Any genuinely uncertain read defaults to "shown" so the bag is
// never closed by mistake.
static bool IsInventoryShown() {
    void* layer = nullptr;
    int r = SehReadInv(&layer);
    if (r == 0) return true;    // couldn't read -> keep the bag
    if (r == 1) return false;   // inventory object gone (closed) -> close the bag with it
    if (!layer) return true;    // exists but no layer yet (transient) -> keep
    int vis = 1;
    try { vis = reinterpret_cast<IWzGr2DLayer*>(layer)->visible; } catch (...) { vis = 1; }
    return vis != 0;
}

static void FreeBag(int kind) {
    if (kind < 0 || kind >= kKindCount) return;
    BagStore& s = g_bag[kind];
    for (int i = 0; i < kInvCap; ++i) { s.obj[i] = ZRef<GW_ItemSlotBase>(); s.id[i] = 0; s.qty[i] = 0; s.isNew[i] = false; }
    s.count = 0;
    s.ready = false;
}

// --- send ---
static constexpr uintptr_t kAddr_ClientSocket_Instance   = 0x00BE7914;
static constexpr uintptr_t kAddr_ClientSocket_SendPacket = 0x0049637B;
static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(kAddr_ClientSocket_SendPacket);
static void SendBagPacket(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(kAddr_ClientSocket_Instance);
    if (sock) ClientSocket_SendPacket(sock, o);
}
static void SendBagReq_Open(int kind) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_Open); o.Encode1((unsigned char)kind);
    SendBagPacket(o);
}
// Withdraw bag slot srcSlot -> player inventory. targetInvSlot = the inventory slot the item was
// dropped on (1-based), or -1 for "first free". The server honors an exact empty slot, else first-free.
static void SendBagReq_Withdraw(int kind, int srcSlot, int targetInvSlot) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_Withdraw); o.Encode1((unsigned char)kind);
    o.Encode2((unsigned short)srcSlot);
    o.Encode2((unsigned short)targetInvSlot);   // -1 -> 0xFFFF -> server reads short -1
    SendBagPacket(o);
    s_bAwaitingSnapshot = true;   // slot indices go stale until the reply snapshot
}
// Deposit player-inventory (invType,invPos) -> bag. targetBagSlot = the bag slot dropped on, or -1.
static void SendBagReq_Deposit(int kind, int invType, int invPos, int targetBagSlot) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_Deposit); o.Encode1((unsigned char)kind);
    o.Encode2((unsigned short)invType); o.Encode2((unsigned short)invPos);
    o.Encode2((unsigned short)targetBagSlot);
    SendBagPacket(o);
    s_bAwaitingSnapshot = true;   // slot indices go stale until the reply snapshot
}
// Rearrange within the bag: move the item at bag slot srcSlot onto bag slot dstSlot.
static void SendBagReq_Move(int kind, int srcSlot, int dstSlot) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_Move); o.Encode1((unsigned char)kind);
    o.Encode2((unsigned short)srcSlot); o.Encode2((unsigned short)dstSlot);
    SendBagPacket(o);
    s_bAwaitingSnapshot = true;   // slot indices go stale until the reply snapshot
}
// Ask the server to merge identical stacks + compact the active bag; replies snapshot.
static void SendBagReq_Merge(int kind) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_Merge); o.Encode1((unsigned char)kind);
    SendBagPacket(o);
    s_bAwaitingSnapshot = true;   // slot indices go stale until the reply snapshot
}
// Ask the server to toggle auto-collect for a bag kind (the Auto button). The reply snapshot's
// trailing byte echoes the new state, so the button redraws lit/greyed to match.
static void SendBagReq_SetAuto(int kind, bool on) {
    COutPacket o(kOpcode_Bag_Send);
    o.Encode1((unsigned char)kReq_SetAuto); o.Encode1((unsigned char)kind);
    o.Encode1((unsigned char)(on ? 1 : 0));
    SendBagPacket(o);
}

// ---------------------------------------------------------------------------
// CUIBagWindow — single window showing one bag (Ore or Scroll), flipped by the
// title-bar SWITCH button.
// ---------------------------------------------------------------------------
class CUIBagWindow : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIBagWindow* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{ nullptr };

    int  m_activeKind;            // currently shown bag kind: 0=ore 1=scroll 2=chair 3=cash
    int  m_nInvScroll;
    int  m_screenX, m_screenY;

    RECT m_rcClose;
    RECT m_rcTab[kKindCount];    // clickable tab labels (select the bag kind)
    RECT m_rcMerge;              // sort/merge button (consolidate stacks)
    int  m_nCloseHover, m_nClosePressed;
    int  m_nMergeHover, m_nMergePressed;
    RECT m_rcAuto;               // Auto-collect toggle button (reflects/toggles the active tab's kind)
    int  m_nAutoHover, m_nAutoPressed;
    int  m_bDragging, m_nDragAnchorX, m_nDragAnchorY;
    int  m_bScrollDrag, m_nScrollGrabDY;
    int  m_armDragType, m_armDragIdx, m_armDragItem, m_armDownX, m_armDownY;
    int   m_nLastClickKey; DWORD m_nLastClickTick;

    alignas(8) unsigned char m_ttBuf[0x600];
    bool m_bTtInit; int m_nTtKey;
    // When true, bag auto-closes if the item inventory is closed (inventory BAG button path).
    // Sidebar / standalone open sets this false so the bag can stay up without inventory.
    bool m_bCloseWithInventory;
    int m_cursorState;    // cursor currently set for the bag window: 0 arrow / 12 finger (button press) / 5 hand (grid cells)
    IWzFontPtr m_pFont;          // basic font (light) — stack-count numerals on dark badges
    IWzFontPtr m_pFontDk;        // Dotum 11 dark — search text / bag label on the light art
    IWzFontPtr m_pFontTab;       // Dotum 11 white — GBK category tab labels

    // --- art / chrome bundled under UI/UIWindow.img/Bag/* (+ Basic.img/VScr4) ---
    IWzCanvasPtr m_pBg;                        // shared background art (STORAGE BAG, 5x5 grid)
    IWzCanvasPtr m_pTabOn[kKindCount];         // tab label glyph (white ORE/SCROLL/CHAIR/CASH), per kind
    IWzCanvasPtr m_pPillL[2], m_pPillF[2], m_pPillR[2];  // vanilla Basic.img/Tab2 9-slice: [0]=grey(unsel) [1]=pink(sel)
    IWzCanvasPtr m_pBtClose[2];               // close button: normal, mouseOver
    IWzCanvasPtr m_pBtSort[3];                // sort/merge button: vanilla red BtSort (normal, pressed, mouseOver)
    IWzCanvasPtr m_pBtAuto[4];                // Auto toggle button: normal, pressed, mouseOver, disabled (off/grey)
    IWzCanvasPtr m_pSbPrev[1], m_pSbNext[1];  // scrollbar up/down arrows
    IWzCanvasPtr m_pSbBase, m_pSbThumb[1];    // scrollbar track tile + thumb
    IWzCanvasPtr m_pDigit[10];                // vanilla ItemNo numerals 0..9 (white, black-outlined) for stack counts
    IWzCanvasPtr m_pNewGlow[4];               // vanilla "new item" glow animation (UI/UIWindow.img/Item/New/inventory/0..3, 36x36)
    int          m_digitW[10];                // per-digit advance widths

    // --- search bar (live name filter) ---
    RECT m_rcSearch;
    bool m_searchActive;            // true while the box is focused (keys captured)
    char m_search[kSearchMax + 1];  // typed text (lowercased; c-cedilla + accented vowels as CP1252 bytes)
    int  m_searchLen;
    char m_pendingAccent;           // buffered dead accent (acute/grave/tilde/circumflex/diaeresis) or 0
    int  m_display[kInvCap];        // when filtering: compacted matching slot indices
    int  m_displayCount;

    CUIBagWindow(int initialKind, int nLeft, int nTop);
    virtual ~CUIBagWindow() override { if (ms_pInstance == this) ms_pInstance = nullptr; }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int  OnMouseMove(int rx, int ry) override;
    virtual int  OnMouseWheel(int rx, int ry, int nWheel) override;
    virtual void OnMouseEnter(int bEnter) override;
    virtual void OnDestroy() override;
    virtual void Update() override {
        // Inventory-linked open: close together with the item inventory.
        // Sidebar / standalone open: keep the bag until the user closes it.
        if (m_bCloseWithInventory && !IsInventoryShown()) { Destroy(); return; }
        InvalidateRect(nullptr);
    }
    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }
    virtual int OnSetFocus(int /*bFocus*/) override { return 0; }   // movement-pause fix
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        const bool isKeyUp = (lParam & 0x80000000) != 0;
        if (!isKeyUp && wParam == VK_ESCAPE) {
            if (m_searchActive) {
                if (m_searchLen) SearchClear();
                SearchSetActive(false);
                return;
            }
            Destroy();
            return;
        }
        void* ctx = GetWvsContext();
        if (ctx) reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                     kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
    }

    BagStore& bag() const { return g_bag[m_activeKind]; }

    // Load a UI.wz canvas by path (links auto-resolved; null-safe).
    static IWzCanvasPtr LoadSprite(const wchar_t* p) {
        IWzCanvasPtr c;
        try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p))); } catch (...) {}
        return c;
    }
    // Opaque blit (background — must cover the world behind it).
    static void BlitAt(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    // Alpha-preserving blit (chrome layered over the opaque background).
    static void BlitA(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    // Draw a vanilla 9-slice tab pill (Basic.img/Tab2) at window-local x, total width W: left cap +
    // horizontally-stretched fill + right cap. sel picks the pink (selected) vs grey (unselected) set.
    void DrawTabPill(IWzCanvasPtr dst, int x, int W, bool sel) {
        IWzCanvasPtr L = m_pPillL[sel ? 1 : 0];
        IWzCanvasPtr F = m_pPillF[sel ? 1 : 0];
        IWzCanvasPtr R = m_pPillR[sel ? 1 : 0];
        int lw = 4, rw = 4;
        try { if (L) lw = (int)L->width; } catch (...) {}
        try { if (R) rw = (int)R->width; } catch (...) {}
        int fillW = W - lw - rw; if (fillW < 1) fillW = 1;
        BlitA(dst, L, x, kPillTop);                         // left cap
        if (F && dst)                                        // fill: stretch the 1px column across the middle
            try { dst->CopyEx(x + lw, kPillTop, F, CANVAS_ALPHATYPE::CA_OVERWRITE, fillW, kPillH, 0, 0, 0, 0); } catch (...) {}
        BlitA(dst, R, x + W - rw, kPillTop);                // right cap
    }
    void LoadSprites() {
        m_pBg          = LoadSprite(L"UI/UIWindow.img/Bag/backgrnd");
        m_pTabOn[0]    = LoadSprite(L"UI/UIWindow.img/Bag/tabOre");
        m_pTabOn[1]    = LoadSprite(L"UI/UIWindow.img/Bag/tabScroll");
        m_pTabOn[2]    = LoadSprite(L"UI/UIWindow.img/Bag/tabChair");
        m_pTabOn[3]    = LoadSprite(L"UI/UIWindow.img/Bag/tabCash");    // reuse the vanilla CASH tab art (WZ) — user re-letters it to "Montaria" in the WZ later
        // Vanilla pink/grey tab pill (shared Basic.img/Tab2 9-slice) — the item-inventory tab look.
        m_pPillL[0]    = LoadSprite(L"UI/Basic.img/Tab2/left0");
        m_pPillL[1]    = LoadSprite(L"UI/Basic.img/Tab2/left1");
        m_pPillF[0]    = LoadSprite(L"UI/Basic.img/Tab2/fill0");
        m_pPillF[1]    = LoadSprite(L"UI/Basic.img/Tab2/fill1");
        m_pPillR[0]    = LoadSprite(L"UI/Basic.img/Tab2/right0");
        m_pPillR[1]    = LoadSprite(L"UI/Basic.img/Tab2/right1");
        m_pBtClose[0]  = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/normal/0");
        m_pBtClose[1]  = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/mouseOver/0");
        // vanilla red sort/merge icon — the item inventory's own BtSort (native v83, 12x12)
        m_pBtSort[0]   = LoadSprite(L"UI/UIWindow.img/Item/BtSort/normal/0");
        m_pBtSort[1]   = LoadSprite(L"UI/UIWindow.img/Item/BtSort/pressed/0");
        m_pBtSort[2]   = LoadSprite(L"UI/UIWindow.img/Item/BtSort/mouseOver/0");
        // Auto toggle button (copied from DamageRank/BtAuto into the bag's own Bag/BtAuto node).
        m_pBtAuto[0]   = LoadSprite(L"UI/UIWindow.img/Bag/BtAuto/normal/0");
        m_pBtAuto[1]   = LoadSprite(L"UI/UIWindow.img/Bag/BtAuto/pressed/0");
        m_pBtAuto[2]   = LoadSprite(L"UI/UIWindow.img/Bag/BtAuto/mouseOver/0");
        m_pBtAuto[3]   = LoadSprite(L"UI/UIWindow.img/Bag/BtAuto/disabled/0");
        // vanilla blue scrollbar (always present in Basic.img)
        m_pSbPrev[0]  = LoadSprite(L"UI/Basic.img/VScr4/enabled/prev0");
        m_pSbNext[0]  = LoadSprite(L"UI/Basic.img/VScr4/enabled/next0");
        m_pSbBase     = LoadSprite(L"UI/Basic.img/VScr4/enabled/base");
        m_pSbThumb[0] = LoadSprite(L"UI/Basic.img/VScr4/enabled/thumb0");
        // vanilla stack-count digits (white glyph + black outline -> readable on any cell)
        for (int i = 0; i < 10; ++i) {
            wchar_t dp[48]; swprintf(dp, 48, L"UI/Basic.img/ItemNo/%d", i);
            m_pDigit[i] = LoadSprite(dp);
            unsigned int w = 0;
            if (m_pDigit[i]) { try { w = m_pDigit[i]->width; } catch (...) { w = 0; } }
            m_digitW[i] = (w > 0 && w < 32) ? (int)w : ((i == 1) ? 5 : 8);
        }
        // vanilla "new item" glow frames (the same effect the item inventory uses on freshly-received items)
        for (int i = 0; i < 4; ++i) {
            wchar_t np[64]; swprintf(np, 64, L"UI/UIWindow.img/Item/New/inventory/%d", i);
            m_pNewGlow[i] = LoadSprite(np);
        }
    }

    // Jump to a specific bag kind (a tab click). Requests that bag's snapshot.
    void SwitchTab(int kind) {
        if (kind < 0 || kind >= kKindCount || kind == m_activeKind) return;
        m_activeKind = kind;
        m_nInvScroll = 0;
        m_nLastClickKey = 0;
        SearchClear();                 // each bag filters independently
        m_nTtKey = 0; HideTip();
        play_ui_sound(L"BtMouseClick");
        SendBagReq_Open(kind);
        InvalidateRect(nullptr);
    }

    // --- search / filtering ---------------------------------------------------
    bool Filtering() const { return m_searchLen > 0; }
    bool MatchItem(int id) const {
        if (!id) return false;
        if (m_searchLen == 0) return true;
        const std::string& n = GetItemNameLower(id);   // normalized: lowercase + accent-stripped
        // Normalize the typed query the SAME way, so matching is accent-insensitive whether the user typed
        // plain ("minerio") or accented ("minério" / with ç) — both reduce to the same ASCII before compare.
        char q[kSearchMax + 1]; int qn = 0;
        for (int i = 0; i < m_searchLen && qn < kSearchMax; ++i) q[qn++] = NormalizeChar((unsigned char)m_search[i]);
        q[qn] = 0;
        if (!n.empty() && strstr(n.c_str(), q)) return true;
        // fall back to the item id so unnamed/unknown items stay searchable
        char ids[16]; _snprintf(ids, sizeof(ids), "%d", id); ids[15] = 0;
        return strstr(ids, q) != nullptr;
    }
    // Rebuild the compacted list of matching slots (only used while filtering).
    void RebuildFilter() {
        m_displayCount = 0;
        if (!Filtering()) return;
        BagStore& s = bag();
        for (int i = 0; i < s.count && i < kInvCap; ++i)
            if (s.id[i] && MatchItem(s.id[i])) m_display[m_displayCount++] = i;
        ClampScroll();
    }
    // Narrow the already-matching set in place after a character is appended: a longer
    // search term can only shrink the result set, so re-test just the current matches
    // (the prefix already matched the shorter term) instead of rescanning the whole bag.
    void NarrowFilter() {
        if (!Filtering()) { m_displayCount = 0; return; }
        BagStore& s = bag();
        int n = 0;
        for (int g = 0; g < m_displayCount; ++g) {
            int i = m_display[g];
            if (i >= 0 && i < kInvCap && s.id[i] && MatchItem(s.id[i])) m_display[n++] = i;
        }
        m_displayCount = n;
        ClampScroll();
    }
    // Actual bag slot shown at grid position `g` (0-based, row-major), or -1.
    int DisplaySlot(int g) const {
        if (Filtering()) return (g >= 0 && g < m_displayCount) ? m_display[g] : -1;
        return (g >= 0 && g < kInvCap) ? g : -1;
    }
    int TotalRows() const {
        int items = Filtering() ? m_displayCount : kInvCap;
        int rows = (items + kInvCols - 1) / kInvCols;
        return rows < kInvVisRows ? kInvVisRows : rows;
    }
    void SearchClear() { m_searchLen = 0; m_search[0] = 0; m_pendingAccent = 0; m_nInvScroll = 0; RebuildFilter(); }
    void SearchSetActive(bool on) {
        if (on == m_searchActive) return;
        m_searchActive = on;
        m_pendingAccent = 0;   // drop any half-typed dead accent when focus changes
        if (on) play_ui_sound(L"BtMouseClick");
        InvalidateRect(nullptr);
    }
    // Handle a key while the box is focused. Returns true if consumed.
    bool HandleSearchKey(unsigned int vk) {
        if (vk == VK_ESCAPE) { if (m_searchLen) SearchClear(); SearchSetActive(false); return true; }
        if (vk == VK_RETURN) { SearchSetActive(false); return true; }
        if (vk == VK_BACK) {
            if (m_searchLen > 0) { m_search[--m_searchLen] = 0; m_nInvScroll = 0; RebuildFilter(); InvalidateRect(nullptr); }
            return true;
        }
        // Translate the key through the ACTIVE keyboard layout (ABNT2 etc.) to its real character, so the
        // box accepts letters, digits, space, cedilla (c-cedilla) AND accented vowels. ToAscii does the
        // dead-key composition (acute/grave/tilde/circumflex + vowel) and gives the pt-BR CP1252 byte.
        //   r == -1 : a dead accent is pending -> swallow this key, compose it on the NEXT key.
        //   r >=  1 : produced a char (letters/digits/space/accents) -> put it in the box.
        //   r ==  0 : nav/control key (arrows, F-keys) -> let it fall through so field movement still works.
        char ch = 0;
        BYTE ks[256];
        if (GetKeyboardState(ks)) {
            WORD out = 0;
            UINT scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
            int r = ToAscii(vk, scan, ks, &out, 0);
            if (r == -1) return true;                                  // dead accent buffered (swallow the key)
            if (r >= 1) {
                ch = (char)(out & 0xFF);
                if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');   // case-insensitive search
            }
        }
        if (!ch) return false;   // nav/control key (arrows, F-keys) -> reaches the game (movement etc.)

        // Manual dead-accent compose: the acute/grave/tilde/circumflex/diaeresis keys arrive as SPACING
        // accents (the layout's own dead-key state was already spent by the game's message pump), so buffer
        // the accent and combine it with the NEXT vowel. A dead accent pressed while one is ALREADY pending
        // doesn't combine -> emit BOTH literally, so pressing the accent twice types "´´". (c-cedilla comes
        // through as a normal char 0xE7 and just types.)
        {
            unsigned char uc = (unsigned char)ch;
            if (uc == 0xB4 || uc == 0x60 || uc == 0x7E || uc == 0x5E || uc == 0xA8) {
                if (m_pendingAccent) { AppendSearchChar(m_pendingAccent); AppendSearchChar(ch); m_pendingAccent = 0; }
                else m_pendingAccent = ch;
                return true;
            }
        }
        if (m_pendingAccent) {
            char acc = m_pendingAccent; m_pendingAccent = 0;
            char comp = ComposeAccent(acc, ch);
            if (comp) ch = comp;
            else AppendSearchChar(acc);   // accent didn't combine (e.g. acute + consonant) -> keep it literally
        }
        AppendSearchChar(ch);
        return true;
    }
    // Append one already-composed character to the box + refresh the filter (narrow in place while a term is
    // active; the first char needs a full rescan to seed the match list).
    void AppendSearchChar(char c) {
        if ((unsigned char)c < 0x20 || m_searchLen >= kSearchMax) return;
        bool wasFiltering = (m_searchLen > 0);
        m_search[m_searchLen++] = c; m_search[m_searchLen] = 0; m_nInvScroll = 0;
        if (wasFiltering) NarrowFilter(); else RebuildFilter();
        InvalidateRect(nullptr);
    }
    // Estimated pixel width of the typed text in the UI font (~Dotum 11) — for the caret. DrawTextA returns
    // the text HEIGHT (out-param puHeight), not width, and there's no extent API, so estimate per glyph.
    static int SearchTextWidth(const char* s, int len) {
        int w = 0;
        for (int i = 0; i < len; ++i) {
            switch ((unsigned char)s[i]) {
                case ' ':                                                       w += 3; break;
                case 'i': case 'j': case 'l': case '.': case ',': case '\'':
                case '!': case '|': case ':': case ';':                         w += 2; break;
                case 'f': case 'r': case 't': case 'I': case '(': case ')':     w += 4; break;
                case 'm': case 'w': case 'M': case 'W':                         w += 8; break;
                default:                                                        w += 6; break;
            }
        }
        return w;
    }

    int  MaxScroll() const { int m = TotalRows() - kInvVisRows; return m > 0 ? m : 0; }
    void ClampScroll() { if (m_nInvScroll < 0) m_nInvScroll = 0; if (m_nInvScroll > MaxScroll()) m_nInvScroll = MaxScroll(); }
    // Track runs between the inset up/down arrows, NOT the raw kScrollTop..kScrollBot.
    void ThumbGeom(int& origin, int& thumbH, int& travel) const {
        int top0 = kScrollTop + kSbArrowH, bot0 = kScrollBot - kSbArrowH;
        int span = bot0 - top0;
        thumbH = span * kInvVisRows / TotalRows();
        if (thumbH < kThumbMinH) thumbH = kThumbMinH;
        if (thumbH > span)       thumbH = span;
        origin = top0;
        travel = span - thumbH;
    }
    void ThumbRect(RECT& rc) const {
        int origin, thumbH, travel;
        ThumbGeom(origin, thumbH, travel);
        int top = origin + (MaxScroll() > 0 ? travel * m_nInvScroll / MaxScroll() : 0);
        rc = { kScrollX, top, kScrollX + kScrollW, top + thumbH };
    }
    // Cursor is over the vertical scrollbar strip (arrows + track).
    bool OverScrollbar(int rx, int ry) const {
        return rx >= kScrollX && rx < kScrollX + kScrollW && ry >= kScrollTop && ry < kScrollBot;
    }
    static void InvRect(int v, RECT& rc) {
        int col = v % kInvCols, row = v / kInvCols;
        rc.left = kGridLeft + col * kColW;
        rc.top  = kGridTop  + row * kRowH;
        rc.right = rc.left + kCell; rc.bottom = rc.top + kCell;
    }
    // Hit an item cell -> actual bag slot index (or -1). Honors the active filter.
    // Returns -1 while a transfer is in flight: the dense slot indices are stale
    // until the server's reply snapshot lands, so no click/drag/withdraw may act.
    int HitItemSlot(int rx, int ry) const {
        if (s_bAwaitingSnapshot) return -1;
        POINT pt{ rx, ry };
        for (int v = 0; v < kInvVisCount; ++v) {
            RECT rc; InvRect(v, rc);
            if (PtInRect(&rc, pt)) return DisplaySlot(m_nInvScroll * kInvCols + v);
        }
        return -1;
    }

    void EnsureTip() { if (!m_bTtInit) { try { TT_Ctor(m_ttBuf); m_bTtInit = true; } catch (...) { m_bTtInit = false; } } }
    void ShowTip(int x, int y, void* pItem) {
        EnsureTip();
        if (!m_bTtInit || !pItem) return;
        __try { ShowItemToolTip(m_ttBuf, x, y, pItem, nullptr, nullptr, 0, 0, 0); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    void HideTip() { if (m_bTtInit) { __try { TT_Clear(m_ttBuf); } __except (EXCEPTION_EXECUTE_HANDLER) {} m_nTtKey = 0; } }

    // Borrow this window's live HUD origin (COM get_origin can throw — kept in its own
    // C++ try/catch, OUT of BeginItemDrag's __try, which may not mix the two EH models).
    IUnknown* GetOwnOrigin() {
        IUnknown* org = nullptr;
        try { if (m_pLayer) org = static_cast<IUnknown*>(m_pLayer->origin); } catch (...) { org = nullptr; }
        return org;
    }

    // Start a NATIVE engine item-drag from bag slot `slot` — the exact sequence the stock
    // item inventory runs in CUIItem::OnMouseButton (verified by disassembly @0x0081D7D6):
    //   d = ZAlloc(0xBF0B00, 0x28)                          [0x00403065]
    //   CDraggableItem::Init(d, iconLayer)                  [0x006FFDA3]  -> stores icon at d+0xc (+AddRef)
    //   d+0x18 = type ; d+0x1c = slot ; d+0x20 = 0 ; d+0x24 = source handler ; *d = vtable 0xAF34D8
    //   CWndMan::BeginDragDrop(CWndMan, source, d)          [0x009E353D]
    // BeginDragDrop sets the engine cursor to state 0xB (the closed "grabbing" hand), stores the
    // source at CWndMan+0x90, and — on this client — ENDS the drag on the NEXT click (never on
    // button-up), giving the native "click to pick up, click to drop" feel. The drop fires
    // CDraggableItem::OnDropped (our hook) -> Withdraw when dropped outside the bag.
    // NB: BeginDragDrop THROWS if the icon (d+0xc) is null, so the icon layer must be valid.
    void BeginItemDrag(int slot, int itemID, int rx, int ry) {
        if (s_bAwaitingSnapshot) return;   // slot is stale until the reply snapshot
        if (!itemID || !CWndMan::IsInstantiated()) return;
        if (slot >= 0 && slot < kInvCap) bag().isNew[slot] = false;   // grabbing an item clears its "new" glow
        m_cursorState = -1;      // the drag takes over the cursor (grab hand); re-evaluate hover after it ends
        CWndMan* wm = CWndMan::GetInstance();
        if (!wm) return;
        EndDragIcon();
        // Borrow THIS window's live HUD origin for the drag icon (the window renders fine, so
        // m_pLayer->origin is a valid screen-space origin) — see CreateDragIconLayer.
        IUnknown* pOrigin = GetOwnOrigin();
        IWzGr2DLayer* pIcon = CreateDragIconLayer(itemID, m_screenX + rx, m_screenY + ry, pOrigin);
        if (!pIcon) return;                // no icon -> BeginDragDrop would throw
        void* d = ZAlloc_Alloc(reinterpret_cast<void*>(kZAlloc_Instance), 0x28);   // game ZAlloc (drag mgr requires it)
        if (!d) { pIcon->Release(); return; }
        __try {
            IDraggable_Init(d, pIcon);                                 // d+0xc = icon (AddRef'd) + base vtable
            *reinterpret_cast<int*>((char*)d + 0x18)  = 0;             // source "type" tag (unused by our withdraw route)
            *reinterpret_cast<int*>((char*)d + 0x1C)  = slot;          // source bag slot index
            *reinterpret_cast<int*>((char*)d + 0x20)  = 0;
            *reinterpret_cast<void**>((char*)d + 0x24) = (char*)this + 4;   // source handler (OnDropped identity)
            *reinterpret_cast<void**>(d) = reinterpret_cast<void*>(kCDraggableItem_Vtable);   // 0xAF34D8
            s_pDragIcon = pIcon;                                       // keep our ref; released in EndDragIcon on drop
            s_manualDragSlot = slot;
            s_bItemDragging = true;                                    // suppress scroll until the drop fires
            BeginDragDrop(wm, (char*)this + 4, d);                     // hand cursor + CWndMan+0x90; drops on NEXT click
            SetCursorState(kCursor_ItemGrab);                          // force the closed grab hand (11) now; re-asserted each
            HideTip();                                                 //   frame in the Draw hook so it can't fall back to a finger
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            EndDragIcon();
        }
    }

    void Withdraw(int slot, int targetInvSlot = -1) {
        if (s_bAwaitingSnapshot) return;   // a transfer is already in flight; index is stale
        if (slot >= 0 && slot < kInvCap && bag().id[slot]) {
            SendBagReq_Withdraw(m_activeKind, slot, targetInvSlot);
            play_ui_sound(L"DragEnd"); HideTip();
        }
    }

    // Raw bag grid slot (0-based, honoring scroll) under the CURRENT cursor, or -1. Used as a DROP
    // TARGET (deposit / rearrange), so it returns the visual cell even when empty. Disabled while the
    // search filter is active (filtered cells don't map 1:1 to slots) -> caller falls back to first-free.
    int SlotAtDropPoint() const {
        if (Filtering()) return -1;
        POINT sp; if (!GetAbsCursor(sp)) return -1;
        POINT pt{ sp.x - m_screenX, sp.y - m_screenY };
        for (int v = 0; v < kInvVisCount; ++v) {
            RECT rc; InvRect(v, rc);
            if (PtInRect(&rc, pt)) return m_nInvScroll * kInvCols + v;
        }
        return -1;
    }

};

// Keep a window fully on-screen when restoring a remembered position (a
// resolution change since last close could otherwise leave it off-screen).
static void ClampBagToScreen(int& x, int& y) {
    int sw = get_screen_width(), sh = get_screen_height();
    if (x < 8) x = 8;
    if (sw > kWndW && x > sw - kWndW) x = sw - kWndW;
    if (y < 0) y = 0;
    if (sh > kWndH && y > sh - kWndH) y = sh - kWndH;
}

// (defined later, in the inventory-button section) — the live item-inventory window's
// width and absolute screen position, used to dock the bag beside it.
static int  InvWndWidth(void* pBase);
static void SehGetWndAbs(void* pIUIMsgHandler, int& l, int& t);

// Where to open the bag: docked just to the RIGHT of the item-inventory window, top-aligned
// (or to the LEFT if there's no room on the right). Returns false if the inventory isn't
// available/positioned, so the caller falls back to the saved/centered position.
static bool InvDockPosition(int& x, int& y) {
    void* inv = nullptr;
    __try { inv = *reinterpret_cast<void**>(0x00BED654); } __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }
    if (!inv) return false;
    int w = InvWndWidth(inv);
    if (w < 80 || w > 2000) return false;
    int absL = 0, absT = 0; SehGetWndAbs(reinterpret_cast<char*>(inv) + 4, absL, absT);
    if (absL <= 0 && absT <= 0) return false;   // inventory not laid out / off-screen
    int sw = get_screen_width();
    x = absL + w + 3;                            // prefer docking to the RIGHT (3px gap)
    if (x + kWndW > sw) x = absL - kWndW - 3;    // no room on the right -> dock to the LEFT
    y = absT;
    return true;
}

// Create the bag window (if absent) focused on `initialKind`, docked beside the item
// inventory. Falls back to the remembered position, else centers it.
static CUIBagWindow* EnsureBagWindow(int initialKind) {
    if (CUIBagWindow::ms_pInstance) return CUIBagWindow::ms_pInstance;
    if (initialKind < 0 || initialKind >= kKindCount) initialKind = kKindOre;
    int x, y;
    if (!InvDockPosition(x, y)) {                        // open right next to the inventory window
        if (s_bSavedPos) { x = s_savedX; y = s_savedY; }
        else { x = (get_screen_width() - kWndW) / 2; y = 110; }
    }
    ClampBagToScreen(x, y);
    return new CUIBagWindow(initialKind, x, y);
}

CUIBagWindow::CUIBagWindow(int initialKind, int nLeft, int nTop)
    : m_activeKind(initialKind), m_nInvScroll(0), m_screenX(nLeft), m_screenY(nTop),
      m_nCloseHover(0), m_nClosePressed(0), m_nMergeHover(0), m_nMergePressed(0), m_nAutoHover(0), m_nAutoPressed(0),
      m_bDragging(0), m_nDragAnchorX(0), m_nDragAnchorY(0),
      m_bScrollDrag(0), m_nScrollGrabDY(0),
      m_armDragType(0), m_armDragIdx(0), m_armDragItem(0), m_armDownX(0), m_armDownY(0),
      m_nLastClickKey(0), m_nLastClickTick(0), m_bTtInit(false), m_nTtKey(0), m_bCloseWithInventory(true),
      m_cursorState(0),
      m_searchActive(false), m_searchLen(0), m_pendingAccent(0), m_displayCount(0) {
    m_search[0] = 0;
    m_rcClose  = { kBtCloseX,  kBtCloseY,  kBtCloseX  + kBtCloseW,  kBtCloseY  + kBtCloseH };
    m_rcMerge  = { kBtMergeX,  kBtMergeY,  kBtMergeX  + kBtMergeW,  kBtMergeY  + kBtMergeH };
    m_rcAuto   = { kBtAutoX,   kBtAutoY,   kBtAutoX   + kBtAutoW,   kBtAutoY   + kBtAutoH };
    for (int k = 0; k < kKindCount; ++k)
        m_rcTab[k] = { kTabHitLeft[k], kTabTop, kTabHitRight[k], kTabBot };
    m_rcSearch = { kSearchBoxL, kSearchBoxTop, kSearchBoxR, kSearchBoxBot };
    ms_pInstance = this;
    LoadSprites();
    CreateWnd(nLeft, nTop, kWndW, kWndH, 10, 1, nullptr, 0);
    play_ui_sound(L"MenuUp");
    m_pFont = nullptr;
    try { get_basic_font(std::addressof(m_pFont), 0); } catch (...) {}
    // Dark Dotum 11 for text drawn on the light art (search box / bag-name label).
    m_pFontDk = nullptr;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", m_pFontDk, nullptr);
        if (m_pFontDk) {
            HRESULT hr = reinterpret_cast<HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long,
                unsigned long, const Ztl_variant_t&)>(kAddr_SetFont)(
                m_pFontDk, L"Dotum", 11, 0xFF202020, Ztl_variant_t(L""));
            if (FAILED(hr)) m_pFontDk = nullptr;
        }
    } catch (...) { m_pFontDk = nullptr; }
    m_pFontTab = nullptr;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", m_pFontTab, nullptr);
        if (m_pFontTab) {
            HRESULT hr = reinterpret_cast<HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long,
                unsigned long, const Ztl_variant_t&)>(kAddr_SetFont)(
                m_pFontTab, L"Dotum", 11, 0xFFFFFFFF, Ztl_variant_t(L""));
            if (FAILED(hr)) m_pFontTab = nullptr;
        }
    } catch (...) { m_pFontTab = nullptr; }
    s_bAwaitingSnapshot = false;     // a fresh window starts un-gated (no transfer in flight yet)
    SendBagReq_Open(m_activeKind);   // request the current snapshot for the active bag
}

void CUIBagWindow::OnDestroy() {
    EndDragIcon();   // drop any in-flight drag ghost so it can't outlive the window
    // Remember placement + active bag so the next open restores them.
    s_savedX = m_screenX; s_savedY = m_screenY; s_bSavedPos = true; s_savedKind = m_activeKind;
    if (m_bTtInit) { __try { TT_Clear(m_ttBuf); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    for (int k = 0; k < kKindCount; ++k) FreeBag(k);
    if (m_bTtInit) { __try { TT_Dtor(m_ttBuf); } __except (EXCEPTION_EXECUTE_HANDLER) {} m_bTtInit = false; }
    m_pFont = nullptr; m_pFontDk = nullptr; m_pFontTab = nullptr;
    m_pBg = nullptr;
    for (int k = 0; k < kKindCount; ++k) m_pTabOn[k] = nullptr;
    for (int i = 0; i < 2; ++i) { m_pPillL[i] = nullptr; m_pPillF[i] = nullptr; m_pPillR[i] = nullptr; }
    m_pBtClose[0] = nullptr; m_pBtClose[1] = nullptr;
    m_pBtSort[0] = nullptr; m_pBtSort[1] = nullptr; m_pBtSort[2] = nullptr;
    m_pBtAuto[0] = nullptr; m_pBtAuto[1] = nullptr; m_pBtAuto[2] = nullptr; m_pBtAuto[3] = nullptr;
    m_pSbPrev[0] = nullptr; m_pSbNext[0] = nullptr; m_pSbBase = nullptr; m_pSbThumb[0] = nullptr;
    for (int i = 0; i < 10; ++i) m_pDigit[i] = nullptr;
    for (int i = 0; i < 4; ++i) m_pNewGlow[i] = nullptr;
    if (ms_pInstance == this) ms_pInstance = nullptr;
    CWnd::OnDestroy();
}

void CUIBagWindow::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr pCanvas = GetCanvas();
    if (!pCanvas) return;
    auto pItemInfo = CItemInfo::GetInstance();
    IWzFont* pf   = m_pFont;
    IWzFont* pfDk = m_pFontDk ? (IWzFont*)m_pFontDk : pf;

    // (1) Shared background — title, the 5x5 grid wells and the search-box frame are baked into
    //     the art (UI/UIWindow.img/Bag/backgrnd). The active bag is shown by the tab labels, not the bg.
    BlitAt(pCanvas, m_pBg, 0, 0);

    // (1b) Tabs — pink/grey pills + white GBK labels (DrawText, not WZ colored glyphs).
    for (int k = 0; k < kKindCount; ++k) {
        bool sel = (k == m_activeKind);
        DrawTabPill(pCanvas, kTabHitLeft[k] + kPillPad, (kTabHitRight[k] - kTabHitLeft[k]) - 2 * kPillPad, sel);
        if (m_pFontTab) {
            try {
                pCanvas->DrawTextA(kTabLabelX[k], kTabLabelY, Ztl_bstr_t(BagTabLabel(k)), m_pFontTab,
                                   Ztl_variant_t(), Ztl_variant_t());
            } catch (...) {}
        } else if (m_pTabOn[k]) {
            BlitA(pCanvas, m_pTabOn[k], kTabLabelX[k], kTabLabelY);
        }
    }

    // (2) Item icons over the baked grid cells, with the per-slot stack count drawn
    //     bottom-left like the vanilla inventory (white numerals on a dark badge).
    BagStore& s = bag();
    for (int v = 0; v < kInvVisCount; ++v) {
        int slot = DisplaySlot(m_nInvScroll * kInvCols + v);
        if (slot >= 0 && slot < kInvCap && s.ready && s.id[slot]) {
            RECT rc; InvRect(v, rc);
            if (pItemInfo)
                pItemInfo->DrawItemIconForSlot(pCanvas, s.id[slot], rc.left + kIconDX, rc.top + kIconBY, 0, 0, 0, 0, 0, 0);
            // New-item glow: the VANILLA inventory "new item" effect (UI/UIWindow.img/Item/New/inventory,
            // a 4-frame 36x36 animation) over a slot just auto-collected (set in HandleBagSnapshot), until the
            // player hovers/grabs it. Centered on the 31px cell; Update() invalidates each frame so it
            // animates. A translucent halo drawn OVER the icon but UNDER the stack count below.
            if (s.isNew[slot] && m_pNewGlow[0]) {
                int f = (int)((GetTickCount() / 150) % 4);
                BlitA(pCanvas, m_pNewGlow[f] ? m_pNewGlow[f] : m_pNewGlow[0],
                      rc.left + (kCell - 36) / 2, rc.top + (kCell - 36) / 2);
            }
            // per-slot stack count — vanilla ItemNo digits (white glyph + black outline,
            // no box) seated at the cell's bottom-left and nudged up so they don't clip.
            int q = s.qty[slot];
            if (q >= 1) {
                char num[12]; _snprintf(num, sizeof(num), "%d", q); num[11] = 0;
                int dx = rc.left + 1, dy = rc.bottom - 14;
                for (const char* np = num; *np; ++np) {
                    int d = *np - '0';
                    if (d >= 0 && d <= 9 && m_pDigit[d]) { BlitA(pCanvas, m_pDigit[d], dx, dy); dx += m_digitW[d]; }
                }
            }
        }
    }

    // (3) Vertical scrollbar (vanilla blue VScr4) in the right margin — ALWAYS drawn (even when the list
    //     fits, e.g. while a search filter narrows it) so it never vanishes mid-search; the thumb simply
    //     fills the whole track when there's nothing to scroll. base + thumb are tiles, stretched to fill.
    {
        auto stretchV = [&](IWzCanvasPtr sp, int y, int h) {
            if (sp && h > 0) try { pCanvas->CopyEx(kScrollX, y, sp, CANVAS_ALPHATYPE::CA_OVERWRITE,
                                                   kScrollW, h, 0, 0, 0, 0); } catch (...) {}
        };
        int trackY = kScrollTop + kSbArrowH, trackH = (kScrollBot - kSbArrowH) - trackY;
        stretchV(m_pSbBase, trackY, trackH);                              // track groove
        BlitA(pCanvas, m_pSbPrev[0], kScrollX, kScrollTop);               // up arrow
        BlitA(pCanvas, m_pSbNext[0], kScrollX, kScrollBot - kSbArrowH);   // down arrow
        // thumb: vanilla VScr4 grip (25px source) 3-sliced — rounded caps + stretched middle
        RECT th; ThumbRect(th);
        IWzCanvasPtr thumb = m_pSbThumb[0];
        const int srcH = 25, cap = 6;
        int thH = th.bottom - th.top;
        if (thumb) {
            if (thH < 2 * cap + 2) { stretchV(thumb, th.top, thH); }
            else try {
                pCanvas->CopyEx(kScrollX, th.top,          thumb, CANVAS_ALPHATYPE::CA_OVERWRITE, kScrollW, cap,           0, 0,          15, cap);
                pCanvas->CopyEx(kScrollX, th.top + cap,    thumb, CANVAS_ALPHATYPE::CA_OVERWRITE, kScrollW, thH - 2 * cap, 0, cap,        15, srcH - 2 * cap);
                pCanvas->CopyEx(kScrollX, th.bottom - cap, thumb, CANVAS_ALPHATYPE::CA_OVERWRITE, kScrollW, cap,           0, srcH - cap, 15, cap);
            } catch (...) {}
        }
    }

    // (5) Sort/merge button (vanilla red BtSort): consolidate identical stacks.
    //     pressed > hover > normal art.
    BlitA(pCanvas, m_nMergePressed ? m_pBtSort[1] : (m_nMergeHover ? m_pBtSort[2] : m_pBtSort[0]),
          m_rcMerge.left, m_rcMerge.top);

    // (5b) Auto-collect toggle button. Lit (normal/hover) when Auto is ON for the active kind, greyed
    //      (disabled art) when OFF; pressed art while the mouse is held on it.
    {
        bool on = s_bagAuto[m_activeKind];
        IWzCanvasPtr a = m_nAutoPressed ? m_pBtAuto[1]
                        : (on ? (m_nAutoHover ? m_pBtAuto[2] : m_pBtAuto[0])
                              : (m_pBtAuto[3] ? m_pBtAuto[3] : m_pBtAuto[0]));
        BlitA(pCanvas, a, m_rcAuto.left, m_rcAuto.top);
    }

    // (6) Close button — mouseOver art when hovered.
    { int coff = m_nClosePressed ? 1 : 0;   // sink the X while it's held (it closes on release)
      BlitA(pCanvas, m_nCloseHover ? m_pBtClose[1] : m_pBtClose[0], m_rcClose.left + coff, m_rcClose.top + coff); }

    // Re-assert the cursor each render frame (the engine resets the sprite otherwise): the sinking finger
    // (state 12) while a title-bar button is physically held, or the closed grab hand (state 11) while a
    // bag item is carried on the cursor — so the drag never falls back to a plain finger. Both are held
    // states, never hover cues. (Mirrors the inventory BAG button's Draw-hook re-assert.)
    if (m_nClosePressed || m_nMergePressed || m_nAutoPressed) SetCursorState(kCursor_ButtonPress);   // 12 — held button
    else if (s_bItemDragging)                                 SetCursorState(kCursor_ItemGrab);      // 11 — carrying an item

    // (7) Search field — drawn ON TOP of the baked search box at the bottom: typed
    //     text (or a dim hint), a blinking caret while focused, and a result-count.
    if (pfDk) {
        if (m_searchLen > 0) {
            try { pCanvas->DrawTextA(kSearchTextX, kSearchTextY, Ztl_bstr_t(m_search), pfDk, Ztl_variant_t(), Ztl_variant_t()); } catch (...) {}
        } else if (!m_searchActive) {
            const char* hint = BagSearchHint(m_activeKind);
            try { pCanvas->DrawTextA(kSearchTextX, kSearchTextY, Ztl_bstr_t(hint), pfDk, Ztl_variant_t(), Ztl_variant_t()); } catch (...) {}
        }
        // caret (blink ~500ms) right AFTER the typed text. DrawTextA returns the text HEIGHT (not width) and
        // there's no extent API, so the caret X uses a per-glyph width estimate (narrow i/l/space, wide m/w).
        if (m_searchActive && ((GetTickCount() / 500) & 1) == 0) {
            int cx = kSearchTextX + SearchTextWidth(m_search, m_searchLen);
            pCanvas->DrawRectangle(cx, kSearchTextY, 1, 12, 0xFF202020);
        }
        // result-count badge when filtering
        if (Filtering()) {
            char cnt[16]; _snprintf(cnt, sizeof(cnt), "%d", m_displayCount); cnt[15] = 0;
            int cw = (int)strlen(cnt) * 6;
            try { pCanvas->DrawTextA(kSearchBoxR - cw - 6, kSearchTextY, Ztl_bstr_t(cnt), pfDk, Ztl_variant_t(), Ztl_variant_t()); } catch (...) {}
        }
    }
}

void CUIBagWindow::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    POINT pt{ rx, ry };
    if (msg == WM_LBUTTONDOWN) {
        if (PtInRect(&m_rcSearch, pt)) { SearchSetActive(true); return; }  // focus the search box
        SearchSetActive(false);            // clicking anything else unfocuses it
        // Title-bar buttons behave like real buttons: DOWN only SINKS them; the action fires on
        // RELEASE (WM_LBUTTONUP) if the cursor is still on the button. So click-and-hold on the X
        // keeps it sunk without closing; you close by releasing.
        if (PtInRect(&m_rcClose, pt)) { m_nClosePressed = 1; SetCursorState(kCursor_ButtonPress); m_cursorState = kCursor_ButtonPress; play_ui_sound(L"BtMouseClick"); InvalidateRect(nullptr); return; }
        for (int k = 0; k < kKindCount; ++k) {
            if (PtInRect(&m_rcTab[k], pt)) { SwitchTab(k); return; }   // tabs still switch on press
        }
        if (PtInRect(&m_rcMerge, pt)) { m_nMergePressed = 1; SetCursorState(kCursor_ButtonPress); m_cursorState = kCursor_ButtonPress; play_ui_sound(L"BtMouseClick"); InvalidateRect(nullptr); return; }  // sink now; sort/merge on release
        if (PtInRect(&m_rcAuto, pt))  { m_nAutoPressed = 1;  SetCursorState(kCursor_ButtonPress); m_cursorState = kCursor_ButtonPress; play_ui_sound(L"BtMouseClick"); InvalidateRect(nullptr); return; }  // sink now; auto toggle on release
        // scrollbar (ignored while ANY item drag is in flight — bag drag-out or a
        // drag in/over the window from the inventory; only a no-drag grab scrolls)
        if (!ScrollLocked() && MaxScroll() > 0 && rx >= kScrollX && rx < kScrollX + kScrollW && ry >= kScrollTop && ry < kScrollBot) {
            if (ry < kScrollTop + kSbArrowH) { m_nInvScroll--; ClampScroll(); return; }
            if (ry >= kScrollBot - kSbArrowH) { m_nInvScroll++; ClampScroll(); return; }
            RECT th; ThumbRect(th);
            if (PtInRect(&th, pt)) {
                m_bScrollDrag = 1; m_nScrollGrabDY = ry - th.top;
                SetCursorState(kCursor_ScrollV); m_cursorState = kCursor_ScrollV;   // up/down arrows (9) while dragging
            }
            return;
        }
        // item cell: start a NATIVE engine drag (BeginItemDrag). ONE click grabs the item onto the
        // cursor (closed-hand cursor, item icon follows) exactly like the stock inventory; the NEXT
        // click drops it — the engine consumes that click, fires CDraggableItem::OnDropped (our hook)
        // and we Withdraw when it lands outside the bag. (Right-click below = quick withdraw.)
        int slot = HitItemSlot(rx, ry);
        if (slot >= 0 && slot < bag().count && bag().id[slot]) {
            BeginItemDrag(slot, bag().id[slot], rx, ry);
            return;
        }
        if (ry < kTitleH) { m_bDragging = 1; m_nDragAnchorX = m_screenX + rx; m_nDragAnchorY = m_screenY + ry; }   // anchor = ABSOLUTE cursor; smooth drag runs in BagWindow_HandleMouseMessage
    } else if (msg == WM_RBUTTONDOWN) {
        int slot = HitItemSlot(rx, ry);
        if (slot >= 0) Withdraw(slot);         // right-click = quick withdraw
    } else if (msg == WM_LBUTTONUP) {
        // NB: a NATIVE item-drag is NOT ended here — this client ends an engine drag on the next
        // CLICK, never on button-up (native "click to pick up, click to drop"). Leave s_bItemDragging.
        m_bDragging = 0; m_bScrollDrag = 0;
        POINT up{ rx, ry };
        // Title-bar buttons fire their action on RELEASE, and only if the cursor is still on them
        // (drag off the button before releasing = cancel). The close button is handled LAST so nothing
        // touches this object after Destroy().
        if (m_nMergePressed) { m_nMergePressed = 0; if (PtInRect(&m_rcMerge, up)) SendBagReq_Merge(m_activeKind); InvalidateRect(nullptr); }
        if (m_nAutoPressed)  { m_nAutoPressed = 0; if (PtInRect(&m_rcAuto, up)) SendBagReq_SetAuto(m_activeKind, !s_bagAuto[m_activeKind]); InvalidateRect(nullptr); }
        if (m_nClosePressed) { m_nClosePressed = 0; if (PtInRect(&m_rcClose, up)) { Destroy(); return; } InvalidateRect(nullptr); }
        // released (and not closed): drop the button-sink / scroll cursor back to arrow; the next hover
        // re-picks the item hand or scrollbar cursor.
        if (m_cursorState == kCursor_ButtonPress || m_cursorState == kCursor_ScrollV) { m_cursorState = kCursor_Arrow; SetCursorState(kCursor_Arrow); }
    }
    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

int CUIBagWindow::OnMouseMove(int rx, int ry) {
    // Window drag is handled at the WndProc level (BagWindow_HandleMouseMessage, dispatched from
    // bypass.cpp) using ABSOLUTE cursor coords, so it stays smooth and keeps tracking even when the
    // cursor leaves the window. The old window-local delta here made the window jump/freeze.
    if (m_bDragging) { HideTip(); return 1; }
    if (m_bScrollDrag) {
        int origin, thumbH, travel;
        ThumbGeom(origin, thumbH, travel);
        int rel = (ry - m_nScrollGrabDY) - origin;
        m_nInvScroll = (travel > 0) ? (rel * MaxScroll() + travel / 2) / travel : 0;
        ClampScroll(); HideTip();
        if (m_cursorState != kCursor_ScrollV) { m_cursorState = kCursor_ScrollV; SetCursorState(kCursor_ScrollV); }  // keep the up/down scroll cursor (9)
        return 1;
    }
    // (item grab is a native engine drag started on mouse-down — see OnMouseButton/BeginItemDrag;
    //  the engine renders the icon + follows the cursor, so there's nothing to do here for dragging.)
    // hover: close box + switch + sort/merge buttons
    POINT pt{ rx, ry };
    m_nCloseHover = PtInRect(&m_rcClose, pt) ? 1 : 0;
    int mgHov = PtInRect(&m_rcMerge, pt) ? 1 : 0;
    if (mgHov != m_nMergeHover) { m_nMergeHover = mgHov; if (mgHov) play_ui_sound(L"BtMouseOver"); InvalidateRect(nullptr); }
    int auHov = PtInRect(&m_rcAuto, pt) ? 1 : 0;
    if (auHov != m_nAutoHover) { m_nAutoHover = auHov; if (auHov) play_ui_sound(L"BtMouseOver"); InvalidateRect(nullptr); }
    // hover tooltip
    int slot = HitItemSlot(rx, ry);
    if (slot >= 0 && slot < kInvCap && bag().id[slot]) bag().isNew[slot] = false;   // hovering an item clears its "new" glow
    int key = (slot >= 0 && slot < bag().count && bag().id[slot]) ? (slot + 1) : 0;
    if (key != m_nTtKey) {
        if (key) {
            GW_ItemSlotBase* p = (slot >= 0 && slot < kInvCap) ? (GW_ItemSlotBase*)bag().obj[slot] : nullptr;
            if (p) { ShowTip(m_screenX + rx + 12, m_screenY + ry, p); m_nTtKey = key; }
            else { HideTip(); }
        } else HideTip();
    }
    // Bag cursor — kept INDEPENDENT of the title-bar buttons so tweaking the buttons never disturbs the
    // item grab: the open/close "grab" hand (state 5) over ANY grid cell (with an item OR empty), exactly
    // like the native inventory, which shows the hand over the whole item grid. The engine breathes that
    // hand (Cursor/5, a 3-frame loop) on its own once state 5 is set. Plain arrow off the grid. The sinking
    // finger (state 12) is a CLICK cue only (the *Pressed flags). Untouched during an item drag (grab hand).
    if (!ScrollLocked()) {
        int want;
        if (m_nClosePressed || m_nMergePressed || m_nAutoPressed) want = kCursor_ButtonPress;  // a button is held
        else if (MaxScroll() > 0 && OverScrollbar(rx, ry))        want = kCursor_ScrollHover;    // over the scrollbar -> animated up/down arrows (7)
        else if (slot >= 0)                                       want = kCursor_ItemHand;      // over any bag cell (item or empty)
        else                                                      want = kCursor_Arrow;
        if (want != m_cursorState) { m_cursorState = want; SetCursorState(want); }
    }
    return 1;
}

int CUIBagWindow::OnMouseWheel(int rx, int ry, int nWheel) {
    if (ScrollLocked()) return 1;                // don't scroll while dragging an item (in OR out)
    m_nInvScroll += (nWheel > 0) ? 1 : -1;       // wheel up -> earlier rows
    ClampScroll(); HideTip(); return 1;
}

void CUIBagWindow::OnMouseEnter(int bEnter) {
    CWnd::OnMouseEnter(bEnter);
    if (!bEnter) {
        m_nCloseHover = 0; m_nMergeHover = 0; m_nAutoHover = 0; HideTip();
        if (m_cursorState != 0) { m_cursorState = 0; SetCursorState(kCursor_Arrow); }   // drop the bag cursor on leave
    }
}

// Parse RESP_SNAPSHOT into g_bag[kind]; open the window if closed, and focus the
// snapshot's bag (covers a server push). A refresh for the already-shown bag keeps
// the current scroll position; switching bags resets it.
static void HandleBagSnapshot(CInPacket* pkt, unsigned char* data, unsigned int& offset, unsigned short length) {
    auto canRead = [&](size_t n) { return offset + n <= length; };
    auto dec1 = [&]() -> int { if (!canRead(1)) return 0; int v = data[offset]; offset += 1; return v; };
    auto dec2 = [&]() -> short { if (!canRead(2)) return 0; short v = *reinterpret_cast<short*>(data + offset); offset += 2; return v; };

    int kind = dec1();
    if (kind < 0 || kind >= kKindCount) return;
    // Snapshot BEFORE wiping: old per-slot item/qty/new so we can light up newly auto-collected slots.
    bool wasUserAction = s_bAwaitingSnapshot;   // reply to a deliberate transfer -> never glows
    bool wasReady      = g_bag[kind].ready;     // very first load -> never glows
    static int  s_oldId[kInvCap]; static int s_oldQty[kInvCap]; static bool s_oldNew[kInvCap];
    for (int i = 0; i < kInvCap; ++i) { s_oldId[i] = g_bag[kind].id[i]; s_oldQty[i] = g_bag[kind].qty[i]; s_oldNew[i] = g_bag[kind].isNew[i]; }
    s_bAwaitingSnapshot = false;   // fresh layout received -> re-enable grid interaction
    FreeBag(kind);
    int count = dec2();                             // item count (short — capacity can exceed a byte, max 200)
    if (count < 0) count = 0;
    short itemSlots[256];                           // each item's slot, in wire order (cap 200 < 256)
    int maxSlot = 0;
    int decoded = 0;                                // items actually parsed (for the qty block)
    for (int i = 0; i < count; ++i) {
        if (!canRead(2)) break;                     // no room left for even the slot short -> truncated
        short slot = dec2();
        GW_ItemSlotBase* p = RawDecodeItem(pkt);   // shares the packet offset
        if (offset > length) { DropRef(p); break; } // engine decode overran the declared length -> stop
        itemSlots[i & 0xFF] = slot;
        if (slot >= 0 && slot < kInvCap) {
            StoreDecoded(g_bag[kind].obj[slot], p);
            g_bag[kind].id[slot] = DecodeItemID(p);
            if (slot + 1 > maxSlot) maxSlot = slot + 1;
        } else DropRef(p);
        ++decoded;
    }
    // Trailing stack-count block (one short per item, same order). Absent on older
    // servers -> canRead fails -> quantities stay 0 (no number drawn). Forward/back
    // compatible: an older client just ignores these trailing bytes.
    for (int i = 0; i < decoded; ++i) {
        short q = canRead(2) ? dec2() : 0;
        short slot = itemSlots[i & 0xFF];
        if (slot >= 0 && slot < kInvCap) g_bag[kind].qty[slot] = q;
    }
    // Trailing Auto-collect flag (one byte, after the qty block). Absent on older servers -> keep prior.
    if (canRead(1)) s_bagAuto[kind] = (dec1() != 0);
    g_bag[kind].count = maxSlot;
    g_bag[kind].ready = true;
    // New-item glow: an UNSOLICITED push (auto-collect on pickup) that adds an item to a slot (or grows
    // a stack) marks it "new" -> pulsing highlight until the player hovers/grabs it. Deliberate transfers
    // (deposit/withdraw/move/merge -> s_bAwaitingSnapshot) and the very first load never glow; existing
    // glow carries over for items that stayed put.
    bool glow = wasReady && !wasUserAction;
    for (int s = 0; s < kInvCap; ++s) {
        int nid = g_bag[kind].id[s];
        if (nid == 0) { g_bag[kind].isNew[s] = false; continue; }
        if (s_oldId[s] == nid) {
            g_bag[kind].isNew[s] = s_oldNew[s] || (glow && g_bag[kind].qty[s] > s_oldQty[s]);
        } else {
            g_bag[kind].isNew[s] = glow;
        }
    }
    // Refresh only if the window is OPEN. A server push (auto-collect on pickup, or a deposit reply)
    // must never POP the window open, so we do NOT create it here — only the BAG button / F9 opens it.
    CUIBagWindow* w = CUIBagWindow::ms_pInstance;
    if (w) {
        if (w->m_activeKind != kind) { w->m_activeKind = kind; w->m_nInvScroll = 0; }
        w->RebuildFilter();                    // item set changed -> refresh the filtered view
        w->ClampScroll();                      // a withdraw can shrink the list past the offset
        w->InvalidateRect(nullptr);
    }
}

// Toggle the bag window open/closed.
// closeWithInventory: true when opened from the inventory BAG button (auto-close with inv);
// false for sidebar / standalone so the bag can stay open without the item inventory.
static void ToggleBags(bool closeWithInventory) {
    if (CUIBagWindow::ms_pInstance) { CUIBagWindow::ms_pInstance->Destroy(); return; }
    CUIBagWindow* w = EnsureBagWindow(s_savedKind);
    if (w) {
        w->m_bCloseWithInventory = closeWithInventory && IsInventoryShown();
    }
}

// --- key hook (chained): routes typing into the bag's search box while it's focused ---
static auto CField_OnKey = reinterpret_cast<void(__thiscall*)(void*, unsigned int, int)>(kAddr_CField_OnKey);
void __fastcall CField_OnKey_bag_hook(void* pThis, void* /*edx*/, unsigned int wParam, int lParam) {
    const bool isKeyUp = (lParam & 0x80000000) != 0;
    CUIBagWindow* w = CUIBagWindow::ms_pInstance;
    if (!isKeyUp && wParam == VK_ESCAPE && w) {
        if (w->m_searchActive) {
            if (w->m_searchLen) w->SearchClear();
            w->SearchSetActive(false);
            return;
        }
        w->Destroy();
        return;
    }
    // While the bag's search box is focused, route printable keys into it.
    if (w && w->m_searchActive && !isKeyUp) {
        if (w->HandleSearchKey(wParam)) return;
    }
    CField_OnKey(pThis, wParam, lParam);
}

// Inventory slot (1-based) under the CURRENT cursor, or -1. Calls the stock item inventory's own
// point->slot hit-test (CUIItem@0x0081DB7E: __thiscall(objectBase, xLocal, yLocal) -> slot, 0 if none)
// with cursor coords made local to the inventory window. Pure query (no side effects). Used as the
// withdraw DROP TARGET; the server still validates the slot against the item's own inventory type.
static int InvSlotAtDropPoint() {
    void* inv = nullptr;
    __try { inv = *reinterpret_cast<void**>(0x00BED654); } __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }
    if (!inv) return -1;
    POINT sp; if (!GetAbsCursor(sp)) return -1;
    int absL = 0, absT = 0; SehGetWndAbs(reinterpret_cast<char*>(inv) + 4, absL, absT);
    int lx = sp.x - absL, ly = sp.y - absT;
    int slot = 0;
    __try { slot = reinterpret_cast<int(__thiscall*)(void*, int, int)>(0x0081DB7E)(inv, lx, ly); }
    __except (EXCEPTION_EXECUTE_HANDLER) { slot = 0; }
    return (slot >= 1) ? slot : -1;
}

// --- drop routing hook (chained with the stock + android handlers) ---
static auto CDraggableItem_OnDropped = reinterpret_cast<int(__thiscall*)(void*, void*, void*, int, int)>(0x004EF140);
int __fastcall CDraggableItem_OnDropped_bag_hook(void* pThis, void* /*edx*/, void* pFrom, void* pTo, int rx, int ry) {
    s_bItemDragging = false;   // any drop ends a bag item-drag
    CUIBagWindow* w = CUIBagWindow::ms_pInstance;
    if (pThis && w) {
        void* srcHandler = nullptr; int srcA = 0, srcB = 0;
        __try {
            srcHandler = *reinterpret_cast<void**>(reinterpret_cast<char*>(pThis) + 0x24);
            srcA = *reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + 0x18);
            srcB = *reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + 0x1C);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return CDraggableItem_OnDropped(pThis, pFrom, pTo, rx, ry);
        }
        void* ourHandler = reinterpret_cast<char*>(w) + 4;
        // (A) drag STARTED in the bag (we tagged +0x24). srcB = source bag slot.
        if (srcHandler == ourHandler) {
            if (pTo == ourHandler || pTo == static_cast<void*>(w)) {
                // dropped back ON the bag -> REARRANGE: move src -> the slot under the cursor.
                int dst = w->SlotAtDropPoint();
                if (dst >= 0 && dst != srcB) SendBagReq_Move(w->m_activeKind, srcB, dst);
            } else {
                // dropped OUTSIDE the bag -> withdraw, honoring the inventory slot dropped on.
                w->Withdraw(srcB, InvSlotAtDropPoint());
            }
            EndDragIcon();
            return 1;
        }
        // (B) a player-inventory drag dropped ON the bag -> deposit into the active bag at the slot
        //     under the cursor (srcA = inventory type, srcB = inventory position).
        if (pTo == ourHandler || pTo == static_cast<void*>(w)) {
            SendBagReq_Deposit(w->m_activeKind, srcA, srcB, w->SlotAtDropPoint());
            play_ui_sound(L"DragEnd");
            return 1;
        }
    }
    return CDraggableItem_OnDropped(pThis, pFrom, pTo, rx, ry);
}

// --- inbound snapshot (routed via BBrCore's central packet dispatcher, NOT a private hook) ---
// PacketDispatcher.cpp already hooks CClientSocket::ProcessPacket and peeks the opcode; for
// kOpcode_Bag_Recv it calls the exported BagWindow_HandleSnapshotPacket (outside the namespace),
// which forwards here. Installing our own ProcessPacket detour too would be a redundant 2nd hook.
static void HandleSnapshotPacketImpl(CInPacket* pPacket) {
    if (!pPacket) return;
    unsigned char* base = reinterpret_cast<unsigned char*>(pPacket);
    unsigned char* data = *reinterpret_cast<unsigned char**>(base + 0x8);
    unsigned int&  offset = *reinterpret_cast<unsigned int*>(base + 0x14);
    unsigned short length = *reinterpret_cast<unsigned short*>(base + 0xC);
    if (offset + 2 <= length) {
        unsigned short opcode = *reinterpret_cast<unsigned short*>(data + offset);
        if (opcode == kOpcode_Bag_Recv) {
            offset += 2;                      // consume opcode
            int respType = -1;                // only advance the cursor on a successful read
            if (offset + 1 <= length) { respType = data[offset]; offset += 1; }
            if (respType == kResp_Snapshot) HandleBagSnapshot(pPacket, data, offset, length);
        }
    }
}

// ===========================================================================
// Inventory-window bag button.
//
// A real engine CCtrlButton injected onto the vanilla ITEM inventory window
// (CUIItem), one slot left of the expand button, that toggles this F9 bag on
// click. A drawn overlay can't be clicked in that title-bar strip (clicks there
// are captured for window-dragging), so we inject a genuine child button and let
// the engine hit-test it; the click arrives as CUIItem::OnChildNotify(nId,0x64,..).
//
// CLIENT-SPECIFIC ADDRESSES (image base 0x400000) — RE-FIND on any other client:
//   CUIItem::OnCreate       0x0081C6C9  (add our button after the original runs)
//   CUIItem::OnChildNotify  0x0081D01F  (button notify; code 0x64 = click, 0x65 = hover)
//   CCtrlButton ctor        0x004258E4 ; allocator = ZAlloc_Alloc(kZAlloc_Instance) (reused from above)
//   CCtrlButton::CreateCtrl == primary vtable[+0x20]; art = UIWindow.img/Bag/BtOreBag
// ===========================================================================
static constexpr uintptr_t kAddr_CUIItem_OnCreate      = 0x0081C6C9;
static constexpr uintptr_t kAddr_CUIItem_OnChildNotify = 0x0081D01F;
static constexpr uintptr_t kAddr_CCtrlButton_ctor      = 0x004258E4;
static constexpr int kCCtrlButton_Size    = 0x5A4;
static constexpr int kVtbl_CreateCtrl_Off = 0x20;

static constexpr unsigned int kInvBtnId       = 0x8888;   // free id (vanilla item window uses 0x7D0..0x7D7)
static constexpr int          kInvNotifyClick = 0x64;     // OnChildNotify code for a click (0x65 = hover enter/leave)

// Button position within the window (window-local): one slot left of the expand
// button (BtFull), which the client creates at (m_width - 30, 6) == (125, 6).
static int g_invBtnX = 97;
static int g_invBtnY = 6;

// SEH-isolated engine calls — each __try lives in its own function with no C++
// object needing unwinding (MSVC C2712 forbids mixing the two).
static void* SehBtnAlloc(size_t n) {
    void* p = nullptr;
    __try { p = ZAlloc_Alloc(reinterpret_cast<void*>(kZAlloc_Instance), n); }
    __except (EXCEPTION_EXECUTE_HANDLER) { p = nullptr; }
    return p;
}
static bool SehBtnCtor(void* mem) {
    bool ok = true;
    __try { reinterpret_cast<void(__thiscall*)(void*)>(kAddr_CCtrlButton_ctor)(mem); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
static bool SehBtnCreateCtrl(void* btn, void* parent, unsigned int id, int x, int y, void* param) {
    bool ok = true;
    __try {
        void** vtbl = *reinterpret_cast<void***>(btn);
        auto pfn = reinterpret_cast<void(__thiscall*)(void*, void*, unsigned int, int, int, int, void*)>(
            vtbl[kVtbl_CreateCtrl_Off / sizeof(void*)]);
        pfn(btn, parent, id, x, y, 0, param);
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
// SEH-isolated read of CWnd::m_width (@ +0x24). Kept out of CreateInvButton, which holds C++
// objects needing unwinding (MSVC C2712 forbids __try in such a function).
static int SehReadWndWidth(void* pWnd) {
    int w = 0;
    __try { w = *reinterpret_cast<int*>(reinterpret_cast<char*>(pWnd) + 0x24); }
    __except (EXCEPTION_EXECUTE_HANDLER) { w = 0; }
    return w;
}

static void BtnWiden(const char* a, wchar_t* w, size_t cap) {
    size_t i = 0;
    if (a) for (; a[i] && i + 1 < cap; ++i) w[i] = (wchar_t)(unsigned char)a[i];
    w[i] = 0;
}

// Our button, ref-held while the inventory window lives. The engine also holds a
// child ref and releases it on window destroy; this ZRef keeps the object valid
// until the next OnCreate releases + recreates it.
static ZRef<CCtrlButton> s_invBtn;
static void* s_invBtnParent = nullptr;   // the CUIItem instance our button belongs to (dedupe guard)

// CUIItem is a TSingleton whose instance pointer lives at this global.
static constexpr uintptr_t kAddr_CUIItem_Instance = 0x00BED654;

static void CreateInvButton(void* pParentWnd) {
    // Dedupe: CUIItem::OnCreate can re-fire on the SAME live window (e.g. inventory expand/collapse).
    // We only drop our OWN ref on recreate, but the engine keeps the previous button parented, so a
    // second add would leave two "BAG" buttons on the window. Skip if this window already has ours.
    if (pParentWnd && pParentWnd == s_invBtnParent) { return; }
    s_invBtn = ZRef<CCtrlButton>();                    // release any button from a previous window
    s_invBtnParent = nullptr;
    void* mem = SehBtnAlloc(kCCtrlButton_Size);
    if (!mem) return;
    if (!SehBtnCtor(mem)) { return; }
    CCtrlButton* btn = reinterpret_cast<CCtrlButton*>(mem);
    s_invBtn = ZRef<CCtrlButton>(btn);                 // AddRef 0 -> 1

    wchar_t wuol[128];
    BtnWiden("UI/UIWindow.img/Bag/BtOreBag", wuol, 128);   // custom 25x11 art (origin 19,6), under the bag's own Bag node
    CCtrlButton::CREATEPARAM param;                    // ctor zeroes flags; sUOL default-constructed
    param.sUOL = wuol;
    // Position relative to the inventory window's ACTUAL width (CWnd::m_width @ +0x24), so the button
    // lands one slot LEFT of the expand button (BtFull @ width-30) regardless of the client's inventory
    // width. The stock storagebag hard-coded x=97 for a 155px window; on a wider inventory that lands
    // too far left (mid-title-bar). g_invBtnX stays the fallback if the width read fails.
    int btnX = g_invBtnX;
    int w = SehReadWndWidth(pParentWnd);
    if (w > 80 && w < 2000) btnX = w - 58;
    bool ok = SehBtnCreateCtrl(btn, pParentWnd, kInvBtnId, btnX, g_invBtnY, &param);
    if (!ok) {
        s_invBtn = ZRef<CCtrlButton>();                // creation failed -> drop it
    } else {
        s_invBtnParent = pParentWnd;                   // remember the owning window
    }
}

// CUIItem::OnCreate — build the window normally, then add our child button.
typedef void(__thiscall* t_CUIItem_OnCreate)(void*, void*);
static auto CUIItem_OnCreate = reinterpret_cast<t_CUIItem_OnCreate>(kAddr_CUIItem_OnCreate);
void __fastcall CUIItem_OnCreate_hook(void* pThis, void* /*edx*/, void* pData) {
    CUIItem_OnCreate(pThis, pData);
    // Only the canonical inventory SINGLETON (TSingleton @0x00BED654) gets the BAG button. The client
    // constructs a transient/secondary CUIItem during init whose OnCreate also fires here; adding a
    // button to it left a lingering second "BAG" button. Gate on the singleton so exactly one window
    // (the one the player actually sees) ever carries the button. Fallback: if the instance pointer
    // isn't set yet, still add (so we never end up with zero buttons).
    void* singleton = nullptr;
    __try { singleton = *reinterpret_cast<void**>(kAddr_CUIItem_Instance); } __except (EXCEPTION_EXECUTE_HANDLER) { singleton = nullptr; }
    if (singleton == nullptr || pThis == singleton) CreateInvButton(pThis);
}

// CUIItem::OnChildNotify — catch our button's click (our id + code 0x64).
typedef void(__thiscall* t_CUIItem_OnChildNotify)(void*, unsigned int, unsigned int, unsigned int);
static auto CUIItem_OnChildNotify = reinterpret_cast<t_CUIItem_OnChildNotify>(kAddr_CUIItem_OnChildNotify);
void __fastcall CUIItem_OnChildNotify_hook(void* pThis, void* /*edx*/, unsigned int nId, unsigned int nCode, unsigned int nParam) {
    if (nId == kInvBtnId) {
        if (nCode == kInvNotifyClick) ToggleBags(true);    // 0x64 = clicked; inventory-linked toggle
        return;                                        // consume (vanilla ignores this id)
    }
    CUIItem_OnChildNotify(pThis, nId, nCode, nParam);
}

// ===========================================================================
// Inventory BAG button (RELIABLE) — drawn on the item inventory via a CUIItem::Draw
// hook, click caught via a CUIItem::OnMouseButton hook. Uses the SAME confirmed
// addresses slotlock.cpp already hooks on THIS client (Draw 0x0081DC20,
// OnMouseButton 0x0081D42E, window width m_width @ +0x24). Chains with slotlock's
// own hooks. Replaces the CCtrlButton child injection (unverified addresses).
//
//   Draw's `this` = object base (IGObj/CWnd vtable) -> GetCanvas()/m_width read direct.
//   OnMouseButton's `this` = IUIMsgHandler sub-object (+4) -> object base = this - 4
//   (matches slotlock's `(CUIItem*)((char*)this - 4)`).
// ===========================================================================
static constexpr uintptr_t kAddr_CUIItem_Draw          = 0x0081DC20;
static constexpr uintptr_t kAddr_CUIItem_OnMouseButton = 0x0081D42E;
static constexpr int kInvBagBtnW = 36, kInvBagBtnH = 12;   // matches UIWindow.img/Bag/BtOreBag art (36x12) — click area = drawn art
static constexpr int kInvBagBtnRightMargin = 89;   // button x = inventory width - this (TUNABLE; bigger = more to the LEFT) — now 89 (3px left of original 86)
static constexpr int kInvBagBtnY = 6;              // title-bar row (TUNABLE)
static IWzCanvasPtr s_pInvBagBtn;
static IWzCanvasPtr s_pInvBagBtnPressed;            // sunk "pressed" art (falls back to a +1,+1 nudge of the normal art if absent)
static IWzCanvasPtr s_pInvBagBtnOver;               // mouseOver "glow" art shown while the cursor hovers the button
static bool s_bInvBagBtnTried = false;
static bool s_invBagBtnPressed = false;             // true while the BAG button is held -> drives the sink animation
static bool s_invBagBtnHover = false;               // true while the cursor is over the BAG button -> mouseOver glow + finger cursor
static RECT  s_invBagScreenRect = { 0, 0, 0, 0 };   // button rect in SCREEN coords (for the WndProc-level click)
// (s_invBagLastDrawTick lives up top so the bag's Update() can close the bag when the inventory hides.)

// SEH-isolated CWnd abs-position read. GetAbsLeft/GetAbsTop are IUIMsgHandler vtable slots, so pass
// the IUIMsgHandler sub-object (object base + 4). Kept out of the Draw hook (which holds a C++ COM ptr).
static void SehGetWndAbs(void* pIUIMsgHandler, int& l, int& t) {
    l = 0; t = 0;
    __try {
        l = reinterpret_cast<int(__thiscall*)(void*)>(0x009E03C5)(pIUIMsgHandler);
        t = reinterpret_cast<int(__thiscall*)(void*)>(0x009E0447)(pIUIMsgHandler);
    } __except (EXCEPTION_EXECUTE_HANDLER) { l = 0; t = 0; }
}

static int InvWndWidth(void* pBase) {
    int w = 0;
    __try { w = *reinterpret_cast<int*>(reinterpret_cast<char*>(pBase) + 0x24); }
    __except (EXCEPTION_EXECUTE_HANDLER) { w = 0; }
    return w;
}
static void InvBagBtnRect(void* pBase, RECT& rc) {
    int w = InvWndWidth(pBase);
    if (w < 80 || w > 2000) w = 175;
    int x = w - kInvBagBtnRightMargin;
    rc = { x, kInvBagBtnY, x + kInvBagBtnW, kInvBagBtnY + kInvBagBtnH };
}

// The BAG button's CURRENT screen rect, read straight from the live inventory singleton. Used by the
// WndProc click so it works IMMEDIATELY even right after the inventory window is dragged (the cached
// rect from Draw would still hold the OLD position until the inventory next redraws).
static bool InvBagLiveScreenRect(RECT& out) {
    void* inv = nullptr;
    __try { inv = *reinterpret_cast<void**>(0x00BED654); } __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }
    if (!inv) return false;
    int w = InvWndWidth(inv);
    if (w < 80 || w > 2000) return false;
    int absL, absT; SehGetWndAbs(reinterpret_cast<char*>(inv) + 4, absL, absT);
    int x = w - kInvBagBtnRightMargin;
    out = { absL + x, absT + kInvBagBtnY, absL + x + kInvBagBtnW, absT + kInvBagBtnY + kInvBagBtnH };
    return true;
}

// Force the item-inventory window to redraw NOW (CWnd::InvalidateRect @0x009E04C9 on the
// inventory singleton), so a press/release of the BAG button shows its sink animation right
// away — exactly the trick botcheck uses (its validate button invalidates its own window).
static void InvInvalidate() {
    void* inv = nullptr;
    __try { inv = *reinterpret_cast<void**>(0x00BED654); } __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }
    if (!inv) return;
    __try { reinterpret_cast<void(__thiscall*)(void*, const RECT*)>(0x009E04C9)(inv, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

typedef void(__thiscall* t_CUIItem_Draw)(void*, const RECT*);
static auto CUIItem_Draw_bag = reinterpret_cast<t_CUIItem_Draw>(kAddr_CUIItem_Draw);
void __fastcall CUIItem_Draw_bag_hook(void* pThis, void* /*edx*/, const RECT* pRect) {
    CUIItem_Draw_bag(pThis, pRect);            // pThis = object base for Draw
    if (!s_bInvBagBtnTried) {
        s_bInvBagBtnTried = true;
        s_pInvBagBtn        = CUIBagWindow::LoadSprite(L"UI/UIWindow.img/Bag/BtOreBag/normal/0");
        s_pInvBagBtnPressed = CUIBagWindow::LoadSprite(L"UI/UIWindow.img/Bag/BtOreBag/pressed/0");
        s_pInvBagBtnOver    = CUIBagWindow::LoadSprite(L"UI/UIWindow.img/Bag/BtOreBag/mouseOver/0");
    }
    if (!s_pInvBagBtn) return;
    // Safety: if flagged pressed but the physical button is up, un-press (guards against a lost
    // WM_LBUTTONUP leaving the button stuck sunk).
    if (s_invBagBtnPressed && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) s_invBagBtnPressed = false;
    // Cursor for the inventory BAG button, re-asserted here each render frame (this hook runs in the
    // render pass, AFTER the engine reprocessed the cursor, so it sticks; it never neutralizes a message,
    // so the cursor can't freeze). Priority: held button > carrying an item > hovering the button.
    //   held           -> sinking finger (12)
    //   carrying a bag item -> closed grab hand (11), even over the BAG button (so a drag isn't shown as "click me")
    //   just hovering  -> the "click me" pointer (state 1) — this cue is ONLY on the BAG button, not the bag's X/sort/auto
    if (s_invBagBtnPressed)    SetCursorState(kCursor_ButtonPress);   // 12 — BAG button held down
    else if (s_bItemDragging)  SetCursorState(kCursor_ItemGrab);      // 11 — carrying a bag item on the cursor
    else if (s_invBagBtnHover) SetCursorState(kCursor_AskClick);      // 1  — hovering the BAG button (asks for a click)
    IWzCanvasPtr canvas;
    try { canvas = reinterpret_cast<CWnd*>(pThis)->GetCanvas(); } catch (...) {}
    if (!canvas) return;
    RECT rc; InvBagBtnRect(pThis, rc);
    // Track the button's live SCREEN rect + a freshness tick so the WndProc click handler (the only
    // thing that sees title-bar clicks) can hit-test it while the inventory is actually visible.
    int absL, absT; SehGetWndAbs(reinterpret_cast<char*>(pThis) + 4, absL, absT);
    s_invBagScreenRect = { absL + rc.left, absT + rc.top, absL + rc.right, absT + rc.bottom };
    s_invBagLastDrawTick = GetTickCount();
    // Draw the BAG button: pressed (sunk) while held > mouseOver (glow) while hovered > normal.
    // No dedicated pressed art -> simulate the sink by nudging the normal art +1,+1.
    IWzCanvasPtr art; int off = 0;
    if (s_invBagBtnPressed) {
        art = s_pInvBagBtnPressed ? s_pInvBagBtnPressed : s_pInvBagBtn;
        off = s_pInvBagBtnPressed ? 0 : 1;
    } else if (s_invBagBtnHover && s_pInvBagBtnOver) {
        art = s_pInvBagBtnOver;
    } else {
        art = s_pInvBagBtn;
    }
    try { canvas->CopyEx(rc.left + off, rc.top + off, art, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0); } catch (...) {}
}

typedef void(__thiscall* t_CUIItem_OnMouseButton)(void*, unsigned int, unsigned int, int, int);
static auto CUIItem_OnMouseButton_bag = reinterpret_cast<t_CUIItem_OnMouseButton>(kAddr_CUIItem_OnMouseButton);
void __fastcall CUIItem_OnMouseButton_bag_hook(void* pThis, void* /*edx*/, unsigned int msg, unsigned int wParam, int rx, int ry) {
    // Right-click deposit (formerly via slotlock): when bag UI is open, deposit the clicked slot.
    // pThis is IUIMsgHandler (+4); CUIItem object base is pThis - 4.
    if (msg == WM_RBUTTONDOWN) {
        void* pItemUi = reinterpret_cast<char*>(pThis) - 4;
        int invType = 0;
        int slot = 0;
        __try {
            invType = *reinterpret_cast<int*>(reinterpret_cast<char*>(pItemUi) + 0x05E4); // m_nItemTI
            slot = reinterpret_cast<int(__thiscall*)(void*, int, int)>(0x0081DB7E)(
                pItemUi, static_cast<int>(rx), static_cast<int>(ry));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            invType = 0;
            slot = 0;
        }
        if (slot >= 1 && BagWindow_DepositFromInventory(invType, slot)) {
            return;
        }
    }
    // BAG title-bar button is driven by BagWindow_HandleMouseMessage (WndProc), not here.
    CUIItem_OnMouseButton_bag(pThis, msg, wParam, rx, ry);
}

// --- destroy the bag window on field-leave / logout (same pattern as damageskinpicker) so it
//     doesn't linger into the login/char-select screen ---
static auto CWvsContext_OnLeaveGame  = reinterpret_cast<void(__thiscall*)(void*)>(0x00A041FF);
static auto CWvsContext_ClearFieldUI = reinterpret_cast<void(__thiscall*)(void*)>(0x00A04920);
void __fastcall CWvsContext_OnLeaveGame_bag_hook(void* pThis, void* /*edx*/) {
    if (CUIBagWindow::ms_pInstance) { CUIBagWindow::ms_pInstance->Destroy(); CUIBagWindow::ms_pInstance = nullptr; }
    CWvsContext_OnLeaveGame(pThis);
}
void __fastcall CWvsContext_ClearFieldUI_bag_hook(void* pThis, void* /*edx*/) {
    if (CUIBagWindow::ms_pInstance) { CUIBagWindow::ms_pInstance->Destroy(); CUIBagWindow::ms_pInstance = nullptr; }
    CWvsContext_ClearFieldUI(pThis);
}

} // namespace BagWindow

void AttachBagWindowMod() {
    using namespace BagWindow;
    ATTACH_HOOK(CField_OnKey, CField_OnKey_bag_hook);
    ATTACH_HOOK(CDraggableItem_OnDropped, CDraggableItem_OnDropped_bag_hook);
    ATTACH_HOOK(CUIItem_Draw_bag, CUIItem_Draw_bag_hook);
    ATTACH_HOOK(CUIItem_OnMouseButton_bag, CUIItem_OnMouseButton_bag_hook);
    ATTACH_HOOK(CWvsContext_OnLeaveGame, CWvsContext_OnLeaveGame_bag_hook);
    ATTACH_HOOK(CWvsContext_ClearFieldUI, CWvsContext_ClearFieldUI_bag_hook);
}

void AttachStorageBagMod() {
    AttachBagWindowMod();
}

// Public toggle entry for other mods: open the bag on the last-viewed kind if closed,
// close it if open — the same path the F9 hotkey uses. (BagWindow::ToggleBags is
// file-static; this is its extern face.)
void BagWindow_Toggle() { BagWindow::ToggleBags(false); } // sidebar/standalone: do not require inventory
void BagWindow_Close() {
    if (BagWindow::CUIBagWindow::ms_pInstance) {
        BagWindow::CUIBagWindow::ms_pInstance->Destroy();
    }
}
bool BagWindow_IsOpen() { return BagWindow::CUIBagWindow::ms_pInstance != nullptr; }

// Public entry for slotlock.cpp: a plain right-click on an inventory item deposits it into its category
// Storage Bag, auto-routed by inventory type (EQUIP->mount, USE->scroll, SETUP->chair, ETC->ore). No-op
// (returns false) unless the bag window is OPEN. Returns true if a deposit request was sent, so the caller
// consumes the click. The whole stack moves; the server validates that the item is actually bag-eligible.
bool BagWindow_DepositFromInventory(int invType, int slot) {
    if (!BagWindow::CUIBagWindow::ms_pInstance) return false;   // bag closed -> right-click has no effect
    if (slot < 1) return false;
    int kind;
    switch (invType) {                                   // InventoryType: 1 EQUIP / 2 USE / 3 SETUP / 4 ETC / 5 CASH
        case 1:  kind = BagWindow::kKindCash;   break;   // EQUIP -> mount bag (the 4th "cash" slot, now Mounts)
        case 2:  kind = BagWindow::kKindScroll; break;   // USE   -> scroll bag
        case 3:  kind = BagWindow::kKindChair;  break;   // SETUP -> chair bag
        case 4:  kind = BagWindow::kKindOre;    break;   // ETC   -> ore bag
        default: return false;                           // CASH / unknown -> not bag-eligible
    }
    BagWindow::SendBagReq_Deposit(kind, invType, slot, -1);   // -1 = first free bag slot; server does the fine-grained check
    return true;
}

// WndProc-level smooth window drag, dispatched from bypass.cpp's CWndMan::TranslateMessage_hook
// (same wiring DamageRank/DamageSkin use). Uses ABSOLUTE cursor coords so the window keeps tracking
// even when the cursor leaves it — the CWnd::OnMouseMove (window-local) drag was janky/froze.
bool BagWindow_HandleMouseMessage(UINT& msg, WPARAM, LPARAM, LRESULT*) {
    using namespace BagWindow;
    // (A) Inventory BAG button (drawn in the inventory title bar, whose mouse messages never reach
    //     CUIItem::OnMouseButton). Driven HERE at the WndProc level against its live SCREEN rect:
    //     HOVER -> the button's own mouseOver art (glow), by flagging it + forcing an inventory redraw.
    //              We do NOT touch the WM_MOUSEMOVE message, so the cursor stays free (no freeze).
    //     DOWN  -> sink the button (pressed art) + eat the click so the engine can't title-bar-drag the
    //              inventory (the jolt). UP -> toggle the bag if released inside the button.
    if (msg == WM_MOUSEMOVE) {
        RECT rc; POINT sp; GetAbsCursor(sp);
        bool over = InvBagLiveScreenRect(rc) && PtInRect(&rc, sp);
        if (over != s_invBagBtnHover) {
            s_invBagBtnHover = over;
            InvInvalidate();     // repaint the button with/without the mouseOver glow (ART only, not the cursor)
        }
        // NB: do NOT touch the cursor on hover — the sinking finger (state 12) is a CLICK cue, not a hover
        //     cue. It's set on WM_LBUTTONDOWN below and re-asserted each render frame in the Draw hook
        //     while the button is held. (Setting the cursor on WM_MOUSEMOVE also froze it — never here.)
        // (not consumed: the move flows on so the cursor position + the bag-window drag keep working)
    }
    if (msg == WM_LBUTTONDOWN && !s_bItemDragging) {   // (while carrying a bag item, let the click end the drag)
        RECT rc;
        if (InvBagLiveScreenRect(rc)) {          // recomputed LIVE -> correct even right after an inventory drag
            POINT sp; GetAbsCursor(sp);
            if (PtInRect(&rc, sp)) {
                s_invBagBtnPressed = true; InvInvalidate();       // sink the button art (pressed)
                SetCursorState(kCursor_ButtonPress);              // finger sinks (state 12) at the moment of the click
                play_ui_sound(L"BtMouseClick");
                msg = WM_NULL;                                    // eat the click: no inventory-window drag (the jolt)
                return true;
            }
        }
    } else if (msg == WM_LBUTTONUP) {
        if (s_invBagBtnPressed) {
            s_invBagBtnPressed = false; InvInvalidate();       // un-sink
            RECT rc; POINT sp; GetAbsCursor(sp);
            if (InvBagLiveScreenRect(rc) && PtInRect(&rc, sp)) ToggleBags(true);  // released inside -> toggle the bag
            msg = WM_NULL;                                      // eat the matching button-up too
            return true;
        }
    }
    // (B) Smooth drag for the OPEN bag window.
    if (msg != WM_MOUSEMOVE && msg != WM_LBUTTONUP) return false;
    CUIBagWindow* w = CUIBagWindow::ms_pInstance;
    if (!w) return false;
    if (msg == WM_LBUTTONUP) { w->m_bDragging = 0; return false; }
    if (!w->m_bDragging || !w->m_pLayer) return false;
    POINT sp; if (!GetAbsCursor(sp)) return false;
    int dx = sp.x - w->m_nDragAnchorX, dy = sp.y - w->m_nDragAnchorY;
    if (dx || dy) {
        w->m_pLayer->RelOffset(dx, dy, Ztl_variant_t(), Ztl_variant_t());
        w->m_screenX += dx; w->m_screenY += dy;
        w->m_nDragAnchorX = sp.x; w->m_nDragAnchorY = sp.y;
        w->HideTip();
    }
    return false;
}

// Central-dispatcher entry: PacketDispatcher routes opcode 0x3725 here.
void BagWindow_HandleSnapshotPacket(CInPacket* pPacket) { BagWindow::HandleSnapshotPacketImpl(pPacket); }

namespace StorageBag {
void HandleServerPacket(CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    BagWindow_HandleSnapshotPacket(reinterpret_cast<CInPacket*>(packet));
}
} // namespace StorageBag

bool StorageBag_HandleMouseMessage(unsigned int& msg, unsigned long wParam, long lParam, long* plResult) {
    return BagWindow_HandleMouseMessage(msg, wParam, lParam, plResult);
}
