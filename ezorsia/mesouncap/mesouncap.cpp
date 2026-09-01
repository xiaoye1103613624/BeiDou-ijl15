// mesouncap.cpp — gold/meso beyond int32 (21.47E).
// BeiDou.exe @ 0x400000. Addresses cross-checked against IDA (original BeiDou.exe).
//
// Strategy (same spirit as EXP Decode8): keep GW_CharacterStat.nMoney as int Tear
// (min(val, INT_MAX)) so CharacterData layout stays 0xE7; store the real value in
// globals and hijack ZXString c_str sites used by meso UI.
//
// Outbound storage/trade meso uses Encode8 (two Encode4 LE) to match server readLong.

#include "stdafx.h"
#include "MesoUncapApi.h"
#include "Memory.h"
#include "compat/hook.h"
#include "MapleClientCollectionTypes/ZXString.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

void MesoLogWrite(const char* path, const char* line) {
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}

void MesoLog(const char* format, ...) {
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

    char pathExe[MAX_PATH];
    GetModuleFileNameA(nullptr, pathExe, MAX_PATH);
    if (char* slash = strrchr(pathExe, '\\')) {
        *(slash + 1) = '\0';
    }
    strcat_s(pathExe, MAX_PATH, "mesouncap_debug.txt");
    MesoLogWrite(pathExe, line);

    char pathDll[MAX_PATH];
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&MesoLog), &self) && self) {
        GetModuleFileNameA(self, pathDll, MAX_PATH);
        if (char* slash = strrchr(pathDll, '\\')) {
            *(slash + 1) = '\0';
        }
        strcat_s(pathDll, MAX_PATH, "mesouncap_debug.txt");
        if (_stricmp(pathExe, pathDll) != 0) {
            MesoLogWrite(pathDll, line);
        }
    }
    OutputDebugStringA(line);
}

constexpr uintptr_t kAddr_CInPacket_Decode4  = 0x00406629;
constexpr uintptr_t kAddr_COutPacket_Encode4 = 0x004065A6;
constexpr uintptr_t kAddr_ZXString_cstr      = 0x00428226;
constexpr uintptr_t kAddr_ZXString_Empty     = 0x00414663; // release buffer (game heap)
constexpr uintptr_t kAddr_ZXString_Assign    = 0x00414617; // Assign(const char*, size) game heap
constexpr uintptr_t kAddr_FormatNumber       = 0x00988690;
constexpr uintptr_t kAddr_StorageFmtDraw     = 0x007C8064;

using Decode4Fn = unsigned int(__thiscall*)(void* packet);
using Encode4Fn = void(__thiscall*)(void* packet, unsigned int value);
using ZXEmptyFn = void(__thiscall*)(void* self);
using ZXAssignFn = int(__thiscall*)(void* self, const char* src, size_t size);

unsigned long long g_lMeso = 0;
unsigned long long g_lFredrickMeso = 0;
unsigned long long g_lStorageMeso = 0;
unsigned long long g_lHmMeso = 0;
unsigned long long g_lTradeMeso[2] = {};
unsigned long long* g_pActiveFmt = &g_lMeso;

unsigned int ClampMesoToInt(unsigned long long v) {
    return v < static_cast<unsigned long long>(INT_MAX)
                   ? static_cast<unsigned int>(v)
                   : static_cast<unsigned int>(INT_MAX);
}

unsigned long long Decode8FromPacket(void* packet) {
    auto Decode4 = reinterpret_cast<Decode4Fn>(kAddr_CInPacket_Decode4);
    const unsigned int low = Decode4(packet);
    const unsigned int high = Decode4(packet);
    return (static_cast<unsigned long long>(low)) |
           (static_cast<unsigned long long>(high) << 32);
}

void Encode8Packet(void* packet, long long value) {
    auto Encode4 = reinterpret_cast<Encode4Fn>(kAddr_COutPacket_Encode4);
    const unsigned long long u = static_cast<unsigned long long>(value);
    Encode4(packet, static_cast<unsigned int>(u & 0xFFFFFFFFu));
    Encode4(packet, static_cast<unsigned int>(u >> 32));
}

// CharacterData DecodeMoney + StatChanged mask 0x40000 — updates g_lMeso.
unsigned int __fastcall MesoDecode8_Character(void* packet, void* /*edx*/) {
    g_lMeso = Decode8FromPacket(packet);
    MesoLog("Decode8 character meso=%llu", g_lMeso);
    return ClampMesoToInt(g_lMeso);
}

unsigned int __fastcall MesoDecode8_Fredrick(void* packet, void* /*edx*/) {
    g_lFredrickMeso = Decode8FromPacket(packet);
    MesoLog("Decode8 fredrick meso=%llu", g_lFredrickMeso);
    return ClampMesoToInt(g_lFredrickMeso);
}

unsigned int __fastcall MesoDecode8_Storage(void* packet, void* /*edx*/) {
    g_lStorageMeso = Decode8FromPacket(packet);
    MesoLog("Decode8 storage meso=%llu", g_lStorageMeso);
    return ClampMesoToInt(g_lStorageMeso);
}

unsigned int __fastcall MesoDecode8_HiredMerchant(void* packet, void* /*edx*/) {
    g_lHmMeso = Decode8FromPacket(packet);
    MesoLog("Decode8 hiredMerchant meso=%llu", g_lHmMeso);
    return ClampMesoToInt(g_lHmMeso);
}

// Trade SET_MESO (CUITrade OnPacket mode 16 → sub_7C208E).
// Caller has movsx edi, al (slot) before this Decode4; edi is preserved across thiscall.
unsigned int __fastcall MesoDecode8_Trade(void* packet, void* /*edx*/) {
    int slot = 0;
    __asm {
        mov slot, edi
    }
    const unsigned long long v = Decode8FromPacket(packet);
    if (slot >= 0 && slot < 2) {
        g_lTradeMeso[slot] = v;
    }
    MesoLog("Decode8 trade slot=%d meso=%llu", slot, v);
    return ClampMesoToInt(v);
}

std::string FormatMesoCommas(unsigned long long value) {
    std::string s = std::to_string(value);
    int insertPos = static_cast<int>(s.length()) - 3;
    while (insertPos > 0) {
        s.insert(static_cast<size_t>(insertPos), ",");
        insertPos -= 3;
    }
    return s;
}

// MUST use game ZXString heap (0x414617). DLL ZXString::Assign uses a separate
// ZAllocEx pool — game then draws/frees the old FormatNumber buffer → UI stays INT_MAX.
const char* FormatAssignGame(void* zxstr, unsigned long long value) {
    const std::string s = FormatMesoCommas(value);
    auto Empty = reinterpret_cast<ZXEmptyFn>(kAddr_ZXString_Empty);
    auto Assign = reinterpret_cast<ZXAssignFn>(kAddr_ZXString_Assign);
    Empty(zxstr);
    Assign(zxstr, s.c_str(), s.length());
    return *reinterpret_cast<const char**>(zxstr);
}

const char* __fastcall MesoFormat_Character(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_lMeso);
}

const char* __fastcall MesoFormat_Fredrick(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_lFredrickMeso);
}

const char* __fastcall MesoFormat_Active(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_pActiveFmt ? *g_pActiveFmt : g_lMeso);
}

const char* __fastcall MesoFormat_Hm(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_lHmMeso);
}

const char* __fastcall MesoFormat_Trade0(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_lTradeMeso[0]);
}

const char* __fastcall MesoFormat_Trade1(ZXString<char>* self, void* /*edx*/) {
    return FormatAssignGame(self, g_lTradeMeso[1]);
}

// Replace FormatNumber inside storage/Fredrick draw — write long string up front.
void __cdecl MesoFormatNumber_Active(void* zxOut, int /*value*/, int /*withComma*/) {
    const unsigned long long v = g_pActiveFmt ? *g_pActiveFmt : 0ULL;
    FormatAssignGame(zxOut, v);
    MesoLog("FormatNumber storageUI -> %llu", v);
}

void __cdecl MesoFormatNumber_Fredrick(void* zxOut, int /*value*/, int /*withComma*/) {
    FormatAssignGame(zxOut, g_lFredrickMeso);
    MesoLog("FormatNumber fredrick -> %llu", g_lFredrickMeso);
}

void __cdecl MesoFormatNumber_Character(void* zxOut, int /*value*/, int /*withComma*/) {
    FormatAssignGame(zxOut, g_lMeso);
    MesoLog("FormatNumber character -> %llu", g_lMeso);
}

// thiscall draw helper: set active fmt source then jump to original (no stack rewrite).
void __declspec(naked) MesoWrapFmt_StorageMeso_Naked() {
    __asm {
        mov eax, offset g_lStorageMeso
        mov g_pActiveFmt, eax
        mov eax, kAddr_StorageFmtDraw
        jmp eax
    }
}

void __declspec(naked) MesoWrapFmt_CharOnStorage_Naked() {
    __asm {
        mov eax, offset g_lMeso
        mov g_pActiveFmt, eax
        mov eax, kAddr_StorageFmtDraw
        jmp eax
    }
}

// Outbound: always Encode8. Expand INT_MAX / -INT_MAX "all" to full global.
void __fastcall MesoEncode8_StorageWithdraw(void* packet, void* /*edx*/, int value) {
    long long v = value;
    if (value == INT_MAX && g_lStorageMeso > static_cast<unsigned long long>(INT_MAX)) {
        v = static_cast<long long>(g_lStorageMeso);
    }
    Encode8Packet(packet, v);
    MesoLog("Encode8 storage withdraw=%lld", v);
}

void __fastcall MesoEncode8_StorageDeposit(void* packet, void* /*edx*/, int value) {
    long long v = value;
    // deposit path does `neg ebx` on the typed amount; full bag → -INT_MAX.
    if (value == -INT_MAX && g_lMeso > static_cast<unsigned long long>(INT_MAX)) {
        v = -static_cast<long long>(g_lMeso);
    }
    Encode8Packet(packet, v);
    MesoLog("Encode8 storage deposit=%lld", v);
}

void __fastcall MesoEncode8_TradeSetMeso(void* packet, void* /*edx*/, int value) {
    long long v = value;
    if (value == INT_MAX && g_lMeso > static_cast<unsigned long long>(INT_MAX)) {
        v = static_cast<long long>(g_lMeso);
    }
    Encode8Packet(packet, v);
    MesoLog("Encode8 trade setMeso=%lld", v);
}

struct CallPatch {
    uintptr_t addr;
    uintptr_t expectedTarget;
    void* newTarget;
    const char* label;
};

bool PatchCallWithVerify(const CallPatch& site) {
    const unsigned char op = *reinterpret_cast<unsigned char*>(site.addr);
    if (op != 0xE8) {
        MesoLog("SKIP %s @ 0x%08X: expected call 0xE8, found 0x%02X",
                site.label, static_cast<unsigned>(site.addr), op);
        return false;
    }
    const int rel = *reinterpret_cast<int*>(site.addr + 1);
    const uintptr_t currentTarget = site.addr + 5 + static_cast<uintptr_t>(rel);
    if (currentTarget != site.expectedTarget) {
        MesoLog("SKIP %s @ 0x%08X: expected target 0x%08X, found 0x%08X",
                site.label, static_cast<unsigned>(site.addr),
                static_cast<unsigned>(site.expectedTarget),
                static_cast<unsigned>(currentTarget));
        return false;
    }

    PatchCall(site.addr, site.newTarget);

    const unsigned char afterOp = *reinterpret_cast<unsigned char*>(site.addr);
    const int afterRel = *reinterpret_cast<int*>(site.addr + 1);
    const uintptr_t afterTarget = site.addr + 5 + static_cast<uintptr_t>(afterRel);
    if (afterOp != 0xE8 || afterTarget != reinterpret_cast<uintptr_t>(site.newTarget)) {
        MesoLog("FAIL %s @ 0x%08X: verify mismatch", site.label, static_cast<unsigned>(site.addr));
        return false;
    }
    MesoLog("OK   %s @ 0x%08X: 0x%08X -> 0x%08X",
            site.label, static_cast<unsigned>(site.addr),
            static_cast<unsigned>(currentTarget),
            static_cast<unsigned>(afterTarget));
    return true;
}

bool g_attached = false;

} // namespace

void AttachMesoUncapMod() {
    if (g_attached) {
        return;
    }
    g_attached = true;

    MesoLog("AttachMesoUncapMod begin UseVirtuProtect=%d", Memory::UseVirtuProtect ? 1 : 0);

    int ok = 0;
    int fail = 0;

    void* const decodeChar = CastHook(&MesoDecode8_Character);
    void* const decodeFred = CastHook(&MesoDecode8_Fredrick);
    void* const decodeStor = CastHook(&MesoDecode8_Storage);
    void* const decodeHm = CastHook(&MesoDecode8_HiredMerchant);
    void* const decodeTrade = CastHook(&MesoDecode8_Trade);
    void* const fmtChar = CastHook(&MesoFormat_Character);
    void* const fmtFred = CastHook(&MesoFormat_Fredrick);
    void* const fmtActive = CastHook(&MesoFormat_Active);
    void* const fmtHm = CastHook(&MesoFormat_Hm);
    void* const fmtTrade0 = CastHook(&MesoFormat_Trade0);
    void* const fmtTrade1 = CastHook(&MesoFormat_Trade1);
    void* const wrapStorFmt = reinterpret_cast<void*>(&MesoWrapFmt_StorageMeso_Naked);
    void* const wrapCharFmt = reinterpret_cast<void*>(&MesoWrapFmt_CharOnStorage_Naked);
    void* const fmtNumStor = reinterpret_cast<void*>(&MesoFormatNumber_Active);
    void* const fmtNumFred = reinterpret_cast<void*>(&MesoFormatNumber_Fredrick);
    void* const fmtNumChar = reinterpret_cast<void*>(&MesoFormatNumber_Character);
    void* const encWithdraw = CastHook(&MesoEncode8_StorageWithdraw);
    void* const encDeposit = CastHook(&MesoEncode8_StorageDeposit);
    void* const encTrade = CastHook(&MesoEncode8_TradeSetMeso);

    const CallPatch kPatches[] = {
        // Character meso Decode8
        { 0x004E2CFC, kAddr_CInPacket_Decode4, decodeChar, "Decode_CharacterData_Money" },
        { 0x004E31F6, kAddr_CInPacket_Decode4, decodeChar, "Decode_StatChanged_MESO" },

        // Fredrick
        { 0x00799DE1, kAddr_CInPacket_Decode4, decodeFred, "Decode_Fredrick_Meso" },
        { 0x0079B058, kAddr_FormatNumber, fmtNumFred, "FormatNumber_Fredrick" },
        { 0x0079B092, kAddr_ZXString_cstr, fmtFred, "Format_Fredrick_Meso" },

        // Storage open/refresh (STORAGE opcode → sub_7C5DFD bit2)
        { 0x007C5E40, kAddr_CInPacket_Decode4, decodeStor, "Decode_Storage_OpenMeso" },

        // Trade SET_MESO S→C (PLAYER_INTERACTION mode 16 → sub_7C208E)
        { 0x007C20A2, kAddr_CInPacket_Decode4, decodeTrade, "Decode_Trade_SetMeso" },

        // Hired merchant
        { 0x00518859, kAddr_CInPacket_Decode4, decodeHm, "Decode_HM_VisitorMeso" },
        { 0x00518FBC, kAddr_CInPacket_Decode4, decodeHm, "Decode_HM_OwnerMeso" },

        // CUIItem backpack meso display
        { 0x0081DCA4, kAddr_ZXString_cstr, fmtChar, "Format_CUIItem_Meso_1" },
        { 0x0081DD4E, kAddr_ZXString_cstr, fmtChar, "Format_CUIItem_Meso_2" },

        // Storage UI: select fmt source then draw; FormatNumber writes long via game Assign
        { 0x007C77A5, kAddr_StorageFmtDraw, wrapStorFmt, "Wrap_StorageUI_StorageMeso" },
        { 0x007C77C8, kAddr_StorageFmtDraw, wrapCharFmt, "Wrap_StorageUI_CharMeso" },
        { 0x007C8088, kAddr_FormatNumber, fmtNumStor, "FormatNumber_StorageUI" },
        { 0x007C80C2, kAddr_ZXString_cstr, fmtActive, "Format_StorageUI_Meso_1" },
        { 0x007C8186, kAddr_ZXString_cstr, fmtActive, "Format_StorageUI_Meso_2" },

        // Trade UI meso lines (this[115] / this[116])
        { 0x007C2E42, kAddr_ZXString_cstr, fmtTrade0, "Format_Trade_Meso0_1" },
        { 0x007C2F00, kAddr_ZXString_cstr, fmtTrade0, "Format_Trade_Meso0_2" },
        { 0x007C314F, kAddr_ZXString_cstr, fmtTrade1, "Format_Trade_Meso1_1" },
        { 0x007C3238, kAddr_ZXString_cstr, fmtTrade1, "Format_Trade_Meso1_2" },

        // Hired merchant draw
        { 0x006FBA12, kAddr_ZXString_cstr, fmtHm, "Format_HM_Meso_1" },

        // Personal shop / player-interaction meso (FormatNumber + Fuse paths)
        { 0x006F0388, kAddr_FormatNumber, fmtNumChar, "FormatNumber_PersonalShop_Meso" },
        { 0x006F03BE, kAddr_ZXString_cstr, fmtChar, "Format_PersonalShop_Meso" },

        // NPC shop (CShopDlg) — player balance only; do not hook item price c_str sites
        { 0x00755E97, kAddr_FormatNumber, fmtNumChar, "FormatNumber_CShopDlg_Meso" },
        { 0x00755ED1, kAddr_ZXString_cstr, fmtChar, "Format_CShopDlg_Meso_FuseFmt" },
        { 0x007562C5, kAddr_ZXString_cstr, fmtChar, "Format_CShopDlg_Meso_FuseA" },
        { 0x00756364, kAddr_ZXString_cstr, fmtChar, "Format_CShopDlg_Meso_FuseB" },

        // Settlement / compare UI character meso
        { 0x005B885E, kAddr_ZXString_cstr, fmtChar, "Format_Settle_Meso_1" },
        { 0x005B8B5F, kAddr_ZXString_cstr, fmtChar, "Format_Settle_Meso_2" },

        // Encode8 outbound (server readLong)
        { 0x007C8909, kAddr_COutPacket_Encode4, encWithdraw, "Encode_Storage_Withdraw" },
        { 0x007C8A14, kAddr_COutPacket_Encode4, encDeposit, "Encode_Storage_Deposit" },
        { 0x007C3952, kAddr_COutPacket_Encode4, encTrade, "Encode_Trade_SetMeso" },
    };

    for (const CallPatch& site : kPatches) {
        if (PatchCallWithVerify(site)) {
            ++ok;
        } else {
            ++fail;
        }
    }

    MesoLog("AttachMesoUncapMod done ok=%d fail=%d", ok, fail);
}
