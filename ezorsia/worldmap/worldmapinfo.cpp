#define NOMINMAX
#include <windows.h>
#include <comdef.h>

#undef min
#undef max

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "hook.h"
#include "ztl/ztl.h"
#include "wvs/tooltip.h"
#include "wvs/util.h"
#include "wvs/msghandler.h"
#include "iconprovider.h"
#include "getwzinfo.h"
#include "players_api.h"

// =====================================================
// ADDRESSES
// =====================================================
namespace addr {
constexpr DWORD WM_OnMouseMove = 0x009EE2B3;
constexpr DWORD WM_OnDestroy   = 0x009EB94A;
constexpr DWORD GetFieldOpt    = 0x00437A0C;
} // namespace addr

// =====================================================
// TYPEDEFS
// =====================================================
using t_OnMouseMove = int(__thiscall*)(void*, int, int);
using t_OnDestroy   = void(__thiscall*)(void*);
using t_GetField    = void*(__cdecl*)();

// =====================================================
// ORIGINAL FUNCTIONS
// =====================================================
static auto OnMouseMove_Orig = reinterpret_cast<t_OnMouseMove>(addr::WM_OnMouseMove);
static auto OnDestroy_Orig   = reinterpret_cast<t_OnDestroy>(addr::WM_OnDestroy);
static t_GetField g_GetField = nullptr;

// =====================================================
// UI CONSTANTS
// =====================================================
namespace ui {

// Tooltip size limits
constexpr int MIN_WIDTH  = 200;
constexpr int MAX_WIDTH  = 460;
constexpr int MAX_HEIGHT = 600;

// Padding
constexpr int LEFT_PAD   = 14;
constexpr int RIGHT_PAD  = 14;
constexpr int TOP_PAD    = 8;
constexpr int BOTTOM_PAD = 8;

// Layout metrics
constexpr int HEADER_HEIGHT   = 56; // map icon + street/name block
constexpr int SECTION_LABEL_H = 18; // "Monsters" / "NPCs" label row
constexpr int LINE_HEIGHT     = 46; // each mob/NPC entry row
constexpr int PLAYER_LINE_H   = 18; // each player entry row (text-only, no portrait)
constexpr int CATEGORY_GAP    = 6;  // gap between mob and NPC sections

// Icon
constexpr int ICON_SIZE  = 42; // max rendered icon dimension
constexpr int ICON_COL_W = 52; // icon column width (icon + spacing)

// Cursor offset when placing tooltip
constexpr int CURSOR_OX  = 18;
constexpr int CURSOR_OY  = 18;

// Approximate pixels per character at 12pt Dotum
constexpr int CHAR_W_BODY = 7;

constexpr int MAX_ENTRIES = 10;

// Chrome — title bar + separator strip at the very top
constexpr int CHROME_H     = 28; // total height reserved for title bar area
constexpr int TITLE_BAR_H  = 20; // height of the amber fill strip
constexpr int TITLE_TEXT_Y =  7; // y of the centered title text

// Colors (ARGB)
constexpr unsigned long COL_BG        = 0x99141A24; // dark background fill
constexpr unsigned long COL_TITLEBAR  = 0x88423010; // amber title bar
constexpr unsigned long COL_SEPARATOR = 0xCCE0C070; // gold separator line

} // namespace ui

// =====================================================
// TOOLTIP CORE
// - g_ttBuf overlay (reference client); never touch dlg embedded tooltip (breaks ESC map UI).
// - Always call Orig first so clicks / close button keep working.
// =====================================================
namespace wm_tick {
void NotifyPlayersUpdated(int mapId);
}

alignas(8) static unsigned char g_ttBuf[1304];
static bool g_ttInit = false;

inline CUIToolTip* GetTooltip() {
    return reinterpret_cast<CUIToolTip*>(g_ttBuf);
}

void EnsureTooltip() {
    if (g_ttInit) {
        return;
    }
    __try {
        reinterpret_cast<void(__thiscall*)(void*)>(0x008E49B5)(g_ttBuf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_ttInit = true;
}

void ClearTooltip() {
    if (!g_ttInit) {
        return;
    }
    __try {
        reinterpret_cast<void(__thiscall*)(void*)>(0x008E6E23)(g_ttBuf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void ClearDlgToolTip(void* ecx) {
    if (!ecx) {
        return;
    }
    __try {
        reinterpret_cast<IUIMsgHandler*>(ecx)->ClearToolTip();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

namespace dbg {
constexpr bool kEnabled = false;

inline void Log(const char* fmt, ...) {
    if (!kEnabled) {
        return;
    }
    FILE* file = nullptr;
    if (fopen_s(&file, "C:\\worldmap_tip.log", "a") != 0 || !file) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
}
} // namespace dbg

namespace ttapi {
using SetStringFn = void(__fastcall*)(void* pThis, void* edx, int x, int y, const char* text);

constexpr DWORD kSetString = 0x008E6E7D;

inline HWND ClientHwnd() {
    __try {
        void* app = *reinterpret_cast<void**>(0x00BE7B38);
        if (app) {
            HWND hwnd = *reinterpret_cast<HWND*>(reinterpret_cast<char*>(app) + 4);
            if (hwnd && IsWindow(hwnd)) {
                return hwnd;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return GetForegroundWindow();
}

inline void SetString(CUIToolTip* tt, int x, int y, const char* text) {
    if (!text || !tt) {
        return;
    }
    __try {
        reinterpret_cast<SetStringFn>(kSetString)(tt, nullptr, x, y, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
} // namespace ttapi

// =====================================================
// WORLD MAP SPOT
// =====================================================
namespace wm {

constexpr int OFF_SPOTS  = 366;
constexpr int STRIDE     = 17;
constexpr int X          = 0;
constexpr int Y          = 1;
constexpr int FIELD_ARR  = 11;
constexpr int ORIGIN_X   = 13;
constexpr int ORIGIN_Y   = 35;
constexpr int HIT_RADIUS = 8;

inline int* getPtr(const void* base, int idx) {
    return *reinterpret_cast<int* const*>(
        reinterpret_cast<const char*>(base) + idx * 4);
}

struct Spot {
    int index = -1;
    int mapId = 0;
    int anchorX = 0;
    int anchorY = 0;
};

constexpr int MAX_SPOT_COUNT = 256;

inline bool IsValidMapId(int mapId) {
    return mapId >= 100000 && mapId <= 999999999;
}

bool HasSpotTable(void* ecx) {
    if (!ecx) {
        return false;
    }
    __try {
        static const int kBaseAdjust[] = { -4, 0 };
        for (int adjust : kBaseAdjust) {
            void* base = static_cast<char*>(ecx) + adjust;
            int* spots = getPtr(base, OFF_SPOTS);
            if (!spots) {
                continue;
            }
            const int count = *(spots - 1);
            if (count > 0 && count <= MAX_SPOT_COUNT) {
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

static Spot GetSpotAtSafe(void* base, int rx, int ry, int hitRadius) {
    Spot out{};
    __try {
        int* spots = getPtr(base, OFF_SPOTS);
        if (!spots) {
            return out;
        }

        int count = *(spots - 1);
        if (count <= 0) {
            return out;
        }
        if (count > MAX_SPOT_COUNT) {
            count = MAX_SPOT_COUNT;
        }

        int cx = rx - ORIGIN_X;
        int cy = ry - ORIGIN_Y;

        int best = -1;
        int bestDist = INT_MAX;
        const int hitR2 = hitRadius * hitRadius;

        for (int i = 0; i < count; i++) {
            int* s = spots + i * STRIDE;
            unsigned* arr = *(unsigned**)(s + FIELD_ARR);
            if (!arr) {
                continue;
            }
            int arrLen = *(arr - 1);
            if (arrLen <= 0) {
                continue;
            }

            int dx = s[X] - cx;
            int dy = s[Y] - cy;
            int dist = dx * dx + dy * dy;

            if (dist <= hitR2 && dist < bestDist) {
                best = i;
                bestDist = dist;
            }
        }

        if (best < 0) {
            return out;
        }

        int* s = spots + best * STRIDE;
        unsigned* arr = *(unsigned**)(s + FIELD_ARR);
        int mapId = (arr && *(arr - 1) > 0) ? static_cast<int>(arr[0]) : 0;
        if (!IsValidMapId(mapId)) {
            return out;
        }

        out.index = best;
        out.mapId = mapId;
        out.anchorX = s[X] + ORIGIN_X;
        out.anchorY = s[Y] + ORIGIN_Y;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
    }
    return out;
}

Spot GetSpot(void* base, int rx, int ry) {
    return GetSpotAtSafe(base, rx, ry, HIT_RADIUS);
}

static Spot GetClosestSpotAt(void* base, int rx, int ry, int maxDistPx) {
    Spot out{};
    __try {
        int* spots = getPtr(base, OFF_SPOTS);
        if (!spots) {
            return out;
        }

        int count = *(spots - 1);
        if (count <= 0) {
            return out;
        }
        if (count > MAX_SPOT_COUNT) {
            count = MAX_SPOT_COUNT;
        }

        const int cx = rx - ORIGIN_X;
        const int cy = ry - ORIGIN_Y;
        const int maxR2 = maxDistPx * maxDistPx;

        int best = -1;
        int bestDist = maxR2 + 1;

        for (int i = 0; i < count; ++i) {
            int* s = spots + i * STRIDE;
            unsigned* arr = *(unsigned**)(s + FIELD_ARR);
            if (!arr || *(arr - 1) <= 0) {
                continue;
            }

            const int dx = s[X] - cx;
            const int dy = s[Y] - cy;
            const int dist = dx * dx + dy * dy;
            if (dist <= maxR2 && dist < bestDist) {
                best = i;
                bestDist = dist;
            }
        }

        if (best < 0) {
            return out;
        }

        int* s = spots + best * STRIDE;
        unsigned* arr = *(unsigned**)(s + FIELD_ARR);
        const int mapId = (arr && *(arr - 1) > 0) ? static_cast<int>(arr[0]) : 0;
        if (!IsValidMapId(mapId)) {
            return out;
        }

        out.index = best;
        out.mapId = mapId;
        out.anchorX = s[X] + ORIGIN_X;
        out.anchorY = s[Y] + ORIGIN_Y;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
    }
    return out;
}

Spot GetSpotFromDialog(void* ecx, int rx, int ry) {
    static const int kBaseAdjust[] = { -4, 0 };
    static const int kRadii[] = { HIT_RADIUS, 12, 18, 28 };

    for (int adjust : kBaseAdjust) {
        void* base = static_cast<char*>(ecx) + adjust;
        for (int radius : kRadii) {
            Spot spot = GetSpotAtSafe(base, rx, ry, radius);
            if (spot.index >= 0 && IsValidMapId(spot.mapId)) {
                return spot;
            }
        }
        Spot closest = GetClosestSpotAt(base, rx, ry, 48);
        if (closest.index >= 0) {
            return closest;
        }
    }
    return {};
}

} // namespace wm

// =====================================================
// QUEST MARKER STATE
// =====================================================
// Replicates the per-NPC classification in CNpc::SetQuestList @ 0x006D1779 so a
// quest bulb can be shown next to NPCs that have an actionable quest. Verified
// against v83.exe.html (image base 0x400000):
//   - quest ids are enumerated for the NPC via CQuestMan::GetQuestByNpc.
//   - in-progress (key present in CharacterData's ZMap @ +0x5FF):
//       CheckCompleteDemand() == 0 (all requirements met) -> completable;  nonzero -> still in progress.
//   - not-in-progress: CheckStartDemand() != 0 -> available.
//   POLARITY (verified — the two checks differ!): CheckStartDemand returns nonzero when startable
//   (the SetQuestList loop skips a quest on jz/eax==0 in the start branch). CheckCompleteDemand
//   returns ~the unmet-requirement count, so 0 == completable (confirmed: != 0 mis-marked
//   in-progress quests as completable). CheckStartDemand already accounts for
//   completion/repeatability, so no separate completed-map lookup is needed.
//   The [0x4B0,0x578) id band is skipped, as the client does.
namespace quest {

constexpr uintptr_t CWvsContext_Inst   = 0x00BE7918; // *(void**) -> CWvsContext*
constexpr uintptr_t CQuestMan_Inst     = 0x00BED614; // *(void**) -> CQuestMan* (null pre-login)
constexpr uintptr_t Off_CharData       = 0x20B8;     // CWvsContext -> CharacterData* (as androidequip.cpp)
constexpr uintptr_t Off_SecondaryStat  = 0x2134;     // &CWvsContext::SecondaryStat (embedded)
constexpr uintptr_t Off_TamingMobLevel = 0x37C0;     // CWvsContext int
constexpr uintptr_t Off_InProgressMap  = 0x5FF;      // CharacterData -> ZMap<u16,ZXString>
constexpr uintptr_t Addr_GetCurFieldID = 0x00A1238B; // long  __thiscall(CWvsContext*)
constexpr uintptr_t Addr_GetQuestByNpc = 0x0071DDEC; // int   __thiscall(qm, u32 npcId, ZArray<u16>&)
constexpr uintptr_t Addr_GetPos        = 0x00500EA5; // void* __thiscall(map, u16* key)
constexpr uintptr_t Addr_CheckStart    = 0x00721163; // int   __thiscall(qm,u16,u32,cd,ss,int,int)
constexpr uintptr_t Addr_CheckComplete = 0x00721D2C; // long  __thiscall(qm,u16,u32,cd,ss)
constexpr int       QBand_Lo           = 0x4B0;
constexpr int       QBand_Hi           = 0x578;

using t_GetCurFieldID = long (__thiscall*)(void*);
using t_GetQuestByNpc = int  (__thiscall*)(void*, unsigned int, void*);
using t_GetPos        = void*(__thiscall*)(void*, unsigned short*);
using t_CheckStart    = int  (__thiscall*)(void*, unsigned short, unsigned int, void*, void*, int, int);
using t_CheckComplete = long (__thiscall*)(void*, unsigned short, unsigned int, void*, void*);

// Inner worker: holds the RAII ZArray (object unwinding) and the raw client calls.
// Kept SEH-free so the __try wrapper below doesn't trip MSVC C2712.
static int GetNpcQuestStateInner(int npcId) {
    void* qm  = *reinterpret_cast<void**>(CQuestMan_Inst);
    void* ctx = *reinterpret_cast<void**>(CWvsContext_Inst);
    if (!qm || !ctx) return 0;

    void* cd = *reinterpret_cast<void**>(reinterpret_cast<char*>(ctx) + Off_CharData);
    if (!cd) return 0;

    void* ss        = reinterpret_cast<char*>(ctx) + Off_SecondaryStat;
    int   tamingLvl = *reinterpret_cast<int*>(reinterpret_cast<char*>(ctx) + Off_TamingMobLevel);
    int   fieldId   = static_cast<int>(reinterpret_cast<t_GetCurFieldID>(Addr_GetCurFieldID)(ctx));

    ZArray<unsigned short> arr;   // GetQuestByNpc RemoveAll's then GetAt-copies into it; ~ZArray frees
    reinterpret_cast<t_GetQuestByNpc>(Addr_GetQuestByNpc)(qm, static_cast<unsigned int>(npcId), &arr);

    size_t count = arr.GetCount();
    if (count == 0 || count > 4096) return 0;

    void* ipMap = reinterpret_cast<char*>(cd) + Off_InProgressMap;
    bool avail = false, prog = false;   // marker priority: completable > available > in-progress

    for (size_t i = 0; i < count; ++i) {
        unsigned short q = arr[i];
        if (q >= QBand_Lo && q < QBand_Hi) continue;            // info/medal band (client skips it)

        bool inProgress = reinterpret_cast<t_GetPos>(Addr_GetPos)(ipMap, &q) != nullptr;
        if (inProgress) {
            // CheckCompleteDemand returns 0 when every completion requirement is met
            // (~unmet-requirement count). 0 = completable; nonzero = still in progress.
            if (reinterpret_cast<t_CheckComplete>(Addr_CheckComplete)(
                    qm, q, static_cast<unsigned int>(npcId), cd, ss) == 0) {
                return 2;                                       // completable (brown book) — top priority
            }
            prog = true;                                        // accepted, not yet completable (open book)
        } else {
            if (reinterpret_cast<t_CheckStart>(Addr_CheckStart)(
                    qm, q, static_cast<unsigned int>(npcId), cd, ss, tamingLvl, fieldId) != 0) {
                avail = true;                                   // available (white bulb)
            }
        }
    }
    return avail ? 1 : (prog ? 3 : 0);
}

// 0 = none, 1 = available (startable), 2 = completable (turn-in ready), 3 = in-progress.
int GetNpcQuestState(int npcId) {
    __try {
        return GetNpcQuestStateInner(npcId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;   // any bad deref -> no marker, never crash
    }
}

} // namespace quest

// =====================================================
// DATA
// =====================================================
struct MobGroup {
    int id, level, count;
    std::string name;
};

struct NpcEntry {
    int id;
    std::string name;
    int state;   // quest marker: 0 none, 1 available, 2 completable, 3 in-progress
};

struct MapLife {
    std::vector<MobGroup>    mobs;
    std::vector<NpcEntry>    npcs;
    std::vector<PlayerEntry> players;
    int playersState = 0;   // 0 not requested, 1 loading (request in flight), 2 ready
};

// =====================================================
// PLAYERS-IN-MAP  (cross-channel, server-fed)
// =====================================================
// The client cannot know who is in a map on which channel — that is server state
// (NPCs/mobs come from the local WZ; players do not). On hovering a NEW map spot we
// fire a tiny request (CP 0x115, mapId); the server replies (LP 0x178) with every
// player in that map across ALL channels of the world: name, level, channel. The
// reply is cached per-map with a short TTL so hovering doesn't spam the server, and
// the tooltip redraws with the data on the next mouse-move once it lands.
namespace players {

constexpr DWORD TTL_MS     = 6000; // cached list considered fresh for 6s
constexpr DWORD MIN_REQ_MS = 2500; // never re-request the same map faster than this
constexpr int   MAX_PLAYERS = 40;  // cap rows (matches the server cap)

struct Entry {
    std::vector<PlayerEntry> list;
    DWORD stamp   = 0;     // tick of last response
    DWORD reqTick = 0;     // tick of last request sent
    bool  ready   = false; // a response has landed at least once
};

static std::mutex                     g_mtx;
static std::unordered_map<int, Entry> g_cache;

constexpr size_t MAX_CACHE = 64; // hard cap on retained per-map player lists

// Evict stale entries (older than TTL_MS) when the cache outgrows MAX_CACHE, so a
// long session hovering many map spots can't retain a permanent entry per map.
// Caller must already hold g_mtx.
static void EvictStaleLocked(DWORD now) {
    if (g_cache.size() <= MAX_CACHE) return;
    for (auto it = g_cache.begin(); it != g_cache.end(); ) {
        if (!it->second.ready || (now - it->second.stamp) > TTL_MS)
            it = g_cache.erase(it);
        else
            ++it;
    }
}

static void SendRequest(int mapId) {
    players::SendWorldMapPlayersRequest(mapId);
}

// Fire a request when we have no fresh data and haven't asked too recently. Always
// touches g_cache[mapId] so the render side sees at least the "loading" state.
void MaybeRequest(int mapId) {
    if (mapId <= 0) return;
    DWORD now = GetTickCount();
    bool send = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        EvictStaleLocked(now);                 // reclaim before (maybe) inserting this map
        Entry& e = g_cache[mapId];
        bool stale  = !e.ready || (now - e.stamp) > TTL_MS;
        bool cooled = (e.reqTick == 0) || (now - e.reqTick) >= MIN_REQ_MS;
        if (stale && cooled) { e.reqTick = now; send = true; }
    }
    if (send) {
        SendRequest(mapId);
    }
}

// Copy out the cached players for mapId. state: 0 none, 1 loading, 2 ready.
void GetCached(int mapId, std::vector<PlayerEntry>& out, int& state) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_cache.find(mapId);
    if (it == g_cache.end()) { state = 0; return; }
    out   = it->second.list;
    state = it->second.ready ? 2 : 1;
}

void ApplyResponse(int mapId, std::vector<PlayerEntry> list) {
    const DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lk(g_mtx);
    Entry& e = g_cache[mapId];
    e.list = std::move(list);
    e.stamp = now;
    e.ready = true;
    wm_tick::NotifyPlayersUpdated(mapId);
}

} // namespace players

// Resolve the WZ-derived (mobs + NPCs + quest markers) portion of a map. This is the
// expensive part: a WZ property-tree walk plus per-NPC native quest calls. It is map-
// static, so GetMapLife memoizes it by mapId and only re-runs this on a map change.
static IWzPropertyPtr OpenMapLifeProperty(int mapId) {
    return OpenMapPropertyById(mapId);
}

static int GetLifeEntryId(IWzPropertyPtr entry) {
    if (!entry) {
        return 0;
    }
    try {
        Ztl_variant_t v = entry->item[L"id"];
        if (v.vt == VT_BSTR && v.bstrVal) {
            return static_cast<int>(wcstol(v.bstrVal, nullptr, 10));
        }
        return get_int32(v, 0);
    } catch (...) {
        return 0;
    }
}

static void ComputeMapWzLife(int mapId, MapLife& out) {
    std::unordered_map<int, MobGroup> mobMap;
    std::unordered_set<int> npcSeen;

    try {
        IWzPropertyPtr map = OpenMapLifeProperty(mapId);
        if (!map) {
            return;
        }

        IWzPropertyPtr life = map->item[L"life"].GetUnknown();
        if (!life) return;

        for (int i = 0, n = (int)life->Getcount(); i < n; i++) {
            wchar_t idx[16];
            _itow_s(i, idx, 10);

            IWzPropertyPtr entry = get_unknown(life->item[idx]);
            if (!entry) continue;

            int id             = GetLifeEntryId(entry);
            std::wstring type  = (const wchar_t*)_bstr_t(entry->item[L"type"]);

            if (id <= 0) {
                continue;
            }

            if (type == L"m") {
                auto& m = mobMap[id];
                if (m.count == 0)
                    m = { id, GetMobLevelById(id), 0, GetMobNameById(id) };
                m.count++;
            } else if (type == L"n" && npcSeen.insert(id).second) {
                out.npcs.push_back({ id, GetNpcById(id), quest::GetNpcQuestState(id) });
            }
        }
    } catch (...) {}

    for (auto& p : mobMap)
        out.mobs.push_back(p.second);

    std::sort(out.mobs.begin(), out.mobs.end(),
        [](const MobGroup& a, const MobGroup& b) { return a.level < b.level; });

    if (out.mobs.size() > ui::MAX_ENTRIES) out.mobs.resize(ui::MAX_ENTRIES);
    if (out.npcs.size() > ui::MAX_ENTRIES) out.npcs.resize(ui::MAX_ENTRIES);
}

MapLife GetMapLife(int mapId) {
    // Memoize the WZ/quest-derived mobs+npcs by mapId: OnMouseMove fires per pixel, so
    // while the cursor sits on (or drifts within the hit radius of) one spot we must NOT
    // re-walk the WZ life node and re-run the native quest sweep every event. Recompute
    // that only when the hovered mapId actually changes; players are always refreshed
    // below since they arrive asynchronously via the server cache (LP 0x178).
    static int     g_lastMapId = -1;
    static MapLife g_lastLife;

    if (mapId != g_lastMapId) {
        MapLife fresh;
        ComputeMapWzLife(mapId, fresh);
        g_lastLife  = std::move(fresh);
        g_lastMapId = mapId;
    }

    MapLife out;
    out.mobs = g_lastLife.mobs;
    out.npcs = g_lastLife.npcs;

    // Players come from the server cache (filled by the LP 0x178 hook), not the WZ.
    players::GetCached(mapId, out.players, out.playersState);

    return out;
}

// =====================================================
// FONT SYSTEM
// =====================================================
static IWzFontPtr g_fontTitle;    // gold  — "Map Info" centered title
static IWzFontPtr g_fontMapName;  // green — street / map name
static IWzFontPtr g_fontMonsters; // red   — Monsters section label
static IWzFontPtr g_fontNpcs;     // blue  — NPCs section label (also reused for Quests label)
static IWzFontPtr g_fontPlayers;  // purple — Players section label
static IWzFontPtr g_fontBody;     // white — entry text
static IWzFontPtr g_fontShadow;   // black — 1px text shadow

bool CreateFont(IWzFontPtr& out, unsigned long color, int size) {
    if (out) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
        if (!out) {
            return false;
        }
        auto fn = reinterpret_cast<HRESULT(__thiscall*)(
            IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(0x0046341A);
        return SUCCEEDED(fn(out, L"Dotum", size, color, Ztl_variant_t(L"B")));
    } catch (...) {
        out = nullptr;
        return false;
    }
}

void EnsureFonts() {
    CreateFont(g_fontTitle,    0xFFFFD45A, 12); // gold
    CreateFont(g_fontMapName,  0xFF6EDB7E, 13); // green
    CreateFont(g_fontMonsters, 0xFFFF6A6A, 12); // red-orange
    CreateFont(g_fontNpcs,     0xFF6FAFFF, 12); // blue (also used for Quests label)
    CreateFont(g_fontPlayers,  0xFFC9A0FF, 12); // light purple
    CreateFont(g_fontBody,     0xFFFFFFFF, 12); // white
    CreateFont(g_fontShadow,   0xFF000000, 12); // black shadow
}

// =====================================================
// SIZING
// =====================================================
int ComputeWidth(const MapLife& data, const std::string& street, const std::string& map) {
    // Map name text starts after the map mark icon
    const int mapTextX = ui::LEFT_PAD + ui::ICON_SIZE + 8;
    int w = mapTextX + (int)(std::max)(street.size(), map.size()) * ui::CHAR_W_BODY + ui::RIGHT_PAD;

    // Mob / NPC entry rows
    for (auto& m : data.mobs) {
        std::string txt = "[Lv. " + std::to_string(m.level) + "] " + m.name
                        + " (x" + std::to_string(m.count) + ")";
        int ew = ui::LEFT_PAD + ui::ICON_COL_W + (int)txt.size() * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }
    for (auto& n : data.npcs) {
        int ew = ui::LEFT_PAD + ui::ICON_COL_W + (int)n.name.size() * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }
    // Player rows are text-only (no icon column): "[Lv.200] LongName  (Ch.20)"
    for (auto& p : data.players) {
        int chars = (int)p.name.size() + 18; // "[Lv.NNN] " + "  (Ch.NN)"
        int ew = ui::LEFT_PAD + chars * ui::CHAR_W_BODY + ui::RIGHT_PAD;
        w = (std::max)(w, ew);
    }

    return (std::max)(ui::MIN_WIDTH, (std::min)(w, ui::MAX_WIDTH));
}

int ComputeHeight(const MapLife& data) {
    bool hasM = !data.mobs.empty();
    bool hasN = !data.npcs.empty();
    bool hasP = data.playersState != 0;   // a Players section is shown once a map is hovered

    int h = ui::CHROME_H + ui::TOP_PAD + ui::HEADER_HEIGHT;

    if (hasM) h += ui::SECTION_LABEL_H + (int)data.mobs.size() * ui::LINE_HEIGHT;
    if (hasM && hasN) h += ui::CATEGORY_GAP;
    if (hasN) h += ui::SECTION_LABEL_H + (int)data.npcs.size() * ui::LINE_HEIGHT;

    if (hasP) {
        if (hasM || hasN) h += ui::CATEGORY_GAP;
        // ready shows one row per player (none when empty); loading shows just the label
        int pRows = (data.playersState == 2) ? (int)data.players.size() : 0;
        h += ui::SECTION_LABEL_H + pRows * ui::PLAYER_LINE_H;
    }

    h += ui::BOTTOM_PAD;
    return std::min(h, ui::MAX_HEIGHT);
}

// =====================================================
// RENDER HELPERS
// =====================================================
int CenterText(const char* txt, int width) {
    return (width - (int)strlen(txt) * 7) / 2;
}

void DrawIconScaled(IWzCanvasPtr canvas, IWzCanvasPtr icon, int x, int y) {
    if (!icon) return;
    const int w = static_cast<int>(icon->Getwidth());
    const int h = static_cast<int>(icon->Getheight());
    if (w <= 0 || h <= 0) return; // skip empty/unloaded canvas — CopyEx would E_INVALIDARG
    int nw = (w >= h) ? ui::ICON_SIZE : (w * ui::ICON_SIZE) / h;
    int nh = (h >= w) ? ui::ICON_SIZE : (h * ui::ICON_SIZE) / w;
    canvas->CopyEx(x, y, icon, CA_OVERWRITE, nw, nh, 0, 0, w, h, vtEmpty);
}

void DrawShadowText(IWzCanvasPtr canvas, int x, int y, const char* txt, IWzFontPtr font) {
    if (!canvas || !txt || !font || !g_fontShadow) {
        return;
    }
    canvas->DrawTextA(x + 1, y + 1, txt, g_fontShadow);
    canvas->DrawTextA(x, y, txt, font);
}

// Blit a marker scaled to fit `maxSize` (the QuestIcon bubbles are 44x44).
void DrawMarker(IWzCanvasPtr canvas, IWzCanvasPtr mark, int x, int y, int maxSize) {
    if (!mark) return;
    const int w = static_cast<int>(mark->Getwidth());
    const int h = static_cast<int>(mark->Getheight());
    if (w <= 0 || h <= 0) return; // empty/unloaded canvas — CopyEx would E_INVALIDARG
    const int nw = (w >= h) ? maxSize : (w * maxSize) / h;
    const int nh = (h >= w) ? maxSize : (h * maxSize) / w;
    canvas->CopyEx(x, y, mark, CA_OVERWRITE, nw, nh, 0, 0, w, h, vtEmpty);
}

// =====================================================
// TOOLTIP RENDER — Canvas panel via SetToolTip_String2 sizing + manual draw.
// Returns: 0 = use Orig native name, 1 = minimal g_ttBuf text, 2 = full canvas panel.
static int ShowTooltip(const std::string& street, const std::string& map, int mapId, int winX, int winY) {
    EnsureTooltip();
    EnsureFonts();

    CUIToolTip* tt = GetTooltip();
    tt->ClearToolTip();

    const MapLife data = GetMapLife(mapId);
    const int width = (std::min)((std::max)(ComputeWidth(data, street, map), ui::MIN_WIDTH), ui::MAX_WIDTH);
    const int height = ComputeHeight(data);

    HWND hWnd = ttapi::ClientHwnd();
    if (!hWnd) {
        hWnd = GetForegroundWindow();
    }
    RECT cr = {};
    if (hWnd) {
        GetClientRect(hWnd, &cr);
    }

    int ttX = winX + ui::CURSOR_OX;
    int ttY = winY + ui::CURSOR_OY;

    if (cr.right > 0 && ttX + width > cr.right) {
        ttX = winX - width - 4;
    }
    if (cr.bottom > 0 && ttY + height > cr.bottom) {
        ttY = winY - height - 4;
    }

    ttX = (std::max)(ttX, 0);
    ttY = (std::max)(ttY, 0);

    // Match reference client: ClearToolTip before each String2 sizing probe.
    // SetString bootstrap leaves a tiny layer at (ttX,ttY); String2 then skips
    // rebuild when layer+position unchanged (sub_8E7150 early-out) → h stays 18.
    ZXString<char> zTitle("");
    auto setLines = [&](int n) -> int {
        std::string s((std::max)(0, n), '\n');
        ZXString<char> zDesc(s.c_str());
        tt->ClearToolTip();
        tt->SetToolTip_String2(ttX, ttY, zTitle, zDesc, 0, 0, 0, width, 1, 0);
        return tt->m_nHeight;
    };

    int h2 = setLines(2);
    int h12 = setLines(12);
    int perLine = (h12 > h2) ? (std::max)(1, (h12 - h2) / 10) : 14;
    int base = h2 - 2 * perLine;
    int need = (std::max)(1, (height - base + perLine - 1) / perLine);
    setLines(need);
    for (int i = 0; i < 4 && tt->m_nHeight < height; ++i) {
        setLines(++need);
    }

    if (!tt->m_pLayer || tt->m_nHeight < 32) {
        const std::string oneLine = street + " - " + map;
        tt->ClearToolTip();
        ttapi::SetString(tt, ttX, ttY, oneLine.c_str());
        dbg::Log("tip mapId=%d fallback SetString layer=%d h=%d need=%d h2=%d h12=%d",
            mapId, tt->m_pLayer ? 1 : 0, tt->m_nHeight, need, h2, h12);
        return tt->m_pLayer ? 1 : 0;
    }

    IWzCanvasPtr canvas = tt->m_pLayer->canvas[0];
    if (!canvas) {
        const std::string oneLine = street + " - " + map;
        ttapi::SetString(tt, ttX, ttY, oneLine.c_str());
        return 1;
    }

    int mode = 2;
    try {
        canvas->DrawRectangle(2, 2, width - 4, tt->m_nHeight - 4, ui::COL_BG);
        canvas->DrawRectangle(4, 4, width - 8, ui::TITLE_BAR_H, ui::COL_TITLEBAR);
        canvas->DrawRectangle(8, 4 + ui::TITLE_BAR_H + 2, width - 16, 1, ui::COL_SEPARATOR);

        const char* titleStr = "Map Info";
        int titleW = static_cast<int>(strlen(titleStr)) * 7;
        DrawShadowText(canvas, (width - titleW) / 2, ui::TITLE_TEXT_Y, titleStr, g_fontTitle);

        int hdrY = ui::CHROME_H;
        int hdrX = ui::LEFT_PAD;

        if (auto mapIcon = GetMapIcon(mapId)) {
            DrawIconScaled(canvas, mapIcon, hdrX, hdrY + ui::TOP_PAD);
            hdrX += ui::ICON_SIZE + 8;
        }

        DrawShadowText(canvas, hdrX, hdrY + ui::TOP_PAD + 4, street.c_str(), g_fontMapName);
        DrawShadowText(canvas, hdrX, hdrY + ui::TOP_PAD + 4 + 16, map.c_str(), g_fontMapName);

        int yPos = ui::CHROME_H + ui::TOP_PAD + ui::HEADER_HEIGHT;

        auto drawSection = [&](const char* label, IWzFontPtr labelFont, auto& list, auto iconFn, auto textFn, auto markFn) {
            std::string hdr = std::string(label) + " (" + std::to_string(list.size()) + "):";
            DrawShadowText(canvas, CenterText(hdr.c_str(), tt->m_nWidth), yPos, hdr.c_str(), labelFont);
            yPos += ui::SECTION_LABEL_H;

            for (auto& e : list) {
                DrawIconScaled(canvas, iconFn(e.id), ui::LEFT_PAD, yPos + 2);
                int qst = markFn(e);
                if (qst) {
                    const int ms = 22;
                    int mx = ui::LEFT_PAD + ui::ICON_SIZE - ms + 2;
                    int my = yPos;
                    if (IWzCanvasPtr mk = GetQuestMarkerIcon(qst)) {
                        DrawMarker(canvas, mk, mx, my, ms);
                    }
                }
                std::string txt = textFn(e);
                DrawShadowText(canvas, ui::LEFT_PAD + ui::ICON_COL_W, yPos + 16, txt.c_str(), g_fontBody);
                yPos += ui::LINE_HEIGHT;
            }
        };

        if (!data.mobs.empty()) {
            drawSection("Monsters", g_fontMonsters, data.mobs, GetMobIcon,
                [](const MobGroup& m) -> std::string {
                    return "[Lv. " + std::to_string(m.level) + "] " + m.name
                        + " (x" + std::to_string(m.count) + ")";
                },
                [](const MobGroup&) { return 0; });
        }

        if (!data.mobs.empty() && !data.npcs.empty()) {
            yPos += ui::CATEGORY_GAP;
        }

        if (!data.npcs.empty()) {
            drawSection("NPCs", g_fontNpcs, data.npcs, GetNpcIcon,
                [](const NpcEntry& n) -> std::string { return n.name; },
                [](const NpcEntry& n) { return n.state; });
        }

        if (data.playersState != 0) {
            if (!data.mobs.empty() || !data.npcs.empty()) {
                yPos += ui::CATEGORY_GAP;
            }

            std::string hdr = (data.playersState == 2)
                ? "Players (" + std::to_string(data.players.size()) + "):"
                : std::string("Players (loading...):");
            DrawShadowText(canvas, CenterText(hdr.c_str(), tt->m_nWidth), yPos, hdr.c_str(), g_fontPlayers);
            yPos += ui::SECTION_LABEL_H;

            for (auto& pl : data.players) {
                std::string line = "[Lv." + std::to_string(pl.level) + "] " + pl.name
                    + "  (Ch." + std::to_string(pl.channel) + ")";
                DrawShadowText(canvas, ui::LEFT_PAD, yPos, line.c_str(), g_fontBody);
                yPos += ui::PLAYER_LINE_H;
            }
        }
    } catch (...) {
        mode = 1;
        const std::string oneLine = street + " - " + map;
        ttapi::SetString(tt, ttX, ttY, oneLine.c_str());
    }

    static int s_lastLogMap = -1;
    static int s_lastMode = -99;
    if (mapId != s_lastLogMap || mode != s_lastMode) {
        s_lastLogMap = mapId;
        s_lastMode = mode;
        dbg::Log("tip mapId=%d npcs=%zu mobs=%zu mode=%d layer=%d h=%d w=%d need=%d",
            mapId, data.npcs.size(), data.mobs.size(), mode, tt->m_pLayer ? 1 : 0, tt->m_nHeight, tt->m_nWidth, need);
    }

    return mode;
}

namespace wm_tick {

static bool s_active = false;
static int s_mapId = -1;
static int s_x = 0;
static int s_y = 0;
static std::string s_street;
static std::string s_map;
static bool s_playersDirty = false;

void Refresh();

void Stop() {
    s_active = false;
    s_mapId = -1;
    s_playersDirty = false;
    ClearTooltip();
}

void Activate(int mapId, int winX, int winY, std::string street, std::string map) {
    s_mapId = mapId;
    s_x = winX;
    s_y = winY;
    s_street = std::move(street);
    s_map = std::move(map);
    s_active = true;
}

void Refresh() {
    if (!s_active || s_mapId <= 0) {
        return;
    }
    ShowTooltip(s_street, s_map, s_mapId, s_x, s_y);
    s_playersDirty = false;
}

void NotifyPlayersUpdated(int mapId) {
    if (s_active && s_mapId == mapId) {
        s_playersDirty = true;
    }
}

void TickRefresh() {
    if (s_active && s_playersDirty) {
        Refresh();
    }
}

} // namespace wm_tick

// =====================================================
// HOOK
// =====================================================
void* GetFieldCtx() {
    if (!g_GetField)
        g_GetField = reinterpret_cast<t_GetField>(addr::GetFieldOpt);
    return g_GetField ? g_GetField() : nullptr;
}

static int CallOnMouseMoveOrig(void* ecx, int x, int y) {
    __try {
        return OnMouseMove_Orig(ecx, x, y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int ShowSpotTooltip(const wm::Spot& spot) {
    std::string full = GetMapById(spot.mapId);
    size_t split = full.find(" - ");
    std::string street = full.substr(0, split);
    std::string mapName = (split != std::string::npos) ? full.substr(split + 3) : full;

    POINT pt = {};
    GetCursorPos(&pt);
    HWND hwnd = ttapi::ClientHwnd();
    if (hwnd) {
        ScreenToClient(hwnd, &pt);
    }

    players::MaybeRequest(spot.mapId);
    wm_tick::Activate(spot.mapId, pt.x, pt.y, street, mapName);
    return ShowTooltip(street, mapName, spot.mapId, pt.x, pt.y);
}

int __fastcall OnMouseMove_Hook(void* ecx, void*, int x, int y) {
    const int ret = CallOnMouseMoveOrig(ecx, x, y);

    if (!ecx || !GetFieldCtx() || !wm::HasSpotTable(ecx)) {
        wm_tick::Stop();
        return ret;
    }

    wm::Spot spot = wm::GetSpotFromDialog(ecx, x, y);
    if (spot.index >= 0 && wm::IsValidMapId(spot.mapId)) {
        static int s_lastLogMap = -1;
        if (spot.mapId != s_lastLogMap) {
            s_lastLogMap = spot.mapId;
            dbg::Log("spot hit idx=%d mapId=%d x=%d y=%d", spot.index, spot.mapId, x, y);
        }
        const int mode = ShowSpotTooltip(spot);
        if (mode >= 1) {
            ClearDlgToolTip(ecx);
        }
        return ret;
    }

    wm_tick::Stop();
    return ret;
}

void __fastcall OnDestroy_Hook(void* ecx, void*) {
    wm_tick::Stop();
    OnDestroy_Orig(ecx);
}

namespace WorldMapInfo {
void TickTooltip() {
    wm_tick::TickRefresh();
}
} // namespace WorldMapInfo

void AttachMapInfoToolTip() {
    ATTACH_HOOK(OnMouseMove_Orig, OnMouseMove_Hook);
    ATTACH_HOOK(OnDestroy_Orig, OnDestroy_Hook);
}
