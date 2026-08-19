// higherstoragelist.cpp — extend CUIStorage / Trunk merchant UI from ~4-5 rows to ~7-8 rows.
// BeiDou.exe @ image base 0x400000 — IDA MCP verified 2026-07-10.
// Pure Memory::WriteByte patches; no hooks or server packets.

#include "stdafx.h"
#include "HigherStorageListApi.h"
#include "ClientAddresses.h"
#include "Memory.h"

#include <cstdio>
#include <cstring>

namespace {
struct PatchSite {
    DWORD addr;
    unsigned char expected;
    unsigned char value;
    const char* label;
};

void HigherShopLog(const char* format, ...) {
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
    strcat_s(path, MAX_PATH, "higher_shop_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
}

bool PatchByteWithVerify(const PatchSite& site) {
    const unsigned char before = *reinterpret_cast<unsigned char*>(site.addr);
    if (before != site.expected) {
        HigherShopLog("SKIP %s @ 0x%08X: expected 0x%02X, found 0x%02X",
                      site.label, site.addr, site.expected, before);
        return false;
    }

    Memory::WriteByte(site.addr, site.value);

    const unsigned char after = *reinterpret_cast<unsigned char*>(site.addr);
    if (after != site.value) {
        HigherShopLog("FAIL %s @ 0x%08X: wrote 0x%02X, read back 0x%02X (UseVirtuProtect=%d)",
                      site.label, site.addr, site.value, after, Memory::UseVirtuProtect ? 1 : 0);
        return false;
    }

    HigherShopLog("OK   %s @ 0x%08X: 0x%02X -> 0x%02X", site.label, site.addr, before, after);
    return true;
}
} // namespace

void AttachHigherStorageListMod() {
    using namespace ClientAddresses::CUIStorage;

    const PatchSite kPatches[] = {
        {kDrawMerchantItemsLoopMax + 3, 0x33, 0xC9, "merchant_draw_loop_max"},
        {kHitTestMerchantLoopMax + 3, 0x1F, 0xBF, "merchant_hit_loop_max"},
        {kDrawPlayerItemsLoopMax + 3, 0x33, 0xC9, "player_draw_loop_max"},
        {kHitTestPlayerLoopMax + 3, 0x1F, 0xBF, "player_hit_loop_max"},
        {kStorageMesoButtonY + 1, 0x26, 0xC6, "storage_meso_btn_y"},
        {kPlayerMesoButtonY + 1, 0x26, 0xC6, "player_meso_btn_y"},
        {kMesoTextY + 1, 0x28, 0xC8, "meso_text_y"},
        {kMerchantScrollbarHeight + 1, 0xC5, 0x64, "merchant_scroll_h_lo"},
        {kMerchantScrollbarHeight + 2, 0x00, 0x01, "merchant_scroll_h_hi"},
        {kMerchantScrollbarFix + 2, 0xFD, 0xF9, "merchant_scroll_fix"},
        {kPlayerScrollbarHeight + 1, 0x9E, 0x40, "player_scroll_h_lo"},
        {kPlayerScrollbarHeight + 2, 0x00, 0x01, "player_scroll_h_hi"},
        {kPlayerScrollbarFix + 2, 0xFC, 0xF8, "player_scroll_fix"},
        {kTabControlY + 1, 0x5C, 0x5F, "tab_y"},
        {kMerchantScrollArea + 3, 0xB4, 0xD0, "merchant_scroll_area"},
        {kPlayerScrollArea + 3, 0x92, 0xD0, "player_scroll_area"},
    };

    HigherShopLog("ApplyPatches begin (UseVirtuProtect=%d, sites=%zu)",
                  Memory::UseVirtuProtect ? 1 : 0, sizeof(kPatches) / sizeof(kPatches[0]));

    int okCount = 0;
    int failCount = 0;
    for (const PatchSite& site : kPatches) {
        if (PatchByteWithVerify(site)) {
            ++okCount;
        } else {
            ++failCount;
        }
    }

    HigherShopLog("ApplyPatches done: ok=%d fail=%d", okCount, failCount);
}
