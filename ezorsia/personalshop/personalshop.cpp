// personalshop.cpp — 32-slot player / hired merchant alignment notes + attach.
//
// Already done elsewhere (no memory patch required for data path):
//   - Server PlayerShop / HiredMerchant: items.size() >= 32
//   - PacketCreator slotMax writeByte(0x20)
//   - UI/UIWindow.img/PersonalShop/backgrnd already 525x553
//     (matches E:\资料\xiaoye\mxd学习\玩家商店加长\PersonalShopDlg.webp)
//
// Client reads slotMax from the interaction packet; the common WZ Item.img
// property clamp at 0x76017C (cmp/push 0x10) is skill/item-info parsing and
// must NOT be raised globally.

#include "stdafx.h"
#include "PersonalShopApi.h"

#include <cstdio>

namespace {
void PsLog(const char* msg) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strcat_s(path, MAX_PATH, "personalshop_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[640];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, msg);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
}
} // namespace

void AttachPersonalShopMod() {
    PsLog("AttachPersonalShopMod: slotMax=32 via packet; UI backgrnd 525x553 already aligned");
}
