#include <windows.h>
#include <comdef.h>
#include "ztl/ztl.h"

#include <string>
#include <unordered_map>

#include "wvs/util.h"
#include "getwzinfo.h"

// =====================================================
// CACHE
// =====================================================
static std::unordered_map<int, std::string> g_mapNames;
static std::unordered_map<int, std::string> g_mobNames;
static std::unordered_map<int, int> g_mobLevels;
static std::unordered_map<int, MobCombatInfo> g_mobCombat;
static std::unordered_map<int, std::string> g_npcNames;

static std::unordered_map<int, int> g_mobToCard;
static std::unordered_map<int, IWzCanvasPtr> g_cardIcons;

// =====================================================
// HELPERS
// =====================================================
namespace wz {

inline IWzPropertyPtr Get(const wchar_t* path) {
    return get_rm()->GetObjectA(path).GetUnknown();
}

inline IWzPropertyPtr GetItem(IWzPropertyPtr parent, const wchar_t* key) {
    return parent ? parent->item[key].GetUnknown() : nullptr;
}

inline std::string GetStr(IWzPropertyPtr p, const wchar_t* key, const char* def = "Unknown") {
    if (!p)
        return def;

    try {
        Ztl_variant_t v = p->item[key];
        if (v.vt == VT_BSTR)
            return (const char*)_bstr_t(v);
    } catch (...) {
    }

    return def;
}

inline int GetInt(IWzPropertyPtr p, const wchar_t* key, int def = 0) {
    if (!p)
        return def;
    Ztl_variant_t v = p->item[key];
    return get_int32(v, def);
}
} // namespace wz

// v83 Map.wz filenames: Map0 starter fields use 9-digit keys (001000000.img), not %08d.
IWzPropertyPtr OpenMapPropertyById(int mapId) {
    if (mapId <= 0) {
        return nullptr;
    }

    const int folder = mapId / 100000000;
    wchar_t path[128];
    static const wchar_t* kFormats[] = {
        L"Map/Map/Map%d/%09d.img",
        L"Map/Map/Map%d/%08d.img",
        L"Map/Map/Map%d/%07d.img",
    };

    for (const wchar_t* fmt : kFormats) {
        swprintf(path, _countof(path), fmt, folder, mapId);
        IWzPropertyPtr map = wz::Get(path);
        if (map) {
            return map;
        }
    }

    if (folder == 0 && mapId >= 1000000) {
        swprintf(path, _countof(path), L"Map/Map/Map1/%09d.img", mapId);
        IWzPropertyPtr map = wz::Get(path);
        if (map) {
            return map;
        }
    }

    return nullptr;
}

// =====================================================
// MAP NAME
// =====================================================
std::string GetMapById(int id) {

    auto it = g_mapNames.find(id);
    if (it != g_mapNames.end())
        return it->second;

    static const wchar_t* categories[] = {
        L"maple", L"victoria", L"ossyria", L"elin",
        L"weddingGL", L"MasteriaGL", L"HalloweenGL",
        L"jp", L"etc", L"singapore", L"event", L"Episode1GL"
    };

    IWzPropertyPtr root = wz::Get(L"String/Map.img");
    if (!root)
        return "Unknown";

    Ztl_bstr_t sId = std::to_wstring(id).c_str();

    for (auto cat : categories) {

        IWzPropertyPtr category = wz::GetItem(root, cat);
        if (!category)
            continue;

        IWzPropertyPtr map = category->item[sId].GetUnknown();
        if (!map)
            continue;

        std::string street = wz::GetStr(map, L"streetName");
        std::string name = wz::GetStr(map, L"mapName");

        std::string result = street + " - " + name;
        g_mapNames[id] = result;

        return result;
    }

    // cache the miss so repeated queries for an absent id don't re-walk all
    // 12 String/Map.img categories every time
    g_mapNames[id] = "Unknown";
    return "Unknown";
}

// =====================================================
// MOB NAME
// =====================================================
std::string GetMobNameById(int id) {

    auto it = g_mobNames.find(id);
    if (it != g_mobNames.end())
        return it->second;

    IWzPropertyPtr root = wz::Get(L"String/Mob.img");
    if (!root)
        return "Unknown";

    Ztl_bstr_t sId = std::to_wstring(id).c_str();

    IWzPropertyPtr mob = root->item[sId].GetUnknown();
    if (!mob) {
        // cache the miss so an absent id doesn't re-fetch String/Mob.img each call
        g_mobNames[id] = "Unknown";
        return "Unknown";
    }

    std::string name = wz::GetStr(mob, L"name");

    g_mobNames[id] = name;
    return name;
}

// =====================================================
// MOB LEVEL
// =====================================================
int GetMobLevelById(int id) {

    auto it = g_mobLevels.find(id);
    if (it != g_mobLevels.end())
        return it->second;

    wchar_t path[64];
    swprintf(path, L"Mob/%07d.img", id);

    IWzPropertyPtr mob = wz::Get(path);
    if (!mob)
        return 0;

    IWzPropertyPtr info = wz::GetItem(mob, L"info");
    int level = wz::GetInt(info, L"level", 0);

    g_mobLevels[id] = level;
    return level;
}

MobCombatInfo GetMobCombatInfoById(int id) {
    auto it = g_mobCombat.find(id);
    if (it != g_mobCombat.end()) {
        return it->second;
    }

    MobCombatInfo info{};
    wchar_t path[64];
    swprintf(path, L"Mob/%07d.img", id);
    IWzPropertyPtr mob = wz::Get(path);
    IWzPropertyPtr node = mob ? wz::GetItem(mob, L"info") : nullptr;
    if (node) {
        info.level = wz::GetInt(node, L"level", 0);
        info.paDamage = wz::GetInt(node, L"PADamage", 0);
        info.pdDamage = wz::GetInt(node, L"PDDamage", 0);
        info.maDamage = wz::GetInt(node, L"MADamage", 0);
        info.mdDamage = wz::GetInt(node, L"MDDamage", 0);
        info.acc = wz::GetInt(node, L"acc", 0);
        info.eva = wz::GetInt(node, L"eva", 0);
        info.elemAttr = wz::GetStr(node, L"elemAttr", "");
        if (info.elemAttr == "Unknown") {
            info.elemAttr.clear();
        }
    }
    if (info.level <= 0) {
        info.level = GetMobLevelById(id);
    }
    g_mobCombat[id] = info;
    return info;
}

// =====================================================
// NPC NAME
// =====================================================
std::string GetNpcById(int id) {

    auto it = g_npcNames.find(id);
    if (it != g_npcNames.end())
        return it->second;

    IWzPropertyPtr root = wz::Get(L"String/Npc.img");
    if (!root)
        return "Unknown";

    Ztl_bstr_t sId = std::to_wstring(id).c_str();

    IWzPropertyPtr npc = root->item[sId].GetUnknown();
    if (!npc) {
        // cache the miss so an absent id doesn't re-fetch String/Npc.img each call
        g_npcNames[id] = "Unknown";
        return "Unknown";
    }

    std::string name = wz::GetStr(npc, L"name");

    g_npcNames[id] = name;
    return name;
}

// =====================================================
// MOB -> CARD ID
// =====================================================
int GetCardIdByMobId(int mobId) {

    auto it = g_mobToCard.find(mobId);
    if (it != g_mobToCard.end())
        return it->second;

    IWzPropertyPtr consume = wz::Get(L"Item/Consume.img");
    if (!consume)
        return 0;

    IWzPropertyPtr group = wz::GetItem(consume, L"0238");
    if (!group)
        return 0;

    // Children of Consume.img/0238 are keyed by the full (zero-padded) card
    // item id (e.g. "02380000"), NOT by a sequential 0..N index. Enumerate the
    // real child keys and return the matching card id, not a loop counter.
    IEnumVARIANTPtr pEnum = group->_NewEnum;
    if (!pEnum)
        return 0;

    while (true) {

        Ztl_variant_t vNext;
        ULONG uCeltFetched = 0;
        if (FAILED(pEnum->Next(1, &vNext, &uCeltFetched)) || uCeltFetched == 0)
            break;

        Ztl_bstr_t sKey = V_BSTR(&vNext);
        int cardId = wcstol(sKey.GetBSTR(), nullptr, 10);

        IWzPropertyPtr item = group->item[sKey].GetUnknown();
        if (!item)
            continue;

        IWzPropertyPtr info = wz::GetItem(item, L"info");
        if (!info)
            continue;

        int mob = wz::GetInt(info, L"mob", 0);

        if (mob == mobId) {
            g_mobToCard[mobId] = cardId;
            return cardId;
        }
    }

    return 0;
}

// =====================================================
// CARD ICON
// =====================================================
IWzCanvasPtr GetCardIconByCardId(int cardId) {

    auto it = g_cardIcons.find(cardId);
    if (it != g_cardIcons.end())
        return it->second;

    try {
        // 0238 children are keyed by the zero-padded 8-digit card item id
        // (e.g. "02380000"), so format with %08d to match the real WZ key.
        wchar_t path[128];
        swprintf(path, L"Item/Consume.img/0238/%08d/info/iconRaw", cardId);

        IWzCanvasPtr icon = get_rm()->GetObjectA(path).GetUnknown();

        if (icon) {
            g_cardIcons[cardId] = icon;
            return icon;
        }

    } catch (...) {
    }

    return nullptr;
}
