// highershoplist.cpp — CShopDlg visible rows 5 -> 9 (BeiDou.exe @ 0x400000).
// Pure Memory patches; verify against live bytes before write.

#include "stdafx.h"
#include "HigherShopListApi.h"
#include "Memory.h"

#include <cstdio>
#include <cstring>

namespace {
struct BytePatch {
    DWORD addr;
    unsigned char expected;
    unsigned char value;
    const char* label;
};

struct IntPatch {
    DWORD addr;
    unsigned int expected;
    unsigned int value;
    const char* label;
};

void ShopLog(const char* format, ...) {
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
    strcat_s(path, MAX_PATH, "higher_shoplist_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
}

bool PatchByteWithVerify(const BytePatch& site) {
    const unsigned char before = *reinterpret_cast<unsigned char*>(site.addr);
    if (before != site.expected) {
        ShopLog("SKIP %s @ 0x%08X: expected 0x%02X, found 0x%02X",
                site.label, site.addr, site.expected, before);
        return false;
    }
    Memory::WriteByte(site.addr, site.value);
    const unsigned char after = *reinterpret_cast<unsigned char*>(site.addr);
    if (after != site.value) {
        ShopLog("FAIL %s @ 0x%08X: wrote 0x%02X, read back 0x%02X",
                site.label, site.addr, site.value, after);
        return false;
    }
    ShopLog("OK   %s @ 0x%08X: 0x%02X -> 0x%02X", site.label, site.addr, before, after);
    return true;
}

bool PatchIntWithVerify(const IntPatch& site) {
    const unsigned int before = *reinterpret_cast<unsigned int*>(site.addr);
    if (before != site.expected) {
        ShopLog("SKIP %s @ 0x%08X: expected 0x%08X, found 0x%08X",
                site.label, site.addr, site.expected, before);
        return false;
    }
    Memory::WriteInt(site.addr, site.value);
    const unsigned int after = *reinterpret_cast<unsigned int*>(site.addr);
    if (after != site.value) {
        ShopLog("FAIL %s @ 0x%08X: wrote 0x%08X, read back 0x%08X",
                site.label, site.addr, site.value, after);
        return false;
    }
    ShopLog("OK   %s @ 0x%08X: 0x%08X -> 0x%08X", site.label, site.addr, before, after);
    return true;
}
} // namespace

void AttachHigherShopListMod() {
    // Visible rows: 5 -> 9. +160px list height (4 * 40).
    // Requires Shop/backgrnd ~463x499 (PatchStorageBg extend-shop).
    // Do NOT patch 0x756E55 (inventory tab type 1..5).

    const BytePatch kBytes[] = {
        // CShopDlg::DrawSellItem loop: cmp eax, 5 -> 9
        {0x00755E46, 0x05, 0x09, "sell_draw_row_max"},
        // Qty-button ZArray Resize(5) -> Resize(9)  [esi+0A8h]
        {0x00753F74, 0x05, 0x09, "qty_button_array_size"},
        // Hit-test scroll window: add ebx, 4 -> 8
        {0x00756E94, 0x04, 0x08, "sell_hit_scroll_win"},
        // Hit-test: lea eax, [edi-4] -> [edi-8]
        {0x00756EA1, 0xFC, 0xF8, "sell_hit_scroll_adj"},
        // ScrollBar range: cmp [ecx-4], 5 -> 9 (buy list)
        {0x0075470D, 0x05, 0x09, "buy_sb_threshold"},
        // ScrollBar: n - 4 -> n - 8 (buy)
        {0x0075471B, 0xFC, 0xF8, "buy_sb_n_minus"},
        // ScrollBar range: cmp [ecx-4], 5 -> 9 (sell list)
        {0x0075473F, 0x05, 0x09, "sell_sb_threshold"},
        // ScrollBar: n - 4 -> n - 8 (sell)
        {0x0075474D, 0xFC, 0xF8, "sell_sb_n_minus"},
        // Do NOT patch 0x75479F (imm8 100). That is CAvatar Create flag at
        // this+1085 — CAvatar::Create uses (flag!=100 ? mode2 : mode0) for
        // layer origins. Changing 100→127 floats the sell-panel character.
        // (Previously mislabeled "sell_sb_height_imm8"; 285 here is avatar X.)
    };

    const IntPatch kInts[] = {
        // CUINumberDlg default max for player shop / hired merchant buy (sub_66B6FC):
        // mov eax, 1000 @+0x66B826 -> 10000. IDA: b8 E8 03 00 00; cmp uses eax.
        {0x0066B826, 0x000003E8, 0x00002710, "player_shop_buy_qty_max"},
        // Draw Y limit: 0x15C(348) -> 0x1FC(508)
        {0x0075574B, 0x0000015C, 0x000001FC, "buy_draw_y_limit"},
        // HitTest buy/sell: 0x147(327) -> 0x1E7(487)
        {0x007560D8, 0x00000147, 0x000001E7, "hit_buy_y_limit"},
        {0x0075619A, 0x00000147, 0x000001E7, "hit_sell_y_limit"},
        // Main scrollbars length/height 194 -> 354 (+160). Same Create slot as
        // HigherStorageList merchant/player scrollbar height patches.
        {0x00753DB9, 0x000000C2, 0x00000162, "scroll_buy_height"},
        {0x00753E1A, 0x000000C2, 0x00000162, "scroll_sell_height"},
        // Do NOT patch CCtrlTab Create a7 (0x753725 / 0x753A61 = 222).
        // IDA CCtrlTab::Create(parent,id,type=2, x,y,WIDTH=222,0) then
        // CCtrlWnd::Create(..., w=222, h=21). a7 is HORIZONTAL tab strip WIDTH.
        // 222→382 covers the char preview and breaks inventory category hit-test.
    };

    ShopLog("ApplyPatches begin (UseVirtuProtect=%d)", Memory::UseVirtuProtect ? 1 : 0);

    int okCount = 0;
    int failCount = 0;
    for (const BytePatch& site : kBytes) {
        if (PatchByteWithVerify(site)) {
            ++okCount;
        } else {
            ++failCount;
        }
    }
    for (const IntPatch& site : kInts) {
        if (PatchIntWithVerify(site)) {
            ++okCount;
        } else {
            ++failCount;
        }
    }

    ShopLog("ApplyPatches done: ok=%d fail=%d", okCount, failCount);
}
