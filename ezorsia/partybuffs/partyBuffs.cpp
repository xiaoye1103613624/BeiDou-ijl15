#include "stdafx.h"
#include "PartyBuffsApi.h"
#include "compat/ClientAddresses.h"
#include "compat/PacketDispatcher.h"
#include "compat/hook.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/statusbar.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/ztl/zcom.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr DWORD kPartyHpDrawAddr = 0x0091FADA;
constexpr DWORD kPartyHpCreateAddr = 0x0091F934;
constexpr DWORD kPartyHpSingletonAddr = 0x00BF11B4;
constexpr DWORD kWvsContextSingletonAddr = 0x00BE7918;
constexpr DWORD kUserPoolSingletonAddr = 0x00BEBFA8;
constexpr DWORD kUserPoolGetUserAddr = 0x009716ED;
constexpr DWORD kPartyMemberIdsOffset = 11992;
constexpr DWORD kPartyHpMinimumWidthImmediateAddr = 0x0091FA54;
constexpr DWORD kToolTipCtorAddr = ClientAddresses::kToolTipCtor;
constexpr DWORD kUserLocalInstanceAddr = 0x00BEBF98;
constexpr int kUserLocalCharIdOffset = 0x11A8;
constexpr unsigned short kWidgetOpcode = CustomRecvOpcode::kCustomChannel;
constexpr BYTE kWidgetSubscribeSubCommand = 0x10;
constexpr BYTE kWidgetBuffCountsSubCommand = 0x11;
constexpr BYTE kWidgetPartyBuffLegacySnapshotSubCommand = 0xA8;
constexpr BYTE kWidgetPartyBuffSnapshotSubCommand = 0xA9;
constexpr BYTE kWidgetPartyHpPercentSubCommand = 0xAA;
constexpr BYTE kWidgetPartyTrackerSubCommand = 0xAB;
constexpr BYTE kWidgetPartyBuffCountsInboundSubCommand = 0xAD;

using ClientSocket_SendPacket_t = void(__thiscall*)(void*, const COutPacket&);
ClientSocket_SendPacket_t g_clientSocketSendPacket =
        reinterpret_cast<ClientSocket_SendPacket_t>(ClientAddresses::kSendPacket);

void SendClientPacket(const COutPacket& packet) {
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (sock) {
        g_clientSocketSendPacket(sock, packet);
    }
}

int ReadLocalCharacterId() {
    char* user = *reinterpret_cast<char**>(kUserLocalInstanceAddr);
    if (!user) {
        return 0;
    }
    __try {
        return *reinterpret_cast<int*>(user + kUserLocalCharIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void SendWidgetSubscribePacket() {
    COutPacket packet(kWidgetOpcode);
    packet.Encode1(kWidgetSubscribeSubCommand);
    SendClientPacket(packet);
}

constexpr int kBaseMinimumWidth = 230;
constexpr int kHpStripWidth = 72;
constexpr int kPreferredIconSize = 22;
constexpr int kPreferredIconPitch = 24;
constexpr int kPartyFontHeight = 29; // stock layout adds 5 => 34px per member
constexpr int kRowPitch = kPartyFontHeight + 5;
constexpr int kIconTop = 1;
constexpr int kHpPercentTop = 21;
constexpr unsigned long kHpPercentColor = 0xFFFFFFFF;
// Darker colors for readability on light party-HP background.
constexpr unsigned long kTrackerExpColor = 0xFFB85C00;
constexpr unsigned long kTrackerMesoColor = 0xFF0A6B2D;

struct PartyBuff {
    int sourceId = 0; // skill > 0, item < 0
    int remainingMs = 0;
    int totalMs = 0;
    DWORD receivedTick = 0;
    int useCount = 0;
    bool active = true;
};

struct RowLayout {
    int startX = 0;
    int pitch = kPreferredIconPitch;
    int iconSize = kPreferredIconSize;
    int count = 0;
};

struct PartyTrackerStats {
    unsigned long long exp = 0;
    unsigned long long meso = 0;
};

using PartyHpDraw_t = void(__thiscall*)(void*, const RECT*);
using PartyHpCreate_t = void(__thiscall*)(void*);

PartyHpDraw_t g_partyHpDraw = reinterpret_cast<PartyHpDraw_t>(kPartyHpDrawAddr);
PartyHpCreate_t g_partyHpCreate = reinterpret_cast<PartyHpCreate_t>(kPartyHpCreateAddr);

std::unordered_map<int, std::vector<PartyBuff>> g_partyBuffs;
std::unordered_map<int, int> g_partyHpPercent;
std::unordered_map<int, PartyTrackerStats> g_partyTracker;
std::unordered_map<int, IWzCanvasPtr> g_buffIconCache;
std::unordered_set<int> g_missingBuffIcons;
std::unordered_set<int> g_pendingIconLoads;
std::unordered_map<int, DWORD> g_missingIconRetryAt; // sourceId -> tick when retry allowed
IWzCanvasPtr g_scaledCooltimeShadows[16];
IWzFontPtr g_hpPercentFont;
IWzFontPtr g_trackerExpFont;
IWzFontPtr g_trackerMesoFont;
IWzFontPtr g_buffCountFont;
IWzFontPtr g_buffCountFontShadow;
bool g_trackerVisible = true;

int g_basePartyWidth = kBaseMinimumWidth;
int g_reservedBuffWidth = 0;
int g_requestedMinimumWidth = kBaseMinimumWidth;
bool g_layoutDirty = false;

alignas(8) unsigned char g_toolTipBuffer[0x600];
bool g_toolTipInitialized = false;
int g_hoveredSourceId = 0;

bool EnsureHpPercentFont() {
    if (g_hpPercentFont) {
        return true;
    }

    PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_hpPercentFont, nullptr);
    if (!g_hpPercentFont) {
        return false;
    }

    Ztl_variant_t style = L"B";
    auto createFont = reinterpret_cast<HRESULT(__thiscall*)(
            IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(0x0046341A);
    return SUCCEEDED(createFont(g_hpPercentFont, Ztl_bstr_t(L"Arial"), 10, kHpPercentColor, style));
}

bool CreateTrackerFont(IWzFontPtr& font, unsigned long color) {
    if (font) {
        return true;
    }
    PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
    if (!font) {
        return false;
    }
    Ztl_variant_t style = L"";
    auto createFont = reinterpret_cast<HRESULT(__thiscall*)(
            IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(0x0046341A);
    return SUCCEEDED(createFont(font, Ztl_bstr_t(L"Arial"), 9, color, style));
}

std::string FormatTrackerNumber(unsigned long long value) {
    std::string raw = std::to_string(value);
    for (int i = static_cast<int>(raw.size()) - 3; i > 0; i -= 3) {
        raw.insert(static_cast<size_t>(i), ",");
    }
    return raw;
}

void DrawPartyTracker(const IWzCanvasPtr& canvas, int characterId, int visibleRow) {
    if (!g_trackerVisible || !canvas) {
        return;
    }
    const auto found = g_partyTracker.find(characterId);
    if (found == g_partyTracker.end()) {
        return;
    }
    if (!CreateTrackerFont(g_trackerExpFont, kTrackerExpColor) ||
            !CreateTrackerFont(g_trackerMesoFont, kTrackerMesoColor)) {
        return;
    }

    const int rowTop = visibleRow * kRowPitch;
    const std::string expText = "EXP: +" + FormatTrackerNumber(found->second.exp);
    const std::string mesoText = "Meso: +" + FormatTrackerNumber(found->second.meso);
    canvas->DrawTextA(5, rowTop + 19, Ztl_bstr_t(expText.c_str()), g_trackerExpFont);
    canvas->DrawTextA(5, rowTop + 28, Ztl_bstr_t(mesoText.c_str()), g_trackerMesoFont);
}

int GetPartyMemberHpPercent(int characterId) {
    const auto cached = g_partyHpPercent.find(characterId);
    if (cached != g_partyHpPercent.end()) {
        return cached->second;
    }

    void* userPool = *reinterpret_cast<void**>(kUserPoolSingletonAddr);
    if (!userPool || characterId <= 0) {
        return -1;
    }

    using GetUser_t = void*(__thiscall*)(void*, unsigned int);
    void* user = reinterpret_cast<GetUser_t>(kUserPoolGetUserAddr)(userPool, characterId);
    if (!user) {
        return -1;
    }

    __try {
        void** vtable = *reinterpret_cast<void***>(user);
        const bool isLocal = reinterpret_cast<int(__thiscall*)(void*)>(vtable[3])(user) != 0;
        if (isLocal) {
            // Real HP override is handled by optional HP packet (0xA7); use cache if present.
            return -1;
        }

        // CUserRemote::m_nPartyHP is the 0..100 value used by CUIPartyHP.
        return (std::clamp)(*reinterpret_cast<int*>(
                reinterpret_cast<BYTE*>(user) + 4512), 0, 100);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void DrawHpPercent(const IWzCanvasPtr& canvas, CWnd* partyWindow, int characterId, int visibleRow) {
    if (!canvas || !partyWindow || !EnsureHpPercentFont()) {
        return;
    }

    const int percent = GetPartyMemberHpPercent(characterId);
    if (percent < 0) {
        return;
    }

    char text[16] = {};
    sprintf_s(text, "%d%%", percent);
    const int textWidth = g_hpPercentFont->CalcTextWidth(Ztl_bstr_t(text));
    const int hpLeft = partyWindow->m_width - kHpStripWidth;
    const int x = hpLeft + (kHpStripWidth - textWidth) / 2;
    const int y = kHpPercentTop + visibleRow * kRowPitch;
    canvas->DrawTextA(x, y, Ztl_bstr_t(text), g_hpPercentFont);
}

static IWzCanvas* GetCanvasSafe(CWnd* partyWindow) noexcept {
    using Fn = IWzCanvas**(__thiscall*)(CWnd*, IWzCanvas**);
    static const Fn fn = reinterpret_cast<Fn>(0x00425C4C);
    IWzCanvas* raw = nullptr;
    __try {
        fn(partyWindow, &raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return raw;
}

IWzCanvasPtr LoadCanvas(const wchar_t* uol) {
    IWzCanvasPtr canvas;
    if (!uol || !*uol || !get_rm()) {
        return canvas;
    }

    try {
        Ztl_variant_t value = get_rm()->GetObjectA(const_cast<wchar_t*>(uol));
        IUnknown* unknown = value.GetUnknown(false, false);
        if (unknown) {
            IWzCanvas* raw = nullptr;
            if (SUCCEEDED(unknown->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw))) && raw) {
                canvas = raw;
                raw->Release();
            }
        }
    } catch (...) {
    }
    return canvas;
}

IWzCanvasPtr CreateScaledCanvas(const IWzCanvasPtr& source, int targetSize) {
    if (!source || targetSize <= 0) {
        return IWzCanvasPtr();
    }

    UINT width = 0;
    UINT height = 0;
    source->get_width(&width);
    source->get_height(&height);
    if (width == 0 || height == 0) {
        return IWzCanvasPtr();
    }

    if (static_cast<int>(width) == targetSize && static_cast<int>(height) == targetSize) {
        return source;
    }

    IWzCanvasPtr target;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", target, nullptr);
    if (!target) {
        return IWzCanvasPtr();
    }

    HRESULT hr = target->Create(targetSize, targetSize, Ztl_variant_t(), Ztl_variant_t());
    if (FAILED(hr)) {
        return IWzCanvasPtr();
    }

    hr = target->CopyEx(
        0, 0, source, CANVAS_ALPHATYPE::CA_REMOVEALPHA,
        targetSize, targetSize, 0, 0, static_cast<int>(width), static_cast<int>(height),
        Ztl_variant_t());

    if (FAILED(hr)) {
        return IWzCanvasPtr();
    }

    return target;
}

void QueueBuffIconLoad(int sourceId) {
    if (sourceId == 0) {
        return;
    }
    if (g_buffIconCache.find(sourceId) != g_buffIconCache.end()) {
        return;
    }

    const DWORD now = GetTickCount();
    const auto missing = g_missingBuffIcons.find(sourceId);
    if (missing != g_missingBuffIcons.end()) {
        const auto retryAt = g_missingIconRetryAt.find(sourceId);
        if (retryAt != g_missingIconRetryAt.end() && now < retryAt->second) {
            return;
        }
        // First load often fails before Skill/Item WZ is ready — retry after 2s.
        g_missingBuffIcons.erase(missing);
        g_missingIconRetryAt[sourceId] = now + 2000;
    }

    g_pendingIconLoads.insert(sourceId);
}

IWzCanvasPtr GetBuffIconCached(int sourceId) {
    const auto cached = g_buffIconCache.find(sourceId);
    if (cached != g_buffIconCache.end()) {
        return cached->second;
    }

    QueueBuffIconLoad(sourceId);
    return IWzCanvasPtr();
}

IWzCanvasPtr GetScaledCooltimeShadow(int shadowIndex, int targetSize) {
    if (shadowIndex < 0 || shadowIndex >= 16) {
        return IWzCanvasPtr();
    }
    if (!CUIStatusBar::IsInstantiated()) {
        return IWzCanvasPtr();
    }
    if (g_scaledCooltimeShadows[shadowIndex]) {
        return g_scaledCooltimeShadows[shadowIndex];
    }

    IWzCanvasPtr original = CUIStatusBar::GetInstance()->m_aCanvasSkillCooltime[shadowIndex];
    if (!original) {
        return IWzCanvasPtr();
    }

    g_scaledCooltimeShadows[shadowIndex] = CreateScaledCanvas(original, targetSize);
    return g_scaledCooltimeShadows[shadowIndex];
}

bool EnsureBuffCountFonts() {
    if (g_buffCountFont && g_buffCountFontShadow) {
        return true;
    }

    auto createFont = reinterpret_cast<HRESULT(__thiscall*)(
            IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(0x0046341A);
    Ztl_variant_t style = L"B"; // Bold

    if (!g_buffCountFont) {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_buffCountFont, nullptr);
        if (g_buffCountFont) {
            createFont(g_buffCountFont, Ztl_bstr_t(L"Arial"), 9, 0xFFFFFFFF, style); // White
        }
    }

    if (!g_buffCountFontShadow) {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_buffCountFontShadow, nullptr);
        if (g_buffCountFontShadow) {
            createFont(g_buffCountFontShadow, Ztl_bstr_t(L"Arial"), 9, 0xFF000000, style); // Black
        }
    }

    return g_buffCountFont && g_buffCountFontShadow;
}

void DrawCountText(const IWzCanvasPtr& canvas, int x, int y, const char* text) {
    if (!EnsureBuffCountFonts()) {
        return;
    }
    int textWidth = g_buffCountFont->CalcTextWidth(Ztl_bstr_t(text));
    int tx = x + kPreferredIconSize - textWidth;
    int ty = y + kPreferredIconSize - 9; // Size 9 is about 9-10px tall

    // Draw drop shadow
    canvas->DrawTextA(tx + 1, ty + 1, Ztl_bstr_t(text), g_buffCountFontShadow);
    canvas->DrawTextA(tx, ty, Ztl_bstr_t(text), g_buffCountFont);
}

IWzCanvasPtr LoadBuffIcon(int sourceId) {
    const auto cached = g_buffIconCache.find(sourceId);
    if (cached != g_buffIconCache.end()) {
        return cached->second;
    }
    if (g_missingBuffIcons.find(sourceId) != g_missingBuffIcons.end()) {
        return IWzCanvasPtr();
    }

    IWzCanvasPtr icon;
    wchar_t uol[180] = {};

    if (sourceId > 0) {
        const int skillBook = sourceId / 10000;
        // Try common GMS083 Skill.img layouts (padded book + padded/unpadded skill id).
        const wchar_t* skillPaths[] = {
            L"Skill/%03d.img/skill/%07d/icon",
            L"Skill/%03d.img/skill/%d/icon",
            L"Skill/%d.img/skill/%d/icon",
            L"Skill/%03d.img/skill/%07d/iconMouseOver",
            L"Skill/%d.img/skill/%07d/icon",
        };
        for (const wchar_t* fmt : skillPaths) {
            swprintf_s(uol, fmt, skillBook, sourceId);
            icon = LoadCanvas(uol);
            if (icon) {
                break;
            }
        }
    } else if (sourceId < 0) {
        const int itemId = -sourceId;
        const wchar_t* folder = nullptr;
        switch (itemId / 1000000) {
        case 2: folder = L"Consume"; break;
        case 3: folder = L"Install"; break;
        case 4: folder = L"Etc"; break;
        case 5: folder = L"Cash"; break;
        default: break;
        }

        if (folder) {
            swprintf_s(uol, L"Item/%s/%04d.img/%08d/info/icon", folder, itemId / 10000, itemId);
            icon = LoadCanvas(uol);
            if (!icon) {
                swprintf_s(uol, L"Item/%s/%04d.img/%d/info/icon", folder, itemId / 10000, itemId);
                icon = LoadCanvas(uol);
            }
        }
    }

    if (icon) {
        g_missingBuffIcons.erase(sourceId);
        g_missingIconRetryAt.erase(sourceId);
        IWzCanvasPtr scaled = CreateScaledCanvas(icon, kPreferredIconSize);
        if (scaled) {
            g_buffIconCache[sourceId] = scaled;
            return scaled;
        }
        g_buffIconCache[sourceId] = icon;
        return icon;
    }

    g_missingBuffIcons.insert(sourceId);
    g_missingIconRetryAt[sourceId] = GetTickCount() + 2000;
    return icon;
}

int GetRemainingMs(const PartyBuff& buff) {
    if (buff.remainingMs <= 0) {
        return 0;
    }
    const DWORD elapsed = GetTickCount() - buff.receivedTick;
    return (std::max)(0, buff.remainingMs - static_cast<int>(elapsed));
}

void CopyFit(const IWzCanvasPtr& destination, const IWzCanvasPtr& source, int x, int y, int size) {
    if (!destination || !source || size <= 0) {
        return;
    }

    UINT width = 0;
    UINT height = 0;
    source->get_width(&width);
    source->get_height(&height);
    if (width == 0 || height == 0) {
        return;
    }

    destination->CopyEx(
            x, y, source, CANVAS_ALPHATYPE::CA_REMOVEALPHA,
            size, size, 0, 0, static_cast<int>(width), static_cast<int>(height),
            Ztl_variant_t());
}

void DrawBuff(const IWzCanvasPtr& canvas, const PartyBuff& buff, int x, int y, int size) {
    IWzCanvasPtr icon = GetBuffIconCached(buff.sourceId);
    if (!icon) {
        // Pending / retrying icon load — keep the widened strip visibly occupied.
        canvas->DrawRectangle(x, y, size, size, 0x60404040);
        return;
    }
    CopyFit(canvas, icon, x, y, size);

    if (!buff.active) {
        canvas->DrawRectangle(x, y, size, size, 0x90000000); // 56% opaque black overlay
    } else {
        const int remainingMs = GetRemainingMs(buff);
        if (remainingMs > 0 && buff.totalMs > 0 && CUIStatusBar::IsInstantiated()) {
            int shadowIndex = static_cast<int>(
                    (static_cast<long long>(remainingMs) * 16LL) / (std::max)(1, buff.totalMs));
            shadowIndex = (std::clamp)(shadowIndex, 0, 15);
            IWzCanvasPtr shadow = GetScaledCooltimeShadow(shadowIndex, size);
            CopyFit(canvas, shadow, x, y, size);
        }
    }

    // Draw use count for item buffs
    if (buff.sourceId < 0 && buff.useCount > 0) {
        char countStr[16] = {};
        sprintf_s(countStr, "%dx", buff.useCount);
        DrawCountText(canvas, x, y, countStr);
    }
}

int GetLargestBuffCount() {
    int largest = 0;
    for (const auto& entry : g_partyBuffs) {
        largest = (std::max)(largest, static_cast<int>(entry.second.size()));
    }
    return largest;
}

void RecalculateRequestedWidth() {
    const int count = GetLargestBuffCount();
    const int screenLimit = (std::max)(kBaseMinimumWidth, get_screen_width() - 10);
    const int preferredReserve = count * kPreferredIconPitch;
    const int desired = (std::min)(screenLimit, g_basePartyWidth + preferredReserve);
    const int reserve = (std::max)(0, desired - g_basePartyWidth);

    if (desired != g_requestedMinimumWidth || reserve != g_reservedBuffWidth) {
        g_requestedMinimumWidth = desired;
        g_reservedBuffWidth = reserve;
        Patch4(kPartyHpMinimumWidthImmediateAddr, g_requestedMinimumWidth);
        g_layoutDirty = true;
    }
}

RowLayout BuildRowLayout(CWnd* partyWindow, int count) {
    RowLayout layout;
    layout.count = count;
    if (!partyWindow || count <= 0) {
        return layout;
    }

    layout.pitch = kPreferredIconPitch;
    layout.iconSize = kPreferredIconSize;
    layout.startX = partyWindow->m_width - kHpStripWidth - count * layout.pitch - 4;
    return layout;
}

void ObserveNaturalWidth(CWnd* partyWindow) {
    if (!partyWindow || partyWindow->m_width <= g_requestedMinimumWidth) {
        return;
    }

    // When the character name is wider than our minimum, the stock constructor
    // chooses nameWidth + 100. Preserve that natural width and add the buff strip.
    const int naturalWidth = partyWindow->m_width - g_reservedBuffWidth;
    if (naturalWidth > g_basePartyWidth) {
        g_basePartyWidth = naturalWidth;
        RecalculateRequestedWidth();
    }
}

void DrawPartyBuffs(CWnd* partyWindow) {
    if (!partyWindow) {
        return;
    }

    void* context = *reinterpret_cast<void**>(kWvsContextSingletonAddr);
    IWzCanvas* rawCanvas = GetCanvasSafe(partyWindow);
    if (!context || !rawCanvas) {
        return;
    }

    IWzCanvasPtr canvas(rawCanvas, false);
    const int* partyIds = reinterpret_cast<const int*>(
            reinterpret_cast<const BYTE*>(context) + kPartyMemberIdsOffset);
    int visibleRow = 0;

    ObserveNaturalWidth(partyWindow);

    for (int slot = 0; slot < 6; ++slot) {
        const int characterId = partyIds[slot];
        if (characterId == 0) {
            continue;
        }

        const auto found = g_partyBuffs.find(characterId);
        if (found != g_partyBuffs.end() && !found->second.empty()) {
            const RowLayout layout = BuildRowLayout(partyWindow, static_cast<int>(found->second.size()));
            const int y = kIconTop + visibleRow * kRowPitch;

            for (int index = 0; index < layout.count; ++index) {
                DrawBuff(
                        canvas,
                        found->second[index],
                        layout.startX + index * layout.pitch,
                        y,
                        layout.iconSize);
            }
        }

        DrawHpPercent(canvas, partyWindow, characterId, visibleRow);
        DrawPartyTracker(canvas, characterId, visibleRow);
        ++visibleRow;
    }
}

void __fastcall PartyHpDraw_Hook(void* pThis, void*, const RECT* pRect) {
    g_partyHpDraw(pThis, pRect);
    __try {
        DrawPartyBuffs(reinterpret_cast<CWnd*>(pThis));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int __fastcall PartyHpFontHeight_Hook(void*, void*) {
    return kPartyFontHeight;
}

void EnsureToolTip() {
    if (!g_toolTipInitialized) {
        reinterpret_cast<void(__thiscall*)(void*)>(kToolTipCtorAddr)(g_toolTipBuffer);
        g_toolTipInitialized = true;
    }
}

std::string GetSkillText(int sourceId, const wchar_t* field) {
    if (sourceId <= 0 || !field) {
        return std::string();
    }

    try {
        wchar_t path[64] = {};
        swprintf_s(path, L"String/Skill.img/%07d", sourceId);
        IWzPropertyPtr property = get_rm()->GetObjectA(path).GetUnknown();
        if (!property) {
            return std::string();
        }
        Ztl_variant_t value = property->item[field];
        if (value.vt == VT_BSTR) {
            return static_cast<const char*>(_bstr_t(value));
        }
    } catch (...) {
    }
    return std::string();
}

const PartyBuff* FindHoveredBuff(CWnd* partyWindow) {
    if (!partyWindow) {
        return nullptr;
    }

    void* inputSystem = *reinterpret_cast<void**>(0x00BEC33C);
    if (!inputSystem) {
        return nullptr;
    }

    POINT cursor = {};
    using GetCursorPosFn = void(__thiscall*)(void*, POINT*);
    reinterpret_cast<GetCursorPosFn>(0x0059A388)(inputSystem, &cursor);

    __try {
        cursor.x -= partyWindow->GetAbsLeft();
        cursor.y -= partyWindow->GetAbsTop();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }

    if (cursor.x < 0 || cursor.y < 0 ||
            cursor.x >= partyWindow->m_width || cursor.y >= partyWindow->m_height) {
        return nullptr;
    }

    void* context = *reinterpret_cast<void**>(kWvsContextSingletonAddr);
    if (!context) {
        return nullptr;
    }

    const int* partyIds = reinterpret_cast<const int*>(
            reinterpret_cast<const BYTE*>(context) + kPartyMemberIdsOffset);
    int visibleRow = 0;

    for (int slot = 0; slot < 6; ++slot) {
        const int characterId = partyIds[slot];
        if (characterId == 0) {
            continue;
        }

        const auto found = g_partyBuffs.find(characterId);
        if (found != g_partyBuffs.end()) {
            const RowLayout layout = BuildRowLayout(partyWindow, static_cast<int>(found->second.size()));
            const int y = kIconTop + visibleRow * kRowPitch;
            for (int index = 0; index < layout.count; ++index) {
                const int x = layout.startX + index * layout.pitch;
                if (cursor.x >= x && cursor.x < x + layout.iconSize &&
                        cursor.y >= y && cursor.y < y + layout.iconSize) {
                    return &found->second[index];
                }
            }
        }
        ++visibleRow;
    }
    return nullptr;
}

void ClearToolTip() {
    if (g_toolTipInitialized) {
        reinterpret_cast<CUIToolTip*>(g_toolTipBuffer)->ClearToolTip();
    }
    g_hoveredSourceId = 0;
}

void ShowBuffToolTip(const PartyBuff& buff) {
    EnsureToolTip();
    auto* toolTip = reinterpret_cast<CUIToolTip*>(g_toolTipBuffer);
    toolTip->ClearToolTip();

    std::string title = GetSkillText(buff.sourceId, L"name");
    std::string description = GetSkillText(buff.sourceId, L"desc");
    if (title.empty()) {
        title = buff.sourceId > 0 ? "Buff" : "Item buff";
    }

    const int remainingSeconds = GetRemainingMs(buff) / 1000;
    if (remainingSeconds > 0) {
        char duration[64] = {};
        sprintf_s(duration, "\r\nRemaining: %02d:%02d", remainingSeconds / 60, remainingSeconds % 60);
        description += duration;
    }

    void* inputSystem = *reinterpret_cast<void**>(0x00BEC33C);
    if (!inputSystem) {
        return;
    }
    POINT cursor = {};
    using GetCursorPosFn = void(__thiscall*)(void*, POINT*);
    reinterpret_cast<GetCursorPosFn>(0x0059A388)(inputSystem, &cursor);
    toolTip->SetToolTip_String2(
            cursor.x + 12,
            cursor.y + 12,
            ZXString<char>(title.c_str()),
            ZXString<char>(description.c_str()),
            0, 0, 0, 220, 1, 0);
}

void SendWidgetBuffCountsPacket() {
    const int localCharId = ReadLocalCharacterId();
    if (localCharId <= 0) {
        return;
    }

    const auto found = g_partyBuffs.find(localCharId);
    if (found == g_partyBuffs.end()) {
        return; // No buffs to sync
    }

    const auto& buffs = found->second;
    int itemBuffCount = 0;
    for (const auto& buff : buffs) {
        if (buff.sourceId < 0 && buff.useCount > 0) {
            itemBuffCount++;
        }
    }

    if (itemBuffCount == 0) {
        return; // No item buffs to sync
    }

    COutPacket packet(kWidgetOpcode);
    packet.Encode1(kWidgetBuffCountsSubCommand);
    packet.Encode1(static_cast<unsigned char>(itemBuffCount));
    for (const auto& buff : buffs) {
        if (buff.sourceId < 0 && buff.useCount > 0) {
            packet.Encode4(static_cast<unsigned int>(buff.sourceId));
            packet.Encode4(static_cast<unsigned int>(buff.useCount));
            packet.Encode1(buff.active ? 1 : 0);
        }
    }

    SendClientPacket(packet);
}

// Stub until tracker-toggle UI is wired; keeps Release build green.
bool PollTrackerToggleClick(CWnd* /*partyWindow*/) {
    return false;
}

void SendTrackerTogglePacket() {
}

} // namespace

void PartyBuffs_UpdateSnapshot(
        int characterId,
        const std::vector<int>& sourceIds,
        const std::vector<int>& remainingTimes,
        const std::vector<int>& totalTimes) {
    if (characterId <= 0) {
        return;
    }

    std::vector<PartyBuff>& oldBuffs = g_partyBuffs[characterId];
    std::vector<PartyBuff> newBuffs;
    newBuffs.reserve(sourceIds.size() + oldBuffs.size());
    const DWORD now = GetTickCount();

    // 1. Process new active buffs
    for (size_t index = 0; index < sourceIds.size(); ++index) {
        int sourceId = sourceIds[index];
        if (sourceId == 0) {
            continue;
        }

        int remainingMs = index < remainingTimes.size() ? remainingTimes[index] : 0;
        int totalMs = index < totalTimes.size() ? totalTimes[index] : remainingMs;

        if (sourceId > 0) {
            // Skills: add as active
            PartyBuff buff;
            buff.sourceId = sourceId;
            buff.remainingMs = remainingMs;
            buff.totalMs = totalMs;
            buff.receivedTick = now;
            buff.useCount = 0;
            buff.active = true;
            newBuffs.push_back(buff);
            QueueBuffIconLoad(sourceId);
        } else {
            // Items: search in oldBuffs to update useCount
            int useCount = 1;
            bool found = false;
            for (const auto& oldBuff : oldBuffs) {
                if (oldBuff.sourceId == sourceId) {
                    found = true;
                    useCount = oldBuff.useCount;

                    // Detect new consumption (refresh or reactivate)
                    int oldCalculatedRemaining = 0;
                    if (oldBuff.active) {
                        const DWORD elapsed = now - oldBuff.receivedTick;
                        oldCalculatedRemaining = (std::max)(0, oldBuff.remainingMs - static_cast<int>(elapsed));
                    }

                    if (!oldBuff.active || remainingMs > oldCalculatedRemaining + 5000) {
                        useCount++;
                    }
                    break;
                }
            }

            PartyBuff buff;
            buff.sourceId = sourceId;
            buff.remainingMs = remainingMs;
            buff.totalMs = totalMs;
            buff.receivedTick = now;
            buff.useCount = useCount;
            buff.active = true;
            newBuffs.push_back(buff);
            QueueBuffIconLoad(sourceId);
        }
    }

    // 2. Keep the inactive item buffs from oldBuffs
    for (const auto& oldBuff : oldBuffs) {
        if (oldBuff.sourceId < 0) {
            bool alreadyInNew = false;
            for (const auto& newBuff : newBuffs) {
                if (newBuff.sourceId == oldBuff.sourceId) {
                    alreadyInNew = true;
                    break;
                }
            }
            if (!alreadyInNew) {
                PartyBuff inactiveBuff = oldBuff;
                inactiveBuff.active = false;
                inactiveBuff.remainingMs = 0;
                inactiveBuff.totalMs = 0;
                newBuffs.push_back(inactiveBuff);
            }
        }
    }

    if (newBuffs.empty()) {
        g_partyBuffs.erase(characterId);
    } else {
        g_partyBuffs[characterId] = std::move(newBuffs);
    }

    RecalculateRequestedWidth();

    if (ReadLocalCharacterId() == characterId) {
        SendWidgetBuffCountsPacket();
    }

    CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);
    if (partyWindow) {
        partyWindow->InvalidateRect(nullptr);
    }
}

void PartyBuffs_UpdateHpPercent(int characterId, int percent) {
    if (characterId <= 0) {
        return;
    }

    g_partyHpPercent[characterId] = (std::clamp)(percent, 0, 100);
    CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);
    if (partyWindow) {
        partyWindow->InvalidateRect(nullptr);
    }
}

bool PartyBuffs_IsTrackerVisible() {
    return g_trackerVisible;
}

void PartyBuffs_ToggleTracker() {
    PartyBuffs_SetTrackerVisible(!g_trackerVisible);
}

void PartyBuffs_SetTrackerVisible(bool visible) {
    g_trackerVisible = visible;
    g_partyTracker.clear();
    CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);
    if (partyWindow) {
        partyWindow->InvalidateRect(nullptr);
    }
}

void PartyBuffs_UpdateTracker(
        int characterId,
        unsigned long long exp,
        unsigned long long meso) {
    if (!g_trackerVisible || characterId <= 0) {
        return;
    }
    g_partyTracker[characterId] = { exp, meso };
    CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);
    if (partyWindow) {
        partyWindow->InvalidateRect(nullptr);
    }
}

void RecreatePartyWindowSafe(CWnd* partyWindow) {
    __try {
        partyWindow->Destroy();
        g_partyHpCreate(partyWindow);
        SendWidgetSubscribePacket();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void PartyBuffs_OnClientTick() {
    // Re-queue timed-out "missing" icons so early WZ misses do not stay blank forever.
    const DWORD nowRetry = GetTickCount();
    for (auto itRetry = g_missingIconRetryAt.begin(); itRetry != g_missingIconRetryAt.end(); ) {
        if (nowRetry >= itRetry->second &&
                g_buffIconCache.find(itRetry->first) == g_buffIconCache.end()) {
            g_missingBuffIcons.erase(itRetry->first);
            g_pendingIconLoads.insert(itRetry->first);
            itRetry = g_missingIconRetryAt.erase(itRetry);
        } else {
            ++itRetry;
        }
    }

    int loadedThisTick = 0;
    auto it = g_pendingIconLoads.begin();
    while (it != g_pendingIconLoads.end() && loadedThisTick < 4) {
        int sourceId = *it;
        it = g_pendingIconLoads.erase(it);

        LoadBuffIcon(sourceId);
        loadedThisTick++;
    }

    // Clean up g_partyBuffs for members no longer in the party
    void* context = *reinterpret_cast<void**>(kWvsContextSingletonAddr);
    if (context) {
        const int* partyIds = reinterpret_cast<const int*>(
                reinterpret_cast<const BYTE*>(context) + kPartyMemberIdsOffset);
        std::unordered_set<int> currentPartyIds;
        for (int slot = 0; slot < 6; ++slot) {
            if (partyIds[slot] != 0) {
                currentPartyIds.insert(partyIds[slot]);
            }
        }

        for (auto itP = g_partyBuffs.begin(); itP != g_partyBuffs.end(); ) {
            if (currentPartyIds.find(itP->first) == currentPartyIds.end()) {
                itP = g_partyBuffs.erase(itP);
            } else {
                ++itP;
            }
        }
    }

    // Local expiration of active buffs (make items inactive, erase skills)
    bool buffsChanged = false;
    for (auto itBuffs = g_partyBuffs.begin(); itBuffs != g_partyBuffs.end(); ) {
        auto& buffVector = itBuffs->second;
        bool memberChanged = false;

        for (auto itB = buffVector.begin(); itB != buffVector.end(); ) {
            PartyBuff& buff = *itB;
            if (buff.active && buff.remainingMs > 0) {
                const DWORD elapsed = GetTickCount() - buff.receivedTick;
                if (static_cast<int>(elapsed) >= buff.remainingMs) {
                    if (buff.sourceId < 0) {
                        // For items, make it inactive instead of removing
                        buff.active = false;
                        buff.remainingMs = 0;
                        buff.totalMs = 0;
                        memberChanged = true;
                        ++itB;
                    } else {
                        // For skills, remove them
                        itB = buffVector.erase(itB);
                        memberChanged = true;
                    }
                    continue;
                }
            }
            ++itB;
        }

        if (memberChanged) {
            buffsChanged = true;
        }

        if (buffVector.empty()) {
            itBuffs = g_partyBuffs.erase(itBuffs);
        } else {
            ++itBuffs;
        }
    }

    if (buffsChanged) {
        RecalculateRequestedWidth();
    }

    CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);

    if ((loadedThisTick > 0 || buffsChanged) && partyWindow) {
        partyWindow->InvalidateRect(nullptr);
    }

    if (g_layoutDirty && partyWindow) {
        g_layoutDirty = false;
        RecreatePartyWindowSafe(partyWindow);
    }

    static DWORD lastCountSend = 0;
    const DWORD now = GetTickCount();
    if (now - lastCountSend >= 3000) {
        lastCountSend = now;
        SendWidgetBuffCountsPacket();
    }

    if (!partyWindow) {
        ClearToolTip();
        return;
    }

    const PartyBuff* hovered = FindHoveredBuff(partyWindow);
    const int sourceId = hovered ? hovered->sourceId : 0;
    if (sourceId != g_hoveredSourceId) {
        if (hovered) {
            g_hoveredSourceId = sourceId;
            ShowBuffToolTip(*hovered);
        } else {
            ClearToolTip();
        }
    }

    static DWORD lastRedraw = 0;
    if (now - lastRedraw >= 1000) {
        lastRedraw = now;
        partyWindow->InvalidateRect(nullptr);
        if (hovered && sourceId == g_hoveredSourceId) {
            ShowBuffToolTip(*hovered);
        }
    }
}

void __fastcall PartyHpCreate_Hook(void* pThis, void*) {
    g_partyHpCreate(pThis);
    SendWidgetSubscribePacket();
}

void AttachPartyBuffsMod() {
    Patch4(kPartyHpMinimumWidthImmediateAddr, kBaseMinimumWidth);

    // CUIPartyHP uses the basic-font height at these sites to calculate both
    // window height and every member row's Y coordinate. Returning 29 makes
    // its native "+ 5" spacing exactly 34px: a 32px icon plus 2px gap.
    constexpr DWORD heightCalls[] = {
        0x0091FA72, // Create: total window height
        0x0092035D, // Draw: member name
        0x00920481, // Draw: offline member name
        0x00920503, // Draw: HP gauge base
        0x009205AD, // Draw: HP gauge fill
        0x00920612, // Draw: HP gauge end
    };
    for (DWORD call : heightCalls) {
        PatchCall(call, &PartyHpFontHeight_Hook);
    }

    ATTACH_HOOK(g_partyHpDraw, PartyHpDraw_Hook);
    ATTACH_HOOK(g_partyHpCreate, PartyHpCreate_Hook);
}

void PartyBuffs_UpdateCounts(int characterId, int count, const unsigned char* payload) {
    if (characterId <= 0 || count <= 0 || !payload) {
        return;
    }

    std::vector<PartyBuff>& buffs = g_partyBuffs[characterId];
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        int offset = i * 9;
        int sourceId = 0;
        int useCount = 0;
        std::memcpy(&sourceId, payload + offset, 4);
        std::memcpy(&useCount, payload + offset + 4, 4);
        bool active = payload[offset + 8] != 0;

        bool found = false;
        for (auto& buff : buffs) {
            if (buff.sourceId == sourceId) {
                found = true;
                if (buff.useCount != useCount || buff.active != active) {
                    buff.useCount = useCount;
                    buff.active = active;
                    if (!active) {
                        buff.remainingMs = 0;
                        buff.totalMs = 0;
                    }
                    changed = true;
                }
                break;
            }
        }

        if (!found) {
            PartyBuff buff;
            buff.sourceId = sourceId;
            buff.useCount = useCount;
            buff.active = active;
            buff.remainingMs = 0;
            buff.totalMs = 0;
            buff.receivedTick = GetTickCount();
            buffs.push_back(buff);
            changed = true;

            QueueBuffIconLoad(sourceId);
        }
    }

    if (changed) {
        RecalculateRequestedWidth();
        CWnd* partyWindow = *reinterpret_cast<CWnd**>(kPartyHpSingletonAddr);
        if (partyWindow) {
            partyWindow->InvalidateRect(nullptr);
        }
    }
}

namespace {
uint8_t ReadU8(const unsigned char*& p) {
    const uint8_t v = *p;
    p += 1;
    return v;
}

uint32_t ReadU32(const unsigned char*& p) {
    const uint32_t v = *reinterpret_cast<const uint32_t*>(p);
    p += 4;
    return v;
}

unsigned long long ReadU64(const unsigned char*& p) {
    const unsigned long long low = ReadU32(p);
    const unsigned long long high = ReadU32(p);
    return low | (high << 32);
}

bool ProcessWidgetPacket(CompatInPacket* packet) {
    if (packet == nullptr) {
        return false;
    }

    const unsigned char* p = packet->CurrentPublic();
    if (p == nullptr) {
        return false;
    }

    const unsigned char* end = packet->Data() + packet->Size();
    if (p + 2 > end) {
        return false;
    }

    const uint16_t opcode = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    p += 2;
    if (opcode != kWidgetOpcode || p >= end) {
        return false;
    }

    const BYTE subCommand = ReadU8(p);
    if (subCommand == kWidgetPartyBuffLegacySnapshotSubCommand) {
        if (p + 5 > end) {
            return true;
        }
        const int characterId = static_cast<int>(ReadU32(p));
        const int count = static_cast<int>(ReadU8(p));
        std::vector<int> sourceIds;
        std::vector<int> remainingTimes;
        std::vector<int> totalTimes;
        sourceIds.reserve(count);
        remainingTimes.reserve(count);
        totalTimes.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (p + 4 > end) {
                break;
            }
            const int sourceId = static_cast<int>(ReadU32(p));
            if (sourceId != 0) {
                sourceIds.push_back(sourceId);
                remainingTimes.push_back(0);
                totalTimes.push_back(0);
            }
        }
        PartyBuffs_UpdateSnapshot(characterId, sourceIds, remainingTimes, totalTimes);
        return true;
    }

    if (subCommand == kWidgetPartyBuffSnapshotSubCommand) {
        if (p + 5 > end) {
            return true;
        }
        const int characterId = static_cast<int>(ReadU32(p));
        const int count = static_cast<int>(ReadU8(p));
        std::vector<int> sourceIds;
        std::vector<int> remainingTimes;
        std::vector<int> totalTimes;
        sourceIds.reserve(count);
        remainingTimes.reserve(count);
        totalTimes.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (p + 12 > end) {
                break;
            }
            const int sourceId = static_cast<int>(ReadU32(p));
            const int remainingMs = static_cast<int>(ReadU32(p));
            const int totalMs = static_cast<int>(ReadU32(p));
            if (sourceId != 0) {
                sourceIds.push_back(sourceId);
                remainingTimes.push_back(remainingMs);
                totalTimes.push_back(totalMs);
            }
        }
        PartyBuffs_UpdateSnapshot(characterId, sourceIds, remainingTimes, totalTimes);
        return true;
    }

    if (subCommand == kWidgetPartyHpPercentSubCommand) {
        if (p + 5 > end) {
            return true;
        }
        const int characterId = static_cast<int>(ReadU32(p));
        const int percent = static_cast<int>(ReadU8(p));
        PartyBuffs_UpdateHpPercent(characterId, percent);
        return true;
    }

    if (subCommand == kWidgetPartyTrackerSubCommand) {
        if (p >= end) {
            return true;
        }
        const BYTE mode = ReadU8(p);
        if (mode == 0 || mode == 1) {
            PartyBuffs_SetTrackerVisible(mode == 1);
        } else if (mode == 2) {
            if (p + 20 > end) {
                return true;
            }
            const int characterId = static_cast<int>(ReadU32(p));
            const unsigned long long exp = ReadU64(p);
            const unsigned long long meso = ReadU64(p);
            PartyBuffs_UpdateTracker(characterId, exp, meso);
        }
        return true;
    }

    if (subCommand == kWidgetPartyBuffCountsInboundSubCommand) {
        if (p + 5 > end) {
            return true;
        }
        const int characterId = static_cast<int>(ReadU32(p));
        const int count = static_cast<int>(ReadU8(p));
        if (count <= 0 || p + (count * 9) > end) {
            return true;
        }
        PartyBuffs_UpdateCounts(characterId, count, p);
        return true;
    }

    // Unknown 0x3713 subcommands (e.g. 0xA7 real HP/MP) — swallow so stock client does not crash.
    return true;
}
} // namespace

namespace PartyBuffs {
namespace {
bool g_hooksAttached = false;
} // namespace

bool HandleServerPacket(CompatInPacket* packet) {
    return ProcessWidgetPacket(packet);
}

void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kWidgetOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kWidgetOpcode) {
                    return false;
                }
                return PartyBuffs::HandleServerPacket(packet);
            });
}

void EnsureHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    AttachPartyBuffsMod();
}
} // namespace PartyBuffs

