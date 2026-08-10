// Soften CWvsApp EC_INVALID_GAME_DATA (0x22000006) throws.
// UI text is StringPool#86 (ijl15 only translates it -- no CRC whitelist table).
//
// Sites:
//   #1-#7 inside InitializeGameData (0x009F8B61) after Item/Skill loaders
//   #8 QuestCategory (0x0071D8DF) and #9 MorphPack (0x0068487C) in CWvsApp::Init
//
// Patch: jnz-success (0x75) -> jmp-success (0xEB) so a false loader return no
// longer terminates. Prefer restoring the bad .img via orange-wz MCP later.

#include "stdafx.h"
#include "Memory.h"
#include "GameDataGuardApi.h"

#include <cstdio>
#include <cstring>

static void GdLog(const char* msg) {
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strncat_s(path, "gamedata_guard.log", _TRUNCATE);
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char line[512]{};
    int n = sprintf_s(line, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    if (n > 0) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
    }
    CloseHandle(file);
}

struct FailSite {
    DWORD addr; // address of the 0x75 jnz opcode before mov eax,0x22000006
    const char* label;
};

static const FailSite kSites[] = {
    // Inside CWvsApp::InitializeGameData (0x009F8B61)
    { 0x009F8BA2, "#1 iteminfo @0x005CA71C" },
    { 0x009F8BF6, "#2 ItemMake/0425/0426 @0x005E6A24" },
    { 0x009F8C6D, "#3 loader @0x0075C060" },
    { 0x009F8C9C, "#4 ItemSkill @0x00761A2D" },
    { 0x009F8CCB, "#5 BF/MCSkill @0x0076333D" },
    { 0x009F8CFA, "#6 BF/MCGuardian @0x00763815" },
    { 0x009F8D29, "#7 ItemSkill @0x00761DDB" },
    // After InitializeGameData, still in CWvsApp::Init (0x009F5239):
    // flash-exit with EC_INVALID_GAME_DATA when these return 0.
    { 0x009F578E, "#8 QuestCategory @0x0071D8DF (post-GameData)" },
    { 0x009F57F0, "#9 MorphPack @0x0068487C (post-GameData)" },
};

void AttachGameDataGuard() {
    GdLog("GameDataGuard attach begin");
    const int total = static_cast<int>(sizeof(kSites) / sizeof(kSites[0]));
    int patched = 0;
    for (const auto& site : kSites) {
        __try {
            unsigned char op = *reinterpret_cast<unsigned char*>(site.addr);
            if (op != 0x75) {
                char buf[192]{};
                sprintf_s(buf, "SKIP %s @%08X opcode=%02X", site.label, site.addr, op);
                GdLog(buf);
                continue;
            }
            Memory::WriteByte(site.addr, 0xEB); // jnz -> jmp (always skip terminate)
            char buf[192]{};
            sprintf_s(buf, "PATCH %s @%08X 75->EB", site.label, site.addr);
            GdLog(buf);
            ++patched;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            char buf[192]{};
            sprintf_s(buf, "EXCEPT %s @%08X", site.label, site.addr);
            GdLog(buf);
        }
    }
    char done[80]{};
    sprintf_s(done, "GameDataGuard attach done patched=%d/%d", patched, total);
    GdLog(done);
    // Softening can mask bad Skill/Item/Quest/Morph until later (AV / 0x800*).
    // Prefer MCP-restoring failing packs over permanent skip.
    GdLog("NOTE: soften-only; if select-char crashes, restore via orange-wz MCP");
}
