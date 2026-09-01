// CombatPower — show "战力：xxxx" under character name tags.
//
// IDA (BeiDou.exe @ 0x400000) verification 2026-09-01:
//   MakeNameTag          0x00942DCC  (__thiscall CUser*, retn) — builds name/guild/medal tags
//   NameTagDraw          0x005F0334  (__thiscall CUser*, retn 0x24)
//   Styles: 1000 name, 1004 guild (middle), 1005 title (middle), 1006 medal badge (bottom)
//   Layer slots @ CUser+0x7C + styleSlot*4: 0=name, 1=middle(1004/1005), 2=medal(1006)
//     confirmed lea ecx,[esi+eax*4+7Ch] @ 0x005F03A7
//   Do NOT draw CP with 1006/1007 — badge row (1006) must stay separate from middle row.
//   CUser+0x11A8         dwCharacterId
//   CUser+0x11B0         guild name (ZXString char*)
//   CUser+0x169          equipped medal item id (MakeNameTag badge branch)
//   UserPool singleton   0x00BEBFA8, GetUser 0x009716ED
// Packet: CustomSendOpcode::kCombatPowerSync = 0x3732 (charId int + power int64)
// Unchanged existing hook addresses; only new attach on MakeNameTag.
//
// Overlap fix: when middle was empty, MakeNameTag stacks medal under the name; CP then
// fills middle at the same Y and covers the badge. After drawing CP, RelOffset the medal
// layer down by middle height + stack gap (matches NameTagDraw's prevH+1 stacking).

#include "stdafx.h"
#include "CombatPowerApi.h"
#include "compat/ClientAddresses.h"
#include "compat/ModRegistry.h"
#include "compat/PacketDispatcher.h"
#include "compat/WzLib/IWzGr2DLayer.h"
#include "compat/hook.h"
#include "compat/wvs/Packet.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {

constexpr uintptr_t kMakeNameTagAddr = 0x00942DCC;
constexpr uintptr_t kNameTagDrawAddr = 0x005F0334;
constexpr uintptr_t kZxCopyAAddr = 0x00444EFA;   // ZXString copy-construct onto stack
constexpr uintptr_t kZxCopyBAddr = 0x004145AB;   // another ZXString/ZRef copy onto stack
constexpr uintptr_t kUserPoolSingletonAddr = 0x00BEBFA8;
constexpr uintptr_t kUserPoolGetUserAddr = 0x009716ED;

constexpr uintptr_t kGetTitleStrAddr = 0x00941F10;

constexpr int kOffCharacterId = 0x11A8;
constexpr int kOffMedalItemId = 0x169;
constexpr int kOffGuildName = 0x11B0; // ZXString<char> first field = char*
constexpr int kOffNameTagOriginA = 0x11A4; // edi in MakeNameTag
constexpr int kOffNameTagOriginB = 0x1150;
constexpr int kOffGuildLogoBg = 0x11B4;
constexpr int kOffGuildLogoBgColor = 0x11B6;
constexpr int kOffGuildLogo = 0x11B8;
constexpr int kOffGuildLogoColor = 0x11BA;
constexpr int kOffNameTagLayerMiddle = 0x80; // slot 1 — guild/title/CP
constexpr int kOffNameTagLayerMedal = 0x84;  // slot 2 — medal badge
// NameTagDraw stacks the next row at previousLayerHeight + 1.
constexpr int kNameTagStackGap = 1;

constexpr unsigned kStyleGuild = 1004; // 0x3EC — middle row (guild emblem)
constexpr unsigned kStyleTitle = 1005; // 0x3ED — middle row (plain text)

// Client renders name tags as GBK (CP936), not UTF-8.
constexpr char kCpLabelGbk[] = "\xD5\xBD\xC1\xA6\xA3\xBA"; // "战力："

using MakeNameTag_t = void(__thiscall*)(void* user);
using ZxCopy_t = void(__thiscall*)(int* dest, int* src);
using NameTagDraw_t = void(__thiscall*)(
        void* user,
        char* str,
        int zref1150,
        int zref11A4,
        int style,
        int colorOrLayer,
        int logoBg,
        int logoBgColor,
        int logo,
        int logoColor);
using UserPoolGetUser_t = void*(__thiscall*)(void* pool, int characterId);
using QueryTagText_t = void(__thiscall*)(void* user, const char** out);

MakeNameTag_t g_makeNameTagOrig = reinterpret_cast<MakeNameTag_t>(kMakeNameTagAddr);
NameTagDraw_t g_nameTagDraw = reinterpret_cast<NameTagDraw_t>(kNameTagDrawAddr);
ZxCopy_t g_zxCopyA = reinterpret_cast<ZxCopy_t>(kZxCopyAAddr);
ZxCopy_t g_zxCopyB = reinterpret_cast<ZxCopy_t>(kZxCopyBAddr);
UserPoolGetUser_t g_userPoolGetUser = reinterpret_cast<UserPoolGetUser_t>(kUserPoolGetUserAddr);

std::mutex g_lock;
std::unordered_map<int, long long> g_powerByChar;

bool IsReadable(const void* p, size_t n) {
    if (!p) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD prot = mbi.Protect & 0xFF;
    if (prot == PAGE_NOACCESS || prot == PAGE_GUARD) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const auto end = begin + mbi.RegionSize;
    const auto ptr = reinterpret_cast<uintptr_t>(p);
    return ptr >= begin && ptr + n <= end;
}

int ReadCharacterId(void* user) {
    if (!IsReadable(user, static_cast<size_t>(kOffCharacterId) + 4)) {
        return 0;
    }
    __try {
        return *reinterpret_cast<int*>(reinterpret_cast<char*>(user) + kOffCharacterId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* ReadCString(const char* s) {
    if (!s || !IsReadable(s, 1) || s[0] == '\0') {
        return nullptr;
    }
    return s;
}

const char* QueryMiddleTagText(void* user, uintptr_t fnAddr) {
    if (!user) {
        return nullptr;
    }
    const char* text = nullptr;
    __try {
        reinterpret_cast<QueryTagText_t>(fnAddr)(user, &text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return ReadCString(text);
}

const char* ReadGuildName(void* user) {
    if (!IsReadable(user, static_cast<size_t>(kOffGuildName) + 4)) {
        return nullptr;
    }
    __try {
        const char* s = *reinterpret_cast<char**>(reinterpret_cast<char*>(user) + kOffGuildName);
        return ReadCString(s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

struct MiddleTagChoice {
    const char* text;
    unsigned style;
    bool withGuildEmblem;
};

MiddleTagChoice ResolveMiddleTag(void* user) {
    // Match MakeNameTag middle-row priority: title, then guild. Never use medal
    // helpers/styles here — row 3 (style 1006) is the medal badge and must not overlap.
    if (const char* title = QueryMiddleTagText(user, kGetTitleStrAddr)) {
        return {title, kStyleTitle, false};
    }
    if (const char* guild = ReadGuildName(user)) {
        return {guild, kStyleGuild, true};
    }
    return {nullptr, kStyleTitle, false};
}

bool HasMedalBadge(void* user) {
    if (!IsReadable(user, static_cast<size_t>(kOffMedalItemId) + 4)) {
        return false;
    }
    __try {
        return *reinterpret_cast<int*>(reinterpret_cast<char*>(user) + kOffMedalItemId) > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* FindUser(int characterId) {
    if (characterId <= 0) {
        return nullptr;
    }
    void* pool = *reinterpret_cast<void**>(kUserPoolSingletonAddr);
    if (!pool) {
        return nullptr;
    }
    __try {
        return g_userPoolGetUser(pool, characterId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Vanilla nameplate has 3 visual rows (name / middle / medal). Middle is shared by
// guild, title and medal — there is no spare slot for a 4th line without rewriting
// the renderer. When guild exists, append CP on the same middle row.
void DrawMiddleLine(void* user, const char* text, unsigned style, bool withGuildEmblem) {
    if (!user || !text || text[0] == '\0' || !g_nameTagDraw) {
        return;
    }

    char* u = reinterpret_cast<char*>(user);
    unsigned short logoBg = 0;
    unsigned char logoBgColor = 0;
    unsigned short logo = 0;
    unsigned char logoColor = 0;
    if (withGuildEmblem && IsReadable(u + kOffGuildLogoBg, 8)) {
        logoBg = *reinterpret_cast<unsigned short*>(u + kOffGuildLogoBg);
        logoBgColor = *reinterpret_cast<unsigned char*>(u + kOffGuildLogoBgColor);
        logo = *reinterpret_cast<unsigned short*>(u + kOffGuildLogo);
        logoColor = *reinterpret_cast<unsigned char*>(u + kOffGuildLogoColor);
    }

    int zref1150 = 0;
    int zref11A4 = 0;
    __try {
        g_zxCopyB(&zref1150, reinterpret_cast<int*>(u + kOffNameTagOriginB));
        g_zxCopyA(&zref11A4, reinterpret_cast<int*>(u + kOffNameTagOriginA));
        g_nameTagDraw(
                user,
                const_cast<char*>(text),
                zref1150,
                zref11A4,
                static_cast<int>(style),
                0,
                static_cast<int>(logoBg),
                static_cast<int>(logoBgColor),
                static_cast<int>(logo),
                static_cast<int>(logoColor));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// When CP was painted into an empty middle slot, medal was already stacked under the
// name at nearly the same Y. Shift medal down by the new middle height (+ vanilla gap).
void PushMedalBelowCombatPowerBody(void* user) {
    if (!user) {
        return;
    }
    char* u = reinterpret_cast<char*>(user);
    if (!IsReadable(u + kOffNameTagLayerMiddle, 4) ||
        !IsReadable(u + kOffNameTagLayerMedal, 4)) {
        return;
    }
    IWzGr2DLayer* middle =
            *reinterpret_cast<IWzGr2DLayer**>(u + kOffNameTagLayerMiddle);
    IWzGr2DLayer* medal =
            *reinterpret_cast<IWzGr2DLayer**>(u + kOffNameTagLayerMedal);
    if (!middle || !medal) {
        return;
    }
    const int h = middle->height;
    if (h <= 0) {
        return;
    }
    medal->RelOffset(0, h + kNameTagStackGap);
}

void PushMedalBelowCombatPower(void* user) {
    __try {
        PushMedalBelowCombatPowerBody(user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void DrawCombatPowerLine(void* user, long long power) {
    if (!user || power <= 0) {
        return;
    }

    char text[128];
    const MiddleTagChoice middle = ResolveMiddleTag(user);
    const bool hasMedal = HasMedalBadge(user);
    if (middle.text != nullptr) {
        _snprintf_s(
                text,
                _TRUNCATE,
                "%s  %s%lld",
                middle.text,
                kCpLabelGbk,
                static_cast<long long>(power));
        DrawMiddleLine(user, text, middle.style, middle.withGuildEmblem);
        // Medal was already stacked under the old middle row; height is unchanged enough.
        return;
    }

    _snprintf_s(
            text,
            _TRUNCATE,
            "%s%lld",
            kCpLabelGbk,
            static_cast<long long>(power));
    // When a medal badge occupies row 3 (1006), use guild middle-row layout (1004)
    // so CP stays on row 2 above the badge — title style (1005) can overlap visually.
    const unsigned style = hasMedal ? kStyleGuild : kStyleTitle;
    DrawMiddleLine(user, text, style, false);
    if (hasMedal) {
        PushMedalBelowCombatPower(user);
    }
}

void ApplyNameTagWithCombatPower(void* user) {
    if (!user || !g_makeNameTagOrig) {
        return;
    }
    __try {
        g_makeNameTagOrig(user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    const int cid = ReadCharacterId(user);
    if (cid <= 0) {
        return;
    }
    const long long power = CombatPower::GetCombatPower(cid);
    if (power > 0) {
        DrawCombatPowerLine(user, power);
    }
}

void __fastcall MakeNameTag_Hook(void* user) {
    ApplyNameTagWithCombatPower(user);
}

void RefreshNameTag(void* user) {
    // Must re-apply CP line: g_makeNameTagOrig is the trampoline (skips this hook).
    ApplyNameTagWithCombatPower(user);
}

bool HandleCombatPowerPacket(CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr || opcode != CustomSendOpcode::kCombatPowerSync) {
        return false;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
        return false;
    }
    packet->Decode<uint16_t>();
    const int characterId = static_cast<int>(packet->Decode<uint32_t>());
    const long long power = static_cast<long long>(packet->Decode<uint64_t>());
    CombatPower::SetCombatPower(characterId, power);
    if (void* user = FindUser(characterId)) {
        RefreshNameTag(user);
    }
    return true;
}

void EnsureHooks() {
    static bool attached = false;
    if (attached) {
        return;
    }
    // Expected prologue: mov eax, imm32 ; call __EH_prolog
    const unsigned char expect[] = {0xB8, 0x14, 0xDA, 0xAD, 0x00};
    if (!IsReadable(reinterpret_cast<const void*>(kMakeNameTagAddr), sizeof(expect))) {
        return;
    }
    if (memcmp(reinterpret_cast<const void*>(kMakeNameTagAddr), expect, sizeof(expect)) != 0) {
        return;
    }
    ATTACH_HOOK(g_makeNameTagOrig, MakeNameTag_Hook);
    attached = true;
}

} // namespace

namespace CombatPower {

void SetCombatPower(int characterId, long long power) {
    if (characterId <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_lock);
    if (power <= 0) {
        g_powerByChar.erase(characterId);
    } else {
        g_powerByChar[characterId] = power;
    }
}

long long GetCombatPower(int characterId) {
    std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_powerByChar.find(characterId);
    return it == g_powerByChar.end() ? 0 : it->second;
}

void ClearCombatPower(int characterId) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_powerByChar.erase(characterId);
}

void RegisterModule() {
    CompatModule module{};
    module.name = "CombatPower";
    module.onAttach = []() {
        PacketDispatcher::RegisterHandler(
                CustomSendOpcode::kCombatPowerSync,
                [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                    return HandleCombatPowerPacket(packet, opcode);
                });
        EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(module));
}

} // namespace CombatPower
