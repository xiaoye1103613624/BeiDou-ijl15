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
// default screen is 800x600. The backdrop is hand-drawn art, shipped ready to use as
// the `backgrnd` canvas of wz/client/UI/ColorPrism.img; the sliders are
// drag-and-click-the-track, with no stepper arrows, because the art has no wells for
// them.
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
#include "compat/hook.h"
#ifndef LOG_ONCE
#define LOG_ONCE(...) ((void)0)
#endif
#ifndef LogMessage
inline void LogMessage(const char*, ...) {}
#endif

#include "ColoringPrismApi.h"
#include "weapontint.h"

#include "compat/wvs/iteminfo.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/wndman.h"
#include "ztl/ztl.h"
// Declared here rather than in weapontint.h: that header includes only <cstdint> so it can be
// dropped into any host, and naming a COM smart pointer in it would drag the whole WZ include
// chain along -- a mis-declared IWzCanvasPtr there cascades into a typedef redefinition inside
// IWzCanvas.h itself. It must also sit AFTER the WZ includes below, for the same reason.
IWzCanvasPtr WeaponTint_TintedCanvasFor(IWzCanvasPtr src, const WeaponTint& t,
                                       bool mirror);

#include <map>
#include <string>
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
// Is this an equip the prism can dye? Equips are 1xxxxxx, and that is now the whole
// test: ordinary gear dyes exactly like a Cash item, because the recolour never cared
// which it was. CharacterSubdirOf already maps every equip category, cash or not, so
// nothing below this gate needed changing to open it up.
//
// THE `cash` FLAG USED TO BE REQUIRED HERE, matching ItemInformationProvider.isCash on
// the server. Both sides dropped it together, and they have to stay in step: a client
// gate looser than the server's lets a drop land on the well and look accepted, only for
// the apply to be refused a round trip later with no obvious reason.
//
// The WZ info node is still required. It is what proves the id names a real item at all,
// and an id with no info node has no art for the swap to walk either.
bool IsDyeableEquip(int itemId) {
    if (itemId / 1000000 != 1) return false;
    auto* pInfo = CItemInfo::GetInstance();
    if (!pInfo) return false;
    try {
        IWzPropertyPtr p = pInfo->GetItemInfo(itemId);   // == the item's `info` node
        if (!p) {
            LOG_ONCE("coloringprism: no WZ info node for item %d", itemId);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// =====================================================
// LAYOUT
// =====================================================
// EVERY ONE OF THESE WAS MEASURED OFF THE SHIPPED BACKDROP, the `backgrnd` canvas of
// wz/client/UI/ColorPrism.img. The art and these constants are one design expressed
// twice, so they must move together: redraw the backdrop, re-measure, update these. A
// redraw that skips the re-measure is invisible until something lands a few pixels off.
//
// The art bakes the frame, the window title and the three HUE / CHROMA / VALUE row
// labels. EVERYTHING ELSE IS DRAWN, including the banner copy and the tab labels, which
// is why the window needs its own fonts: the item icon, the three gradients, the thumbs,
// the numbers and the buttons all move, and the banner and tabs change with the tab.
constexpr int kWndW = 301, kWndH = 406;

// A transparent margin around the window, so a skill effect plays at TRUE SIZE instead of being
// cut at the border. Roughly triples the width available to an effect and quadruples the height.
// Not larger than this: the window layer sits at (chrome - margin), and a margin approaching the
// screen size would push that origin far negative for a window near an edge.
constexpr int kMarginX = 100, kMarginY = 80;
constexpr int kTitleH = 23;                          // drag region; white interior y5..18
constexpr int kBtCloseX = 281, kBtCloseY = 5, kBtCloseW = 12, kBtCloseH = 12;

// TABS. These are the Item Inventory's own Basic.img/Tab2 pieces: 4px left and
// right caps with a one-pixel fill column between them. They are 19px high and
// must never be vertically scaled. Four 44px tabs leave a clear gap before the
// right end of the strip.
constexpr int kTabT = 23, kTabH = 19, kTabW = 44;
// SIX tabs at a 44px pitch, ending at 270 against an interior that runs to about x290. A
// SEVENTH does not fit at this pitch; adding one means dropping to 40px and shortening a
// label, or hosting several no-drop kinds under one tab.
constexpr int kTabCount = 5;
// TWO GROUPS, not five peers. Item and Skills take a DROP; Hair, Eyes and Skin dye the
// character's own look and have no well at all. The 8px gap after the second tab is where the
// rule goes. The strip ends at 234 against the banner's 291, with room to spare since Effects
// folded into Item.
constexpr int kTabX[kTabCount] = { 6, 50, 102, 146, 190 };
constexpr int kTabSepX = 96;                         // the rule, in the gap
enum Tab { kTabItem = 0, kTabSkill = 1, kTabHair = 2, kTabEye = 3, kTabSkin = 4 };



constexpr int kBannerT = 50, kBannerB = 115;         // blue banner
constexpr int kWellX = 21, kWellY = 59, kWellSize = 48;   // the drop well, measured
constexpr int kIconX = 29, kIconBaseline = 99;       // 32x32 icon centred in it, bottom-left
constexpr int kIconSize = 32;
// THE LAYER CHIPS, badged on the icon well's bottom corners.
//
// An item can carry TWO independent tints: its own sprite and the glow art hanging off it. These
// pick which one the sliders are editing, and they replace what used to be a second tab. 14x14,
// art at UI/UIWindow.img/ColorPrism/LayerBt/<item|glow>/<normal|on|off>, generated by
// Tools/import_coloring_prism.py -- redraw there and these numbers follow.
//
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
constexpr int kPreviewL = 11,  kPreviewR = 289;      // and its left/right edges
// The two thin vertical separators painted inside the preview pane. The avatar's feet
// are clamped far enough inside them that its body never crosses either line.
constexpr int kPreviewSepLeft = 26, kPreviewSepRight = 275;
constexpr int kPreviewAvatarHalfW = 18;

// AT THE BOTTOM OF THE PREVIEW PANE, centred, rather than on the well.
//
// The well cannot hold them: it is 48px wide with a 32px icon centred in it, leaving 9px and 7px
// of gutter, so a 14px chip could only straddle its border -- and straddling reads as hanging
// off the well rather than belonging to it. The pane has room the banner does not. The avatar's
// feet are at kAvatarY and the pane runs to kPreviewB, so this strip is empty by construction,
// and it sits where the eye already is while a colour is being judged.
constexpr int kChipSize   = 14;
constexpr int kChipGap    = 6;
constexpr int kChipY      = kPreviewB - kChipSize - 4;
constexpr int kChipX[2]   = { (kPreviewL + kPreviewR) / 2 - kChipSize - kChipGap / 2,
                              (kPreviewL + kPreviewR) / 2 + kChipGap / 2 };
constexpr int kChipPad    = 3;                       // hit slop; 14px is small to aim at
enum ChipLayer { kChipItem = 0, kChipGlow = 1 };

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
    { L"UI/UIWindow.img/ColorPrism/trackTone",     0, kTintHueMax },
    { L"UI/UIWindow.img/ColorPrism/trackChroma", kTintDeltaMin, kTintDeltaMax },
    { L"UI/UIWindow.img/ColorPrism/trackBright", kTintDeltaMin, kTintDeltaMax },
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
// How long a skill effect stays on screen in the pane once a cast begins. The swing pose is
// often shorter than the effect, so tying the two together cut longer skills off part way.

// What the preview plays, and ONLY this. `effect` is the skill effect on the CASTER, and for a
// buff it is also the buff effect the caster wears -- every self buff checked (Magic Guard,
// Dark Sight) carries `effect` and nothing else.
//
// Deliberately NOT `affected`, which is the RECIPIENT's art: the client's own hardcoded
// `Skill/MobSkill.img/%03d/level/%d/affected` is what an affected PLAYER wears when a mob skill
// lands, and every party buff checked (Iron Will, Hyper Body, Bless, Dispel) carries it
// alongside `effect` precisely because party members show it. Not `special` either, which holds
// big set-piece art (a 207x236 stag among them) a caster never sees on themselves, nor
// `effect0`, a 1x1 placeholder on most of the 71 skills carrying one.
//
// Kept as a table rather than folded into one path, because which nodes count as caster-side
// is the question this feature keeps getting wrong, and a table is where the answer belongs.
const wchar_t* const kSkillFxNodes[] = { L"effect" };
constexpr int kSkillFxNodeCount = static_cast<int>(_countof(kSkillFxNodes));

// THE PREVIEW LAYER MUST NOT OUTLIVE THE CLONES IT WAS BUILT FROM.
//
// This crashed three times and each fix missed, because the layer looked like the problem and
// its LIFETIME RELATIVE TO THE CLONE CACHE was. An isolation build settled it: with the layer
// skipped and the tint swap still running, everything was stable, so the swap was never at
// fault.
//
// The layer holds the tinted clones. ClearClonesForTint drops exactly those clones every time
// the colour changes -- which is every slider step -- so a layer built at one colour is
// holding freed canvases a moment later. That is the garbage-pointer read the renderer took,
// and the heap fault the NEXT clone allocation took, and it is why it only ever happened
// while previewing a tint.
//
// So the rule is: rebuild the layer whenever the SKILL or the COLOUR changes. Tying it to the
// skill alone was the exact wrong move, since the colour is the half that frees the canvases.
constexpr bool kSkillPreviewLayer = true;

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
int  s_tab = kTabItem;

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
// CASH EFFECT PREVIEW
// =====================================================
// A cash effect is not part of the avatar: the client renders it as its own animated
// Gr2D layer hung off a CUser. The window's preview avatar is a bare CAvatar with no
// CUser behind it, so nothing would draw it here, and dropping one on the well would
// show an unchanged character while the sliders moved.
//
// So the window builds the layer itself, the same way the Cash Shop preview does. The
// factory takes ONE owned reference for each of its three object arguments and writes one
// owned reference into an out slot that it does NOT release first, so the slot has to be
// zeroed before every call.
constexpr uintptr_t kAddr_CreateAnimLayer = 0x0043EA3E;
using t_CreateAnimLayer = void**(__cdecl*)(void**, void*, int, void*, int, int,
                                           void*, int, int, int);
auto CreateAnimLayer = reinterpret_cast<t_CreateAnimLayer>(kAddr_CreateAnimLayer);

constexpr size_t kOff_AvatarFaceOrigin     = 0x10B4;   // CAvatar::GetFaceOrigin 0x00932CBF
constexpr size_t kOff_AvatarBodyOrigin     = 0x10B8;
constexpr size_t kOff_AvatarLayerUnderFace = 0x10C8;   // CAvatar::GetLayerUnderFace 0x00451E7E

// Split out because __try may not share a function with anything that unwinds, and the
// caller holds COM pointers.
void* SehCallCreateAnimLayer(void* pNode, int bFlip, void* pOrigin, void* pOverlay) {
    void* pLayer = nullptr;
    __try {
        CreateAnimLayer(&pLayer, pNode, bFlip, pOrigin, 0, 0, pOverlay, 3, 255, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { pLayer = nullptr; }
    return pLayer;
}

// Which effect node a pose wants. Per-action effects have one node per action NAME, so a
// pose change is a real rebuild rather than a reuse.
const wchar_t* ActionNameForPose(int nPackedMA, int nActionCode) {
    if (nActionCode != kNoActionCode) return L"swingO1";   // the only override struck here
    switch (nPackedMA >= 0 ? (nPackedMA >> 1) : 0) {
        case 1:  return L"walk1";
        case 3:  return L"jump";
        case 5:  return L"prone";
        default: return L"stand1";
    }
}


// The effect layer for one item in one pose, or null if this WZ shape is not one the
// window plays. Returns an OWNED reference.
IWzGr2DLayer* CreateEffectLayer(void* pAvatar, int itemId, const wchar_t* action) {
    if (!pAvatar || itemId <= 0) return nullptr;

    // A PROBE ONLY COUNTS IF THE NODE IT LANDS ON HAS FRAME 0.
    //
    // Testing the pointer for null does not work here: this client's GetObjectA answers a
    // MISSING path with a non-null empty object rather than with null, so the first probe
    // always "succeeds" and the later ones never run. That is what made a plain
    // effect/default item like 5010083 report itself as an unplayable shape: the per-action
    // probe for `stand1` matched nothing, returned an empty node anyway, and the default
    // probe that would have found its fourteen frames was skipped.
    //
    // Requiring frame 0 also keeps the original intent, which was never really about probe
    // order: it is how the empty placeholder a per-action item leaves for a pose it has no
    // art for, the follow-trail siblings, and the canvas-less afterimage nodes get rejected.
    // FRAME 0 IS A CANVAS, so it is tested through IUnknown. Assigning it to an
    // IWzPropertyPtr runs QueryInterface(IWzProperty), which a canvas in this client does
    // not answer, so that test rejected every node including the good ones and the item
    // still came out "unplayable". get_unknown is indifferent to which it is.
    auto probe = [](const wchar_t* p) -> IWzPropertyPtr {
        IWzPropertyPtr n;
        try { n = get_rm()->GetObjectA(const_cast<wchar_t*>(p)).GetUnknown(); } catch (...) {}
        if (!n) return nullptr;
        bool has0 = false;
        try {
            Ztl_variant_t v = n->item[L"0"];
            has0 = (get_unknown(v) != nullptr);
        } catch (...) {}
        return has0 ? n : IWzPropertyPtr(nullptr);
    };

    IWzPropertyPtr node;
    {
        wchar_t path[192];
        // Per-action first: those items carry `action = 1` and an EMPTY effect/default, so
        // taking default first would pick a node with no frames when a real pose node exists.
        if (action) {
            _snwprintf_s(path, _countof(path), _TRUNCATE,
                         L"Item/Cash/0501.img/%08d/effect/%s", itemId, action);
            node = probe(path);
        }
        if (!node) {
            _snwprintf_s(path, _countof(path), _TRUNCATE,
                         L"Item/Cash/0501.img/%08d/effect/default", itemId);
            node = probe(path);
        }
        if (!node) {
            // 5281xxx keep their canvases directly under `effect` in a different group.
            _snwprintf_s(path, _countof(path), _TRUNCATE,
                         L"Item/Cash/0528.img/%08d/effect", itemId);
            node = probe(path);
        }
    }
    if (!node) {
        LOG_ONCE("coloringprism: fx %d: no playable effect node for action %ls (tried "
                 "per-action, default, 0528)", itemId, action ? action : L"(null)");
        return nullptr;
    }
    // `pos` (0 body, 1 face, 2 center, 3 ground) says which anchor the origins are
    // measured from. Only the face case has a distinct vector on a DLL-owned avatar.
    int nPos = 0;
    try { nPos = get_int32(node->item[L"pos"], 0); } catch (...) {}
    const size_t offOrigin = (nPos == 1) ? kOff_AvatarFaceOrigin : kOff_AvatarBodyOrigin;

    void* pOrigin = *reinterpret_cast<void**>(reinterpret_cast<char*>(pAvatar) + offOrigin);
    auto* pOverlay = *reinterpret_cast<IWzGr2DLayer**>(
        reinterpret_cast<char*>(pAvatar) + kOff_AvatarLayerUnderFace);
    if (!pOverlay) {
        LOG_ONCE("coloringprism: fx %d: avatar has no layer-under-face yet (pos=%d, "
                 "origin=%p)", itemId, nPos, pOrigin);
        return nullptr;                            // avatar not laid out yet
    }

    int bFlip = 0;
    try { bFlip = pOverlay->flip; } catch (...) {}

    IWzProperty* pNode = node.GetInterfacePtr();
    if (!pNode) return nullptr;
    pNode->AddRef();
    if (pOrigin) reinterpret_cast<IUnknown*>(pOrigin)->AddRef();
    pOverlay->AddRef();

    void* pLayer = SehCallCreateAnimLayer(pNode, bFlip, pOrigin, pOverlay);
    if (!pLayer) return nullptr;

    auto* pRet = reinterpret_cast<IWzGr2DLayer*>(pLayer);
    try { pRet->Animate(GA_REPEAT); } catch (...) {}   // the factory does not start it
    return pRet;
}




// The client's own action code -> name converter. Caller owns the out-param, unlike the
// reverse lookup next to it, which consumes its argument.
using t_ActionNameFromCode = Ztl_bstr_t*(__cdecl*)(Ztl_bstr_t*, int);
auto get_action_name_from_code =
    reinterpret_cast<t_ActionNameFromCode>(0x004A8CE6);

// The action a skill actually plays, as a code for CAvatar::SetActionCode, or kNoActionCode
// when the skill does not name one (295 of 616 do not, and those keep the generic swing).
//
// Resolved by scanning the client's own code-to-name converter rather than by calling the
// reverse lookup at 0x004A8D14: that one compares WIDE strings and CONSUMES its by-value
// argument, so passing a temporary double-releases the buffer. Scanning 162 entries once per
// skill and caching the answer avoids the whole hazard.
// An action NAME to the code CAvatar::SetActionCode wants, or kNoActionCode.
//
// Resolved by scanning the client's own code-to-name converter rather than by calling the
// reverse lookup at 0x004A8D14: that one compares WIDE strings and CONSUMES its by-value
// argument, so passing a temporary double-releases the buffer. Scanning 162 entries once per
// name and caching the answer avoids the whole hazard.
int ActionCodeForName(const wchar_t* want) {
    if (!want || !*want) return kNoActionCode;
    static std::map<std::wstring, int> s_cache;
    const std::wstring key(want);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second;

    int code = kNoActionCode;
    for (int c = 0; c < 162; ++c) {
        Ztl_bstr_t name;
        get_action_name_from_code(&name, c);
        const wchar_t* s = name.GetBSTR();
        if (s && key == s) { code = c; break; }
    }
    s_cache.emplace(key, code);
    return code;
}

int SkillActionCode(int skillId) {
    static std::map<int, int> s_cache;
    if (skillId <= 0) return kNoActionCode;
    auto it = s_cache.find(skillId);
    if (it != s_cache.end()) return it->second;

    int code = kNoActionCode;
    try {
        wchar_t path[128];
        _snwprintf_s(path, _countof(path), _TRUNCATE,
                     L"Skill/%03d.img/skill/%07d/action", skillId / 10000, skillId);
        IWzPropertyPtr pAction = get_rm()->GetObjectA(path).GetUnknown();
        if (pAction) {
            Ztl_variant_t v = pAction->item[L"0"];
            if (V_VT(&v) == VT_BSTR && V_BSTR(&v)) code = ActionCodeForName(V_BSTR(&v));
        }
    } catch (...) {
    }
    s_cache.emplace(skillId, code);
    return code;
}

// Does this weapon's art actually define this action?
//
// Worth checking rather than trusting the type: swingT1 is missing from the set every
// two-handed weapon shares (types 40, 41 and 42), and 1H maces and claws have NO attack action
// common to all of their weapons. A pose the weapon cannot play is a pose that does not draw.
bool WeaponHasAction(int weaponId, const wchar_t* action) {
    if (weaponId <= 0 || !action) return false;
    try {
        wchar_t path[160];
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"Character/Weapon/%08d.img/%s",
                     weaponId, action);
        IWzPropertyPtr node = get_rm()->GetObjectA(path).GetUnknown();
        // GetObjectA hands back a non-null but EMPTY object for a path that does not exist,
        // so frame 0 resolving is the only proof the action is really there.
        return node && get_unknown(node->item[L"0"]) != nullptr;
    } catch (...) {
    }
    return false;
}

// The attack pose for the weapon an avatar is holding, as an action code.
//
// The type table is what makes a bowman skill look like a bowman skill: skills themselves
// almost never name an attack action (6 of 616 name shoot1), so without this every unnamed
// skill previewed as a sword swing whatever was in hand.
int WeaponAttackActionCode(void* pAvatar) {
    const int weaponId = WeaponTint_BaseWeaponIdOf(pAvatar);
    // The wielded-type code, the same id/10000-100 the tint walk uses.
    const int wt = (weaponId > 0) ? (weaponId / 10000 - 100) : -1;

    const wchar_t* want = L"swingO1";       // every one-handed type, and bare hands
    switch (wt) {
        case 45: want = L"shoot1";  break;  // bow
        case 46: want = L"shoot2";  break;  // crossbow
        case 49: want = L"shoot2";  break;  // gun
        case 43: want = L"stabT1";  break;  // spear
        case 44: want = L"swingP1"; break;  // polearm
        case 40: case 41: case 42:          // two-handed sword, axe, mace
        case 48: want = L"swingT1"; break;  // knuckle
        default: break;
    }

    // Fall back through progressively more common actions, so a weapon missing its
    // type's usual attack still strikes rather than freezing on a pose it cannot draw.
    static const wchar_t* const kFallback[] = { L"swingT1", L"stabT1", L"swingO1", L"stabO1" };
    if (!WeaponHasAction(weaponId, want)) {
        want = nullptr;
        for (const wchar_t* f : kFallback) {
            if (WeaponHasAction(weaponId, f)) { want = f; break; }
        }
    }
    if (!want) return 5;                    // swingO1 by code: no weapon art to go on
    const int code = ActionCodeForName(want);
    return (code != kNoActionCode) ? code : 5;
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
    int  m_tab;                                      // kTabItem / Skill / Hair / Eye / Skin
    // Which layer of the dropped item the sliders edit. Meaningless until something is on the
    // well, and forced to the layer the item actually has when one lands: see SnapLayer.
    int  m_layer;
    int  m_chipHover;                                // -1, or the chip the cursor is over
    IWzCanvasPtr m_pChip[2][3];                      // [item|glow][normal|on|off]
    WeaponTintTarget m_target;                       // the item being dyed (0 = none yet)
    WeaponTint m_tint;                               // live slider values

    // Preview playground: the avatar walks, jumps and swings under the player's own keys.
    int  m_bFocused;
    bool m_keyL, m_keyR, m_keyD;
    float m_avX, m_avY, m_velX, m_velY;
    bool m_bAirborne, m_bFacingLeft, m_bAttackReq;
    DWORD m_tLastStep;
    int m_nLastMA, m_nLastCode;
    // The selected cash effect's animation. OWNED, and it holds the avatar's own origin
    // vectors, so it MUST be released before the avatar is.
    // The chrome, drawn at kWndW x kWndH in the ORIGINAL coordinates and blitted into the
    // middle of the real canvas. Null means canvas creation failed, and then the margin is
    // zero and the window is its old self.
    IWzCanvasPtr m_pChrome;
    int          m_marginX, m_marginY;
    // Whether the margin can be ERASED. Set from the clear every Draw: if the surface cannot be
    // written, the margin is left alone rather than painted into, because an effect drawn where
    // nothing can rub it out again accumulates one frame on top of the next.
    bool         m_bMarginPaint;
    int CanvasW() const { return kWndW + m_marginX * 2; }
    int CanvasH() const { return kWndH + m_marginY * 2; }

    // The dropped skill's icon, cached on its id so a repaint does not re-resolve it.
    IWzCanvasPtr m_pSkillIcon;
    int          m_nSkillIconId;
    bool         m_bSkillFxPlaying;
    // Latched by the attack keypress and consumed by RefreshEffect. A latch rather than a
    // direct read because m_bAttackReq is cleared at the end of StepPlayground, which runs
    // first.
    bool         m_bFxTrigger;
    // The skill effect the pane is playing. Deliberately NOT a Gr2D layer: see the note on
    // DrawSkillFx. The nodes are WZ-owned and outlive us; the frame cursors are ours.
    //
    // One entry per row of kSkillFxNodes, because a buff is not a single animation. They
    // run SIDE BY SIDE rather than in sequence, so each keeps its own cursor and its own
    // origin.
    struct SkillFx {
        IWzPropertyPtr node;
        // Tinted and HELD. Holding our own reference is what makes this safe: the clone cache
        // may evict this colour at any moment, and a borrowed pointer would be freed
        // underneath the blit.
        IWzCanvasPtr   frame;
        int            index = 0;
        DWORD          at = 0;
        int            ox = 0, oy = 0;
    };
    SkillFx        m_skillFx[kSkillFxNodeCount];
    int            m_nSkillFxNodeId;
    IWzGr2DLayer* m_pEffectLayer;
    int m_nEffectItem;                 // what it was built for, 0 = nothing
    int m_nEffectMA, m_nEffectCode;    // and for which pose

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
    // CLICK-THROUGH MARGIN. Without this the window would silently swallow every click in a
    // 100px band around itself, which is worse than the clipping this exists to remove.
    virtual int HitTest(int rx, int ry, CCtrlWnd** ppCtrl) override {
        if (rx < m_marginX || ry < m_marginY ||
            rx >= m_marginX + kWndW || ry >= m_marginY + kWndH) {
            return 0;
        }
        return CWnd::HitTest(rx, ry, ppCtrl);
    }
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
        try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p))); } catch (...) {}
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
    // Blit CLIPPED to a box, for art bigger than the pane it goes in. CopyEx takes the
    // destination size and the source rect as separate pairs, so clipping is a matter of moving
    // the source origin in by however far the destination had to be pushed, then shrinking both
    // to what is left. Sizes stay equal on the two sides, so this crops rather than scales.
    static void BlitAClipped(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y,
                             int l, int t, int r, int b, CANVAS_ALPHATYPE alpha) {
        if (!dst || !src) return;
        try {
            int w = static_cast<int>(src->width), h = static_cast<int>(src->height);
            int sx = 0, sy = 0;
            if (x < l) { sx = l - x; w -= sx; x = l; }
            if (y < t) { sy = t - y; h -= sy; y = t; }
            if (x + w > r) w = r - x;
            if (y + h > b) h = b - y;
            if (w <= 0 || h <= 0) return;                 // entirely outside the box
            dst->CopyEx(x, y, src, alpha, w, h, sx, sy, w, h);
        } catch (...) {
        }
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
    bool NeedsDrop() const {
        return m_tab == kTabItem || m_tab == kTabSkill;
    }
    // Skin joins Hair and Eye on the LOOK side: nothing to drag in, and the server reads
    // the character's own nSkin rather than an inventory address.
    // Hair and Face take the LOOK actions rather than apply/restore: they have no
    // inventory address for the server to re-verify, so it reads the character's own look.
    // Does the previewed effect frame need mirroring?
    //
    // The avatar and the skill effect art disagree about which way "unflipped" faces, so the
    // DEFAULT pose is the one that needs reversing -- keying the mirror off m_bFacingLeft
    // itself left the resting preview flipped, which is the case anyone looks at first.
    // m_bFacingLeft tracks the walk direction, so the mirror rides its inverse.
    //
    // The pixels and the placement MUST read this same accessor: mirroring moves the origin to
    // the far edge, so a frame reversed by one rule and positioned by the other lands on the
    // wrong side of the character.
    bool EffectNeedsMirror() const { return !m_bFacingLeft; }

    bool IsLookTab() const {
        return m_tab == kTabHair || m_tab == kTabEye || m_tab == kTabSkin;
    }

    // Which of the item's two layers the chips currently have selected. A cash effect item is
    // glow and nothing else, so it answers GLOW whatever the chip says -- pointing at the bare
    // item id for one would make a dropped cash effect look like the window had stopped working.
    bool DyeingGlow() const {
        if (!m_target.itemId) return false;
        if (IsCashEffectItemId(m_target.itemId)) return true;
        return m_layer == kChipGlow;
    }
    // Does the dropped item have a second layer at all? Both answers are structural rather than
    // a preference: a cash effect has no sprite, and most equips have no glow.
    bool HasGlowLayer() const {
        if (!m_target.itemId) return false;
        return IsCashEffectItemId(m_target.itemId)
            || WeaponTint_ItemHasEffectArt(m_target.itemId);
    }
    bool HasItemLayer() const {
        return m_target.itemId && !IsCashEffectItemId(m_target.itemId);
    }

    int TargetKey() const {
        switch (m_tab) {
            case kTabItem:    return !m_target.itemId ? 0
                                   : DyeingGlow() ? EffectTintKeyFor(m_target.itemId)
                                                  : m_target.itemId;
            case kTabSkill:   return m_target.skillId ? SkillTintKeyFor(m_target.skillId) : 0;
            case kTabHair:    return kTintKey_Hair;
            case kTabSkin:    return kTintKey_Skin;
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
        // Latch the PRESS for the skill preview before the level test below swallows it.
        if (m_bAttackReq) m_bFxTrigger = true;
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
        else if (attacking) {
            // On the Skills tab, cast in the pose the SKILL names, and when it names none,
            // in the attack pose of the WEAPON being held -- so a bowman skill shoots and a
            // polearm skill sweeps, rather than every skill swinging a one-handed sword.
            int code = 5;                                  // swingO1, the generic attack
            if (m_tab == kTabSkill && m_target.skillId > 0) {
                const int sc = SkillActionCode(m_target.skillId);
                code = (sc != kNoActionCode) ? sc : WeaponAttackActionCode(m_pAvatar);
            }
            ApplyPlayPose(2, code);
        }
        else if (moving) ApplyPlayPose(1, kNoActionCode);
        else if (m_keyD) ApplyPlayPose(5, kNoActionCode);
        else ApplyPlayPose(2, kNoActionCode);
        m_bAttackReq = false;
        // The avatar is parented to the WINDOW layer, so its position is in canvas
        // space and takes the margin. m_avX/m_avY stay in chrome space, which is what
        // the physics clamps and the effect blit are written against.
        MoveAvatar(m_pAvatar, static_cast<int>(m_avX) + m_marginX,
                   static_cast<int>(m_avY) + m_marginY);
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
    void ReleaseEffect();
    void RefreshEffect();
    // Offsets move the effect into the target canvas; l/t/r/b clip it there. Called twice per
    // Draw: once into the chrome, once for the four margin bands around it.
    void DrawSkillFx(IWzCanvasPtr pCanvas, int ox, int oy, int l, int t, int r, int b,
                     CANVAS_ALPHATYPE alpha);

    void SendConfirm();
    void CloseNow();
    void ResetValues() {
        m_tint = WeaponTint{};
        PushPreview();
    }

    // Is the cursor inside the drop WELL? Padded outward, because a dragged icon is
    // held by the point it was grabbed at rather than by its centre, so the cursor
    // sits a few pixels off from where the player thinks the icon is.
    // The chip rect, padded: 14px is a small thing to aim at, so the hit target is bigger than
    // the art, exactly as the well's own is.
    static void ChipRect(int which, RECT& rc) {
        rc.left   = kChipX[which] - kChipPad;
        rc.top    = kChipY - kChipPad;
        rc.right  = kChipX[which] + kChipSize + kChipPad;
        rc.bottom = kChipY + kChipSize + kChipPad;
    }
    bool ChipEnabled(int which) const {
        if (m_tab != kTabItem || !m_target.itemId) return false;
        return which == kChipGlow ? HasGlowLayer() : HasItemLayer();
    }
    // Force the selection onto a layer the item actually has. Called whenever the well changes:
    // dropping a plain equip while Glow was selected would otherwise leave the sliders pointed
    // at a key nothing will ever draw.
    void SnapLayer() {
        if (!m_target.itemId) { m_layer = kChipItem; return; }
        if (m_layer == kChipGlow && !HasGlowLayer()) m_layer = kChipItem;
        if (m_layer == kChipItem && !HasItemLayer()) m_layer = kChipGlow;
    }

    bool CursorOverWell() const {
        POINT sp;
        if (!GetAbsCursor(sp)) return false;
        const int x = sp.x - m_screenX, y = sp.y - m_screenY;
        constexpr int kPad = 4;
        return x >= kIconX - kPad && x < kIconX + kIconSize + kPad
            && y >= kIconBaseline - kIconSize - kPad && y < kIconBaseline + kPad;
    }

    // Adopt a dropped SKILL as the thing being dyed.
    //
    // A skill has no inventory address at all, so it does not fill invType/invPos the way an
    // item does; the id IS the target. It is validated only for range, because the client
    // only ever hands over a skill the character actually owns -- the drag cannot start
    // anywhere else.
    bool SetSkillTarget(int skillId) {
        CommitValueEdit();
        if (skillId <= 0) return false;
        if (m_target.skillId && m_target.skillId != skillId) {
            WeaponTint_SetPreview(SkillTintKeyFor(m_target.skillId), WeaponTint{}, false);
        }
        m_target.invType = 0;
        m_target.invPos  = 0;
        m_target.itemId  = 0;
        m_target.skillId = skillId;
        if (m_tab != kTabSkill) SetTab(kTabSkill);
        m_tint = WeaponTint_GetSavedFor(TargetKey());
        ReleaseEffect();                 // the pane must rebuild for the new skill
        m_bAvatarDirty = true;
        play_ui_sound(L"DragEnd");
        InvalidateRect(nullptr);
        return true;
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
        // Cash EFFECT items (5010000..5019999) are admitted alongside equips. They
        // are not equips at all: they sit in the Cash tab and play an effect around the
        // character, and the Effects tab dyes that effect the same way it dyes an item's
        // glow, through the same key and the same columns.
        if (!IsDyeableEquip(itemId) && !IsCashEffectItemId(itemId)) {
            // Refused. This used to be a bare sound, which is why a gate that rejected
            // EVERYTHING looked exactly like a drop that never arrived -- so say which
            // item was turned away, once.
            LOG_ONCE("coloringprism: refused item %d (not an equip or cash effect)", itemId);
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
        if (m_tab != kTabItem) SetTab(kTabItem);
        SnapLayer();                                    // never point at a layer it lacks
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
        Ztl_variant_t v = pNode->item[L"name"];
        if (v.vt != VT_BSTR) return false;
        const char* p = (const char*)_bstr_t(v);
        if (!p || !*p) return false;
        strncpy(out, p, cap - 1);
        out[cap - 1] = '\0';
        return true;
    };

    // Cash first: this window dyes Cash items, so it hits on the first try in the
    // overwhelming majority of cases.
    try {
        IWzPropertyPtr pCash = get_rm()->GetObjectA(L"String/Cash.img").GetUnknown();
        if (pCash && take(IWzPropertyPtr(pCash->item[key].GetUnknown()))) return true;
    } catch (...) {}

    static const wchar_t* kEqpCategories[] = {
        L"Cap", L"Accessory", L"Coat", L"Longcoat", L"Pants", L"Shoes", L"Glove",
        L"Shield", L"Cape", L"Ring", L"Weapon", L"PetEquip", L"Taming", L"Mechanic",
    };
    try {
        IWzPropertyPtr pEqp = get_rm()->GetObjectA(L"String/Eqp.img").GetUnknown();
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
    m_pBg = LoadSprite(L"UI/UIWindow.img/ColorPrism/backgrnd");
    m_pBgLook = LoadSprite(L"UI/UIWindow.img/ColorPrism/backgrndLook");
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
    static const wchar_t* kChipKind[2]  = { L"item", L"glow" };
    static const wchar_t* kChipState[3] = { L"normal", L"on", L"off" };
    for (int k = 0; k < 2; ++k) {
        for (int s = 0; s < 3; ++s) {
            wchar_t p[80];
            _snwprintf_s(p, _countof(p), _TRUNCATE,
                         L"UI/UIWindow.img/ColorPrism/LayerBt/%s/%s", kChipKind[k], kChipState[s]);
            m_pChip[k][s] = LoadSprite(p);
        }
    }
    m_pBtClose[0] = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/normal/0");
    m_pBtClose[1] = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/mouseOver/0");
}

CUIColorPrism::CUIColorPrism(int nLeft, int nTop)
    : m_screenX(nLeft), m_screenY(nTop), m_marginX(0), m_marginY(0),
      m_bMarginPaint(false), m_tab(s_tab),
      m_bFocused(0), m_keyL(false), m_keyR(false), m_keyD(false),
      m_avX(static_cast<float>(kAvatarX)), m_avY(static_cast<float>(kAvatarY)),
      m_velX(0.0f), m_velY(0.0f), m_bAirborne(false), m_bFacingLeft(false),
      m_bAttackReq(false), m_tLastStep(GetTickCount()), m_nLastMA(-1), m_nLastCode(-2),
      m_layer(kChipItem), m_chipHover(-1),
      m_nSkillIconId(0), m_bSkillFxPlaying(false), m_bFxTrigger(false),
      m_nSkillFxNodeId(0),
      m_pEffectLayer(nullptr), m_nEffectItem(0), m_nEffectMA(-1), m_nEffectCode(kNoActionCode),
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

    // The chrome canvas decides whether there is a margin at all, so it is settled BEFORE the
    // window is sized. Built here rather than in Draw: creating COM canvases inside a draw call
    // is a known crash source in this window's history.
    m_marginX = m_marginY = 0;
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", m_pChrome, nullptr);
        if (m_pChrome) {
            m_pChrome->Create(kWndW, kWndH, 0, CP_A8R8G8B8);
            m_marginX = kMarginX;
            m_marginY = kMarginY;
        }
    } catch (...) {
        m_pChrome = nullptr;
    }
    if (!m_pChrome) m_marginX = m_marginY = 0;
    // The window layer starts at (chrome - margin), so the chrome cannot sit closer to the top
    // left than the margin without putting that origin negative. Nudging it in is better than
    // finding out how CreateWnd handles a negative corner.
    if (m_screenX < m_marginX) m_screenX = m_marginX;
    if (m_screenY < m_marginY) m_screenY = m_marginY;

    // Focus immediately so arrow keys and attack/jump controls drive the preview,
    // not the field player.
    CreateWnd(m_screenX - m_marginX, m_screenY - m_marginY,
              CanvasW(), CanvasH(), 10, 1, nullptr, 1);

    // A8R8G8B8, OR THE MARGIN CANNOT BE ERASED. A window canvas is handed over as A4R4G4B4,
    // and the only thing that erases one is a raw-surface write, which handles A8R8G8B8 alone
    // -- so the clear refused on the format check before touching a pixel, and the margin
    // accumulated every frame of the animation. Nothing else here needs the extra bits.
    if (m_pChrome) {
        int fmt = 0;
        try {
            if (IWzCanvasPtr c = GetCanvas()) {
                c->pixelFormat = CP_A8R8G8B8;
                fmt = static_cast<int>(c->pixelFormat);
            }
        } catch (...) {
        }
        LOG_ONCE("coloringprism: window canvas %dx%d pixel format %d (%d wanted)",
                 CanvasW(), CanvasH(), fmt, static_cast<int>(CP_A8R8G8B8));
    }

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

void CUIColorPrism::ReleaseEffect() {
    if (m_pEffectLayer) {
        try { m_pEffectLayer->Release(); } catch (...) {}
        m_pEffectLayer = nullptr;
    }
    m_nEffectItem = 0;
    m_nEffectMA   = -1;
    m_nEffectCode = kNoActionCode;
}

// (Re)built when the item or the pose changes.
//
// THE MEMO IS THE WHOLE TEST, deliberately not `m_pEffectLayer && ...`. A null layer is a
// legitimate cached answer of "this WZ shape is not one we play", and demanding a live
// layer here would re-run three GetObjectA probes every frame for as long as such an item
// stayed on the well.

// Paint the frame Update already resolved and tinted. This does NOTHING but blit.
//
// A BLIT, NOT A LAYER. Handing the client a Gr2D layer built from tinted clones crashed five
// different ways: the clone cache frees a colour's clones the instant that colour changes,
// and once the client owns the layer no ordering on our side makes that safe. Nothing here is
// owned by anyone else, and each canvas is held by its SkillFx rather than borrowed.
void CUIColorPrism::DrawSkillFx(IWzCanvasPtr pCanvas, int ox, int oy,
                                int l, int t, int r, int b, CANVAS_ALPHATYPE alpha) {
    if (!pCanvas) return;
    for (int i = 0; i < kSkillFxNodeCount; ++i) {
        const SkillFx& fx = m_skillFx[i];
        if (!fx.frame) continue;
        try {
            int w = 0;
            try { w = static_cast<int>(fx.frame->width); } catch (...) {}
            if (w <= 0) continue;

            // FACING. The origin is the anchor point on the character, so subtracting it puts
            // the sprite where the avatar is. A mirrored frame carries that anchor on the far
            // edge, so it is measured from there instead.
            const int x = ox + (EffectNeedsMirror()
                        ? static_cast<int>(m_avX) - (w - fx.ox)
                        : static_cast<int>(m_avX) - fx.ox);
            const int y = oy + static_cast<int>(m_avY) - fx.oy;

            // TRUE SIZE. Shrinking it to fit would preview the wrong SIZE, which is half of
            // what an effect is, so it runs over the chrome and on into the margin the way it
            // would cover the screen in game. Clipping needs no repositioning either, so the
            // effect stays anchored to the character rather than nudged around to fit.
            BlitAClipped(pCanvas, fx.frame, x, y, l, t, r, b, alpha);
        } catch (...) {
        }
    }
}

void CUIColorPrism::RefreshEffect() {
    if (!m_pAvatar) { ReleaseEffect(); return; }

    // A SKILL preview is a BLIT, not a layer; the frame cursor is advanced here and the
    // drawing happens in DrawSkillFx. See the note there for why.
    if (m_tab == kTabSkill && m_target.skillId > 0) {
        ReleaseEffect();                      // no layer is ever built for a skill
        const DWORD now = GetTickCount();
        if (m_nSkillFxNodeId != m_target.skillId) {
            m_nSkillFxNodeId = m_target.skillId;
            for (int i = 0; i < kSkillFxNodeCount; ++i) {
                m_skillFx[i] = SkillFx();
                try {
                    wchar_t path[160];
                    _snwprintf_s(path, _countof(path), _TRUNCATE,
                                 L"Skill/%03d.img/skill/%07d/%s",
                                 m_target.skillId / 10000, m_target.skillId,
                                 kSkillFxNodes[i]);
                    m_skillFx[i].node = get_rm()->GetObjectA(path).GetUnknown();
                } catch (...) {}
            }
            m_bSkillFxPlaying = false;
            m_bFxTrigger      = false;
        }
        // STARTED BY THE KEYPRESS, not by the pose. The pose is a LEVEL -- some skill actions
        // never return to kNoActionCode, so it reads as attacking indefinitely, which both
        // looped the effect and restarted it the moment it managed to stop. A latched press
        // fires exactly once.
        if (m_bFxTrigger) {
            m_bFxTrigger      = false;
            m_bSkillFxPlaying = true;
            for (int i = 0; i < kSkillFxNodeCount; ++i) {
                m_skillFx[i].index = 0;
                m_skillFx[i].at    = now;
            }
        }

        // RESOLVE AND TINT HERE, NOT IN Draw. Cloning is COM canvas work -- create, copy,
        // lock, unlock -- and doing it re-entrantly while the client is painting is what made
        // the preview render for a moment and then die. Draw now only blits.
        int running = 0;                      // nodes still holding a cursor this pass
        for (int i = 0; i < kSkillFxNodeCount; ++i) {
            SkillFx& fx = m_skillFx[i];
            if (!m_bSkillFxPlaying || !fx.node) { fx.frame = nullptr; continue; }
            ++running;
            try {
                wchar_t idx[12];
                _snwprintf_s(idx, _countof(idx), _TRUNCATE, L"%d", fx.index);
                Ztl_variant_t v = fx.node->item[idx];
                IUnknownPtr pUnk = get_unknown(v);
                IWzCanvasPtr pFrame;
                if (pUnk) pUnk.QueryInterface(__uuidof(IWzCanvas), &pFrame);
                // Out of frames. The cursor is NOT wound back: leaving it past the end is
                // what makes this permanent until the next keypress, and winding it back is
                // what used to leave the effect on screen forever. A node whose frame 0 is
                // not a canvas at all lands here too and simply never draws.
                if (!pFrame) { fx.frame = nullptr; continue; }

                int delay = 100;              // the client's default when a frame has none
                try { delay = get_int32(pFrame->property->item[L"delay"], 100); } catch (...) {}
                if (delay <= 0) delay = 100;
                if (now - fx.at >= static_cast<DWORD>(delay)) {
                    fx.at = now;
                    ++fx.index;
                }
                fx.ox = fx.oy = 0;
                try {
                    IWzVector2DPtr pOrigin = pFrame->property->item[L"origin"].GetUnknown();
                    if (pOrigin) { fx.ox = pOrigin->x; fx.oy = pOrigin->y; }
                } catch (...) {}
                // MIRRORED when the avatar faces left. Placing the sprite on the other side
                // was not enough on its own: anything with a direction to it, a slash or a
                // thrown bolt, still pointed right while the character faced left.
                fx.frame = WeaponTint_TintedCanvasFor(pFrame, m_tint, EffectNeedsMirror());
            } catch (...) {
                fx.frame = nullptr;
            }
        }
        // ONE PASS AND DONE. When every node has run out there is nothing left to draw, and
        // saying so here is what finally clears the margin: the effect stops being painted
        // rather than being painted over.
        bool anyFrame = false;
        for (int i = 0; i < kSkillFxNodeCount; ++i) {
            if (m_skillFx[i].frame) { anyFrame = true; break; }
        }
        if (m_bSkillFxPlaying && running > 0 && !anyFrame) m_bSkillFxPlaying = false;
        return;
    }

    const int want = (m_target.itemId && IsCashEffectItemId(m_target.itemId))
                   ? m_target.itemId : 0;
    if (want == 0) { ReleaseEffect(); return; }
    if (m_nEffectItem == want && m_nEffectMA == m_nLastMA && m_nEffectCode == m_nLastCode) {
        return;
    }
    ReleaseEffect();
    // The layer keeps refs to whatever canvases it is built from, so the tint has to be in
    // the tree for the duration of the build. This is the preview avatar, so the swap
    // resolves the window's LIVE slider value rather than the saved colour.
    WeaponTint_BeginCashEffectSwap(m_pAvatar, want);
    m_pEffectLayer = CreateEffectLayer(m_pAvatar, want,
                                       ActionNameForPose(m_nLastMA, m_nLastCode));
    WeaponTint_EndCashEffectSwap();
    // Recorded even when the build FAILED, so an unplayable shape is attempted once per
    // pose rather than once per frame.
    m_nEffectItem = want;
    m_nEffectMA   = m_nLastMA;
    m_nEffectCode = m_nLastCode;
}

void CUIColorPrism::ReleaseAvatar() {
    ReleaseEffect();                   // BEFORE the avatar: it holds the avatar's vectors
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
    SehBuildAvatar(m_avatarRef, charData, pLayer, static_cast<int>(m_avX) + m_marginX,
                   static_cast<int>(m_avY) + m_marginY,
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
        const int kind = (m_tab == kTabHair) ? kTintKey_Hair
                       : (m_tab == kTabSkin) ? kTintKey_Skin
                       : kTintKey_Face;
        if (undo) WeaponTint_SendRestoreLook(kind, s_prismPos);
        else      WeaponTint_SendApplyLook(kind, m_tint, s_prismPos);
    } else if (m_tab == kTabSkill) {
        // By skill ID. The item actions carry an inventory address the server re-reads, and a
        // skill has none, so sending one of those with itemId zero is what made a skill tint
        // last only as long as the session.
        if (undo) WeaponTint_SendRestoreSkill(m_target.skillId, s_prismPos);
        else      WeaponTint_SendApplySkill(m_target.skillId, m_tint, s_prismPos);
    } else {
        const int layer = DyeingGlow() ? kTintLayer_Effects : kTintLayer_Body;
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
                    // Same floor as at creation: the layer origin must not go negative.
                    if (m_screenX < m_marginX) m_screenX = m_marginX;
                    if (m_screenY < m_marginY) m_screenY = m_marginY;
                    MoveWnd(m_screenX - m_marginX, m_screenY - m_marginY);
                }
            }
        }
    }
    // Rebuild the pane avatar at most ~6/sec while the sliders move; every rebuild
    // re-runs CAvatar::Init and re-tints the weapon's frames. A failed build leaves
    // the flag SET so the next tick retries -- clearing it up front would wedge the
    // pane blank for the rest of the session after one bad frame.
    if (m_bAvatarDirty) {
        const DWORD now = GetTickCount();
        if (now - m_nAvatarDirtyTick >= 150) {
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
    // After the pose has settled and the override is resynced, so a per-action effect is
    // built for the pose actually on screen.
    RefreshEffect();
    InvalidateRect(nullptr);
}

// ---------------------------------------------------------------------------
void CUIColorPrism::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr pReal = GetCanvas();
    if (!pReal) return;
    // The margin is UNDEFINED MEMORY until something paints it, because CWnd::Draw's own body
    // only blits a background handle a code-drawn window never sets. Clearing it every Draw is
    // also what makes it go away the moment a skill effect stops playing.
    // CLEARED BY WRITING THE RAW SURFACE. Nothing else works: raw_DrawRectangle(0,0,w,h,0) and
    // CopyEx with CA_OVERWRITE from a transparent source both return S_OK and erase nothing,
    // which is why the margin accumulated every frame of an animation on top of the last. The
    // chrome canvas needs no clear -- the background art repaints all of it, opaque, every time.
    if (m_pChrome) {
        m_bMarginPaint = WeaponTint_ClearCanvas(pReal.GetInterfacePtr(),
                                                CanvasW(), CanvasH());
    }
    IWzCanvasPtr pCanvas = m_pChrome ? m_pChrome : pReal;
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
    static const char* kTabLabel[kTabCount] = { "Items", "Skills", "Hair", "Eyes", "Skin" };
    for (int t = 0; t < kTabCount; ++t) {
        const bool selected = (m_tab == t);
        DrawInventoryTab(pCanvas, kTabX[t], kTabT, kTabW, selected);
        DrawTextCentred(pCanvas, selected ? pfLt : pfDk, kTabX[t], kTabW,
                        kTabT + (kTabH - 12) / 2, kTabLabel[t]);
    }
    // The rule between the two groups, engraved the way the rest of this chrome is: one dark
    // line with a light one under it. Inset a few pixels top and bottom so it reads as a
    // divider rather than as the edge of another tab.
    try {
        pCanvas->raw_DrawRectangle(kTabSepX,     kTabT + 4, 1, kTabH - 8, 0xFF6E7480);
        pCanvas->raw_DrawRectangle(kTabSepX + 1, kTabT + 4, 1, kTabH - 8, 0xFFE8ECF4);
    } catch (...) {
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
        if (m_target.skillId > 0) {
            // A SKILL has no item icon, so the item path draws nothing at all -- which is
            // why the well looked empty while the target was in fact set. Every one of the
            // 616 player skills carries an icon node, so blit that instead.
            //
            // Cached on the id: this runs every repaint, and GetObjectA on each one would
            // re-resolve the same node while the sliders are being dragged.
            if (m_nSkillIconId != m_target.skillId) {
                wchar_t path[128];
                _snwprintf_s(path, _countof(path), _TRUNCATE,
                             L"Skill/%03d.img/skill/%07d/icon",
                             m_target.skillId / 10000, m_target.skillId);
                m_pSkillIcon = LoadSprite(path);
                m_nSkillIconId = m_target.skillId;
                if (!m_pSkillIcon) {
                    LOG_ONCE("coloringprism: no icon node for skill %d", m_target.skillId);
                }
            }
            if (m_pSkillIcon) {
                // kIconBaseline is the BOTTOM of the 32x32 item icon, so a skill icon of a
                // different height has to sit on the same baseline rather than the same top.
                int h = kIconSize;
                try { h = static_cast<int>(m_pSkillIcon->height); } catch (...) {}
                BlitA(pCanvas, m_pSkillIcon, kIconX, kIconBaseline - h);
            }
        } else if (auto* pItemInfo = CItemInfo::GetInstance()) {
            try {
                pItemInfo->DrawItemIconForSlot(pCanvas, m_target.itemId,
                                               kIconX, kIconBaseline, 0, 0, 0, 0, 0, 0);
            } catch (...) {
                LOG_ONCE("coloringprism: icon draw threw for item %d", m_target.itemId);
            }
        }
    }

    // COMPOSITED over the pane, not copied onto it: CA_REMOVEALPHA is the alpha-blended
    // blit despite the name reading backwards. CA_OVERWRITE here replaced the pane with the
    // effect's own translucency, which is most of why the preview and the cast looked like
    // two different colours.
    // (4) The LAYER CHIPS, badged on the well. Only on the Item tab, and only once something is
    //     on the well: before that there is no item whose layers they would describe.
    if (m_tab == kTabItem && m_target.itemId) {
        for (int c = 0; c < 2; ++c) {
            const int state = !ChipEnabled(c) ? 2
                            : ((m_layer == c) ? 1 : 0);
            if (m_pChip[c][state]) BlitA(pCanvas, m_pChip[c][state], kChipX[c], kChipY);
        }
    }

    DrawSkillFx(pCanvas, 0, 0, 0, 0, kWndW, kWndH, CANVAS_ALPHATYPE::CA_REMOVEALPHA);

    // (5) Instructions on the blue banner, in white. What they say AND where they sit
    //     depend on the tab, because the two halves of this window work differently.
    //     Equip and Effects want a drop, so their copy clears the icon well; Hair and
    //     Face are ready the moment they are selected, so theirs loses the drag line
    //     and centres on the full width.
    // PER TAB, indexed by the tab id. One shared block used to serve all three drop tabs, so
    // the Skills tab read "Drag a Cash item onto the slot" -- the one instruction a player on
    // that tab cannot act on. The look tabs share their copy because they genuinely do the
    // same thing. Lines run to 35 characters in the 213px band, which is what fits.
    static const char* const kBannerDrop[kTabCount][3] = {
        /* Item   */ { "Drag an item onto the slot.",
                       "Adjust the sliders, then press OK.",
                       "One Coloring Prism is used per dye." },
        /* Skills */ { "Drag a skill onto the slot.",
                       "Press attack to preview the cast.",
                       "One Coloring Prism is used per dye." },
        /* Hair   */ { nullptr, nullptr, nullptr },
        /* Eyes   */ { nullptr, nullptr, nullptr },
        /* Skin   */ { nullptr, nullptr, nullptr },
    };
    // Once an item is on the well the middle line stops being an instruction and becomes a
    // STATUS: which of the item's two layers the sliders are about to dye. That is the labelling
    // the chips cannot carry -- there is no room for a word beside a 14px badge -- and it reads
    // as a sentence rather than creaking as a control label.
    const char* itemStatus = nullptr;
    if (m_tab == kTabItem && m_target.itemId) {
        itemStatus = DyeingGlow() ? "Dyeing this item's effects."
                                  : "Dyeing the item itself.";
    }
    static const char* kBannerLook[2] = {
        "Adjust the sliders, then press OK.",
        "One Coloring Prism is used per dye.",
    };
    if (NeedsDrop()) {
        for (int i = 0; i < 3; ++i) {
            const char* line = (m_tab >= 0 && m_tab < kTabCount) ? kBannerDrop[m_tab][i]
                                                                 : nullptr;
            if (i == 1 && itemStatus) line = itemStatus;
            if (line)
                DrawTextCentred(pCanvas, pfLt, kBannerTextX, kBannerTextW,
                                kBannerTextT + i * kBannerLineH, line);
        }
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

    // (7) CHIP TOOLTIP, last so nothing paints over it.
    //
    // Drawn here rather than through the client's own: ShowItemToolTip takes an ITEM and renders
    // the full item card, so there is no stock way to say a sentence. A 10px silhouette needs
    // this; the icons only have to be distinguishable, the words do the explaining.
    if (m_chipHover >= 0 && m_tab == kTabItem && m_target.itemId) {
        const bool glow = (m_chipHover == kChipGlow);
        const char* l0 = glow ? "Dye the effects this" : "Dye the item's own";
        const char* l1 = glow ? "item plays"           : "colours";
        if (glow && !HasGlowLayer())      { l0 = "This item has no"; l1 = "effects to dye"; }
        else if (!glow && !HasItemLayer()) { l0 = "This is an effect"; l1 = "with no item"; }

        // At the TOP of the pane, clear of the avatar, rather than floating just above the chip
        // that raised it. The chips sit on the pane's bottom edge, so a tooltip anchored to them
        // lands squarely over the character -- which is the one thing in this window that must
        // stay readable while a colour is being judged. Centred on the pane for the same reason:
        // detached from the chip, it has no reason to sit off to one side.
        constexpr int kTipW = 132, kTipH = 32;
        const int tx = (kPreviewL + kPreviewR) / 2 - kTipW / 2;
        const int ty = kPreviewT + 4;
        try {
            pCanvas->raw_DrawRectangle(tx, ty, kTipW, kTipH, 0xFF2B2F3A);
            pCanvas->raw_DrawRectangle(tx, ty, kTipW, 1, 0xFF8F97A6);
            pCanvas->raw_DrawRectangle(tx, ty + kTipH - 1, kTipW, 1, 0xFF8F97A6);
            pCanvas->raw_DrawRectangle(tx, ty, 1, kTipH, 0xFF8F97A6);
            pCanvas->raw_DrawRectangle(tx + kTipW - 1, ty, 1, kTipH, 0xFF8F97A6);
        } catch (...) {
        }
        DrawTextCentred(pCanvas, pfLt, tx, kTipW, ty + 3, l0);
        DrawTextCentred(pCanvas, pfLt, tx, kTipW, ty + 16, l1);
    }

    { const int off = m_nClosePressed ? 1 : 0;
      BlitA(pCanvas, m_nCloseHover ? m_pBtClose[1] : m_pBtClose[0],
            kBtCloseX + off, kBtCloseY + off); }

    // Re-assert the held-button cursor; the engine resets the sprite each frame.
    if (m_nBtnPressed >= 0 || m_nClosePressed || m_sliderDrag >= 0) {
        SetCursorState(kCursor_ButtonPress);
    }

    if (!m_pChrome) return;                          // no margin: pCanvas WAS the real canvas
    // The finished chrome, verbatim. CA_OVERWRITE copies the source alpha rather than
    // compositing it, so the window's transparent corners stay transparent.
    BlitA(pReal, m_pChrome, m_marginX, m_marginY);

    // ...and then the parts of the effect that fall OUTSIDE the chrome, into the four bands
    // around it. Four clips rather than one because the middle is already drawn, at the right
    // depth: inside the window the effect sits over the pane and under the sliders, exactly as
    // before, and only its overflow reaches the margin.
    if (!m_bMarginPaint) return;                     // cannot erase it, so do not paint it
    const int mL = m_marginX, mT = m_marginY;
    const int mR = m_marginX + kWndW, mB = m_marginY + kWndH;
    // VERBATIM out here, unlike the pane pass above. The margin is transparent with the game
    // world behind it, and CA_REMOVEALPHA flattens a sprite cutout to opaque, which would wrap
    // the effect in a black box.
    const CANVAS_ALPHATYPE keep = CANVAS_ALPHATYPE::CA_OVERWRITE;
    DrawSkillFx(pReal, mL, mT, 0,  0,  mL,        CanvasH(), keep);   // left
    DrawSkillFx(pReal, mL, mT, mR, 0,  CanvasW(), CanvasH(), keep);   // right
    DrawSkillFx(pReal, mL, mT, mL, 0,  mR,        mT,        keep);   // top
    DrawSkillFx(pReal, mL, mT, mL, mB, mR,        CanvasH(), keep);   // bottom
}

// ---------------------------------------------------------------------------
void CUIColorPrism::OnMouseButton(unsigned int msg, unsigned int /*wParam*/, int rx, int ry) {
    // Into chrome coordinates, which is what every hit rect below is written in.
    rx -= m_marginX; ry -= m_marginY;
    POINT pt{ rx, ry };

    if (msg == WM_LBUTTONDOWN) {
        // Moving to another control commits the current field first, so a player can
        // type a value and immediately drag a different slider or press OK.
        CommitValueEdit();
        for (int t = 0; t < kTabCount; ++t) {
            RECT rc = { kTabX[t], kTabT, kTabX[t] + kTabW, kTabT + kTabH };
            if (PtInRect(&rc, pt)) { SetTab(t); return; }
        }
        // The layer chips. A disabled one is CONSUMED, not ignored: the click landed on a
        // control, and letting it fall through to the well underneath would read as the chip
        // being decorative rather than unavailable.
        if (m_tab == kTabItem && m_target.itemId) {
            for (int c = 0; c < 2; ++c) {
                RECT rc; ChipRect(c, rc);
                if (!PtInRect(&rc, pt)) continue;
                if (ChipEnabled(c) && m_layer != c) {
                    CommitValueEdit();
                    m_layer = c;
                    m_tint = WeaponTint_GetSavedFor(TargetKey());   // that layer's own colour
                    m_bAvatarDirty = true;
                    play_ui_sound(L"BtMouseClick");
                }
                InvalidateRect(nullptr);
                return;
            }
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
    rx -= m_marginX; ry -= m_marginY;               // into chrome coordinates
    POINT pt{ rx, ry };
    int hover = -1;
    for (int i = 0; i < kBtnCount; ++i) {
        RECT rc = { kBtnX[i], kBtnT, kBtnX[i] + kBtnW, kBtnT + kBtnH };
        if (PtInRect(&rc, pt)) { hover = i; break; }
    }
    RECT rcClose = { kBtCloseX, kBtCloseY, kBtCloseX + kBtCloseW, kBtCloseY + kBtCloseH };
    const int closeHover = PtInRect(&rcClose, pt) ? 1 : 0;
    int chipHover = -1;
    if (m_tab == kTabItem && m_target.itemId) {
        for (int c = 0; c < 2; ++c) {
            RECT rc; ChipRect(c, rc);
            if (PtInRect(&rc, pt)) { chipHover = c; break; }
        }
    }
    if (chipHover != m_chipHover) {
        m_chipHover = chipHover;
        InvalidateRect(nullptr);
    }
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
// A SKILL dropped on the well. Separate from the item drop because a skill drag is a
// different draggable class entirely; see the file header.
bool ColorPrism_HandleSkillDrop(void* pTo, int skillId) {
    auto* w = ColorPrism::CUIColorPrism::ms_pInstance;
    if (!w) return false;
    const bool bAddressed =
        pTo && (pTo == static_cast<void*>(w) || pTo == reinterpret_cast<char*>(w) + 4);
    if (!bAddressed && !w->CursorOverWell()) return false;
    return w->SetSkillTarget(skillId);
}

bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos) {
    auto* w = ColorPrism::CUIColorPrism::ms_pInstance;
    if (!w) return false;

    const bool bAddressed =
        pTo && (pTo == static_cast<void*>(w) || pTo == reinterpret_cast<char*>(w) + 4);
    if (!bAddressed && !w->CursorOverWell()) return false;

    w->SetTarget(invType, invPos);
    return true;      // consumed either way: the item must not also be moved
}

// CDraggableSkill::OnDropped, vtable 0x00B39810 slot 1. Same argument shape as the item
// version at 0x004EF140, and the skill id sits at draggable+0x18 -- verified by the client's
// own use of it, which pushes that field into a CSkillInfo lookup at 0x007616F6.
//
// THIS FILE OWNS THIS ADDRESS. It owned none before, and the one-owner rule applies here as
// everywhere else: nothing else in the DLL Detours 0x004FAA22, and a second Detour on it
// would break whichever installed last.
using t_SkillDropped = int(__thiscall*)(void*, void*, void*, int, int);
auto CDraggableSkill_OnDropped =
    reinterpret_cast<t_SkillDropped>(0x004FAA22);

int SehSkillIdOfDraggable(void* pThis) {
    int id = 0;
    __try {
        id = *reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return id;
}

int __fastcall CDraggableSkill_OnDropped_Hook(void* pThis, void* /*edx*/, void* pFrom,
                                              void* pTo, int rx, int ry) {
    const int skillId = SehSkillIdOfDraggable(pThis);
    if (skillId > 0 && ColorPrism_HandleSkillDrop(pTo, skillId)) {
        return 1;      // consumed: the skill must not also land in a quickslot
    }
    return CDraggableSkill_OnDropped(pThis, pFrom, pTo, rx, ry);
}

void AttachColoringPrismMod() {
    ATTACH_HOOK(CDraggableSkill_OnDropped, CDraggableSkill_OnDropped_Hook);
    LogMessage("Coloring Prism: window ready (item %d)", ColorPrism::kItemColoringPrism);
}

int ColorPrism_PeekCashItemId(int invPos) {
    void* item = ColorPrism::SehItemAt(5, invPos);
    return ColorPrism::SehDecodeItemId(item);
}

void ColoringPrism_OpenWindow() { ColorPrism::OpenWindow(); }
void ColoringPrism_CloseWindow() {
    if (ColorPrism::CUIColorPrism::ms_pInstance) {
        ColorPrism::CUIColorPrism::ms_pInstance->CloseNow();
    }
}
bool ColoringPrism_IsOpen() { return ColorPrism::CUIColorPrism::ms_pInstance != nullptr; }
void ColoringPrism_RequestOpen() { WeaponTint_RequestSnapshot(); }
void ColoringPrism_HandleServerPacket(CompatInPacket* packet) { WeaponTint_HandleSync(packet); }
void ColoringPrism_OnTick() { WeaponTint_Tick(); }