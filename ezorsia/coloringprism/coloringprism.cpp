// ============================================================
// coloringprism.cpp: the Coloring Prism window (item 5782000)
//
// Double-clicking 5782000 in the Cash inventory opens a CWnd with a live avatar
// preview and three slider rows -- Tone (hue 0..359), Chroma (saturation
// -100..+100) and Brightness (value -100..+100) -- that recolor the player's worn
// Cash weapon. Confirm sends the values to the server, which stores them on the item
// and consumes the prism. The other button zeroes the three sliders, and confirming
// from there UNDOES a colour, because an identity tint is sent as a restore rather than
// an apply: the server can then refuse it without consuming anything when the target was
// never tinted.
//
// Structure follows the reference dialog in the modern client (title, item banner,
// preview pane, three labelled slider rows with numeric readouts, Reset / Confirm /
// Cancel) but is redrawn at v83 scale: that dialog is 466x721 and this client's
// default screen is 800x600. The backdrop is hand-drawn art at
// Tools/art/coloring-prism.png; the sliders are drag-and-click-the-track, with no
// stepper arrows, because the art has no wells for them.
//
// SPLIT OF RESPONSIBILITIES
//   weapontint.cpp   owns the recolor itself, the tint state and both opcodes.
//   this file        owns the window and its one cash item id.
//
// HOOKS: none. The double-click is dispatched from the host's existing cash-item
// use hooks (a second Detour on 0x00A0A63F / 0x00A1DC5B / 0x004863D5 breaks
// whatever already owns them), and the inbound opcode is routed by the packet
// dispatcher.
//
// -------------------------------------------------------------------------
// WARNING: CLIENT-BUILD-SPECIFIC ADDRESSES
// Every 0x00XXXXXX below is tied to THIS v83 MapleStory.exe (image base
// 0x400000). All of them are byte-verified.
// -------------------------------------------------------------------------
// ============================================================

#include "stdafx.h"
#ifndef LOG_ONCE
#define LOG_ONCE(...) ((void)0)
#endif
#include "compat/hook.h"
#include "weapontint.h"
#include "ColoringPrismApi.h"

#include "compat/wvs/iteminfo.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/wndman.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <memory>

namespace ColorPrism {

// =====================================================
// ITEMS
// =====================================================
// ONE item drives every tab. There was briefly a second prism for hair and eyes, back
// when those were a separate window; once the tabs existed it was just a second thing to
// carry that did strictly less, so the Coloring Prism pays for all four tabs.
constexpr int kItemColoringPrism = 5782000;   // opens this window

// =====================================================
// ADDRESSES
// =====================================================
constexpr uintptr_t kAddr_play_ui_sound      = 0x00989588;
constexpr uintptr_t kAddr_get_basic_font     = 0x0098A707;
constexpr uintptr_t kAddr_SetFont            = 0x0046341A;
constexpr uintptr_t kAddr_ProcessBasicUIKey  = 0x00A07431;
constexpr uintptr_t kAddr_CWvsContext        = 0x00BE7918;
constexpr uintptr_t kAddr_CurrentStage       = 0x00BEDED4;
constexpr uintptr_t kAddr_InputSystem        = 0x00BEC33C;
constexpr uintptr_t kAddr_GetCursorPos       = 0x0059A388;
constexpr uintptr_t kAddr_SetCursorState     = 0x0059A6D9;

// The avatar-preview quartet, all byte-verified.
constexpr uintptr_t kAddr_ZRefCAvatar_Alloc  = 0x00428967;
constexpr uintptr_t kAddr_ZRef_Release       = 0x00428C15;
constexpr uintptr_t kAddr_AvatarLook_ctor    = 0x004283FE;
constexpr uintptr_t kAddr_CAvatar_Init       = 0x0045149F;
constexpr int       kOff_CharacterDataInCtx  = 2094 * 4;
constexpr int       kAvatarStandAction       = 5;

// InventoryType values as the server names them, plus the worn Cash-weapon position.
constexpr int kInvTypeEquipped = -1;
constexpr int kInvTypeEquip    = 1;
constexpr int kCashWeaponSlot  = -111;

// GW_CharacterData containers, all byte-verified.
// The inventory tabs are a 1-BASED array of 8-byte ZRefs whose payload is at +4; worn
// slots are reported as NEGATIVE positions, with the Cash overlay at -(100 + slot).
constexpr size_t kCtxCharacterData     = 0x20B8;
constexpr size_t kCdInventoryArrayBase = 0x447;
constexpr size_t kCdWornBase           = 0x0EB;
constexpr size_t kCdWornCashBase       = 0x28B;
constexpr int    kWornSlotCount        = 0x33;
constexpr int    kCashSlotOffset       = 100;
constexpr uintptr_t kAddr_TSecType_long_GetData = 0x0042873D;

// AvatarLook slot each equip category occupies, so an UNWORN item can be shown on
// the preview avatar. Standard v83 slots; the weapon is the odd one out because a
// Cash weapon lives in nWeaponStickerID rather than in anHairEquip (0x004E7358).
constexpr int kSlotWeaponSticker = -1;   // sentinel: write kAL_WeaponSticker instead
int AvatarSlotOf(int itemId) {
    const int hi = itemId / 100000;
    if (hi == 13 || hi == 14 || hi == 16 || hi == 17) return kSlotWeaponSticker;
    switch (itemId / 10000) {
        case 100: return 1;    // hat
        case 101: return 2;    // face acc
        case 102: return 3;    // eye acc
        case 103: return 4;    // earring
        case 104: case 105: return 5;    // coat / overall
        case 106: return 6;    // pants
        case 107: return 7;    // shoes
        case 108: return 8;    // glove
        case 109: return 10;   // shield
        case 110: return 9;    // cape
        default:  return 0;    // not previewable on the avatar
    }
}

auto play_ui_sound  = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
auto get_basic_font = reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);
using t_ZRefCAvatar_Alloc = void*(__thiscall*)(void*);
using t_ZRef_Release      = long (__thiscall*)(void*, int);
using t_AvatarLook_ctor   = void*(__thiscall*)(void*, void*);
using t_CAvatar_Init      = void (__thiscall*)(void*, void*, int, void*, void*, int, int, int, int, int);
auto ZRefCAvatar_Alloc = reinterpret_cast<t_ZRefCAvatar_Alloc>(kAddr_ZRefCAvatar_Alloc);
auto ZRef_Release      = reinterpret_cast<t_ZRef_Release>(kAddr_ZRef_Release);
auto AvatarLook_ctor   = reinterpret_cast<t_AvatarLook_ctor>(kAddr_AvatarLook_ctor);
auto CAvatar_Init      = reinterpret_cast<t_CAvatar_Init>(kAddr_CAvatar_Init);

// AvatarLook field offsets for this client build.
constexpr int kAL_WeaponSticker = 0x15;
constexpr int kAL_HairEquip0    = 0x19;   // anHairEquip[0]; slot n is +4*n

// Engine cursor sprites. The engine resets the sprite every frame, so a held
// state has to be re-asserted from Draw().
constexpr int kCursor_Arrow = 0, kCursor_ButtonPress = 12;
void SetCursorState(int state) {
    void* input = *reinterpret_cast<void**>(kAddr_InputSystem);
    if (input) reinterpret_cast<void(__thiscall*)(void*, int)>(kAddr_SetCursorState)(input, state);
}
bool GetAbsCursor(POINT& sp) {
    sp.x = 0; sp.y = 0;
    void* input = *reinterpret_cast<void**>(kAddr_InputSystem);
    if (!input) return false;
    reinterpret_cast<void(__thiscall*)(void*, POINT*)>(kAddr_GetCursorPos)(input, &sp);
    return true;
}

// =====================================================
// RESOLVING A DROPPED ITEM
// =====================================================
// Plaintext item id from a GW_ItemSlotBase* (TSecType<long> at +0xC).
int SehDecodeItemId(void* pItem) {
    if (!pItem) return 0;
    int id = 0;
    __try {
        id = reinterpret_cast<int(__thiscall*)(const void*)>(kAddr_TSecType_long_GetData)(
                 reinterpret_cast<const char*>(pItem) + 0x0C);
    } __except (EXCEPTION_EXECUTE_HANDLER) { id = 0; }
    return id;
}

// The item the player has at (invType, invPos), or nullptr. Every dereference is a
// raw read at a hardcoded offset, so the whole walk sits under SEH: a torn container
// must produce "nothing" rather than a fault in the middle of a drop.
void* SehItemAt(int invType, int invPos) {
    void* found = nullptr;
    __try {
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);
        if (!ctx) return nullptr;
        auto* cd = *reinterpret_cast<unsigned char**>(
                       static_cast<unsigned char*>(ctx) + kCtxCharacterData);
        if (!cd) return nullptr;

        if (invPos < 0) {                       // worn
            int i = -invPos;
            const unsigned char* base = cd + kCdWornBase;
            if (i > kCashSlotOffset) { i -= kCashSlotOffset; base = cd + kCdWornCashBase; }
            if (i < 1 || i > kWornSlotCount) return nullptr;
            found = *reinterpret_cast<void* const*>(base + 8 * static_cast<size_t>(i));
        } else {                                // an inventory tab
            if (invType < 1 || invType > 5 || invPos < 1) return nullptr;
            auto* arr = *reinterpret_cast<unsigned char**>(
                            cd + kCdInventoryArrayBase + 4 * static_cast<size_t>(invType));
            if (!arr) return nullptr;
            const int count = *reinterpret_cast<const int*>(arr - 4);
            if (invPos >= count) return nullptr;      // the array is 1-based, element 0 unused
            found = *reinterpret_cast<void* const*>(arr + 8 * static_cast<size_t>(invPos) + 4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return found;
}

// Is this a CASH equip? Equips are 1xxxxxx and the cash flag is `cash` in the item's
// own WZ info. Reading exactly the property the SERVER reads
// (ItemInformationProvider.isCash) is deliberate: a client gate that is looser than
// the server's would let a drop through only for the apply to be refused later.
//
// CItemInfo::GetItemInfo (0x005DA83C) returns the `info` NODE, not the item root.
// It is literally `GetItemRoot(id)->item[stringpool(0x3C1)]`, and the client's own
// callers index its result with `tamingMob` / `bodyRelMove`, both children of info.
// So the lookup here is one level, NOT info/cash: descending into `info` again finds
// nothing and made this return false for EVERY item, which silently refused every drop.
bool IsCashEquip(int itemId) {
    if (itemId / 1000000 != 1) return false;
    auto* pInfo = CItemInfo::GetInstance();
    if (!pInfo) return false;
    try {
        IWzPropertyPtr p = pInfo->GetItemInfo(itemId);   // == the item's `info` node
        if (!p) {
            LOG_ONCE("coloringprism: no WZ info node for item %d", itemId);
            return false;
        }
        return get_int32(p->item[L"cash"], 0) != 0;
    } catch (...) {
        return false;
    }
}

// =====================================================
// LAYOUT
// =====================================================
// These MIRROR Tools/import_coloring_prism.py, and both were MEASURED off the
// hand-drawn backdrop at Tools/art/coloring-prism.png. The three files MUST agree;
// redraw the art -> re-measure -> update both (the same rule
// import_skill_window_art.py:27-28 states for the skill window).
//
// The art bakes the frame, the title, the banner message, the HUE/CHROMA/VALUE row
// labels and the hint text, so Draw() paints only what moves: the item icon, the
// three gradients, the thumbs, the numbers and the buttons.
constexpr int kWndW = 301, kWndH = 406;
constexpr int kTitleH = 23;                          // drag region; white interior y5..18
constexpr int kBtCloseX = 281, kBtCloseY = 5, kBtCloseW = 12, kBtCloseH = 12;

// TABS. These are the Item Inventory's own Basic.img/Tab2 pieces: 4px left and
// right caps with a one-pixel fill column between them. They are 19px high and
// must never be vertically scaled. Four 44px tabs leave a clear gap before the
// right end of the strip.
constexpr int kTabT = 23, kTabH = 19, kTabW = 44;
constexpr int kTabCount = 4;
constexpr int kTabX[kTabCount] = { 6, 50, 94, 138 };
enum Tab { kTabEquip = 0, kTabEffects = 1, kTabHair = 2, kTabEye = 3 };

constexpr int kBannerT = 50, kBannerB = 115;         // blue banner
constexpr int kWellX = 21, kWellY = 59, kWellSize = 48;   // the drop well, measured
constexpr int kIconX = 29, kIconBaseline = 99;       // 32x32 icon centred in it, bottom-left
constexpr int kIconSize = 32;
// Copy occupies only the blue banner section after the icon divider. Keeping this
// explicit prevents a long instruction from running underneath the right bevel.
constexpr int kBannerTextX = 76, kBannerTextW = 213, kBannerTextT = 62;
constexpr int kBannerLineH = 14;
// Hair and Face have no well, so their copy uses the WHOLE blue band. Measured off the
// art: the band's interior runs x8..290, and the well punches a hole through x20..67,
// which is the only reason the ordinary copy starts at 76. Two lines instead of three,
// so the top moves down half a line to keep the block on the same centre (y82).
constexpr int kBannerFullX = 8, kBannerFullW = 283;
constexpr int kBannerLookT = 69;

constexpr int kPreviewT = 121, kPreviewB = 271;      // pane interior (11,121)-(289,271)
// The two thin vertical separators painted inside the preview pane. The avatar's feet
// are clamped far enough inside them that its body never crosses either line.
constexpr int kPreviewSepLeft = 26, kPreviewSepRight = 275;
constexpr int kPreviewAvatarHalfW = 18;

// Three slider rows. Each has a 53x29 PINK LABEL PLATE baked into the art at x9..61 and a
// dithered well to its right; the DLL letters the plate and draws the track into the well.
constexpr int kRowsT = 277;                          // interior top of row 0 (border at 276)
constexpr int kRowStep = 32;                         // measured: borders at 276 / 308 / 340
constexpr int kRowH = 28;                            // interior height
constexpr int kLabelX = 9, kLabelW = 53;             // the baked pink plate
constexpr int kTrackX = 68, kTrackW = 176, kTrackH = 8;
constexpr int kValueX = 248, kValueW = 46;

// Footer interior is y378..401. Basic.img/BtOK2 and BtCancel2 are a matched, direct
// 47x18 vanilla pair, so their baked lettering needs no overpainting. NOTE the left one
// wears Cancel's lettering but RESETS the sliders; the window is closed with the title
// chip. Repoint it at CloseNow if the wording matters more than the affordance.
constexpr int kBtnT = 380, kBtnW = 47, kBtnH = 18;
constexpr int kBtnCount = 2;
constexpr int kBtnX[kBtnCount] = { 12, kWndW - kBtnW - 12 };
constexpr int kBtnCancel = 0;
constexpr int kBtnOK = 1;

// Feet position of the preview avatar inside the pane, window-local.
constexpr int kAvatarX = (11 + 289) / 2;             // 150
constexpr int kAvatarY = kPreviewT + 105;
constexpr int kAvatarScale = 100;

// Movement constants for the preview, matching the client's own 100% walk and jump speed.
constexpr float kWalkPxPerSec = 125.0f, kJumpVel0 = -555.0f;
constexpr float kGravity = 2000.0f, kFallSpeedMax = 670.0f;
constexpr float kWalkAccel = 1400.0f, kWalkDrag = 800.0f, kStopEps = 4.0f;

constexpr int kThumbW = 22, kThumbH = 14;           // UI/Basic.img/Slider/thumb*
constexpr int kTrackTravel = kTrackW - kThumbW;

// The three rows, in draw order.
enum Row { kRowTone = 0, kRowChroma = 1, kRowBright = 2, kRowCount = 3 };
struct RowSpec { const wchar_t* track; int lo; int hi; };
const RowSpec kRows[kRowCount] = {
    { L"UI/ColorPrism.img/trackTone",     0, kTintHueMax },
    { L"UI/ColorPrism.img/trackChroma", kTintDeltaMin, kTintDeltaMax },
    { L"UI/ColorPrism.img/trackBright", kTintDeltaMin, kTintDeltaMax },
};

// HOW THE PREVIEW AVATAR IS POSED.
//
// CAvatar::Init's action argument is NOT an index into the client's 162-entry action-name
// table. It is a PACKED MOVE ACTION: (moveAction << 1) | facingBit, stored raw at
// CAvatar+0x4E8 (0x004514F5) and unpacked at 0x00451EC8 -- `and edx,1` for the facing
// flag, then `sar eax,1` for the move action.
//
// Only move actions 1..10 exist. 0x00451F81 does `lea eax,[edi-1]; cmp eax,9; ja ...`
// and jumps through the table at 0x0045207C; ANYTHING out of range falls to
// 0x00451FFD `xor eax,eax`, which is action code 0 = walk1. Resolving WZ action NAMES
// to raw action codes (stand1=2, prone=33, sit=39, ...) and passing those is therefore a
// silent failure: Init halves them, almost everything lands out of range, and every one
// renders a walking frame. Pass the move action, never the code.
//
// The mapping, read straight off that jump table:
//   1 -> walk1/walk2   2 -> stand1/stand2   3 -> jump    4 -> alert   5 -> prone
//   6 -> fly           7 -> ladder          8 -> rope    9 -> dead   10 -> sit
// walk2 / stand2 are the client's own choice from CAvatar+0x468 / +0x46C, not ours.
//
// ATTACK POSES take the other route. Anything outside the ten move actions is reached by
// writing the ACTION CODE OVERRIDE at CAvatar+0x4EC, which CAvatar::GetActionCode
// (0x00451E4C) prefers over the move action whenever it is > -1:
//     mov eax,[esi+0x4E8]; call 0x451EC8   ; move action -> code, kept in edi
//     mov ecx,esi; call 0x451B6A           ; read the override
//     cmp eax,-1; jle -> use edi           ; override wins unless it is -1
// The setter is CAvatar::SetActionCode @0x004571AB, __thiscall, one int arg (`ret 4`):
//     or [esi+0x4EC],-1  /  ClearActionLayer(1)  /  mov [esi+0x4EC],arg
//     /  call [vtbl+0x14] = PrepareActionLayer(6,100,0)
// It rebuilds the layers itself, so nothing else is needed -- no Update tick, no rebuild.
// It must run AFTER Init, because Init issues its own SetMoveAction.
//
// The facing bit is set on every move action, and both pose paths pack it inline: that
// is what the bare `(2 << 1) | facing` at the BuildAvatar call site is.
//
// An action code, where one is passed instead, is an index into the client's 162-entry
// action table. That table is a CRT static initializer built from the executable's own
// encrypted string pool, NOT from the WZ, so its order is baked into this binary and no
// asset import can perturb it. That is what makes hardcoding one safe.
constexpr int kNoActionCode = -1;

// CAvatar::SetActionCode: see the block above. If a code ever needs converting back to a
// WZ action name, use get_action_name_from_code (0x004A8CE6, caller-owned out-param). Do
// NOT use the reverse lookup at 0x004A8D14: it compares WIDE strings (0x00402F0E is
// `mov edx,[eax-4]; shr edx,1`, 0x00402F22 is `mov bx, word ptr [eax]`) and it CONSUMES
// its by-value argument (0x004A8D41 releases the buffer), so a temporary is released twice.
using t_SetActionCode = void(__thiscall*)(void*, int);
auto CAvatar_SetActionCode = reinterpret_cast<t_SetActionCode>(0x004571AB);
using t_SetMoveAction = void(__thiscall*)(void*, int, int);
using t_CAvatarUpdate = void(__thiscall*)(void*);
auto CAvatar_SetMoveAction = reinterpret_cast<t_SetMoveAction>(0x004520F1);
auto CAvatar_Update = reinterpret_cast<t_CAvatarUpdate>(0x004522A6);
constexpr size_t kOff_AvatarPosVector = 0x10AC;
constexpr size_t kOff_ActionCodeOverride = 0x4EC;

// Remembered across opens, like the bag window's position.
bool s_bSavedPos = false;
int  s_savedX = 0, s_savedY = 0;

// The inventory position the prism was double-clicked from. The server treats it
// as a hint and re-verifies the item there, so a stale value costs nothing.
int  s_prismPos = 0;

// Which tab the window opens on, set by whichever prism was double-clicked. Remembered
// across opens so reopening returns you where you were.
int  s_tab = kTabEquip;

// Playground leaves are defined after the class with the other SEH wrappers.
void SehSetMoveAction(void* pAvatar, int moveAction);
void SehSetActionCode(void* pAvatar, int actionCode);
int  SehReadActionCode(void* pAvatar);
void SehUpdateAvatar(void* pAvatar);
void MoveAvatar(void* pAvatar, int x, int y);

// Match the preview's controls to the player's actual key bindings.
constexpr uintptr_t kAddr_FuncKeyMappedMan = 0x00BED5A0;
constexpr int kFuncKeyCount = 89, kFKType_BasicAction = 5;
constexpr int kFKAction_Attack = 52, kFKAction_Jump = 53;
bool FuncKeyIsAction(int scan, int wantId) {
    if (scan == 0x36) scan = 0x2A;
    if (scan < 0 || scan >= kFuncKeyCount) return false;
    __try {
        void* map = *reinterpret_cast<void**>(kAddr_FuncKeyMappedMan);
        if (!map) return false;
        const char* entry = reinterpret_cast<const char*>(map) + 4 + scan * 5;
        if (*entry != static_cast<char>(kFKType_BasicAction)) return false;
        int id = 0; memcpy(&id, entry + 1, sizeof(id));
        return id == wantId;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// =====================================================
// THE WINDOW
// =====================================================
class CUIColorPrism : public CWnd {
public:
    ZALLOC_GLOBAL                                    // MANDATORY: the engine frees us
    inline static CUIColorPrism* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{ nullptr };

    int  m_screenX, m_screenY;
    int  m_tab;                                      // kTabEquip / Effects / Hair / Face
    WeaponTintTarget m_target;                       // the item being dyed (0 = none yet)
    WeaponTint m_tint;                               // live slider values

    // Preview playground: the avatar walks, jumps and swings under the player's own keys.
    int  m_bFocused;
    bool m_keyL, m_keyR, m_keyD;
    float m_avX, m_avY, m_velX, m_velY;
    bool m_bAirborne, m_bFacingLeft, m_bAttackReq;
    DWORD m_tLastStep;
    int m_nLastMA, m_nLastCode;

    // title-bar drag
    int  m_bDragging, m_nDragAnchorX, m_nDragAnchorY;
    // slider drag: which row, and the grab offset inside the thumb
    int  m_sliderDrag, m_sliderGrabDX;
    // The numeric readout doubles as a compact editable field. It is code-drawn so
    // it shares the slider row's existing art and needs no stock CCtrlEdit child.
    int  m_editRow;                                 // kRow*, or -1 when not typing
    char m_editText[8];                             // "359" / "+100" / "-100"
    // button states
    int  m_nBtnPressed, m_nBtnHover;                 // index into kBtnX, or -1
    int  m_nClosePressed, m_nCloseHover;
    void* m_pCurrentStage;                           // close when the stage changes
    unsigned int m_avatarRef[2];                     // ZRef<CAvatar> storage
    void* m_pAvatar;
    DWORD m_nAvatarDirtyTick;                        // throttle the preview rebuild
    bool  m_bAvatarDirty;

    IWzFontPtr m_pFont;                              // basic UI font
    IWzFontPtr m_pFontDk;                            // Dotum 12 dark -- readouts, item name
    IWzFontPtr m_pFontLt;                            // Dotum 12 WHITE -- banner + pink plates
    IWzFontPtr m_pFontTitle;                         // Dotum 12 black -- window title

    IWzCanvasPtr m_pBg;
    // Hair / Face backdrop: the same frame without the drop well. Optional; if the art
    // is absent this stays null and the ordinary backdrop is used for every tab.
    IWzCanvasPtr m_pBgLook;
    IWzCanvasPtr m_pTrack[kRowCount];
    IWzCanvasPtr m_pThumb[3];                        // normal, pressed, mouseOver
    IWzCanvasPtr m_pBtOK[3];                       // Basic.img/BtOK2: normal, pressed, hover
    IWzCanvasPtr m_pBtCancel[3];                   // Basic.img/BtCancel2: matched vanilla cancel
    // UI/Basic.img/Tab2, indexed by [selected]. This is the vanilla inventory
    // tab chrome; the names are drawn separately because its baked labels are
    // specifically Equip / Use / Set-up / Etc / Cash.
    IWzCanvasPtr m_pTabLeft[2], m_pTabFill[2], m_pTabRight[2];
    IWzCanvasPtr m_pBtClose[2];

    CUIColorPrism(int nLeft, int nTop);
    virtual ~CUIColorPrism() override {
        ReleaseAvatar();
        if (ms_pInstance == this) ms_pInstance = nullptr;
    }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int  OnMouseMove(int rx, int ry) override;
    virtual int  OnMouseWheel(int, int, int) override { return 1; }
    virtual void OnMouseEnter(int bEnter) override {
        CWnd::OnMouseEnter(bEnter);
        if (!bEnter) { m_nBtnHover = -1; m_nCloseHover = 0; }
    }
    virtual void OnDestroy() override;
    virtual void Update() override;
    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }
    virtual int OnSetFocus(int bFocus) override {
        m_bFocused = bFocus;
        if (!bFocus) m_keyL = m_keyR = m_keyD = false;
        return 1; // keep field input on the preview while this window holds focus
    }
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        if ((lParam & 0x80000000u) == 0 && m_editRow >= 0 && HandleValueKey(wParam)) return;
        const bool up = (lParam & 0x80000000u) != 0;
        switch (wParam) {
            case VK_LEFT:  m_keyL = !up; return;
            case VK_RIGHT: m_keyR = !up; return;
            case VK_DOWN:  m_keyD = !up; return;
            default: break;
        }
        const int scan = static_cast<int>((lParam >> 16) & 0xFF);
        const bool jump = FuncKeyIsAction(scan, kFKAction_Jump);
        const bool attack = FuncKeyIsAction(scan, kFKAction_Attack);
        if (jump || attack) {
            if (!up && !m_bAirborne) {
                if (jump) { m_bAirborne = true; m_velY = kJumpVel0; }
                else m_bAttackReq = true;
            }
            return;
        }
        // Stock-key fallback, for a key map the player has never rebound.
        if (wParam == VK_SPACE || wParam == VK_MENU || wParam == VK_LMENU) {
            if (!up && !m_bAirborne) { m_bAirborne = true; m_velY = kJumpVel0; }
            return;
        }
        if (wParam == VK_CONTROL || wParam == VK_LCONTROL) {
            if (!up && !m_bAirborne) m_bAttackReq = true;
            return;
        }
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);
        if (ctx) reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                     kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
    }

    static IWzCanvasPtr LoadSprite(const wchar_t* p) {
        IWzCanvasPtr c;
        try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p), vtEmpty, vtEmpty)); } catch (...) {}
        return c;
    }
    static void BlitAt(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    static void BlitA(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    void DrawInventoryTab(IWzCanvasPtr dst, int x, int y, int width, bool selected) const;
    void LoadSprites();

    // --- what this tab dyes -------------------------------------------------
    // The Equip and Effects tabs dye the item on the well, under two DIFFERENT keys, so a
    // glow can be recoloured independently of the item it hangs off. Hair and Eye need no
    // target at all -- the server reads the character's own look.
    //
    // "Eye" rather than "Face" because that is what it actually changes: the tint keys off
    // the FACE img (which is where v83 stores eye colour), but weapontint.cpp masks the walk
    // down to the IRIS pixels only, derived by diffing the style's colour siblings. Eyebrows,
    // lashes and the mouth keep their own colours.
    bool NeedsDrop() const { return m_tab == kTabEquip || m_tab == kTabEffects; }
    // Hair and Face take the LOOK actions rather than apply/restore: they have no
    // inventory address for the server to re-verify, so it reads the character's own look.
    bool IsLookTab() const { return m_tab == kTabHair || m_tab == kTabEye; }

    int TargetKey() const {
        switch (m_tab) {
            case kTabEquip:   return m_target.itemId;
            case kTabEffects: return m_target.itemId ? EffectTintKeyFor(m_target.itemId) : 0;
            case kTabHair:    return kTintKey_Hair;
            default:          return kTintKey_Face;   // kTabEye -- the FACE img is where an
                                                      // eye colour lives; only its iris
                                                      // pixels are actually recoloured.
        }
    }
    // Is there something for Confirm to act on?
    bool Ready() const { return !NeedsDrop() || m_target.IsSet(); }

    void SetTab(int tab) {
        CommitValueEdit();
        if (tab == m_tab || tab < 0 || tab >= kTabCount) return;
        // Drop the outgoing tab's preview before switching, or a colour tried on Hair
        // would stay on the character while the sliders drove something else.
        DropPreview();
        m_tab = tab;
        s_tab = tab;
        m_tint = WeaponTint_GetSavedFor(TargetKey());
        m_bAvatarDirty = true;
        play_ui_sound(L"BtMouseClick");
        InvalidateRect(nullptr);
    }

    // Every key this window can be driving, so leaving or switching clears them all --
    // the player may have dragged Hair, moved to Equip and then closed.
    void DropPreview() {
        WeaponTint_SetPreview(kTintKey_Hair, WeaponTint{}, false);
        WeaponTint_SetPreview(kTintKey_Face, WeaponTint{}, false);
        if (m_target.itemId) {
            WeaponTint_SetPreview(m_target.itemId, WeaponTint{}, false);
            WeaponTint_SetPreview(EffectTintKeyFor(m_target.itemId), WeaponTint{}, false);
        }
    }

    void ApplyPlayPose(int moveAction, int actionCode) {
        if (!m_pAvatar) return;
        const int ma = (moveAction << 1) | (m_bFacingLeft ? 1 : 0);
        if (ma == m_nLastMA && actionCode == m_nLastCode) return;
        WeaponTint_BeginForcedScope();
        if (ma != m_nLastMA) { SehSetMoveAction(m_pAvatar, ma); m_nLastMA = ma; }
        if (actionCode != m_nLastCode) { SehSetActionCode(m_pAvatar, actionCode); m_nLastCode = actionCode; }
        WeaponTint_EndForcedScope();
    }
    void StepPlayground(DWORD now) {
        if (!m_pAvatar) return;
        DWORD dt = now - m_tLastStep;
        m_tLastStep = now;
        if (dt == 0) return;
        if (dt > 100) dt = 100;
        const float s = static_cast<float>(dt) / 1000.0f;
        const bool attacking = m_bAttackReq || SehReadActionCode(m_pAvatar) != kNoActionCode;
        int dir = 0;
        if (!attacking) {
            if (m_keyL && !m_keyR) dir = -1;
            else if (m_keyR && !m_keyL) dir = 1;
        }
        if (dir) {
            m_bFacingLeft = dir < 0;
            m_velX += dir * kWalkAccel * s;
            if (m_velX > kWalkPxPerSec) m_velX = kWalkPxPerSec;
            if (m_velX < -kWalkPxPerSec) m_velX = -kWalkPxPerSec;
        } else if (!m_bAirborne) {
            const float drag = kWalkDrag * s;
            if (m_velX > 0.0f) m_velX = (m_velX > drag) ? m_velX - drag : 0.0f;
            else if (m_velX < 0.0f) m_velX = (m_velX < -drag) ? m_velX + drag : 0.0f;
        }
        m_avX += m_velX * s;
        const float lo = static_cast<float>(kPreviewSepLeft + kPreviewAvatarHalfW);
        const float hi = static_cast<float>(kPreviewSepRight - kPreviewAvatarHalfW);
        if (m_avX < lo) { m_avX = lo; m_velX = 0.0f; }
        if (m_avX > hi) { m_avX = hi; m_velX = 0.0f; }
        if (m_bAirborne) {
            m_velY += kGravity * s;
            if (m_velY > kFallSpeedMax) m_velY = kFallSpeedMax;
            m_avY += m_velY * s;
            if (m_avY >= static_cast<float>(kAvatarY)) {
                m_avY = static_cast<float>(kAvatarY); m_velY = 0.0f; m_bAirborne = false;
            }
        }
        const bool moving = m_velX > kStopEps || m_velX < -kStopEps;
        if (m_bAirborne) ApplyPlayPose(3, kNoActionCode);
        else if (attacking) ApplyPlayPose(2, 5); // swingO1, the preview's attack pose
        else if (moving) ApplyPlayPose(1, kNoActionCode);
        else if (m_keyD) ApplyPlayPose(5, kNoActionCode);
        else ApplyPlayPose(2, kNoActionCode);
        m_bAttackReq = false;
        MoveAvatar(m_pAvatar, static_cast<int>(m_avX), static_cast<int>(m_avY));
    }

    // --- slider maths -------------------------------------------------------
    int Value(int row) const {
        switch (row) {
            case kRowTone:   return m_tint.hue;
            case kRowChroma: return m_tint.chroma;
            default:         return m_tint.bright;
        }
    }
    void SetValue(int row, int v) {
        const RowSpec& r = kRows[row];
        if (v < r.lo) v = r.lo;
        if (v > r.hi) v = r.hi;
        if (v == Value(row)) return;
        switch (row) {
            case kRowTone:   m_tint.hue    = static_cast<short>(v); break;
            case kRowChroma: m_tint.chroma = static_cast<signed char>(v); break;
            default:         m_tint.bright = static_cast<signed char>(v); break;
        }
        PushPreview();
    }
    static int RowTop(int row) { return kRowsT + row * kRowStep; }
    int ThumbX(int row) const {
        const RowSpec& r = kRows[row];
        const int span = r.hi - r.lo;
        if (span <= 0) return kTrackX;
        return kTrackX + (Value(row) - r.lo) * kTrackTravel / span;
    }
    void ThumbRect(int row, RECT& rc) const {
        const int x = ThumbX(row), y = RowTop(row) + (kRowH - kThumbH) / 2;
        rc = { x, y, x + kThumbW, y + kThumbH };
    }
    // Window-local x -> value, using the thumb's LEFT edge (matches ThumbX).
    void SetValueFromX(int row, int x) {
        const RowSpec& r = kRows[row];
        int v = r.lo;
        if (kTrackTravel > 0) {
            v = r.lo + (x - kTrackX) * (r.hi - r.lo) / kTrackTravel;
        }
        SetValue(row, v);
    }

    // --- direct numeric entry ----------------------------------------------
    void BeginValueEdit(int row) {
        if (row < 0 || row >= kRowCount) return;
        m_editRow = row;
        // Treat the click as select-all. A blank caret lets the player type "270"
        // immediately instead of having to erase the old slider value first.
        m_editText[0] = 0;
        InvalidateRect(nullptr);
    }
    void CancelValueEdit() {
        if (m_editRow < 0) return;
        m_editRow = -1;
        m_editText[0] = 0;
        InvalidateRect(nullptr);
    }
    void CommitValueEdit() {
        if (m_editRow < 0) return;
        const int row = m_editRow;
        const char* p = m_editText;
        int sign = 1;
        if (*p == '+' || *p == '-') { sign = (*p == '-') ? -1 : 1; ++p; }
        int value = 0;
        bool hasDigit = false;
        while (*p >= '0' && *p <= '9') {
            hasDigit = true;
            value = value * 10 + (*p++ - '0');
        }
        m_editRow = -1;
        m_editText[0] = 0;
        if (hasDigit) SetValue(row, sign * value);  // SetValue clamps to the row range.
        InvalidateRect(nullptr);                     // also erases a caret on a no-op value.
    }
    bool HandleValueKey(unsigned int key) {
        if (key == VK_RETURN) { CommitValueEdit(); play_ui_sound(L"BtMouseClick"); return true; }
        if (key == VK_ESCAPE) { CancelValueEdit(); play_ui_sound(L"MenuDown"); return true; }
        const size_t len = strlen(m_editText);
        if (key == VK_BACK) {
            if (len) m_editText[len - 1] = 0;
            InvalidateRect(nullptr);
            return true;
        }
        int digit = -1;
        if (key >= '0' && key <= '9') digit = static_cast<int>(key - '0');
        else if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) digit = static_cast<int>(key - VK_NUMPAD0);
        if (digit >= 0) {
            if (len + 1 < _countof(m_editText)) {
                m_editText[len] = static_cast<char>('0' + digit);
                m_editText[len + 1] = 0;
                InvalidateRect(nullptr);
            }
            return true;
        }
        if (m_editRow != kRowTone && len == 0 &&
            (key == VK_OEM_PLUS || key == VK_ADD || key == VK_OEM_MINUS || key == VK_SUBTRACT)) {
            m_editText[0] = (key == VK_OEM_MINUS || key == VK_SUBTRACT) ? '-' : '+';
            m_editText[1] = 0;
            InvalidateRect(nullptr);
            return true;
        }
        if (key == VK_OEM_PLUS || key == VK_ADD || key == VK_OEM_MINUS || key == VK_SUBTRACT) return true;
        return false;
    }

    // --- preview ------------------------------------------------------------
    // The tint drives BOTH the pane avatar and the one in the world, so the player
    // judges the colour on their character rather than on a swatch.
    //
    // Only marks dirty: Update() does the work on a throttle. Applying it here
    // would re-tint every one of the weapon's canvases and reload the world
    // avatar's action layers on EVERY mouse-move step of a drag. The numeric
    // readouts and the thumb still track the mouse exactly, because Draw() reads
    // m_tint directly.
    void PushPreview() {
        m_bAvatarDirty = true;
        InvalidateRect(nullptr);
    }
    void BuildAvatar();
    void ReleaseAvatar();

    void SendConfirm();
    void CloseNow();
    void ResetValues() {
        m_tint = WeaponTint{};
        PushPreview();
    }

    // Is the cursor inside the drop WELL? Padded outward, because a dragged icon is
    // held by the point it was grabbed at rather than by its centre, so the cursor
    // sits a few pixels off from where the player thinks the icon is.
    bool CursorOverWell() const {
        POINT sp;
        if (!GetAbsCursor(sp)) return false;
        const int x = sp.x - m_screenX, y = sp.y - m_screenY;
        constexpr int kPad = 4;
        return x >= kIconX - kPad && x < kIconX + kIconSize + kPad
            && y >= kIconBaseline - kIconSize - kPad && y < kIconBaseline + kPad;
    }

    // Adopt a dropped item as the one being dyed. Returns false (with a refusal
    // sound) for anything the prism cannot colour, so a mis-drop is obvious.
    bool SetTarget(int invType, int invPos) {
        CommitValueEdit();
        const int itemId = SehDecodeItemId(SehItemAt(invType, invPos));
        if (itemId <= 0) {
            // The drop landed on the well but nothing could be read from it. Worth a
            // line, because the only way this happens is a container offset being
            // wrong for whichever window the drag started in.
            LOG_ONCE("coloringprism: drop on well resolved to no item (invType=%d invPos=%d)",
                     invType, invPos);
            return false;
        }
        if (!IsCashEquip(itemId)) {
            // Refused. This used to be a bare sound, which is why a gate that rejected
            // EVERYTHING looked exactly like a drop that never arrived -- so say which
            // item was turned away, once.
            LOG_ONCE("coloringprism: refused item %d (not a cash equip)", itemId);
            play_ui_sound(L"BtMouseClick");
            return false;
        }
        if (m_target.IsSet() && m_target.itemId != itemId) {
            // Swapping targets mid-session: drop the outgoing item's preview so it
            // does not keep the colour we were trying on it.
            WeaponTint_SetPreview(m_target.itemId, WeaponTint{}, false);
        }
        m_target.invType = invType;
        m_target.invPos  = invPos;
        m_target.itemId  = itemId;
        m_tint = WeaponTint_GetSavedFor(TargetKey());   // start from its stored colour
        m_bAvatarDirty = true;
        play_ui_sound(L"DragEnd");
        InvalidateRect(nullptr);
        return true;
    }
};

// ---------------------------------------------------------------------------
// SEH leaves. C2712 forbids __try in any function that also holds a C++ object
// needing unwinding, so every raw-memory / engine call gets its own tiny wrapper.
// ---------------------------------------------------------------------------
void* SehCharacterData() {
    void* charData = nullptr;
    __try {
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext);
        if (ctx) charData = *reinterpret_cast<void**>(
                     reinterpret_cast<char*>(ctx) + kOff_CharacterDataInCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        charData = nullptr;
    }
    return charData;
}

bool SehBuildAvatar(unsigned int* pRef, void* charData, IWzGr2DLayer* pLayer,
                    int x, int y, int previewItemId, int action, int actionCode,
                    void** ppAvatarOut) {
    *ppAvatarOut = nullptr;
    __try {
        void* pAvatar = ZRefCAvatar_Alloc(pRef);
        if (!pAvatar) return false;
        // CAvatar may finish constructing its face after Init returns. Bind this
        // exact preview avatar now so that deferred work still sees the slider tint;
        // ReleaseAvatar unbinds it before the engine can reuse the allocation.
        WeaponTint_BindPreviewAvatar(pAvatar);
        *ppAvatarOut = pAvatar; // also lets the SEH cleanup unbind the allocation
        unsigned char buf[0x210];
        memset(buf, 0, sizeof(buf));
        AvatarLook_ctor(buf, charData);
        // Show the item being dyed even when it is sitting in the inventory rather
        // than worn -- otherwise the pane previews a colour you cannot see.
        if (previewItemId > 0) {
            const int slot = AvatarSlotOf(previewItemId);
            if (slot == kSlotWeaponSticker) {
                *reinterpret_cast<int*>(buf + kAL_WeaponSticker) = previewItemId;
            } else if (slot > 0) {
                *reinterpret_cast<int*>(buf + kAL_HairEquip0 + 4 * slot) = previewItemId;
            }
        }
        // CAvatar::Init consumes two refs on the layer.
        // Scope the preview to this exact avatar. A global "preview build" flag
        // leaks the live slider tint to the world avatar if it rebuilds reentrantly.
        pLayer->AddRef();
        pLayer->AddRef();
        WeaponTint_BeginForcedScope(pAvatar);
        CAvatar_Init(pAvatar, buf, action, pLayer, pLayer,
                     1, x, y, kAvatarScale, 0);
        WeaponTint_EndForcedScope();
        // AFTER Init, never before: Init issues its own SetMoveAction, which would rebuild
        // over the override. SetActionCode clears the layer cache and re-prepares by itself.
        if (actionCode != kNoActionCode) CAvatar_SetActionCode(pAvatar, actionCode);
        *ppAvatarOut = pAvatar;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WeaponTint_EndForcedScope();
        WeaponTint_UnbindPreviewAvatar(*ppAvatarOut);
        *ppAvatarOut = nullptr;
        return false;
    }
    return true;
}

void SehReleaseAvatar(unsigned int* pRef) {
    __try {
        if (pRef[1]) ZRef_Release(pRef, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void SehSetMoveAction(void* pAvatar, int moveAction) {
    if (!pAvatar) return;
    __try { CAvatar_SetMoveAction(pAvatar, moveAction, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void SehSetActionCode(void* pAvatar, int actionCode) {
    if (!pAvatar) return;
    __try { CAvatar_SetActionCode(pAvatar, actionCode); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

int SehReadActionCode(void* pAvatar) {
    int value = kNoActionCode;
    if (!pAvatar) return value;
    __try { value = *reinterpret_cast<int*>(reinterpret_cast<char*>(pAvatar) + kOff_ActionCodeOverride); }
    __except (EXCEPTION_EXECUTE_HANDLER) { value = kNoActionCode; }
    return value;
}

void SehUpdateAvatar(void* pAvatar) {
    if (!pAvatar) return;
    __try { CAvatar_Update(pAvatar); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void MoveAvatar(void* pAvatar, int x, int y) {
    if (!pAvatar) return;
    try {
        IWzVector2D* pos = *reinterpret_cast<IWzVector2D**>(
            reinterpret_cast<char*>(pAvatar) + kOff_AvatarPosVector);
        if (pos) pos->RelMove(x, y, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {}
}

void RestoreFocusToWndMan() {
    try {
        void* wndMan = *reinterpret_cast<void**>(0x00BEC20C);
        if (wndMan) reinterpret_cast<void(__thiscall*)(void*, void*)>(0x009E3264)(
            wndMan, reinterpret_cast<char*>(wndMan) + 4);
    } catch (...) {}
}

// CreateWnd's bSetFocus flag is advisory for custom dialogs on this client build: a
// previously focused inventory child can keep it. A window built through the client's
// stock dialog path gets focus for free; the prism is custom, so explicitly hand
// CWndMan this window.
void FocusCustomWindow(CWnd* wnd) {
    if (!wnd) return;
    try {
        void* wndMan = *reinterpret_cast<void**>(0x00BEC20C);
        if (wndMan) reinterpret_cast<void(__thiscall*)(void*, void*)>(0x009E3264)(wndMan, wnd);
    } catch (...) {}
}

// --- text -------------------------------------------------------------------
// The backdrop bakes no text, so these three are used for every string in the window.
// Each is individually guarded: DrawTextA reaches the engine's font machinery and a
// throw there must cost one label, never the rest of the frame.
int TextWidth(IWzFont* pFont, const char* s) {
    if (!pFont || !s) return 0;
    try { return static_cast<int>(pFont->CalcTextWidth(Ztl_bstr_t(s), Ztl_variant_t())); }
    catch (...) { return static_cast<int>(strlen(s)) * 6; }
}

void DrawText(IWzCanvasPtr pCanvas, IWzFont* pFont, int x, int y, const char* s) {
    if (!pCanvas || !pFont || !s || !*s) return;
    try {
        pCanvas->DrawTextA(x, y, Ztl_bstr_t(s), pFont, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {}
}

void DrawTextCentred(IWzCanvasPtr pCanvas, IWzFont* pFont, int x, int w, int y, const char* s) {
    DrawText(pCanvas, pFont, x + (w - TextWidth(pFont, s)) / 2, y, s);
}

void DrawTextRight(IWzCanvasPtr pCanvas, IWzFont* pFont, int right, int y, const char* s) {
    DrawText(pCanvas, pFont, right - TextWidth(pFont, s), y, s);
}

// The display name of an item, out of String.wz.
//
// Two shapes to cover. Cash.img is flat (`Cash.img/<id>/name`), but Eqp.img nests one
// level under a CATEGORY (`Eqp.img/Eqp/<Category>/<id>/name`). The categories are a
// fixed, closed list in v83, so they are tried by name rather than enumerated: an
// IEnumVARIANT walk here would be more code and would have to be collect-first to be
// safe (weapontint.cpp's ChildNames exists for exactly that reason), for no gain.
bool ItemNameOf(int itemId, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = '\0';
    wchar_t key[24];
    _snwprintf_s(key, _countof(key), _TRUNCATE, L"%d", itemId);

    auto take = [&](IWzPropertyPtr pNode) -> bool {
        if (!pNode) return false;
        Ztl_variant_t vName = pNode->item[L"name"];
        if (V_VT(&vName) == VT_BSTR && V_BSTR(&vName)) {
            int n = WideCharToMultiByte(CP_ACP, 0, V_BSTR(&vName), -1, out, (int)cap, nullptr, nullptr);
            return n > 1;
        }
        return false;
    };

    // Cash first: this window dyes Cash items, so it hits on the first try in the
    // overwhelming majority of cases.
    try {
        IWzPropertyPtr pCash = get_rm()->GetObjectA(L"String/Cash.img", vtEmpty, vtEmpty).GetUnknown();
        if (pCash && take(IWzPropertyPtr(pCash->item[key].GetUnknown()))) return true;
    } catch (...) {}

    static const wchar_t* kEqpCategories[] = {
        L"Cap", L"Accessory", L"Coat", L"Longcoat", L"Pants", L"Shoes", L"Glove",
        L"Shield", L"Cape", L"Ring", L"Weapon", L"PetEquip", L"Taming", L"Mechanic",
    };
    try {
        IWzPropertyPtr pEqp = get_rm()->GetObjectA(L"String/Eqp.img", vtEmpty, vtEmpty).GetUnknown();
        if (pEqp) {
            IWzPropertyPtr pRoot = pEqp->item[L"Eqp"].GetUnknown();
            if (pRoot) {
                for (const wchar_t* cat : kEqpCategories) {
                    IWzPropertyPtr pCat = pRoot->item[cat].GetUnknown();
                    if (!pCat) continue;
                    if (take(IWzPropertyPtr(pCat->item[key].GetUnknown()))) return true;
                }
            }
        }
    } catch (...) {}
    return false;
}

// A 12px Dotum face in one colour. Returns null on failure and every caller checks,
// so a font the engine refuses simply means that one string is not drawn.
IWzFontPtr MakeFont(unsigned long colour) {
    IWzFontPtr f;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", f, nullptr);
        if (f) {
            HRESULT hr = reinterpret_cast<HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long,
                unsigned long, const Ztl_variant_t&)>(kAddr_SetFont)(
                f, L"Dotum", 12, colour, Ztl_variant_t(L""));
            if (FAILED(hr)) f = nullptr;
        }
    } catch (...) { f = nullptr; }
    return f;
}

void* SehCurrentStage() {
    void* s = nullptr;
    __try { s = *reinterpret_cast<void**>(kAddr_CurrentStage); }
    __except (EXCEPTION_EXECUTE_HANDLER) { s = nullptr; }
    return s;
}

// ---------------------------------------------------------------------------
// The item inventory builds its tab strip from this exact three-piece set. The fill
// source is one pixel wide, so CopyEx can stretch it without softening the native
// bevel. Keep the caps flush with the requested width: this same rectangle is used
// by the click hit test below.
void CUIColorPrism::DrawInventoryTab(IWzCanvasPtr dst, int x, int y, int width,
                                     bool selected) const {
    const int state = selected ? 1 : 0;
    IWzCanvasPtr left = m_pTabLeft[state];
    IWzCanvasPtr fill = m_pTabFill[state];
    IWzCanvasPtr right = m_pTabRight[state];
    if (!left || !fill || !right || width < 9) return;

    int leftW = 4, rightW = 4;
    try { leftW = static_cast<int>(left->width); } catch (...) {}
    try { rightW = static_cast<int>(right->width); } catch (...) {}
    const int fillW = width - leftW - rightW;
    if (fillW < 1) return;

    BlitA(dst, left, x, y);
    if (dst) {
        try {
            dst->CopyEx(x + leftW, y, fill, CANVAS_ALPHATYPE::CA_OVERWRITE,
                        fillW, kTabH, 0, 0, 0, 0);
        } catch (...) {}
    }
    BlitA(dst, right, x + width - rightW, y);
}

// ---------------------------------------------------------------------------
void CUIColorPrism::LoadSprites() {
    m_pBg = LoadSprite(L"UI/ColorPrism.img/backgrnd");
    m_pBgLook = LoadSprite(L"UI/ColorPrism.img/backgrndLook");
    for (int i = 0; i < kRowCount; ++i) m_pTrack[i] = LoadSprite(kRows[i].track);

    m_pThumb[0] = LoadSprite(L"UI/Basic.img/Slider/thumbNormal");
    m_pThumb[1] = LoadSprite(L"UI/Basic.img/Slider/thumbPressed");
    m_pThumb[2] = LoadSprite(L"UI/Basic.img/Slider/thumbMouseOver");

    // Direct vanilla footer buttons. Both already contain the correct stock lettering,
    // unlike donor buttons such as Shop/BtBuy whose text would have to be painted over.
    const wchar_t* kStates[3] = { L"normal", L"pressed", L"mouseOver" };
    for (int i = 0; i < 3; ++i) {
        wchar_t p[64];
        _snwprintf_s(p, _countof(p), _TRUNCATE, L"UI/Basic.img/BtOK2/%s/0", kStates[i]);
        m_pBtOK[i] = LoadSprite(p);
        _snwprintf_s(p, _countof(p), _TRUNCATE, L"UI/Basic.img/BtCancel2/%s/0", kStates[i]);
        m_pBtCancel[i] = LoadSprite(p);
    }
    m_pTabLeft[0]  = LoadSprite(L"UI/Basic.img/Tab2/left0");
    m_pTabLeft[1]  = LoadSprite(L"UI/Basic.img/Tab2/left1");
    m_pTabFill[0]  = LoadSprite(L"UI/Basic.img/Tab2/fill0");
    m_pTabFill[1]  = LoadSprite(L"UI/Basic.img/Tab2/fill1");
    m_pTabRight[0] = LoadSprite(L"UI/Basic.img/Tab2/right0");
    m_pTabRight[1] = LoadSprite(L"UI/Basic.img/Tab2/right1");
    m_pBtClose[0] = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/normal/0");
    m_pBtClose[1] = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/mouseOver/0");
}

CUIColorPrism::CUIColorPrism(int nLeft, int nTop)
    : m_screenX(nLeft), m_screenY(nTop), m_tab(s_tab),
      m_bFocused(0), m_keyL(false), m_keyR(false), m_keyD(false),
      m_avX(static_cast<float>(kAvatarX)), m_avY(static_cast<float>(kAvatarY)),
      m_velX(0.0f), m_velY(0.0f), m_bAirborne(false), m_bFacingLeft(false),
      m_bAttackReq(false), m_tLastStep(GetTickCount()), m_nLastMA(-1), m_nLastCode(-2),
      m_bDragging(0), m_nDragAnchorX(0), m_nDragAnchorY(0),
      m_sliderDrag(-1), m_sliderGrabDX(0),
      m_editRow(-1),
      m_nBtnPressed(-1), m_nBtnHover(-1), m_nClosePressed(0), m_nCloseHover(0),
      m_pCurrentStage(nullptr), m_pAvatar(nullptr),
      m_nAvatarDirtyTick(0), m_bAvatarDirty(true) {
    m_avatarRef[0] = m_avatarRef[1] = 0;
    m_editText[0] = 0;
    ms_pInstance = this;

    // No target until the player drops one on the well. Defaulting to the worn Cash
    // weapon would silently dye the wrong thing the moment anything else is worn. The
    // The Hair and Eye tabs need no target at all, so they are usable immediately.
    m_target = WeaponTintTarget{};
    m_tint = WeaponTint_GetSavedFor(TargetKey());

    LoadSprites();
    // Focus immediately so arrow keys and attack/jump controls drive the preview,
    // not the field player.
    CreateWnd(nLeft, nTop, kWndW, kWndH, 10, 1, nullptr, 1);
    FocusCustomWindow(this);
    play_ui_sound(L"MenuUp");

    try { get_basic_font(std::addressof(m_pFont), 0); } catch (...) {}
    // EVERY piece of text in this window is drawn here: the backdrop bakes none of it --
    // not the title, not the banner message, not the HUE/CHROMA/VALUE labels. That is the
    // whole point of the new art, and it is why three faces are needed rather than one.
    //
    // All three are Dotum rather than the MapleUI pixel face. MapleUI's digits are tiny
    // and thin at the size that matches a v83 label and came out unreadable in a numeric
    // plate; legibility of a live value beats font consistency with static art.
    m_pFontDk    = MakeFont(0xFF455B71);   // readouts + item name, sampled off the art
    m_pFontLt    = MakeFont(0xFFFFFFFF);   // blue banner + the pink label plates
    m_pFontTitle = MakeFont(0xFF000000);   // the white title strip

    m_pCurrentStage = SehCurrentStage();
    // Show the saved color immediately, so opening the window never looks like a
    // reset even though the sliders are now driving the render.
    if (m_target.itemId) PushPreview();
    WeaponTint_RequestSnapshot();
}

void CUIColorPrism::ReleaseAvatar() {
    WeaponTint_UnbindPreviewAvatar(m_pAvatar);
    SehReleaseAvatar(m_avatarRef);
    m_avatarRef[0] = m_avatarRef[1] = 0;
    m_pAvatar = nullptr;
}

void CUIColorPrism::BuildAvatar() {
    ReleaseAvatar();
    void* charData = SehCharacterData();
    if (!charData) return;
    IWzGr2DLayer* pLayer = m_pLayer.GetInterfacePtr();
    if (!pLayer) return;
    SehBuildAvatar(m_avatarRef, charData, pLayer, static_cast<int>(m_avX), static_cast<int>(m_avY),
                    m_target.itemId, (2 << 1) | (m_bFacingLeft ? 1 : 0), kNoActionCode, &m_pAvatar);
    m_nLastMA = -1;
    m_nLastCode = -2;
    if (!m_pAvatar) {
        LOG_ONCE("coloringprism: preview avatar build failed (item=%d)", m_target.itemId);
    }
}

void CUIColorPrism::OnDestroy() {
    s_savedX = m_screenX; s_savedY = m_screenY; s_bSavedPos = true;
    ReleaseAvatar();
    // Leaving without confirming must not leave the world avatar recolored -- on ANY of
    // the four tabs, since the player may have tried colours on several.
    DropPreview();
    m_pFont = nullptr; m_pFontDk = nullptr;
    m_pFontLt = nullptr; m_pFontTitle = nullptr;
    m_pBg = nullptr;
    m_pBgLook = nullptr;
    for (int i = 0; i < kRowCount; ++i) m_pTrack[i] = nullptr;
    for (int i = 0; i < 3; ++i) {
        m_pThumb[i] = nullptr;
        m_pBtOK[i] = nullptr;
        m_pBtCancel[i] = nullptr;
    }
    m_pBtClose[0] = nullptr; m_pBtClose[1] = nullptr;
    for (int state = 0; state < 2; ++state) {
        m_pTabLeft[state] = nullptr;
        m_pTabFill[state] = nullptr;
        m_pTabRight[state] = nullptr;
    }
    if (ms_pInstance == this) ms_pInstance = nullptr;
    CWnd::OnDestroy();
    RestoreFocusToWndMan(); // CWnd unregisters focus; hand keyboard control back to the field.
}

void CUIColorPrism::CloseNow() {
    play_ui_sound(L"MenuDown");
    Destroy();
}

void CUIColorPrism::SendConfirm() {
    CommitValueEdit();
    if (!Ready()) return;
    // Every tab burns the same Coloring Prism; the ACTION differs only because hair and
    // eyes carry no inventory address for the server to re-verify against.
    //
    // An identity tint means "put it back", which is a RESTORE rather than an apply --
    // that lets the server refuse without consuming anything when the target was never
    // tinted, which is what makes Reset+Confirm safe to press on a vanilla colour.
    const bool undo = m_tint.IsIdentity();
    if (IsLookTab()) {
        const int kind = (m_tab == kTabHair) ? kTintKey_Hair : kTintKey_Face;
        if (undo) WeaponTint_SendRestoreLook(kind, s_prismPos);
        else      WeaponTint_SendApplyLook(kind, m_tint, s_prismPos);
    } else {
        const int layer = (m_tab == kTabEffects) ? kTintLayer_Effects : kTintLayer_Body;
        if (undo) WeaponTint_SendRestore(m_target, s_prismPos, layer);
        else      WeaponTint_SendApply(m_target, m_tint, s_prismPos, layer);
    }
    play_ui_sound(L"Apply");
    // Adopt the color locally before the window closes: OnDestroy drops the
    // preview, and without this the weapon would snap back to its old color for
    // the length of the round trip. The reply snapshot is authoritative and
    // corrects this if the server refused (no prism in the inventory, say).
    WeaponTint_AdoptOptimistic(TargetKey(), m_tint);
    Destroy();
}

// ---------------------------------------------------------------------------
void CUIColorPrism::Update() {
    // Auto-close on a stage change (field warp, cash shop, character select).
    // Polling rather than hooking OnLeaveGame avoids a teardown race.
    if (SehCurrentStage() != m_pCurrentStage) { Destroy(); return; }

    // A slider drag has to keep tracking when the cursor leaves the window.
    // Driving it from OnMouseMove makes the drag freeze, so it runs from here
    // off the absolute cursor instead.
    if (m_sliderDrag >= 0) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            m_sliderDrag = -1;
        } else {
            POINT sp;
            if (GetAbsCursor(sp)) {
                SetValueFromX(m_sliderDrag, sp.x - m_screenX - m_sliderGrabDX);
            }
        }
    }
    // Same treatment for the title-bar drag.
    if (m_bDragging) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            m_bDragging = 0;
        } else {
            POINT sp;
            if (GetAbsCursor(sp)) {
                int nx = sp.x - m_nDragAnchorX, ny = sp.y - m_nDragAnchorY;
                if (nx != m_screenX || ny != m_screenY) {
                    m_screenX = nx; m_screenY = ny;
                    MoveWnd(m_screenX, m_screenY);
                }
            }
        }
    }
    // Rebuild the pane avatar at most ~15fps while the sliders move; every rebuild
    // re-runs CAvatar::Init and re-tints the weapon's frames. A failed build leaves
    // the flag SET so the next tick retries -- clearing it up front would wedge the
    // pane blank for the rest of the session after one bad frame.
    if (m_bAvatarDirty) {
        const DWORD now = GetTickCount();
        if (now - m_nAvatarDirtyTick >= 66) {
            m_nAvatarDirtyTick = now;
            // Push the slider values into the tint engine first -- that clears the
            // stale clones and reloads the WORLD avatar -- then rebuild the pane
            // avatar, which picks the new clones up.
            if (Ready()) WeaponTint_SetPreview(TargetKey(), m_tint, true);
            BuildAvatar();
            m_bAvatarDirty = (m_pAvatar == nullptr);
        }
    }
    // Fixed-step animation and movement driver. Update
    // is deliberately outside StepPlayground: it can rebuild action layers as a swing
    // ends, and the tint hook must see the forced-preview scope while that happens.
    StepPlayground(GetTickCount());
    WeaponTint_BeginForcedScope();
    SehUpdateAvatar(m_pAvatar);
    WeaponTint_EndForcedScope();
    m_nLastCode = SehReadActionCode(m_pAvatar);
    InvalidateRect(nullptr);
}

// ---------------------------------------------------------------------------
void CUIColorPrism::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr pCanvas = GetCanvas();
    if (!pCanvas) return;
    IWzFont* pf    = m_pFont;
    IWzFont* pfDk  = m_pFontDk    ? static_cast<IWzFont*>(m_pFontDk)    : pf;
    IWzFont* pfLt  = m_pFontLt    ? static_cast<IWzFont*>(m_pFontLt)    : pf;
    IWzFont* pfTtl = m_pFontTitle ? static_cast<IWzFont*>(m_pFontTitle) : pfDk;

    // Hair and Face swap in a wellless backdrop when that art exists; falling back to
    // the ordinary one keeps the window drawable rather than blank if it does not.
    BlitAt(pCanvas, (!NeedsDrop() && m_pBgLook) ? m_pBgLook : m_pBg, 0, 0);

    // (1) TITLE. The supplied background carries the native-style COLORING PRISM
    // title, so it must not be drawn a second time here.

    // (2) TABS. Inventory-native Tab2 chrome, with this window's own labels drawn on
    // top: the donor's baked text names inventory categories, not prism targets.
    static const char* kTabLabel[kTabCount] = { "Equip", "Effects", "Hair", "Eyes" };
    for (int t = 0; t < kTabCount; ++t) {
        const bool selected = (m_tab == t);
        DrawInventoryTab(pCanvas, kTabX[t], kTabT, kTabW, selected);
        DrawTextCentred(pCanvas, selected ? pfLt : pfDk, kTabX[t], kTabW,
                        kTabT + (kTabH - 12) / 2, kTabLabel[t]);
    }

    // (3) The banner's 48x48 well is the DROP TARGET, and is left EMPTY until an item
    //     lands in it -- an icon sitting there before the drop reads as "already holding
    //     something", which is exactly the wrong cue. Once an item is dropped its icon
    //     appears here, which is how the player knows the drop took. The 32x32 icon is
    //     centred in the well and bottom-left anchored.
    //
    //     Guarded: CItemInfo::DrawItemIconForSlot (0x005D6458) reaches GetEquipItem
    //     (0x005CA785), whose loader raises _com_error E_FAIL for ids outside v83's
    //     cash-weapon whitelist: a real crash stack proves it, and the
    //     failure is never cached so it repeats every frame. Unguarded it would unwind
    //     out of Draw and abandon every later step, blanking the sliders and buttons too.
    if (m_target.IsSet() && NeedsDrop()) {
        if (auto* pItemInfo = CItemInfo::GetInstance()) {
            try {
                pItemInfo->DrawItemIconForSlot(pCanvas, m_target.itemId,
                                               kIconX, kIconBaseline, 0, 0, 0, 0, 0, 0);
            } catch (...) {
                LOG_ONCE("coloringprism: icon draw threw for item %d", m_target.itemId);
            }
        }
    }

    // (5) Instructions on the blue banner, in white. What they say AND where they sit
    //     depend on the tab, because the two halves of this window work differently.
    //     Equip and Effects want a drop, so their copy clears the icon well; Hair and
    //     Face are ready the moment they are selected, so theirs loses the drag line
    //     and centres on the full width.
    static const char* kBannerDrop[3] = {
        "Drag a Cash item onto the slot.",
        "Adjust the sliders, then press OK.",
        "One Coloring Prism is used per dye.",
    };
    static const char* kBannerLook[2] = {
        "Adjust the sliders, then press OK.",
        "One Coloring Prism is used per dye.",
    };
    if (NeedsDrop()) {
        for (int i = 0; i < 3; ++i)
            DrawTextCentred(pCanvas, pfLt, kBannerTextX, kBannerTextW,
                             kBannerTextT + i * kBannerLineH, kBannerDrop[i]);
    } else {
        for (int i = 0; i < 2; ++i)
            DrawTextCentred(pCanvas, pfLt, kBannerFullX, kBannerFullW,
                             kBannerLookT + i * kBannerLineH, kBannerLook[i]);
    }

    // (5) Three slider rows. The supplied background already carries the HUE,
    // CHROMA and VALUE labels, so only the moving controls and values are drawn here.
    for (int row = 0; row < kRowCount; ++row) {
        const int y = RowTop(row);
        const int textY = y + (kRowH - 13) / 2;

        BlitA(pCanvas, m_pTrack[row], kTrackX, y + (kRowH - kTrackH) / 2);

        RECT th; ThumbRect(row, th);
        IWzCanvasPtr thumb = (m_sliderDrag == row && m_pThumb[1]) ? m_pThumb[1] : m_pThumb[0];
        BlitA(pCanvas, thumb, th.left, th.top);

        // Tone is an absolute angle; Chroma and Brightness are signed deltas, so they
        // carry their sign the way the reference dialog does.
        char num[16];
        if (row == m_editRow) _snprintf_s(num, _countof(num), _TRUNCATE, "%s_", m_editText);
        else if (row == kRowTone) _snprintf_s(num, _countof(num), _TRUNCATE, "%d", Value(row));
        else                 _snprintf_s(num, _countof(num), _TRUNCATE, "%+d", Value(row));
        num[15] = 0;
        DrawTextRight(pCanvas, pfDk, kValueX + kValueW - 6, textY, num);
    }

    // (6) Matched vanilla Cancel / OK footer buttons. The left button is repurposed
    // as a slider reset; only the title-bar chip discards the preview and closes.
    for (int i = 0; i < kBtnCount; ++i) {
        int state = 0;
        if (m_nBtnPressed == i)     state = 1;
        else if (m_nBtnHover == i)  state = 2;
        IWzCanvasPtr art = (i == kBtnCancel)
            ? (m_pBtCancel[state] ? m_pBtCancel[state] : m_pBtCancel[0])
            : (m_pBtOK[state] ? m_pBtOK[state] : m_pBtOK[0]);
        BlitA(pCanvas, art, kBtnX[i], kBtnT);
    }

    { const int off = m_nClosePressed ? 1 : 0;
      BlitA(pCanvas, m_nCloseHover ? m_pBtClose[1] : m_pBtClose[0],
            kBtCloseX + off, kBtCloseY + off); }

    // Re-assert the held-button cursor; the engine resets the sprite each frame.
    if (m_nBtnPressed >= 0 || m_nClosePressed || m_sliderDrag >= 0) {
        SetCursorState(kCursor_ButtonPress);
    }
}

// ---------------------------------------------------------------------------
void CUIColorPrism::OnMouseButton(unsigned int msg, unsigned int /*wParam*/, int rx, int ry) {
    POINT pt{ rx, ry };

    if (msg == WM_LBUTTONDOWN) {
        // Moving to another control commits the current field first, so a player can
        // type a value and immediately drag a different slider or press OK.
        CommitValueEdit();
        for (int t = 0; t < kTabCount; ++t) {
            RECT rc = { kTabX[t], kTabT, kTabX[t] + kTabW, kTabT + kTabH };
            if (PtInRect(&rc, pt)) { SetTab(t); return; }
        }
        // sliders next -- they are the busiest target
        for (int row = 0; row < kRowCount; ++row) {
            const int y = RowTop(row);
            RECT th; ThumbRect(row, th);
            if (PtInRect(&th, pt)) {
                m_sliderDrag = row;
                m_sliderGrabDX = rx - th.left;
                return;
            }
            RECT rcTrack = { kTrackX, y, kTrackX + kTrackW, y + kRowH };
            if (PtInRect(&rcTrack, pt)) {
                // Jump the thumb under the cursor and continue as a drag.
                m_sliderDrag = row;
                m_sliderGrabDX = kThumbW / 2;
                SetValueFromX(row, rx - m_sliderGrabDX);
                return;
            }
        }
        for (int row = 0; row < kRowCount; ++row) {
            const int y = RowTop(row);
            RECT rcValue = { kValueX, y, kValueX + kValueW, y + kRowH };
            if (PtInRect(&rcValue, pt)) { BeginValueEdit(row); return; }
        }
        for (int i = 0; i < kBtnCount; ++i) {
            RECT rc = { kBtnX[i], kBtnT, kBtnX[i] + kBtnW, kBtnT + kBtnH };
            if (PtInRect(&rc, pt)) { m_nBtnPressed = i; InvalidateRect(nullptr); return; }
        }
        RECT rcClose = { kBtCloseX, kBtCloseY, kBtCloseX + kBtCloseW, kBtCloseY + kBtCloseH };
        if (PtInRect(&rcClose, pt)) { m_nClosePressed = 1; InvalidateRect(nullptr); return; }

        if (ry < kTitleH && m_pLayer) {
            POINT sp;
            if (GetAbsCursor(sp)) {
                m_bDragging = 1;
                m_nDragAnchorX = sp.x - m_screenX;
                m_nDragAnchorY = sp.y - m_screenY;
            }
        }
        return;
    }

    if (msg == WM_LBUTTONUP) {
        m_bDragging = 0;
        m_sliderDrag = -1;

        if (m_nBtnPressed >= 0) {
            const int i = m_nBtnPressed;
            m_nBtnPressed = -1;
            RECT rc = { kBtnX[i], kBtnT, kBtnX[i] + kBtnW, kBtnT + kBtnH };
            if (PtInRect(&rc, pt)) {
                play_ui_sound(L"BtMouseClick");
                if (i == kBtnCancel) ResetValues();
                else if (i == kBtnOK && Ready()) SendConfirm();
                return;                              // `this` may be gone
            }
            InvalidateRect(nullptr);
            return;
        }
        if (m_nClosePressed) {
            m_nClosePressed = 0;
            RECT rcClose = { kBtCloseX, kBtCloseY, kBtCloseX + kBtCloseW, kBtCloseY + kBtCloseH };
            if (PtInRect(&rcClose, pt)) { CloseNow(); return; }
            InvalidateRect(nullptr);
        }
        SetCursorState(kCursor_Arrow);
    }
}

int CUIColorPrism::OnMouseMove(int rx, int ry) {
    POINT pt{ rx, ry };
    int hover = -1;
    for (int i = 0; i < kBtnCount; ++i) {
        RECT rc = { kBtnX[i], kBtnT, kBtnX[i] + kBtnW, kBtnT + kBtnH };
        if (PtInRect(&rc, pt)) { hover = i; break; }
    }
    RECT rcClose = { kBtCloseX, kBtCloseY, kBtCloseX + kBtCloseW, kBtCloseY + kBtCloseH };
    const int closeHover = PtInRect(&rcClose, pt) ? 1 : 0;
    if (hover != m_nBtnHover || closeHover != m_nCloseHover) {
        if (hover >= 0 && hover != m_nBtnHover) play_ui_sound(L"BtMouseOver");
        m_nBtnHover = hover;
        m_nCloseHover = closeHover;
        InvalidateRect(nullptr);
    }
    return 1;
}

// =====================================================
// OPENING / THE TWO ITEMS
// =====================================================
void ClampToScreen(int& x, int& y) {
    const int sw = get_screen_width(), sh = get_screen_height();
    if (x < 8) x = 8;
    if (sw > kWndW && x > sw - kWndW) x = sw - kWndW;
    if (y < 0) y = 0;
    if (sh > kWndH && y > sh - kWndH) y = sh - kWndH;
}

void OpenWindow() {
    if (CUIColorPrism::ms_pInstance) {
        CUIColorPrism::ms_pInstance->InvalidateRect(nullptr);
        return;
    }
    int x, y;
    if (s_bSavedPos) { x = s_savedX; y = s_savedY; }
    else { x = (get_screen_width() - kWndW) / 2; y = (get_screen_height() - kWndH) / 2; }
    ClampToScreen(x, y);
    new CUIColorPrism(x, y);
}

} // namespace ColorPrism

// =====================================================
// DISPATCH FROM THE CASH-USE HOOKS
// =====================================================
// The three cash-use addresses are owned outright by whatever Detoured them first,
// and a second Detour on them breaks it. So this file exports a pair of predicates
// for those hooks to call instead.
bool ColorPrism_IsPrismItem(int nItemID) {
    return nItemID == ColorPrism::kItemColoringPrism;
}

void ColorPrism_OnUse(int nPOS, int nItemID) {
    if (nItemID != ColorPrism::kItemColoringPrism) return;
    ColorPrism::s_prismPos = nPOS;
    // Reopens on whichever tab was last used: one prism pays for all four, so there is
    // no tab it could open on that it cannot then act on.
    ColorPrism::OpenWindow();
}

// =====================================================
// DROP TARGET
// =====================================================
// Called from the host's CDraggableItem::OnDropped hook (0x004EF140).
//
// A second Detour on that address is the same hazard documented for the cash-use
// functions, so this file hooks nothing and is dispatched from whatever already
// owns it, exactly as the double-click is.
//
// TWO ways a drop can be recognised, because the first one cannot be relied on.
//
//   (1) `pTo` names this window. `pTo` is CWndMan+0x8C, the IUIMsgHandler the drag
//       tracker last latched onto -- initialised to the drag SOURCE at the end of
//       CWndMan::BeginDragDrop (0x009E37AB) and re-pointed on mouse-move by the
//       hit test at 0x009E3DBA. Because CWnd inherits IUIMsgHandler at +4, the
//       value compared against is the window pointer OR window+4, which is the
//       comparison CUIBagWindow makes.
//
//       Measured in-game: this route fires on EVERY drop, so it is the real path.
//
//   (2) the cursor is over the 32x32 WELL. Kept as a belt-and-braces fallback for
//       the case where the tracker latches onto nothing (pTo null). It is checked
//       second, so it can never override a drop the engine addressed elsewhere.
//
// Returns true if the drop was consumed, which tells the caller not to pass it on.
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos) {
    auto* w = ColorPrism::CUIColorPrism::ms_pInstance;
    if (!w) return false;

    const bool bAddressed =
        pTo && (pTo == static_cast<void*>(w) || pTo == reinterpret_cast<char*>(w) + 4);
    if (!bAddressed && !w->CursorOverWell()) return false;

    w->SetTarget(invType, invPos);
    return true;      // consumed either way: the item must not also be moved
}

void AttachColoringPrismMod() {
}

int ColorPrism_PeekCashItemId(int invPos) {
    void* item = ColorPrism::SehItemAt(5, invPos); // CASH tab
    return ColorPrism::SehDecodeItemId(item);
}

void ColoringPrism_OpenWindow() {
    ColorPrism::OpenWindow();
}

void ColoringPrism_CloseWindow() {
    if (ColorPrism::CUIColorPrism::ms_pInstance) {
        ColorPrism::CUIColorPrism::ms_pInstance->CloseNow();
    }
}

bool ColoringPrism_IsOpen() {
    return ColorPrism::CUIColorPrism::ms_pInstance != nullptr;
}

void ColoringPrism_RequestOpen() {
    // Opening is client-side via item 5782000; request a tint snapshot instead.
    WeaponTint_RequestSnapshot();
}

void ColoringPrism_HandleServerPacket(CompatInPacket* packet) {
    WeaponTint_HandleSync(packet);
}

void ColoringPrism_OnTick() {
    WeaponTint_Tick();
}