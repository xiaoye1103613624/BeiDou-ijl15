#include "stdafx.h"

#include "compat/hook.h"

#include "compat/wvs/iteminfo.h"

#include "compat/ztl/ztl.h"

#include <intrin.h>



// ===========================================================================

// Equip-slot grid transmog badge — jmp patch at the DrawItemIconForSlot call

// inside CUIItem::Draw only (NOT a global hook on sub_5D6458).

// IDA-confirmed v083 addresses (image base 0x400000):

//   CUIItem::Draw              sub_81DC20  [0x0081DC20 .. 0x0081E255) size 0x635

//   hook call site             0x0081DEE9  call sub_5D6458

//   resume after call          0x0081DEEE

//   CUIItem singleton          dword_BED654 0x00BED654  (ctor sub_81C414)

//   CUIItem::SlotAtPoint       sub_81DB7E  0x0081DB7E

//   Draw loop slot index       [ebp+0x84]  (arg_0) in CUIItem::Draw

//   CUIEquip singleton         dword_BED650 0x00BED650  (ctor sub_832586)

//   CUIEquip::Draw             sub_83451B  [0x0083451B .. 0x008351CA) — also calls

//                              sub_5D6458 @ 0x8346F3 / 0x834F02 but never through this jmp

//   DrawItemIconForSlot        sub_5D6458  0x005D6458

// ===========================================================================



struct GW_ItemSlotBase : public ZRefCounted {

    virtual ~GW_ItemSlotBase() = 0;

    virtual int IsProtectedItem() = 0;

    virtual int IsPreventSlipItem() = 0;

    virtual int IsSupportWarmItem() = 0;

    virtual int IsBindedItem() = 0;

    virtual int IsPossibleTradingItem() = 0;

    virtual int GetType() = 0; // 1 = Equip

};



struct GW_ItemSlotEquip {

    MEMBER_AT(int, 0xF9, nAnvilItemID)

};



static constexpr uintptr_t kAddr_CUIItem_Draw          = 0x0081DC20;

static constexpr uintptr_t kAddr_CUIItem_Draw_End      = 0x0081E255; // +0x635

static constexpr uintptr_t kAddr_DrawItemIconForSlot_Call = 0x0081DEE9;

static constexpr uintptr_t kAddr_DrawItemIconForSlot_Ret  = 0x0081DEEE;

static constexpr uintptr_t kAddr_DrawItemIconForSlot      = 0x005D6458;



static constexpr uintptr_t kAddr_CUIItem_Instance   = 0x00BED654;

static constexpr uintptr_t kAddr_CUIEquip_Instance  = 0x00BED650;

static constexpr uintptr_t kAddr_CUIItem_SlotAtPoint = 0x0081DB7E;



static constexpr uintptr_t kAddr_InputSystem        = 0x00BEC33C;

static constexpr uintptr_t kAddr_GetCursorPos       = 0x0059A388;

static constexpr uintptr_t kAddr_GetWndAbsLeft      = 0x009E03C5;

static constexpr uintptr_t kAddr_GetWndAbsTop       = 0x009E0447;



static constexpr int kCUIItem_LayerOffset = 0x18;



struct SavedIconDrawCtx {

    IWzCanvasPtr pCanvas;

    int x;

    int y;

};



static SavedIconDrawCtx g_iconDrawCtx;



static int SafeGetItemType(GW_ItemSlotBase* pItem) {

    if (!pItem) {

        return 0;

    }

    int nType = 0;

    __try { nType = pItem->GetType(); }

    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }

    return nType;

}



static int SafeGetAnvilItemId(GW_ItemSlotBase* pItem) {

    if (!pItem) {

        return 0;

    }

    int nAnvilItemID = 0;

    __try { nAnvilItemID = reinterpret_cast<GW_ItemSlotEquip*>(pItem)->nAnvilItemID; }

    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }

    return nAnvilItemID;

}



static bool IsUiWindowVisible(uintptr_t singletonAddr) {

    void* ui = nullptr;

    __try { ui = *reinterpret_cast<void**>(singletonAddr); }

    __except (EXCEPTION_EXECUTE_HANDLER) { ui = nullptr; }

    if (!ui) {

        return false;

    }

    void* layer = nullptr;

    __try { layer = *reinterpret_cast<void**>(reinterpret_cast<char*>(ui) + kCUIItem_LayerOffset); }

    __except (EXCEPTION_EXECUTE_HANDLER) { layer = nullptr; }

    if (!layer) {

        return false;

    }

    int vis = 0;

    __try { vis = reinterpret_cast<IWzGr2DLayer*>(layer)->visible; }

    __except (EXCEPTION_EXECUTE_HANDLER) { vis = 0; }

    return vis != 0;

}



static bool IsInventoryShown() {

    return IsUiWindowVisible(kAddr_CUIItem_Instance);

}



static bool IsEquipWindowShown() {

    return IsUiWindowVisible(kAddr_CUIEquip_Instance);

}



static bool GetEngineCursor(POINT& sp) {

    sp.x = 0;

    sp.y = 0;

    void* pInputSystem = nullptr;

    __try { pInputSystem = *reinterpret_cast<void**>(kAddr_InputSystem); }

    __except (EXCEPTION_EXECUTE_HANDLER) { pInputSystem = nullptr; }

    if (!pInputSystem) {

        return false;

    }

    __try {

        reinterpret_cast<void(__thiscall*)(void*, POINT*)>(kAddr_GetCursorPos)(

            pInputSystem, &sp);

    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    return true;

}



static void GetInvWndAbs(void* inv, int& absL, int& absT) {

    absL = 0;

    absT = 0;

    if (!inv) {

        return;

    }

    void* pHandler = reinterpret_cast<char*>(inv) + 4;

    __try {

        absL = reinterpret_cast<int(__thiscall*)(void*)>(kAddr_GetWndAbsLeft)(pHandler);

        absT = reinterpret_cast<int(__thiscall*)(void*)>(kAddr_GetWndAbsTop)(pHandler);

    } __except (EXCEPTION_EXECUTE_HANDLER) {

        absL = 0;

        absT = 0;

    }

}



static int QueryInventoryHoveredSlot() {

    if (!IsInventoryShown() || IsEquipWindowShown()) {

        return 0;

    }



    void* inv = nullptr;

    __try { inv = *reinterpret_cast<void**>(kAddr_CUIItem_Instance); }

    __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }

    if (!inv) {

        return 0;

    }



    POINT sp;

    if (!GetEngineCursor(sp)) {

        return 0;

    }



    int absL = 0;

    int absT = 0;

    GetInvWndAbs(inv, absL, absT);

    if (absL <= 0 && absT <= 0) {

        return 0;

    }



    int slot = 0;

    __try {

        slot = reinterpret_cast<int(__thiscall*)(void*, int, int)>(

            kAddr_CUIItem_SlotAtPoint)(inv, sp.x - absL, sp.y - absT);

    } __except (EXCEPTION_EXECUTE_HANDLER) { slot = 0; }

    return (slot >= 1 && slot <= 96) ? slot : 0;

}



static int GetInventoryHoveredSlotCached() {

    static DWORD s_lastTick = 0;

    static int s_hoveredSlot = 0;

    const DWORD tick = GetTickCount();

    if (tick != s_lastTick) {

        s_lastTick = tick;

        s_hoveredSlot = QueryInventoryHoveredSlot();

    }

    return s_hoveredSlot;

}



static bool IsCallerInsideCUIItemDraw() {

    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());

    return ret >= kAddr_CUIItem_Draw && ret < kAddr_CUIItem_Draw_End;

}



static bool IsCUIItemDrawContext(void* pCUIItemThis, int nSlot) {

    if (!pCUIItemThis || nSlot < 1 || nSlot > 96) {

        return false;

    }

    void* inv = nullptr;

    __try { inv = *reinterpret_cast<void**>(kAddr_CUIItem_Instance); }

    __except (EXCEPTION_EXECUTE_HANDLER) { inv = nullptr; }

    return inv != nullptr && inv == pCUIItemThis;

}



// Badge overlay only — vanilla icon draw runs via sub_5D6458 in naked hook first.

void __cdecl TryDrawTransmogBadge(

    CItemInfo* pThis,

    GW_ItemSlotBase* pItem,

    void* pCUIItemThis,

    int nSlot)

{

    if (!pItem || !pThis || !g_iconDrawCtx.pCanvas) {

        return;

    }

    if (!IsCallerInsideCUIItemDraw()) {

        return;

    }

    if (!IsCUIItemDrawContext(pCUIItemThis, nSlot)) {

        return;

    }

    if (IsEquipWindowShown()) {

        return;

    }

    if (SafeGetItemType(pItem) != 1) {

        return;

    }

    const int nAnvilItemID = SafeGetAnvilItemId(pItem);

    if (!nAnvilItemID) {

        return;

    }

    if (!IsInventoryShown() || GetInventoryHoveredSlotCached() != nSlot) {

        return;

    }

    pThis->DrawItemIconForSlot(

        g_iconDrawCtx.pCanvas, nAnvilItemID,

        g_iconDrawCtx.x + 18, g_iconDrawCtx.y,

        0, 0, 0, 1, 0, 1);

}



// At 0x0081DEE9: ecx=pThis, esi=GW_ItemSlotBase*, ebx=CUIItem*, [esp]=canvas,

// [esp+8]=x, [esp+0xC]=y for sub_5D6458.  Save draw coords, call original, then badge.

void __declspec(naked) CItemInfo__DrawItemIconForSlot_hook() {

    __asm {

        push    edi

        mov     eax, [esp+4]

        mov     g_iconDrawCtx.pCanvas, eax

        mov     eax, [esp+0Ch]

        mov     g_iconDrawCtx.x, eax

        mov     eax, [esp+10h]

        mov     g_iconDrawCtx.y, eax



        mov     edi, [ebp+84h]

        call    dword ptr [kAddr_DrawItemIconForSlot]



        push    ebx

        push    edi

        push    esi

        push    ecx

        call    TryDrawTransmogBadge

        add     esp, 10h



        pop     edi

        jmp     dword ptr [kAddr_DrawItemIconForSlot_Ret]

    }

}



namespace {

bool g_itemIconHooksAttached = false;

} // namespace



// Bisect + IDA (2026-07-10): PatchJmp naked hook at 0x0081DEE9 still crashes on
// backpack open. Disabled — transmog shown via tooltip.cpp only until a safe
// post-Draw or trampoline hook is implemented (see kaentake __fastcall helper).
static constexpr bool kItemIconHookEnabled = false;

void AttachFusionAnvilItemIconHooks() {
    if (!kItemIconHookEnabled || g_itemIconHooksAttached) {
        return;
    }

    g_itemIconHooksAttached = true;
    PatchJmp(kAddr_DrawItemIconForSlot_Call, &CItemInfo__DrawItemIconForSlot_hook);
}

