#include <windows.h>
#include <comdef.h>
#include "ztl/ztl.h"

#include <unordered_map>
#include <string>

#include "wvs/util.h"
#include "getwzinfo.h"
#include "iconprovider.h"

// =====================================================
// CACHE
// =====================================================

static std::unordered_map<int, IWzCanvasPtr> g_mobIcons;
static std::unordered_map<int, IWzCanvasPtr> g_npcIcons;
static std::unordered_map<int, IWzCanvasPtr> g_mapIcons;

// =====================================================
// CORE LOADER
// =====================================================

static IWzCanvasPtr LoadCanvas(const wchar_t* path) {

    try {
        Ztl_variant_t v = get_rm()->GetObjectA(path);

        IUnknown* unk = v.GetUnknown(false, false);
        if (!unk)
            return nullptr;

        IWzCanvas* raw = nullptr;

        if (SUCCEEDED(unk->QueryInterface(__uuidof(IWzCanvas), (void**)&raw))) {
            IWzCanvasPtr out = raw;
            raw->Release();
            return out;
        }

    } catch (...) {
    }

    return nullptr;
}

// =====================================================
// PATH HELPERS
// =====================================================

static IWzCanvasPtr TryLoadFormatted(const wchar_t* fmt, int id) {
    wchar_t path[128];
    swprintf(path, fmt, id);
    return LoadCanvas(path);
}

// =====================================================
// MOB ICON
// =====================================================

IWzCanvasPtr GetMobIcon(int id) {

    auto it = g_mobIcons.find(id);
    if (it != g_mobIcons.end())
        return it->second;

    IWzCanvasPtr canvas;

    canvas = TryLoadFormatted(L"Mob/%07d.img/stand/0", id);
    if (!canvas)
        canvas = TryLoadFormatted(L"Mob/%08d.img/stand/0", id);
    if (!canvas)
        canvas = TryLoadFormatted(L"Mob/%07d.img/fly/0", id);
    if (!canvas)
        canvas = TryLoadFormatted(L"Mob/%08d.img/fly/0", id);

    g_mobIcons[id] = canvas;
    return canvas;
}

// =====================================================
// NPC ICON
// =====================================================

IWzCanvasPtr GetNpcIcon(int id) {

    auto it = g_npcIcons.find(id);
    if (it != g_npcIcons.end())
        return it->second;

    IWzCanvasPtr canvas;

    canvas = TryLoadFormatted(L"Npc/%07d.img/stand/0", id);
    if (!canvas)
        canvas = TryLoadFormatted(L"Npc/%08d.img/stand/0", id);

    g_npcIcons[id] = canvas;
    return canvas;
}

// =====================================================
// MAP ICON
// =====================================================

IWzCanvasPtr GetMapIcon(int mapId) {

    auto it = g_mapIcons.find(mapId);
    if (it != g_mapIcons.end())
        return it->second;

    IWzCanvasPtr canvas;

    try {
        IWzPropertyPtr map = OpenMapPropertyById(mapId);
        if (map) {

            IWzPropertyPtr info = map->item[L"info"].GetUnknown();
            if (info) {

                Ztl_variant_t vMark = info->item[L"mapMark"];

                if (vMark.vt == VT_BSTR) {

                    std::wstring mark = (const wchar_t*)_bstr_t(vMark);

                    wchar_t markPath[256];
                    swprintf(markPath, L"Map/MapHelper.img/mark/%s", mark.c_str());

                    canvas = LoadCanvas(markPath);
                }
            }
        }

        if (!canvas) {
            canvas = LoadCanvas(L"Map/MapHelper.img/mark/0");
        }

    } catch (...) {
    }

    g_mapIcons[mapId] = canvas;
    return canvas;
}

// =====================================================
// QUEST MARKER ICON
// =====================================================
// The authentic above-NPC quest speech-bubble markers from UI/UIWindow.img/QuestIcon:
//   QuestIcon/0 = white light bulb  -> quest AVAILABLE to accept   (state 1)
//   QuestIcon/2 = brown book        -> quest READY TO TURN IN      (state 2)
//   QuestIcon/1 = open book         -> quest IN PROGRESS           (state 3)
// These are 44x44 — the caller scales them down to a badge.

static IWzCanvasPtr g_questMarker[4];   // [1] = available, [2] = completable, [3] = in-progress

IWzCanvasPtr GetQuestMarkerIcon(int state) {
    if (state < 1 || state > 3)
        return nullptr;
    if (!g_questMarker[state]) {
        const wchar_t* path =
            (state == 2) ? L"UI/UIWindow.img/QuestIcon/2/0" :   // brown book = ready to turn in
            (state == 3) ? L"UI/UIWindow.img/QuestIcon/1/0" :   // open book  = in progress
                           L"UI/UIWindow.img/QuestIcon/0/0";    // white bulb = available
        g_questMarker[state] = LoadCanvas(path);
    }
    return g_questMarker[state];
}

// =====================================================
// CACHE CONTROL
// =====================================================

void ClearEntityIconCache() {
    g_mobIcons.clear();
    g_npcIcons.clear();
}

void ClearMapIconCache() {
    g_mapIcons.clear();
}

