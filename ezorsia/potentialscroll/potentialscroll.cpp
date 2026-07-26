// potentialscroll.cpp — Hyper/Potential scrolls + magnify + Cash cube drag
//
// BeiDou.exe v083 @ 0x004F5497 = is_correct_upgrade_equip(nUItemID, nEItemID)
// Vanilla: (scroll/100)%100 must equal (equip/10000)%100, except chaos 20490*.
// 2049300 -> type 93 vs glove 08 -> client rejects drop, never sends packet.
//
// Hook A: if scroll/100 is 20493/20494/20497/20498/20499 OR magnify 2460000~3
// and target is equip, return true.
// Call sites (verified): 0x004F4E1C, 0x0082CCC5.
//
// Hook B (Phase11): Cash cubes 5062000/01/02/2100 drag onto 背包装备栏.
// Native 083 has no MiracleCube pick-UI and does not treat Cash as upgrade scrolls.
// Intercept CDraggableItem::OnDropped (via FusionAnvil chain): when Cash cube is
// dropped on CUIItem Equip tab slot, send USE_CASH_ITEM (0x4F) with equip slot int
// — same body resolveCashCubeTarget expects after itemId.

#include "stdafx.h"
#include "PotentialScrollApi.h"
#include "compat/hook.h"
#include "compat/wvs/packet_legacy.h"

#include <cstdint>
#include <windows.h>

namespace {
constexpr uintptr_t kIsCorrectUpgradeEquip = 0x004F5497;
constexpr uintptr_t kAddr_CUIItem_Instance = 0x00BED654;
constexpr uintptr_t kAddr_CUIItem_SlotAtPoint = 0x0081DB7E;
constexpr uintptr_t kAddr_CharacterData_GetItem = 0x004282F7;
constexpr uintptr_t kAddr_TSecType_long_GetData = 0x0042873D;
constexpr uintptr_t kAddr_CWvsContext_Instance = 0x00BE7918;
constexpr uintptr_t kOffset_CharacterData_InContext = 0x20B8;
constexpr uintptr_t kAddr_ClientSocket_Instance = 0x00BE7914;
constexpr uintptr_t kAddr_ClientSocket_SendPacket = 0x0049637B;
constexpr uintptr_t kAddr_play_ui_sound = 0x00989588;

constexpr int kOpcode_UseCashItem = 0x4F;
constexpr int kInventory_Equip = 1;
constexpr int kInventory_Cash = 5;
constexpr int kCUIItem_ItemTI_Offset = 0x05E4;

using IsCorrectUpgradeEquip_t = int(__cdecl*)(int nUItemID, int nEItemID);
IsCorrectUpgradeEquip_t Real_is_correct_upgrade_equip =
    reinterpret_cast<IsCorrectUpgradeEquip_t>(kIsCorrectUpgradeEquip);

bool g_scrollHookAttached = false;

int __cdecl Hook_is_correct_upgrade_equip(int nUItemID, int nEItemID) {
    const int family = nUItemID / 100;
    if ((family == 20493 || family == 20494 || family == 20497
         || family == 20498 || family == 20499)
        && (nEItemID / 1000000) == 1) {
        return 1;
    }
    // Phase7: 鉴定放大镜 2460000~2460003
    if (nUItemID >= 2460000 && nUItemID <= 2460003
        && (nEItemID / 1000000) == 1) {
        return 1;
    }
    return Real_is_correct_upgrade_equip(nUItemID, nEItemID);
}

// Align with server PotentialHyperConfig.isCashCube (Cash drag-onto-equip).
bool IsCashCubeItemId(int itemId) {
    return itemId == 5062000  // Miracle
        || itemId == 5062102  // 7th-anniv alias → main
        || itemId == 5062001  // Premium
        || itemId == 5062100  // Premium alt
        || itemId == 5062002  // Super
        || itemId == 5062003  // Ultimate
        || itemId == 5062004  // Weird (Cash)
        || itemId == 5062500; // Bonus / master additional
}

struct ZRefOut {
    void* m_unused;
    void* m_pItem;
};

typedef void(__thiscall* t_CharacterData_GetItem)(void*, ZRefOut*, int, int);
typedef int32_t(__thiscall* t_TSecType_long_GetData)(const void*);
typedef void(__thiscall* t_SendPacket)(void*, const COutPacket&);
typedef int(__thiscall* t_SlotAtPoint)(void*, int, int);

static void* FetchInventoryItem(int nTI, int nPOS) {
    void* pCtx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
    if (!pCtx) {
        return nullptr;
    }
    void* pCharData = *reinterpret_cast<void**>(
        reinterpret_cast<char*>(pCtx) + kOffset_CharacterData_InContext);
    if (!pCharData) {
        return nullptr;
    }
    ZRefOut out = {};
    reinterpret_cast<t_CharacterData_GetItem>(kAddr_CharacterData_GetItem)(
        pCharData, &out, nTI, nPOS);
    return out.m_pItem;
}

static int DecodeItemID(void* pItem) {
    if (!pItem) {
        return 0;
    }
    return reinterpret_cast<t_TSecType_long_GetData>(kAddr_TSecType_long_GetData)(
        reinterpret_cast<const char*>(pItem) + 0x0C);
}

static void SendUseCashCube(int cashPos, int itemId, int equipSlot) {
    COutPacket oPacket(kOpcode_UseCashItem);
    oPacket.Encode2(static_cast<unsigned short>(cashPos));
    oPacket.Encode4(static_cast<unsigned int>(itemId));
    // resolveCashCubeTarget: prefer int slot (bag >0 / worn <0)
    oPacket.Encode4(static_cast<unsigned int>(equipSlot));
    void* sock = *reinterpret_cast<void**>(kAddr_ClientSocket_Instance);
    if (sock) {
        reinterpret_cast<t_SendPacket>(kAddr_ClientSocket_SendPacket)(sock, oPacket);
    }
    reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound)(L"DragEnd");
}

static void* ResolveCUIItemFromDropTarget(void* pTo) {
    if (!pTo) {
        return nullptr;
    }
    void* inv = nullptr;
    __try {
        inv = *reinterpret_cast<void**>(kAddr_CUIItem_Instance);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        inv = nullptr;
    }
    if (!inv) {
        return nullptr;
    }
    // pTo is IUIMsgHandler* (+4 into CWnd/CUIItem). Match both.
    if (pTo == inv) {
        return inv;
    }
    auto* adj = reinterpret_cast<void*>(reinterpret_cast<char*>(pTo) - 4);
    if (adj == inv) {
        return inv;
    }
    // Some paths pass CWnd* directly.
    auto* adj2 = reinterpret_cast<void*>(reinterpret_cast<char*>(pTo) + 4);
    if (adj2 == inv) {
        return inv;
    }
    return nullptr;
}

} // namespace

bool PotentialScroll_TryHandleCashCubeDrop(
    int nItemTI, int nSlotPosition, void* /*pFrom*/, void* pTo, int rx, int ry)
{
    if (nItemTI != kInventory_Cash || nSlotPosition == 0) {
        return false;
    }
    void* pCashItem = nullptr;
    __try {
        pCashItem = FetchInventoryItem(kInventory_Cash, nSlotPosition);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    int itemId = 0;
    __try {
        itemId = DecodeItemID(pCashItem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!IsCashCubeItemId(itemId)) {
        return false;
    }

    void* ui = ResolveCUIItemFromDropTarget(pTo);
    if (!ui) {
        return false;
    }
    int tabTI = 0;
    __try {
        tabTI = *reinterpret_cast<int*>(reinterpret_cast<char*>(ui) + kCUIItem_ItemTI_Offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // Prefer 背包装备栏 (tab EQUIP). Cash tab drop onto self is not use.
    if (tabTI != kInventory_Equip) {
        return false;
    }

    int eqSlot = 0;
    __try {
        eqSlot = reinterpret_cast<t_SlotAtPoint>(kAddr_CUIItem_SlotAtPoint)(ui, rx, ry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        eqSlot = 0;
    }
    if (eqSlot < 1 || eqSlot > 96) {
        return false;
    }

    void* pEquip = nullptr;
    __try {
        pEquip = FetchInventoryItem(kInventory_Equip, eqSlot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!pEquip) {
        return false;
    }
    int eqId = 0;
    __try {
        eqId = DecodeItemID(pEquip);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if ((eqId / 1000000) != 1) {
        return false;
    }

    SendUseCashCube(nSlotPosition, itemId, eqSlot);
    return true;
}

void AttachPotentialScrollMod() {
    if (g_scrollHookAttached) {
        return;
    }
    g_scrollHookAttached = true;
    ATTACH_HOOK(Real_is_correct_upgrade_equip, Hook_is_correct_upgrade_equip);
}

namespace PotentialScroll {
void ApplyPatches() {
    AttachPotentialScrollMod();
}
} // namespace
