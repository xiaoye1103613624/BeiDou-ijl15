#include "stdafx.h"
#include "ColoringPrismApi.h"
#include "weapontint.h"
#include "compat/PacketDispatcher.h"
#include "compat/hook.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/msghandler.h"

namespace {
bool g_coloringPrismHooksAttached = false;

struct CDraggableItemView {
    MEMBER_AT(int, 0x18, m_nItemTI)
    MEMBER_AT(int, 0x1C, m_nSlotPosition)
};

constexpr uintptr_t kAddr_DraggableOnDropped = 0x004EF140;   // PE+cross-mod verified
constexpr uintptr_t kAddr_OnDoubleClick = 0x004EFD25;        // cash-tab double-click

using t_OnDropped = int(__thiscall*)(CDraggableItemView*, IUIMsgHandler*, IUIMsgHandler*, int, int);
using t_OnDoubleClick = int(__thiscall*)(CDraggableItemView*);

auto Orig_OnDropped = reinterpret_cast<t_OnDropped>(kAddr_DraggableOnDropped);
auto Orig_OnDoubleClick = reinterpret_cast<t_OnDoubleClick>(kAddr_OnDoubleClick);

int __fastcall Hook_OnDropped(CDraggableItemView* pThis, void*, IUIMsgHandler* pFrom,
                              IUIMsgHandler* pTo, int rx, int ry) {
    if (ColorPrism_HandleItemDrop(pTo, pThis->m_nItemTI, pThis->m_nSlotPosition)) {
        return 1;
    }
    return Orig_OnDropped(pThis, pFrom, pTo, rx, ry);
}

int __fastcall Hook_OnDoubleClick(CDraggableItemView* pThis, void*) {
    // Prefer cash inventory double-click of 5782000. Item id is resolved by host
    // cash-use paths elsewhere; here we only open when TI is cash (5) and the
    // slot still holds the prism (ColorPrism_OnUse validates id via caller).
    // DamageSkin and others also chain this address — call original always unless
    // we consume the prism open.
    const int ti = pThis->m_nItemTI;
    const int pos = pThis->m_nSlotPosition;
    // Resolve item id the same way coloringprism SehItemAt / TSec does is heavy;
    // ColorPrism_OnUse is safe to call only when we know the id. Peek via a thin
    // path: ask CharacterData through ColorPrism_IsPrismItem after reading id from
    // extension helpers exported... Use double-click only when pos>0 and ti==5,
    // and defer id check to a helper in coloringprism that reads inventory.
    
    if (ti == 5 && pos > 0) {
        const int id = ColorPrism_PeekCashItemId(pos);
        if (ColorPrism_IsPrismItem(id)) {
            ColorPrism_OnUse(pos, id);
            return 1;
        }
    }
    return Orig_OnDoubleClick(pThis);
}
} // namespace

void AttachColoringPrismHostHooks() {
    ATTACH_HOOK(Orig_OnDropped, Hook_OnDropped);
    ATTACH_HOOK(Orig_OnDoubleClick, Hook_OnDoubleClick);
}

namespace ColoringPrism {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kColoringPrismRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kColoringPrismRecvOpcode) {
                    return false;
                }
                ColoringPrism_HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_coloringPrismHooksAttached) {
        return;
    }
    g_coloringPrismHooksAttached = true;
    AttachWeaponTintMod();
    AttachColoringPrismMod();
    AttachColoringPrismHostHooks();
}
} // namespace ColoringPrism
