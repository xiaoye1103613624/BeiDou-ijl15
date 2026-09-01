#include "stdafx.h"
#include "EquipGrowthApi.h"
#include "../Client.h"
#include "compat/ClientAddresses.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

void EquipGrowth_RedrawActive();
void EquipGrowth_OnCacheUpdated(int itemId);

namespace {
constexpr const char* kVersion = "GROWTH_TIP_REEN_20260803";

bool g_hooksAttached = false;
bool g_handlerRegistered = false;

// Per-itemId caches keyed by fingerprint so enhance/itemLevel changes invalidate.
struct TipCache {
    int enhance = -1;
    int itemLevel = -1;
    int scroll = -1;
    std::string text;
    short bonus[15] = {};
    bool hasBonus = false;
    short flame[15] = {};
    bool hasFlame = false;
    bool resolved = false;   // authoritative empty-or-text (server or local-complete)
    bool fromServer = false; // true if last fill came from 0x17B
    bool localOnly = false;  // true if local paint is enough (no wire needed)
};

std::map<int, TipCache> g_cache;
std::set<int> g_pendingRequests;
std::map<int, DWORD> g_pendingTick;
std::map<int, int> g_requestAttempts;

// Pending: first wait 3s; then stop (no infinite retry). Max 1 enrichment send per fingerprint.
constexpr DWORD kPendingTimeoutMs = 3000;
constexpr int kMaxRequestAttempts = 1;

struct StatKey {
    const wchar_t* minKey;
    const char* label; // GBK for tip body (match set tip style)
    int bonusIdx;      // growthBonus index; -1 if unused in main tip peel
};

// Labels in GBK hex to avoid MSVC CP936 mangling.
static const StatKey kStatKeys[] = {
        {L"incSTRMin", "\xC1\xA6\xC1\xBF", 0},                 // 力量
        {L"incDEXMin", "\xC3\xF4\xBD\xDD", 1},                 // 敏捷
        {L"incINTMin", "\xD6\xC7\xC1\xA6", 2},                 // 智力
        {L"incLUKMin", "\xD4\xCB\xC6\xF8", 3},                 // 运气
        {L"incPADMin", "\xB9\xA5\xBB\xF7\xC1\xA6", 6},         // 攻击力
        {L"incMADMin", "\xC4\xA7\xC1\xA6", 7},                 // 魔法力
        {L"incMHPMin", "MaxHP", 4},
        {L"incMMPMin", "MaxMP", 5},
        {L"incPDDMin", "\xB7\xC0\xD3\xF9\xC1\xA6", 8},         // 防御力
        {L"incMDDMin", "\xC4\xA7\xB7\xA8\xB7\xC0\xD3\xF9", 9}, // 魔法防御
        {L"incACCMin", "\xC3\xFC\xD6\xD0", 10},                // 命中
        {L"incEVAMin", "\xBB\xD8\xB1\xDC", 11},                // 回避
        {L"incSpeedMin", "\xD2\xC6\xB6\xAF\xCB\xD9\xB6\xC8", 13}, // 移动速度
        {L"incJumpMin", "\xCC\xF8\xD4\xBE", 14},               // 跳跃
};

void DiagLog(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, _countof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    FILE* f = nullptr;
    if (fopen_s(&f, "equip_growth_tip.log", "a") == 0 && f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line);
        fclose(f);
    }
}

void SendGrowthRequest(CompatOutPacketBuilder& builder) {
    void* socket = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (!socket) {
        DiagLog("send skip: no socket");
        return;
    }
    CompatOutPacket packet = builder.Build();
    reinterpret_cast<void(__fastcall*)(void*, void*, CompatOutPacket*)>(
            ClientAddresses::kSendPacket)(socket, nullptr, &packet);
}

std::string DecodePacketString(CompatInPacket* packet) {
    const uint16_t len = packet->Decode<uint16_t>();
    std::string s;
    if (len == 0 || !packet->CanRead(len)) {
        return s;
    }
    const unsigned char* p = packet->Current();
    if (p) {
        s.assign(reinterpret_cast<const char*>(p), len);
    }
    packet->SetOffset(packet->GetOffset() + len);
    return s;
}

void ClearPending(int itemId) {
    g_pendingRequests.erase(itemId);
    g_pendingTick.erase(itemId);
}

void ResetRequestBudget(int itemId) {
    g_requestAttempts.erase(itemId);
    ClearPending(itemId);
}

static int ReadWzInt(IWzPropertyPtr node, const wchar_t* key) {
    if (!node) {
        return 0;
    }
    try {
        Ztl_variant_t v;
        if (FAILED(node->get_item(const_cast<wchar_t*>(key), &v))) {
            return 0;
        }
        Ztl_variant_t vInt;
        if (V_VT(&v) == VT_EMPTY || V_VT(&v) == VT_ERROR ||
            FAILED(ZComAPI::ZComVariantChangeType(&vInt, &v, 0, VT_I4))) {
            return 0;
        }
        return V_I4(&vInt);
    } catch (...) {
        return 0;
    }
}

static IWzPropertyPtr GetChildProp(IWzPropertyPtr parent, const wchar_t* key) {
    if (!parent || !key) {
        return nullptr;
    }
    try {
        Ztl_variant_t v;
        if (FAILED(parent->get_item(const_cast<wchar_t*>(key), &v))) {
            return nullptr;
        }
        return IWzPropertyPtr(v.GetUnknown(false, false));
    } catch (...) {
        return nullptr;
    }
}

static const wchar_t* EquipCategoryFolder(int itemId) {
    const int prefix = itemId / 10000;
    switch (prefix) {
        case 100: return L"Cap";
        case 101: return L"Accessory";
        case 102: return L"Accessory";
        case 103: return L"Accessory";
        case 104: return L"Coat";
        case 105: return L"Longcoat";
        case 106: return L"Pants";
        case 107: return L"Shoes";
        case 108: return L"Glove";
        case 109: return L"Shield";
        case 110: return L"Cape";
        case 111: return L"Ring";
        case 112: return L"Accessory";
        case 113: return L"Belt";
        case 114: return L"Accessory";
        case 115: return L"Shoulder";
        case 116: return L"Pocket";
        case 118: return L"Badge";
        case 119: return L"Emblem";
        case 120: return L"Totem";
        default: break;
    }
    if (prefix >= 130 && prefix <= 170) {
        return L"Weapon";
    }
    return nullptr;
}

static IWzPropertyPtr GetLevelInfoRootFromProp(IWzPropertyPtr pItem) {
    if (!pItem) {
        return nullptr;
    }
    IWzPropertyPtr pInfo = GetChildProp(pItem, L"info");
    if (!pInfo) {
        return nullptr;
    }
    IWzPropertyPtr pLevel = GetChildProp(pInfo, L"level");
    if (!pLevel) {
        return nullptr;
    }
    IWzPropertyPtr nested = GetChildProp(pLevel, L"info");
    return nested ? nested : pLevel; // some packs put N under level directly
}

/** info/level/info root for equip itemLevel growth tree. */
static IWzPropertyPtr GetLevelInfoRoot(int itemId) {
    if (itemId <= 0) {
        return nullptr;
    }
    try {
        CItemInfo* info = CItemInfo::GetInstance();
        if (info) {
            IWzPropertyPtr fromItemInfo = GetLevelInfoRootFromProp(info->GetItemInfo(itemId));
            if (fromItemInfo) {
                return fromItemInfo;
            }
        }
        // ResMan fallback (same path game loads Character/*.img)
        const wchar_t* folder = EquipCategoryFolder(itemId);
        IWzResManPtr rm = get_rm();
        if (!folder || !rm) {
            return nullptr;
        }
        wchar_t path[160];
        _snwprintf_s(path, _TRUNCATE, L"Character/%s/%08d.img", folder, itemId);
        Ztl_variant_t vRoot = rm->GetObjectA(path);
        IWzPropertyPtr pRoot(get_unknown(vRoot));
        return GetLevelInfoRootFromProp(pRoot);
    } catch (...) {
        return nullptr;
    }
}

static std::string Utf8ToGbk(const std::string& utf8) {
    if (utf8.empty()) {
        return utf8;
    }
    const int wLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wLen <= 0) {
        return utf8;
    }
    std::wstring wide(static_cast<size_t>(wLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wLen);
    while (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    const int gLen = WideCharToMultiByte(936, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (gLen <= 0) {
        return utf8;
    }
    std::string gbk(static_cast<size_t>(gLen), '\0');
    WideCharToMultiByte(936, 0, wide.c_str(), -1, &gbk[0], gLen, nullptr, nullptr);
    while (!gbk.empty() && gbk.back() == '\0') {
        gbk.pop_back();
    }
    return gbk;
}

static bool TextHasLevelSegment(const std::string& text) {
    // GBK 「级效果」
    return std::strstr(text.c_str(), "\xBC\xB6\xD0\xA7\xB9\xFB") != nullptr;
}

/** CItemInfo EQUIPITEM +0x200 → ZArray; count at ptr[-1] (IDA sub_5D2941). */
static int ResolveMaxItemLevelLocal(int itemId) {
    if (itemId <= 0) {
        return 0;
    }
    try {
        CItemInfo* info = CItemInfo::GetInstance();
        if (!info) {
            return 0;
        }
        CItemInfo::EQUIPITEM* eq = info->GetEquipItem(itemId);
        if (!eq) {
            return 0;
        }
        void** levelArr = *reinterpret_cast<void***>(reinterpret_cast<char*>(eq) + 512);
        if (!levelArr) {
            return 0;
        }
        const int count = *reinterpret_cast<int*>(reinterpret_cast<char*>(levelArr) - 4);
        if (count < 0 || count > 40) {
            return 0;
        }
        return count;
    } catch (...) {
        return 0;
    }
}

/** Fallback maxLevel by probing WZ info/level/info/N children. */
static int ResolveMaxItemLevelFromWz(int itemId) {
    IWzPropertyPtr root = GetLevelInfoRoot(itemId);
    if (!root) {
        return 0;
    }
    int cur = 1;
    for (;;) {
        wchar_t key[16];
        _snwprintf_s(key, _TRUNCATE, L"%d", cur);
        IWzPropertyPtr node = GetChildProp(root, key);
        if (!node) {
            return cur; // first missing → maxLevel
        }
        // Empty/stub nodes stop growth (match server getEquipLevel)
        int kids = 0;
        try {
            // Prefer presence of any known Min key over child count.
            for (const StatKey& sk : kStatKeys) {
                if (ReadWzInt(node, sk.minKey) != 0) {
                    kids = 2;
                    break;
                }
            }
            if (kids == 0) {
                // Node exists but no stats — treat as end (server: children.size()<=1)
                return cur;
            }
        } catch (...) {
            return cur;
        }
        ++cur;
        if (cur > 40) {
            return 40;
        }
    }
}

static int ResolveMaxItemLevel(int itemId) {
    int n = ResolveMaxItemLevelLocal(itemId);
    if (n > 1) {
        return n;
    }
    return ResolveMaxItemLevelFromWz(itemId);
}

static bool FingerprintMatch(const TipCache& c, int enhance, int itemLevel, int scroll) {
    return c.enhance == enhance && c.itemLevel == itemLevel && c.scroll == scroll;
}

static void FillBonusFromWz(int itemId, int itemLevel, short outBonus[15], bool& hasBonus) {
    hasBonus = false;
    memset(outBonus, 0, sizeof(short) * 15);
    if (itemId <= 0 || itemLevel <= 1) {
        return;
    }
    IWzPropertyPtr root = GetLevelInfoRoot(itemId);
    if (!root) {
        return;
    }
    bool any = false;
    for (int node = 1; node < itemLevel; ++node) {
        wchar_t key[16];
        _snwprintf_s(key, _TRUNCATE, L"%d", node);
        IWzPropertyPtr lvl = GetChildProp(root, key);
        if (!lvl) {
            continue;
        }
        for (const StatKey& sk : kStatKeys) {
            if (sk.bonusIdx < 0 || sk.bonusIdx >= 15) {
                continue;
            }
            const int v = ReadWzInt(lvl, sk.minKey);
            if (v != 0) {
                outBonus[sk.bonusIdx] = static_cast<short>(outBonus[sk.bonusIdx] + v);
                any = true;
            }
        }
    }
    hasBonus = any;
}

/**
 * Local tip: title + 「N级效果」segments from WZ node N (like set tip 「N件套效果」).
 * Shows all levels with stats; no Hyper / scroll combat%. GBK body for String2.
 */
static std::string BuildLocalGrowthText(int itemId, int /*enhance*/, int /*itemLevel*/, int /*scroll*/,
                                        int maxItemLevel, bool* outHasSegments = nullptr) {
    if (outHasSegments) {
        *outHasSegments = false;
    }
    if (maxItemLevel <= 1) {
        return "";
    }
    IWzPropertyPtr root = GetLevelInfoRoot(itemId);
    std::string sb;
    sb.reserve(384);
    // 装备成长属性
    sb += "\xD7\xB0\xB1\xB8\xB3\xC9\xB3\xA4\xCA\xF4\xD0\xD4\r\n";

    bool anySegment = false;
    // WZ node N → 「N级效果」+ Min attrs (验收：Lv2 含 1级效果 / 2级效果)
    for (int node = 1; node < maxItemLevel; ++node) {
        IWzPropertyPtr lvl;
        if (root) {
            wchar_t key[16];
            _snwprintf_s(key, _TRUNCATE, L"%d", node);
            lvl = GetChildProp(root, key);
        }
        std::vector<std::pair<const char*, int>> lines;
        if (lvl) {
            for (const StatKey& sk : kStatKeys) {
                const int v = ReadWzInt(lvl, sk.minKey);
                if (v != 0) {
                    lines.emplace_back(sk.label, v);
                }
            }
        }
        if (lines.empty()) {
            continue;
        }
        anySegment = true;
        char header[48];
        _snprintf_s(header, _TRUNCATE, "%d\xBC\xB6\xD0\xA7\xB9\xFB\r\n", node);
        sb += header;
        for (const auto& ln : lines) {
            char row[80];
            // Match set tip: "label : +N" (ASCII colon + spaces) for IndentLeft canvas.
            _snprintf_s(row, _TRUNCATE, "%s : +%d\r\n", ln.first, ln.second);
            sb += row;
        }
    }
    if (!anySegment) {
        // Incomplete local read — do NOT invent Lv1/max stub (was marked resolved → tip hidden).
        return "";
    }
    if (outHasSegments) {
        *outHasSegments = true;
    }
    while (!sb.empty() && (sb.back() == '\n' || sb.back() == '\r')) {
        sb.pop_back();
    }
    return sb;
}

/**
 * Local is complete when WZ 「N级效果」segments exist, or growth confirmed absent.
 * maxLevel>1 alone is NOT enough (EQUIPITEM count can exist while property tree unread).
 */
static bool LocalIsSufficient(int enhance, int itemLevel, int scroll, int maxItemLevel,
                              const TipCache* existing) {
    if (enhance < 0 || itemLevel < 0 || scroll < 0) {
        return false; // id-only path may still enrich
    }
    if (existing && FingerprintMatch(*existing, enhance, itemLevel, scroll)
            && existing->fromServer && existing->resolved) {
        return true;
    }
    if (maxItemLevel <= 1) {
        return true; // confirmed no growth tip
    }
    if (existing && FingerprintMatch(*existing, enhance, itemLevel, scroll)
            && existing->localOnly && existing->resolved
            && TextHasLevelSegment(existing->text)) {
        return true;
    }
    return false;
}

static void StoreLocalTip(int itemId, int enhance, int itemLevel, int scroll,
                          const std::string& text, bool markResolvedLocal,
                          const short* bonus, bool hasBonus) {
    TipCache& c = g_cache[itemId];
    const bool sameFp = FingerprintMatch(c, enhance, itemLevel, scroll);
    // Local WZ 「N级效果」is authoritative — always overwrite server UTF-8 mix.
    c.fromServer = false;
    c.enhance = enhance;
    c.itemLevel = itemLevel;
    c.scroll = scroll;
    c.text = text;
    c.localOnly = markResolvedLocal;
    c.resolved = markResolvedLocal;
    if (hasBonus && bonus) {
        c.hasBonus = true;
        memcpy(c.bonus, bonus, sizeof(c.bonus));
    } else if (!sameFp) {
        c.hasBonus = false;
        memset(c.bonus, 0, sizeof(c.bonus));
    }
}

bool HandleGrowthInbound(void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr) {
        return false;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
        unsigned short legacy = 0;
        if (packet->TryPeekLegacyOpcode(legacy) && legacy == opcode) {
            packet->SetOffset(4);
            if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
                DiagLog("handler miss peek op=0x%X expect=0x%X off=%zu size=%lu",
                        static_cast<unsigned>(peeked), static_cast<unsigned>(opcode),
                        packet->GetOffset(), packet->Size());
                return false;
            }
        } else {
            DiagLog("handler miss peek op=0x%X expect=0x%X off=%zu size=%lu",
                    static_cast<unsigned>(peeked), static_cast<unsigned>(opcode),
                    packet->GetOffset(), packet->Size());
            return false;
        }
    }
    packet->Decode<uint16_t>();
    if (opcode != CustomSendOpcode::kEquipGrowthTip) {
        return false;
    }
    DiagLog("handler hit opcode=0x%X off=%zu size=%lu VERSION %s",
            static_cast<unsigned>(opcode), packet->GetOffset(), packet->Size(), kVersion);
    const int itemId = static_cast<int>(packet->Decode<uint32_t>());
    const bool hasData = packet->Decode<uint8_t>() != 0;
    std::string text = DecodePacketString(packet);
    short bonus[15] = {};
    bool hasBonus = false;
    short flame[15] = {};
    bool hasFlame = false;
    if (packet->CanRead(1)) {
        const uint8_t flag = packet->Decode<uint8_t>();
        if (flag != 0 && packet->CanRead(15 * 2)) {
            hasBonus = true;
            for (int i = 0; i < 15; ++i) {
                bonus[i] = static_cast<short>(packet->Decode<uint16_t>());
            }
        }
    }
    if (packet->CanRead(1)) {
        const uint8_t flameFlag = packet->Decode<uint8_t>();
        if (flameFlag != 0 && packet->CanRead(15 * 2)) {
            hasFlame = true;
            for (int i = 0; i < 15; ++i) {
                flame[i] = static_cast<short>(packet->Decode<uint16_t>());
            }
        }
    }
    ClearPending(itemId);
    g_requestAttempts.erase(itemId);
    if (hasData && text.empty()) {
        DiagLog("recv itemId=%d hasData=1 emptyText (incomplete, not resolved)", itemId);
        auto it = g_cache.find(itemId);
        if (it != g_cache.end()) {
            it->second.resolved = false;
            it->second.fromServer = false;
        }
        return true;
    }
    TipCache& c = g_cache[itemId];
    c.resolved = true;
    c.fromServer = true;
    c.localOnly = false;
    if (hasData && !text.empty()) {
        // Prefer local GBK 「N级效果」; else convert server UTF-8 → GBK for String2.
        bool hasSeg = false;
        const int maxLv = ResolveMaxItemLevel(itemId);
        const std::string local = BuildLocalGrowthText(
                itemId, c.enhance > 0 ? c.enhance : 0,
                c.itemLevel > 0 ? c.itemLevel : 1,
                c.scroll > 0 ? c.scroll : 0, maxLv, &hasSeg);
        if (hasSeg && TextHasLevelSegment(local)) {
            c.text = local;
            c.localOnly = true;
        } else if (TextHasLevelSegment(c.text)) {
            // keep existing local GBK
        } else {
            c.text = Utf8ToGbk(text);
        }
        if (hasBonus) {
            c.hasBonus = true;
            for (int i = 0; i < 15; ++i) {
                c.bonus[i] = bonus[i];
            }
        } else if (!c.hasBonus && c.itemLevel > 1) {
            FillBonusFromWz(itemId, c.itemLevel, c.bonus, c.hasBonus);
        }
        DiagLog("recv itemId=%d OK textLen=%zu bonus=%d flame=%d localRebuild=%d (push or reply)",
                itemId, c.text.size(), c.hasBonus ? 1 : 0, hasFlame ? 1 : 0, hasSeg ? 1 : 0);
    } else {
        c.text.clear();
        c.hasBonus = false;
        memset(c.bonus, 0, sizeof(c.bonus));
        DiagLog("recv itemId=%d empty hasData=%d flame=%d", itemId, hasData ? 1 : 0, hasFlame ? 1 : 0);
    }
    if (hasFlame) {
        c.hasFlame = true;
        for (int i = 0; i < 15; ++i) {
            c.flame[i] = flame[i];
        }
    } else if (!hasData) {
        c.hasFlame = false;
        memset(c.flame, 0, sizeof(c.flame));
    }
    EquipGrowth_OnCacheUpdated(itemId);
    return true;
}

void RegisterInboundHandlers() {
    PacketDispatcher::RegisterHandler(CustomSendOpcode::kEquipGrowthTip, HandleGrowthInbound);
    PacketDispatcher::SetDirectHandler(CustomSendOpcode::kEquipGrowthTip, HandleGrowthInbound);
    g_handlerRegistered = true;
    DiagLog("VERSION %s", kVersion);
    DiagLog("register handler opcode=0x%X hooked=%d handlers=%u mapHas=%d",
            static_cast<unsigned>(CustomSendOpcode::kEquipGrowthTip),
            PacketDispatcher::IsHookInstalled() ? 1 : 0,
            static_cast<unsigned>(PacketDispatcher::HandlerCount()),
            PacketDispatcher::HasHandler(CustomSendOpcode::kEquipGrowthTip) ? 1 : 0);
}

void EnsureHandlersRegistered() {
    if (g_handlerRegistered &&
        PacketDispatcher::HasHandler(CustomSendOpcode::kEquipGrowthTip)) {
        return;
    }
    RegisterInboundHandlers();
}

static void MaybeSendEnrichment(int itemId, int enhance, int itemLevel, int scroll) {
    TipCache* existing = nullptr;
    auto it = g_cache.find(itemId);
    if (it != g_cache.end()) {
        existing = &it->second;
        if (existing->resolved && FingerprintMatch(*existing, enhance, itemLevel, scroll)
                && (existing->fromServer || existing->localOnly)) {
            return; // zero wire
        }
    }
    const int maxLv = ResolveMaxItemLevel(itemId);
    if (LocalIsSufficient(enhance, itemLevel, scroll, maxLv, existing)) {
        return;
    }
    int attempts = 0;
    auto ait = g_requestAttempts.find(itemId);
    if (ait != g_requestAttempts.end()) {
        attempts = ait->second;
    }
    if (attempts >= kMaxRequestAttempts) {
        DiagLog("enrich skip cap itemId=%d attempts=%d (local only)", itemId, attempts);
        return;
    }
    const DWORD now = GetTickCount();
    auto pit = g_pendingTick.find(itemId);
    if (g_pendingRequests.count(itemId) > 0 && pit != g_pendingTick.end()) {
        if (now - pit->second < kPendingTimeoutMs) {
            return;
        }
        DiagLog("enrich pending timeout itemId=%d — no retry", itemId);
        ClearPending(itemId);
        g_requestAttempts[itemId] = kMaxRequestAttempts;
        return;
    }
    g_pendingRequests.insert(itemId);
    g_pendingTick[itemId] = now;
    g_requestAttempts[itemId] = attempts + 1;
    CompatOutPacketBuilder builder(CustomRecvOpcode::kEquipGrowthTipRequest);
    builder.WriteInt(itemId);
    SendGrowthRequest(builder);
    DiagLog("send req itemId=%d opcode=0x%X attempt=%d reason=enrich handlers=%u",
            itemId, CustomRecvOpcode::kEquipGrowthTipRequest, g_requestAttempts[itemId],
            static_cast<unsigned>(PacketDispatcher::HandlerCount()));
}
} // namespace

namespace EquipGrowth {
void RegisterPacketHandler() {
    RegisterInboundHandlers();
}

void EnsureHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    EnsureHandlersRegistered();
}

void OnHoverEquip(int itemId, int enhance, int itemLevel, int scrollLevel) {
    if (!Client::enableGrowthCompanionTip || itemId <= 0) {
        return;
    }
    EnsureHandlersRegistered();
    if (enhance < 0 || enhance > 10) {
        enhance = 0;
    }
    if (itemLevel < 0 || itemLevel > 40) {
        itemLevel = 0;
    }
    if (scrollLevel < 0 || scrollLevel > 99) {
        scrollLevel = 0;
    }
    const int maxLv = ResolveMaxItemLevel(itemId);
    auto it = g_cache.find(itemId);
    if (it != g_cache.end() && !FingerprintMatch(it->second, enhance, itemLevel, scrollLevel)) {
        DiagLog("fingerprint change itemId=%d old=%d/%d/%d new=%d/%d/%d",
                itemId, it->second.enhance, it->second.itemLevel, it->second.scroll,
                enhance, itemLevel, scrollLevel);
        g_cache.erase(it);
        ResetRequestBudget(itemId);
        it = g_cache.end();
    }
    // Prefer existing resolved segmented text (second hover / cache).
    // Rebuild if still on legacy "label +N" (no set-style " : ") so indent canvas matches set tip.
    if (it != g_cache.end() && !it->second.text.empty()
            && FingerprintMatch(it->second, enhance, itemLevel, scrollLevel)
            && it->second.resolved && TextHasLevelSegment(it->second.text)
            && it->second.text.find(" : ") != std::string::npos) {
        if (!it->second.hasBonus && itemLevel > 1) {
            short bonus[15] = {};
            bool hasBonus = false;
            FillBonusFromWz(itemId, itemLevel, bonus, hasBonus);
            if (hasBonus) {
                it->second.hasBonus = true;
                memcpy(it->second.bonus, bonus, sizeof(it->second.bonus));
                EquipGrowth_OnCacheUpdated(itemId);
            }
        }
        return;
    }
    // Drop stale stub / empty-resolved from prior builds (Lv1/max without 级效果).
    if (it != g_cache.end() && it->second.resolved && !TextHasLevelSegment(it->second.text)) {
        g_cache.erase(it);
        ResetRequestBudget(itemId);
        it = g_cache.end();
    }
    bool hasSeg = false;
    const std::string local =
            BuildLocalGrowthText(itemId, enhance, itemLevel, scrollLevel, maxLv, &hasSeg);
    short bonus[15] = {};
    bool hasBonus = false;
    FillBonusFromWz(itemId, itemLevel, bonus, hasBonus);
    if (hasSeg && !local.empty()) {
        StoreLocalTip(itemId, enhance, itemLevel, scrollLevel, local, true, bonus, hasBonus);
        DiagLog("local tip itemId=%d enh=%d lv=%d scroll=%d max=%d len=%zu bonus=%d sufficient=1",
                itemId, enhance, itemLevel, scrollLevel, maxLv, local.size(),
                hasBonus ? 1 : 0);
        EquipGrowth_OnCacheUpdated(itemId);
        return; // setlike: local WZ segments enough, no wire
    }
    if (maxLv <= 1) {
        TipCache& c = g_cache[itemId];
        c.enhance = enhance;
        c.itemLevel = itemLevel;
        c.scroll = scrollLevel;
        c.text.clear();
        c.resolved = true;
        c.localOnly = true;
        c.fromServer = false;
        c.hasBonus = false;
        DiagLog("local empty resolved itemId=%d (no wz growth)", itemId);
        return;
    }
    // maxLv>1 but segments unread — do NOT mark resolvedEmpty; ask server once.
    DiagLog("local incomplete itemId=%d enh=%d lv=%d scroll=%d max=%d — enrich",
            itemId, enhance, itemLevel, scrollLevel, maxLv);
    MaybeSendEnrichment(itemId, enhance, itemLevel, scrollLevel);
}

void RequestGrowthTip(int itemId) {
    if (!Client::enableGrowthCompanionTip || itemId <= 0) {
        return;
    }
    EnsureHandlersRegistered();
    auto it = g_cache.find(itemId);
    if (it != g_cache.end() && it->second.resolved && !it->second.text.empty()) {
        return;
    }
    if (it != g_cache.end() && it->second.resolved && it->second.localOnly
            && it->second.text.empty()) {
        return;
    }
    const int maxLv = ResolveMaxItemLevel(itemId);
    if (maxLv > 1) {
        bool hasSeg = false;
        const std::string local = BuildLocalGrowthText(itemId, 0, 1, 0, maxLv, &hasSeg);
        if (hasSeg && !local.empty()) {
            short bonus[15] = {};
            bool hasBonus = false;
            StoreLocalTip(itemId, 0, 1, 0, local, true, bonus, hasBonus);
            DiagLog("local tip (id-only) itemId=%d max=%d len=%zu", itemId, maxLv,
                    local.size());
            EquipGrowth_OnCacheUpdated(itemId);
            return;
        }
        MaybeSendEnrichment(itemId, -1, -1, -1);
        return;
    }
    MaybeSendEnrichment(itemId, -1, -1, -1);
}

void RedrawActivePanel() {
    EquipGrowth_RedrawActive();
}

void InvalidateEmptyCache(int itemId) {
    if (itemId <= 0) {
        return;
    }
    auto it = g_cache.find(itemId);
    if (it == g_cache.end()) {
        return;
    }
    if (!it->second.text.empty()) {
        return; // keep positive cache across hide/show
    }
    g_cache.erase(it);
    ResetRequestBudget(itemId);
}

void InvalidateCache(int itemId) {
    if (itemId <= 0) {
        return;
    }
    g_cache.erase(itemId);
    ResetRequestBudget(itemId);
}

const char* GetGrowthTipText(int itemId) {
    const auto it = g_cache.find(itemId);
    if (it != g_cache.end() && !it->second.text.empty()) {
        return it->second.text.c_str();
    }
    return "";
}

int GetGrowthTipItemLevel(int itemId) {
    const auto it = g_cache.find(itemId);
    if (it != g_cache.end() && it->second.itemLevel >= 0) {
        return it->second.itemLevel;
    }
    return 0;
}

bool HasGrowthTip(int itemId) {
    const auto it = g_cache.find(itemId);
    return it != g_cache.end() && !it->second.text.empty();
}

bool IsGrowthTipResolved(int itemId) {
    const auto it = g_cache.find(itemId);
    return it != g_cache.end() && it->second.resolved;
}

int GetGrowthBonusForStat(int itemId, int statIdx) {
    if (itemId <= 0 || statIdx < 0 || statIdx >= 15) {
        return 0;
    }
    const auto it = g_cache.find(itemId);
    if (it == g_cache.end() || !it->second.hasBonus) {
        return 0;
    }
    return static_cast<int>(it->second.bonus[statIdx]);
}

int GetFlameBonusForStat(int itemId, int statIdx) {
    if (itemId <= 0 || statIdx < 0 || statIdx >= 15) {
        return 0;
    }
    const auto it = g_cache.find(itemId);
    if (it == g_cache.end() || !it->second.hasFlame) {
        return 0;
    }
    return static_cast<int>(it->second.flame[statIdx]);
}
} // namespace EquipGrowth
