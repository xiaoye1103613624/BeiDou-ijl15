// ============================================================
// invexpand.cpp — inventory 「扩充」btn 2007 → YesNo + 4000 NX / +4
//
// Replaces stock EnterCashShop jump with a channel CashShopWindow action.
//
// IDA verification (BeiDou.exe / user-ida-pro-mcp, 2026-08-31):
//   CUIItem::OnButtonClicked  0x0081D39F  — case 2007 → write SN + EnterCashShop
//   CUIItem::OnChildNotify    0x0081D01F  — a3==100 → vtable+32 (OnButtonClicked)
//   current tab               this+0x5E4  (dword index 377; 1=Equip..5=Cash)
//   CUtilDlg::YesNo           0x00992BFD  (return 6 = Yes)
//   EnterCashShop             0x00A04DCA  (swallowed; not called)
// Existing hook addresses: UNCHANGED. This module only ADDS a Detour on
// OnButtonClicked (new attach point).
// ============================================================

#include "stdafx.h"
#include "InvExpandApi.h"
#include "compat/hook.h"
#include "compat/ClientAddresses.h"
#include "compat/wvs/packet_legacy.h"
#include "cashshopwnd.h"
#include "ztl/ztl.h"

#include <cstdint>

namespace {

constexpr uintptr_t kAddr_CUIItem_OnButtonClicked = 0x0081D39F;
constexpr uintptr_t kAddr_CUtilDlg_YesNo          = 0x00992BFD;
constexpr uintptr_t kOff_CUIItem_Tab              = 0x5E4;  // this+377 dwords

// CashShopWindowPackets.ACTION_EXPAND_SLOTS
constexpr unsigned char kAction_ExpandSlots = 5;
// CashShop.NX_CREDIT
constexpr unsigned int kNxCredit = 1;

constexpr unsigned int kBtnExpand = 2007;
constexpr int kYesNo_Yes = 6;

// Expected prolog of OnButtonClicked (IDA get_bytes).
static const unsigned char kProlog[] = {
    0x8B, 0x44, 0x24, 0x04, 0x3D, 0xD2, 0x07, 0x00, 0x00
};

// GBK: "是否要扩展当前背包？\r\n（消耗 4000 点券，增加 4 格）"
static const char kConfirmMsg[] = {
    '\xCA', '\xC7', '\xB7', '\xF1', '\xD2', '\xAA', '\xC0', '\xA9', '\xD5', '\xB9',
    '\xB5', '\xB1', '\xC7', '\xB0', '\xB1', '\xB3', '\xB0', '\xFC', '\xA3', '\xBF',
    '\x0D', '\x0A',
    '\xA3', '\xA8', '\xCF', '\xFB', '\xBA', '\xC4', ' ', '4', '0', '0', '0', ' ',
    '\xB5', '\xE3', '\xC8', '\xAF', '\xA3', '\xAC', '\xD4', '\xF6', '\xBC', '\xD3',
    ' ', '4', ' ', '\xB8', '\xF1', '\xA3', '\xA9',
    '\0'
};

// GBK: "现金栏无法通过此方式扩展。"
static const char kCashTabMsg[] = {
    '\xCF', '\xD6', '\xBD', '\xF0', '\xC0', '\xB8', '\xCE', '\xDE', '\xB7', '\xA8',
    '\xCD', '\xA8', '\xB9', '\xFD', '\xB4', '\xCB', '\xB7', '\xBD', '\xCA', '\xBD',
    '\xC0', '\xA9', '\xD5', '\xB9', '\xA1', '\xA3',
    '\0'
};

typedef void(__thiscall* t_OnButtonClicked)(void* pThis, unsigned int btnId);
static auto Orig_OnButtonClicked =
    reinterpret_cast<t_OnButtonClicked>(kAddr_CUIItem_OnButtonClicked);

typedef int(__cdecl* t_CUtilDlg_YesNo)(ZXString<char>, const wchar_t*, void*, int, int);
static auto CUtilDlg_YesNo = reinterpret_cast<t_CUtilDlg_YesNo>(kAddr_CUtilDlg_YesNo);

typedef int(__cdecl* t_CUtilDlg_Notice)(ZXString<char>, const wchar_t*, void*, int, int);
static auto CUtilDlg_Notice =
    reinterpret_cast<t_CUtilDlg_Notice>(0x009929DD);

static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

static void SendPacket(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (sock) {
        ClientSocket_SendPacket(sock, o);
    }
}

static void Notice(const char* msg) {
    try {
        ZXString<char> zmsg(msg);
        CUtilDlg_Notice(zmsg, nullptr, nullptr, 0, 0);
    } catch (...) {
    }
}

static int AskYesNo(const char* msg) {
    int r = 0;
    try {
        ZXString<char> zmsg(msg);
        r = CUtilDlg_YesNo(zmsg, nullptr, nullptr, 0, 0);
    } catch (...) {
        r = 0;
    }
    return r;
}

static int ReadCurrentTab(void* pThis) {
    if (!pThis) {
        return 0;
    }
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + kOff_CUIItem_Tab);
}

static void SendExpandSlots(int invType) {
    COutPacket o(kCashShopActionOpcode);
    o.Encode1(kAction_ExpandSlots);
    o.Encode4(kNxCredit);
    o.Encode1(static_cast<unsigned char>(invType & 0xFF));
    SendPacket(o);
}

static bool PrologMatches() {
    const auto* p = reinterpret_cast<const unsigned char*>(kAddr_CUIItem_OnButtonClicked);
    for (size_t i = 0; i < sizeof(kProlog); ++i) {
        if (p[i] != kProlog[i]) {
            return false;
        }
    }
    return true;
}

void __fastcall OnButtonClicked_Hook(void* pThis, void* /*edx*/, unsigned int btnId) {
    if (btnId == kBtnExpand) {
        const int tab = ReadCurrentTab(pThis);
        // Tabs 1..4 = EQUIP/USE/SETUP/ETC; 5 = CASH (rejected).
        if (tab < 1 || tab > 4) {
            Notice(kCashTabMsg);
            return;
        }
        if (AskYesNo(kConfirmMsg) == kYesNo_Yes) {
            SendExpandSlots(tab);
        }
        return;  // swallow EnterCashShop
    }
    Orig_OnButtonClicked(pThis, btnId);
}

bool g_attached = false;

}  // namespace

namespace InvExpand {

void EnsureHooks() {
    if (g_attached) {
        return;
    }
    if (!PrologMatches()) {
        std::cout << "[invexpand] OnButtonClicked prolog mismatch; skip hook" << std::endl;
        return;
    }
    ATTACH_HOOK(Orig_OnButtonClicked, OnButtonClicked_Hook);
    g_attached = true;
    std::cout << "[invexpand] hooked CUIItem::OnButtonClicked @ 0x0081D39F (btn 2007)"
              << std::endl;
}

}  // namespace InvExpand
