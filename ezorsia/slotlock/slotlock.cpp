// slotlock.cpp — inventory slot lock (right-click toggle, sort/merge preserve)
// Addresses verified against BeiDou.exe v083 IDA (image base 0x400000).
// Chains with storagebag CUIItem Draw / OnMouseButton hooks via Detours.

#include "stdafx.h"
#include "SlotLockApi.h"
#include "StorageBagApi.h"
#include "ClientAddresses.h"
#include "compat/hook.h"
#include "wvs/packet_legacy.h"
#include "wvs/wnd.h"
#include "wvs/config.h"
#include "wvs/statusbar.h"
#include "ztl/ztl.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <windows.h>

namespace SlotLock {
namespace {

constexpr auto kInventoryLockBorderColor = 0xAA4A90E2u;
constexpr auto kShopLockBorderColor = 0xCC2E86DEu;
constexpr int kInventoryDrawThickness = 2;
constexpr int kShopDrawThickness = 2;
constexpr int kDragLockKey = VK_SHIFT;
constexpr bool kPreventSellingLockedItems = true;
constexpr int kSellBlockMessageType = 0xC; // CHAT_TYPE_SYSTEM
// GBK bytes for sell-blocked tip (CP936). Keep source ASCII-safe for MSVC CP936.
static const char kSellBlockedMessage[] = {
    '\xB8', '\xC3', '\xB8', '\xF1', '\xD7', '\xD3', '\xD2', '\xD1', '\xCB', '\xF8', '\xB6', '\xA8',
    '\xA3', '\xAC', '\xCE', '\xDE', '\xB7', '\xA8', '\xB3', '\xF6', '\xCA', '\xDB', '\xA1', '\xA3', '\0'
};
constexpr int kMaxInventorySlot = 96;

enum InventoryType {
    IT_EQUIP = 0x1,
    IT_CONSUME = 0x2,
    IT_INSTALL = 0x3,
    IT_ETC = 0x4,
    IT_CASH = 0x5,
};

constexpr int kFirstInventoryType = IT_EQUIP;
constexpr int kLastInventoryType = IT_CASH;
constexpr int kMaskCount = (kMaxInventorySlot + 31) / 32;
uint32_t g_slotLockMasks[kLastInventoryType + 1][kMaskCount]{};

class CCtrlTab : public CCtrlWnd {
public:
    MEMBER_AT(int, 0x3C, m_nCurTab)
};

class CCtrlScrollBar : public CCtrlWnd {
public:
    MEMBER_AT(int, 0x38, m_nCurPos)
};

class CShopDlg {
public:
    struct ITEM {
        unsigned char pad0[0x48];
        MEMBER_AT(int, 0xC, nPos)
    };

    MEMBER_AT(ZRef<CCtrlTab>, 0xA0, m_pTabSell)
    MEMBER_AT(int, 0xF4, m_nSellSelected)
    MEMBER_AT(ZRef<CCtrlScrollBar>, 0xB4, m_pSBSell)
    MEMBER_AT(ZArray<ITEM>, 0xD4, m_aSellItem)
};

static auto CShopDlg_DrawSellItem =
    reinterpret_cast<void(__thiscall*)(void*, IWzCanvasPtr)>(0x0075577A);
static auto CShopDlg_SendSellRequest =
    reinterpret_cast<void(__thiscall*)(void*)>(0x00756A04);

class CUIItem : public CUIWnd, public TSingleton<CUIItem, 0x00BED654> {
public:
    MEMBER_AT(int, 0x05E0, m_nFirstPosition)
    MEMBER_AT(int, 0x05E4, m_nItemTI)
    MEMBER_AT(int, 0x0604, m_bExtended)

    int GetSlotPositionFromPoint(int rx, int ry) {
        return reinterpret_cast<int(__thiscall*)(CUIItem*, int, int)>(0x0081DB7E)(this, rx, ry);
    }
    int GetItemSlotRect(int nSlotPosition, tagRECT* pRc) {
        return reinterpret_cast<int(__thiscall*)(CUIItem*, int, tagRECT*)>(0x0081E2C8)(this, nSlotPosition, pRc);
    }
};

static auto CUIItem_OnMouseButton =
    reinterpret_cast<void(__thiscall*)(void*, unsigned int, unsigned int, int, int)>(0x0081D42E);
static auto CUIItem_OnMouseMove =
    reinterpret_cast<int(__thiscall*)(void*, int, int)>(0x0081D8AD);
static auto CUIItem_Draw =
    reinterpret_cast<void(__thiscall*)(void*, const tagRECT*)>(0x0081DC20);
static auto CConfig_SaveCharacter =
    reinterpret_cast<void(__thiscall*)(CConfig*)>(0x0049D90D);
static auto CConfig_LoadCharacter =
    reinterpret_cast<void(__thiscall*)(CConfig*, int, unsigned int)>(0x0049D0B6);
static auto CClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

constexpr uintptr_t kSendSortItem_SendPacket_call = 0x00A08FD5;
constexpr uintptr_t kSendGatherItem_SendPacket_call = 0x00A08F43;

bool IsValidInventoryType(int inventoryType) {
    return inventoryType >= kFirstInventoryType && inventoryType <= kLastInventoryType;
}

bool IsValidSlot(int slot) {
    return slot >= 1 && slot <= kMaxInventorySlot;
}

bool IsSlotLocked(int inventoryType, int slot) {
    if (!IsValidInventoryType(inventoryType) || !IsValidSlot(slot)) {
        return false;
    }
    const int maskIndex = (slot - 1) / 32;
    const int bitIndex = (slot - 1) % 32;
    return (g_slotLockMasks[inventoryType][maskIndex] & (1u << bitIndex)) != 0;
}

void SetSlotLocked(int inventoryType, int slot, bool locked) {
    if (!IsValidInventoryType(inventoryType) || !IsValidSlot(slot)) {
        return;
    }
    const int maskIndex = (slot - 1) / 32;
    const int bitIndex = (slot - 1) % 32;
    const uint32_t bit = 1u << bitIndex;
    if (locked) {
        g_slotLockMasks[inventoryType][maskIndex] |= bit;
    } else {
        g_slotLockMasks[inventoryType][maskIndex] &= ~bit;
    }
}

void SaveSlotLocks(CConfig* config) {
    if (!config) {
        return;
    }
    char key[32];
    for (int tab = kFirstInventoryType; tab <= kLastInventoryType; ++tab) {
        for (int maskIndex = 0; maskIndex < kMaskCount; ++maskIndex) {
            std::snprintf(key, sizeof(key), "SlotLock_%d_%d", tab, maskIndex);
            config->SetOpt_Int(CConfig::CHARACTER_OPT, key,
                              static_cast<int32_t>(g_slotLockMasks[tab][maskIndex]));
        }
    }
}

void LoadSlotLocks(CConfig* config) {
    for (auto& masks : g_slotLockMasks) {
        for (uint32_t& mask : masks) {
            mask = 0;
        }
    }
    if (!config) {
        return;
    }
    char key[32];
    for (int tab = kFirstInventoryType; tab <= kLastInventoryType; ++tab) {
        for (int maskIndex = 0; maskIndex < kMaskCount; ++maskIndex) {
            std::snprintf(key, sizeof(key), "SlotLock_%d_%d", tab, maskIndex);
            g_slotLockMasks[tab][maskIndex] = static_cast<uint32_t>(
                config->GetOpt_Int(CConfig::CHARACTER_OPT, key, 0, INT_MIN, INT_MAX));
        }
    }
}

void __fastcall CConfig_SaveCharacter_hook(CConfig* pThis, void* /*edx*/) {
    CConfig_SaveCharacter(pThis);
    SaveSlotLocks(pThis);
}

void __fastcall CConfig_LoadCharacter_hook(CConfig* pThis, void* /*edx*/, int nWorldID,
                                           unsigned int dwCharacterId) {
    CConfig_LoadCharacter(pThis, nWorldID, dwCharacterId);
    LoadSlotLocks(pThis);
}

struct DragLockState {
    bool active = false;
    bool lockMode = false;
    int invType = 0;
    void Reset() {
        active = false;
        lockMode = false;
        invType = 0;
    }
} s_DragState;

void __fastcall CUIItem_OnMouseButton_hook(void* pThis, void* /*edx*/, unsigned int msg,
                                           unsigned int wParam, int rx, int ry) {
    if (msg == WM_RBUTTONDOWN) {
        CUIItem* ui = reinterpret_cast<CUIItem*>(reinterpret_cast<char*>(pThis) - 4);
        const int slotPos = ui->GetSlotPositionFromPoint(rx, ry);
        const int inventoryType = ui->m_nItemTI;

        if (IsValidInventoryType(inventoryType) && IsValidSlot(slotPos)) {
            const bool shiftHeld = (GetKeyState(kDragLockKey) & 0x8000) != 0;
            // Plain right-click with bag open -> deposit into storage bag (priority over lock).
            if (!shiftHeld && BagWindow_DepositFromInventory(inventoryType, slotPos)) {
                return;
            }

            const bool currentLockState = IsSlotLocked(inventoryType, slotPos);
            SetSlotLocked(inventoryType, slotPos, !currentLockState);
            SaveSlotLocks(CConfig::GetInstance());

            if (shiftHeld) {
                s_DragState.Reset();
                s_DragState.active = true;
                s_DragState.lockMode = !currentLockState;
                s_DragState.invType = inventoryType;
            }
            ui->InvalidateRect(nullptr);
        }
    } else if (msg == WM_RBUTTONUP) {
        s_DragState.Reset();
    }

    CUIItem_OnMouseButton(pThis, msg, wParam, rx, ry);
}

int __fastcall CUIItem_OnMouseMove_hook(void* pThis, void* /*edx*/, int rx, int ry) {
    if (!s_DragState.active || !(GetKeyState(kDragLockKey) & 0x8000)) {
        if (s_DragState.active) {
            s_DragState.Reset();
        }
        return CUIItem_OnMouseMove(pThis, rx, ry);
    }

    CUIItem* ui = reinterpret_cast<CUIItem*>(reinterpret_cast<char*>(pThis) - 4);
    const int currentInventoryType = ui->m_nItemTI;
    if (currentInventoryType != s_DragState.invType) {
        return CUIItem_OnMouseMove(pThis, rx, ry);
    }

    const int slotPos = ui->GetSlotPositionFromPoint(rx, ry);
    if (IsValidSlot(slotPos) && s_DragState.lockMode != IsSlotLocked(currentInventoryType, slotPos)) {
        SetSlotLocked(currentInventoryType, slotPos, s_DragState.lockMode);
        SaveSlotLocks(CConfig::GetInstance());
        ui->InvalidateRect(nullptr);
    }
    return CUIItem_OnMouseMove(pThis, rx, ry);
}

void AppendSlotLockData(COutPacket* pPacket) {
    CUIItem* ui = CUIItem::GetInstance();
    if (!pPacket || !ui || !IsValidInventoryType(ui->m_nItemTI)) {
        if (pPacket) {
            pPacket->Encode1(0);
        }
        return;
    }

    const int inventoryType = ui->m_nItemTI;
    uint8_t count = 0;
    for (int slot = 1; slot <= kMaxInventorySlot; ++slot) {
        if (IsSlotLocked(inventoryType, slot)) {
            ++count;
        }
    }

    pPacket->Encode1(count);
    for (int slot = 1; slot <= kMaxInventorySlot; ++slot) {
        if (IsSlotLocked(inventoryType, slot)) {
            pPacket->Encode1(static_cast<uint8_t>(slot));
        }
    }
}

void __fastcall SendPacket_WithSlotLock_hook(void* pSocket, void* /*edx*/, COutPacket* pPacket) {
    AppendSlotLockData(pPacket);
    CClientSocket_SendPacket(pSocket, *pPacket);
}

void DrawLockBorder(IWzCanvasPtr canvas, const tagRECT& rc, int thickness, uint32_t color) {
    if (!canvas) {
        return;
    }
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    canvas->DrawRectangle(rc.left, rc.top, width, thickness, color);
    canvas->DrawRectangle(rc.left, rc.bottom - thickness, width, thickness, color);
    canvas->DrawRectangle(rc.left, rc.top, thickness, height, color);
    canvas->DrawRectangle(rc.right - thickness, rc.top, thickness, height, color);
}

void __fastcall CUIItem_Draw_hook(void* pThis, void* /*edx*/, const tagRECT* pRect) {
    CUIItem_Draw(pThis, pRect);

    auto* ui = reinterpret_cast<CUIItem*>(pThis);
    const int inventoryType = ui->m_nItemTI;
    if (!IsValidInventoryType(inventoryType)) {
        return;
    }

    IWzCanvasPtr canvas;
    try {
        canvas = reinterpret_cast<CWnd*>(ui)->GetCanvas();
    } catch (...) {
        return;
    }
    if (!canvas) {
        return;
    }

    const int firstSlot = ui->m_nFirstPosition;
    const int visibleSlots = ui->m_bExtended ? kMaxInventorySlot : 24;
    for (int slot = firstSlot; slot < firstSlot + visibleSlots && slot <= kMaxInventorySlot; ++slot) {
        if (!IsSlotLocked(inventoryType, slot)) {
            continue;
        }
        tagRECT rc{};
        if (ui->GetItemSlotRect(slot, &rc)) {
            DrawLockBorder(canvas, rc, kInventoryDrawThickness, kInventoryLockBorderColor);
        }
    }
}

void __fastcall CShopDlg_DrawSellItem_hook(CShopDlg* pThis, void* /*edx*/, IWzCanvasPtr pCanvas) {
    CShopDlg_DrawSellItem(pThis, pCanvas);
    if (!pCanvas || !pThis->m_pSBSell || !pThis->m_pTabSell) {
        return;
    }

    const int scrollBarCurPos = pThis->m_pSBSell->m_nCurPos;
    if (scrollBarCurPos < 0) {
        return;
    }

    const auto currentInventoryType = pThis->m_pTabSell->m_nCurTab + 1;
    const auto& aSellData = pThis->m_aSellItem;

    constexpr int SHOP_SELL_X = 236;
    constexpr int SHOP_SELL_Y_FIRST = 146 - 19;
    constexpr int SHOP_SELL_STRIDE = 40;
    constexpr int SHOP_SELL_Y_END = SHOP_SELL_Y_FIRST + (40 * 5);
    constexpr int SHOP_SELL_ROW_W = 200;
    constexpr int SHOP_SELL_ROW_H = 35;

    int rowIdx = 0;
    for (int y = SHOP_SELL_Y_FIRST; y < SHOP_SELL_Y_END; y += SHOP_SELL_STRIDE, ++rowIdx) {
        const unsigned int actualIdx = static_cast<unsigned int>(scrollBarCurPos + rowIdx);
        if (actualIdx >= aSellData.GetCount()) {
            break;
        }
        const auto& item = aSellData[actualIdx];
        if (!IsSlotLocked(currentInventoryType, item.nPos)) {
            continue;
        }
        RECT rc{SHOP_SELL_X, y, SHOP_SELL_X + SHOP_SELL_ROW_W, y + SHOP_SELL_ROW_H};
        DrawLockBorder(pCanvas, rc, kShopDrawThickness, kShopLockBorderColor);
    }
}

void AddChatHint(const char* msg) {
    auto* pBar = CUIStatusBar::GetInstance();
    if (!pBar) {
        return;
    }
    reinterpret_cast<void(__thiscall*)(CUIStatusBar*, const char*, int, int, int, int, void*)>(
        0x008DB070)(pBar, msg, kSellBlockMessageType, -1, 0, 0, nullptr);
}

void __fastcall CShopDlg_SendSellRequest_hook(CShopDlg* pThis, void* /*edx*/) {
    if (!pThis->m_pTabSell) {
        CShopDlg_SendSellRequest(pThis);
        return;
    }
    const auto currentInventoryType = pThis->m_pTabSell->m_nCurTab + 1;
    const int nSellSelected = pThis->m_nSellSelected;
    const auto& aSellData = pThis->m_aSellItem;
    if (nSellSelected >= 0 && !aSellData.IsEmpty()) {
        const int aCount = static_cast<int>(aSellData.GetCount());
        if (nSellSelected < aCount) {
            const int nPos = aSellData[nSellSelected].nPos;
            if (IsSlotLocked(currentInventoryType, nPos)) {
                AddChatHint(kSellBlockedMessage);
                return;
            }
        }
    }
    CShopDlg_SendSellRequest(pThis);
}

} // namespace

void AttachSlotLockModInternal() {
    ATTACH_HOOK(CUIItem_OnMouseButton, CUIItem_OnMouseButton_hook);
    ATTACH_HOOK(CUIItem_OnMouseMove, CUIItem_OnMouseMove_hook);
    ATTACH_HOOK(CUIItem_Draw, CUIItem_Draw_hook);

    PatchCall(kSendGatherItem_SendPacket_call, SendPacket_WithSlotLock_hook);
    PatchCall(kSendSortItem_SendPacket_call, SendPacket_WithSlotLock_hook);

    ATTACH_HOOK(CShopDlg_DrawSellItem, CShopDlg_DrawSellItem_hook);
    if (kPreventSellingLockedItems) {
        ATTACH_HOOK(CShopDlg_SendSellRequest, CShopDlg_SendSellRequest_hook);
    }

    ATTACH_HOOK(CConfig_SaveCharacter, CConfig_SaveCharacter_hook);
    ATTACH_HOOK(CConfig_LoadCharacter, CConfig_LoadCharacter_hook);
}

void EnsureHooks() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;
    AttachSlotLockModInternal();
}

} // namespace SlotLock

void AttachSlotLockMod() {
    SlotLock::EnsureHooks();
}
