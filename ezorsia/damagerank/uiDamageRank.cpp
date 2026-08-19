#define NOMINMAX
#include <windows.h>
#include <comdef.h>

#undef min
#undef max

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "uiDamageRank.h"
#include "DamageRankApi.h"
#include "DamageRankHud.h"
#include "DamageRankNet.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/wndman.h"
#include "compat/ztl/zcom.h"

namespace {
constexpr unsigned char kDamageRankOpen = 1;
constexpr unsigned char kDamageRankReset = 2;
constexpr unsigned char kDamageRankClose = 3;

constexpr int kLayerWidth = 223;
constexpr int kLayerHeightExpanded = 223;
constexpr int kLayerHeightMinimized = 20;
constexpr int kLayerZ = 1000;

constexpr int kLayerBottomHeight = 2;

constexpr int kMaxPlayerRows = 30;
constexpr int kMaxSkillRows = 15;

constexpr int kHeaderHeight = 26;
constexpr int kTitleY = 26;
constexpr int kRowsStartY = 52;

// Player gauge asset height. Do not change this lightly.
constexpr int kPlayerRowHeight = 21;

// Visual gap between player gauge rows.
constexpr int kPlayerRowGap = 2;
constexpr int kPlayerRowPitch = kPlayerRowHeight + kPlayerRowGap;

constexpr int kSkillRowHeight = 42;

constexpr int kResetX = 5;
constexpr int kSwitchX = 137;
constexpr int kAutoX = 169;
constexpr int kMinX = 192;
constexpr int kCloseX = 206;

constexpr int kBottomRollHitHeight = 6;

constexpr int kScrollX = 205;
constexpr int kScrollWidth = 15;
constexpr int kScrollButtonHeight = 13;
constexpr int kScrollThumbMinHeight = 12;

uint8_t ReadU8(const uint8_t*& p) {
    const uint8_t v = *p;
    p += 1;
    return v;
}

uint16_t ReadU16(const uint8_t*& p) {
    const uint16_t v = *reinterpret_cast<const uint16_t*>(p);
    p += 2;
    return v;
}

uint32_t ReadU32(const uint8_t*& p) {
    const uint32_t v = *reinterpret_cast<const uint32_t*>(p);
    p += 4;
    return v;
}

int32_t ReadS32(const uint8_t*& p) {
    const int32_t v = *reinterpret_cast<const int32_t*>(p);
    p += 4;
    return v;
}

unsigned long long ReadU64(const uint8_t*& p) {
    const unsigned long long low = ReadU32(p);
    const unsigned long long high = ReadU32(p);
    return low | (high << 32);
}

std::string ReadStr(const uint8_t*& p) {
    const uint16_t len = ReadU16(p);
    if (len == 0) {
        return std::string();
    }

    std::string s(reinterpret_cast<const char*>(p), reinterpret_cast<const char*>(p) + len);
    p += len;
    return s;
}

bool CreateFontObject(IWzFontPtr& outFont, const wchar_t* face, unsigned long color) {
    if (outFont) {
        return true;
    }

    PcCreateObject<IWzFontPtr>(L"Canvas#Font", outFont, nullptr);
    if (!outFont) {
        return false;
    }

    Ztl_variant_t style = L"B";
    auto fnCreate =
            reinterpret_cast<HRESULT(__thiscall*)(IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(
                    0x0046341A);

    return SUCCEEDED(fnCreate(outFont, Ztl_bstr_t(face), 12, color, style));
}


std::string TrimName(const std::string& value, size_t maxLen = 12) {
    if (value.empty()) {
        return std::string("(unknown)");
    }
    if (value.size() <= maxLen) {
        return value;
    }
    if (maxLen <= 3) {
        return value.substr(0, maxLen);
    }
    return value.substr(0, maxLen - 3) + "...";
}

int CalcTextWidth(IWzFontPtr font, const std::wstring& text) {
    if (!font || text.empty()) {
        return 0;
    }
    return font->CalcTextWidth(Ztl_bstr_t(text.c_str()));
}

using CanvasBlitFn = HRESULT(__stdcall*)(IWzCanvas*, long, long, IWzCanvas*, Ztl_variant_t);

bool BlitCanvas(IWzCanvasPtr dest, int x, int y, IWzCanvasPtr src, long alpha = 255) {
    if (!dest || !src) {
        return false;
    }

    auto rawDest = dest.GetInterfacePtr();
    auto rawSrc = src.GetInterfacePtr();
    if (!rawDest || !rawSrc) {
        return false;
    }

    auto vtbl = *reinterpret_cast<void***>(rawDest);
    if (!vtbl) {
        return false;
    }

    auto fn = reinterpret_cast<CanvasBlitFn>(vtbl[0x80 / 4]);
    if (!fn) {
        return false;
    }

    Ztl_variant_t vAlpha(alpha);
    const HRESULT hr = fn(rawDest, x, y, rawSrc, vAlpha);
    return SUCCEEDED(hr);
}
static void* GetWvsContextInstanceRaw() {
    return *reinterpret_cast<void**>(0x00BE7918);
}

static const char* GetLocalCharacterNameRaw() {
    void* pContext = GetWvsContextInstanceRaw();
    if (!pContext) {
        return nullptr;
    }

    auto fnGetCharacterName =
            reinterpret_cast<const char*(__thiscall*)(void*)>(0x004AC308);

    return fnGetCharacterName(pContext);
}
int GetPlayerVisibleRows(size_t playerCount) {
    const int count = static_cast<int>(playerCount);
    return count < kMaxPlayerRows ? count : kMaxPlayerRows;
}
int GetPlayerLayerHeight(size_t playerCount) {
    const int rows = GetPlayerVisibleRows(playerCount);
    return kRowsStartY + (kPlayerRowPitch * rows) + kLayerBottomHeight;
}
int GetSkillVisibleRows(size_t skillCount) {
    const int count = static_cast<int>(skillCount);
    return count < kMaxSkillRows ? count : kMaxSkillRows;
}

int GetSkillLayerHeight(size_t skillCount) {
    const int rows = GetSkillVisibleRows(skillCount);
    return kRowsStartY + (kSkillRowHeight * rows) + kLayerBottomHeight;
}
int GetClientUpdateTime() {
    using GetUpdateTimeFn = int(__cdecl*)();
    auto fnGetUpdateTime = reinterpret_cast<GetUpdateTimeFn>(0x00987257);
    return fnGetUpdateTime();
}
void SetGameCursorState(int /*state*/) {
    // Standalone Gr2D UI: do not mutate CInputSystem cursor state here.
}

bool GetInputCursorPointForDamageRank(POINT& pt) {
    void* inputSystem = *reinterpret_cast<void**>(0x00BEC33C);
    if (!inputSystem) {
        return false;
    }

    using GetCursorPosFn = void(__thiscall*)(void*, POINT*);
    auto fnGetCursorPos = reinterpret_cast<GetCursorPosFn>(0x0059A388);

    pt.x = 0;
    pt.y = 0;
    fnGetCursorPos(inputSystem, &pt);
    return true;
}

class CDamageRankHitShell : public CWnd {
private:
    unsigned char m_nativeWndBody[(0x6C > sizeof(CWnd)) ? (0x6C - sizeof(CWnd)) : 1];
    bool m_shellCreated = false;

public:
    CDamageRankHitShell() {
        reinterpret_cast<void(__thiscall*)(CWnd*)>(0x009DE383)(this);
    }

    bool EnsureCreated() {
        if (m_shellCreated) {
            return true;
        }

        if (!CWndMan::IsInstantiated()) {
            return false;
        }

        CreateWnd(-30000, -30000, 1, 1, kLayerZ, 1, nullptr, 0);
        m_shellCreated = true;
        HideNativeLayer();
        SetHitRect(-30000, -30000, 0, 0);
        return true;
    }

    void Sync(int x, int y, int width, int height, bool enabled) {
        if (!EnsureCreated()) {
            return;
        }

        if (!enabled || width <= 0 || height <= 0) {
            SetHitRect(-30000, -30000, 0, 0);
            return;
        }

        SetHitRect(x, y, width, height);
    }

private:
    void HideNativeLayer() {
        if (m_pLayer) {
            m_pLayer->visible = 0;
            m_pLayer->color = 0;
        }
        if (m_pOverlabLayer) {
            m_pOverlabLayer->visible = 0;
            m_pOverlabLayer->color = 0;
        }
    }

    void SetHitRect(int x, int y, int width, int height) {
        if (width < 0) {
            width = 0;
        }
        if (height < 0) {
            height = 0;
        }

        m_width = width;
        m_height = height;

        const int layerWidth = (width > 0) ? width : 1;
        const int layerHeight = (height > 0) ? height : 1;

        if (m_pLayer) {
            m_pLayer->width = layerWidth;
            m_pLayer->height = layerHeight;
            m_pLayer->visible = 0;
            m_pLayer->color = 0;
            m_pLayer->RelMove(x, y);
        }

        if (m_pOverlabLayer) {
            m_pOverlabLayer->width = layerWidth;
            m_pOverlabLayer->height = layerHeight;
            m_pOverlabLayer->visible = 0;
            m_pOverlabLayer->color = 0;
            m_pOverlabLayer->RelMove(x, y);
        }
    }
};

CDamageRankHitShell& GetDamageRankHitShell() {
    static CDamageRankHitShell shell;
    return shell;
}

void SyncDamageRankHitShell(int x, int y, int width, int height, bool visible) {
    GetDamageRankHitShell().Sync(x, y, width, height, visible);
}
} // namespace

bool DamageRank_HandleMouseMessage(
        UINT msg,
        WPARAM wParam,
        LPARAM lParam,
        LRESULT* /*plResult*/) {
    if (msg != WM_LBUTTONDOWN &&
            msg != WM_LBUTTONUP &&
            msg != WM_MOUSEMOVE &&
            msg != WM_MOUSEWHEEL) {
        return false;
    }

    static bool s_mouseDownOnDamageRank = false;
    auto& ui = CUIDamageRank::GetInstance();

    if (msg == WM_MOUSEWHEEL) {
        POINT pt = {};
        if (GetInputCursorPointForDamageRank(pt)) {
            const int wheelDelta = static_cast<short>((wParam >> 16) & 0xFFFF);
            ui.HandleMouseWheel(static_cast<int>(pt.x), static_cast<int>(pt.y), wheelDelta);
        }
        return false;
    }

    const int screenX = static_cast<short>(lParam & 0xFFFF);
    const int screenY = static_cast<short>((lParam >> 16) & 0xFFFF);

    if (msg == WM_LBUTTONDOWN) {
        s_mouseDownOnDamageRank = false;
        if (ui.IsVisible() && ui.IsScreenPointInside(screenX, screenY)) {
            s_mouseDownOnDamageRank = true;
            ui.HandleMouseLButtonDown(screenX, screenY);
            return true;
        }
        return false;
    }

    if (msg == WM_MOUSEMOVE) {
        if (ui.IsVisible() &&
                (s_mouseDownOnDamageRank ||
                 ui.IsInputCaptured() ||
                 ui.IsScreenPointInside(screenX, screenY))) {
            ui.HandleMouseMove(screenX, screenY);
        }
        return false;
    }

    if (msg == WM_LBUTTONUP) {
        if (s_mouseDownOnDamageRank ||
                ui.IsInputCaptured() ||
                (ui.IsVisible() && ui.IsScreenPointInside(screenX, screenY))) {
            ui.HandleMouseLButtonUp(screenX, screenY);
        }
        s_mouseDownOnDamageRank = false;
        return false;
    }

    return false;
}

CDamageRankData& CDamageRankData::GetInstance() {
    static CDamageRankData instance;
    return instance;
}

void CDamageRankData::Reset() {
    m_players.clear();
    m_skills.clear();
}

void CDamageRankData::UpsertPlayer(int charId, const std::string& name, int job, unsigned long long totalDamage) {
    auto& entry = m_players[charId];
    entry.charId = charId;
    entry.name = name;
    entry.job = job;
    entry.totalDamage = totalDamage;
}

void CDamageRankData::UpsertSkill(
        int skillId,
        const std::string& skillName,
        unsigned long long deltaDamage,
        unsigned long long totalDamage,
        unsigned long long maxDamage,
        unsigned long long minDamage,
        unsigned int count) {
    auto& entry = m_skills[skillId];
    entry.skillId = skillId;
    entry.skillName = skillName;
    entry.deltaDamage = deltaDamage;
    entry.totalDamage = totalDamage;
    entry.maxDamage = maxDamage;
    entry.minDamage = minDamage;
    entry.count = count;
}

std::vector<CDamageRankData::PlayerEntry> CDamageRankData::SnapshotPlayers() const {
    std::vector<PlayerEntry> values;
    values.reserve(m_players.size());
    for (const auto& pair : m_players) {
        values.push_back(pair.second);
    }
    return values;
}

std::vector<CDamageRankData::SkillEntry> CDamageRankData::SnapshotSkills() const {
    std::vector<SkillEntry> values;
    values.reserve(m_skills.size());
    for (const auto& pair : m_skills) {
        values.push_back(pair.second);
    }
    return values;
}

CUIDamageRank& CUIDamageRank::GetInstance() {
    static CUIDamageRank instance;
    return instance;
}

std::wstring CUIDamageRank::ToWide(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }

    const int required = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return std::wstring();
    }

    std::wstring result;
    result.resize(static_cast<size_t>(required - 1));
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &result[0], required - 1);
    return result;
}

std::wstring CUIDamageRank::FormatComma(unsigned long long value) {
    std::wstring s = std::to_wstring(value);
    for (int pos = static_cast<int>(s.size()) - 3; pos > 0; pos -= 3) {
        s.insert(static_cast<size_t>(pos), L",");
    }
    return s;
}

void CUIDamageRank::SendControlOpen() {
    SendDamageRankControl(kDamageRankOpen);
}

void CUIDamageRank::SendControlReset() {
    SendDamageRankControl(kDamageRankReset);
}

void CUIDamageRank::SendControlClose() {
    SendDamageRankControl(kDamageRankClose);
}

void CUIDamageRank::PlayUISound(const wchar_t* soundName) {
    if (!soundName || !*soundName) {
        return;
    }

    using PlayUISoundRawFn = void(__cdecl*)(const wchar_t*);
    auto fnPlayUISound = reinterpret_cast<PlayUISoundRawFn>(0x00989588);
    fnPlayUISound(soundName);
}

void CUIDamageRank::ToggleByHotkey() {
    auto& instance = GetInstance();
    if (!instance.EnsureCreated()) {
        MessageBoxA(nullptr, "DamageRank layer creation failed.", "BeiDou", MB_ICONERROR);
        return;
    }

    if (!instance.IsVisible()) {
        SendControlOpen();
        instance.SetMode(Mode::Player);
        instance.m_minimized = false;
        instance.SetVisible(true);
        instance.Redraw();
        return;
    }

    if (instance.m_minimized) {
        instance.m_minimized = false;
        instance.SetMode(Mode::Player);
        instance.Redraw();
        return;
    }

    if (instance.GetMode() == Mode::Player) {
        instance.SetMode(Mode::Skill);
        instance.Redraw();
        return;
    }

    instance.m_minimized = true;
    instance.Redraw();
}

bool CUIDamageRank::IsPanelOpen() {
    auto& instance = GetInstance();
    return instance.IsVisible() && !instance.m_minimized;
}

void CUIDamageRank::ClosePanel() {
    auto& instance = GetInstance();
    if (!instance.IsVisible()) {
        return;
    }
    SendControlClose();
    instance.SetVisible(false);
    instance.m_minimized = false;
    instance.m_mode = Mode::Player;
}

void CUIDamageRank::ToggleBySidebar() {
    if (IsPanelOpen()) {
        ClosePanel();
        return;
    }
    auto& instance = GetInstance();
    if (!instance.EnsureCreated()) {
        return;
    }
    SendControlOpen();
    instance.SetMode(Mode::Player);
    instance.m_minimized = false;
    instance.SetVisible(true);
    instance.Redraw();
}

bool CUIDamageRank::EnsureCreated() {
    if (m_created && m_layer) {
        return true;
    }

    if (m_created && !m_layer) {
        m_created = false;
    }

    if (!EnsureFonts()) {
        return false;
    }

    if (!EnsureAssetsLoaded()) {
    }

    if (!CreateLayer()) {
        return false;
    }

    m_created = true;
    SetVisible(false);
    return true;
}

bool CUIDamageRank::EnsureFonts() {
    if (m_fontMain && m_fontOutline) {
        return true;
    }

    const bool outlineOk = CreateFontObject(m_fontOutline, L"Arial", 0xFF000000);
    const bool mainOk = CreateFontObject(m_fontMain, L"Arial", 0xFFFFFFFF);

    return outlineOk && mainOk;
}

bool CUIDamageRank::LoadCanvasByUol(const char* uol, IWzCanvasPtr& outCanvas) {
    outCanvas = IWzCanvasPtr();

    auto rm = get_rm();
    if (!rm || !uol || !*uol) {
        return false;
    }

    try {
        Ztl_variant_t v1(vtMissing);
        Ztl_variant_t v2(vtMissing);
        Ztl_variant_t obj = rm->GetObjectA(Ztl_bstr_t(uol), v1, v2);

        IUnknown* unk = obj.GetUnknown(false, false);
        if (!unk) {
            return false;
        }

        IWzCanvas* rawCanvas = nullptr;
        const HRESULT hr = unk->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&rawCanvas));
        if (FAILED(hr) || !rawCanvas) {
            return false;
        }

        outCanvas = rawCanvas;
        rawCanvas->Release();
        return (outCanvas != nullptr);
    } catch (...) {
        return false;
    }
}

bool CUIDamageRank::LoadCanvasByAnyUol(
        IWzCanvasPtr& outCanvas,
        std::initializer_list<const char*> uols) {
    outCanvas = IWzCanvasPtr();

    for (const char* uol : uols) {
        IWzCanvasPtr canvas;
        if (LoadCanvasByUol(uol, canvas) && canvas) {
            outCanvas = canvas;
            return true;
        }
    }

    return false;
}

void CUIDamageRank::LogCanvasInfo(const char* tag, const IWzCanvasPtr& canvas) {
    if (!canvas) {
        return;
    }

    try {
        const unsigned int w = canvas->Getwidth();
        const unsigned int h = canvas->Getheight();
    } catch (...) {
    }
}

bool CUIDamageRank::EnsureAssetsLoaded() {
    if (m_assetsLoadTried) {
        return m_assetsReady;
    }

    m_assetsLoadTried = true;
    bool ok = true;

    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/backgrndmax", m_bgMax);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/backgrndmin", m_bgMin);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/backgrndcenter", m_bgCenter);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/backgrndbottom", m_bgBottom);

    LoadCanvasByUol("UI/UIWindow.img/DamageRank/backgrndbottomover", m_bgBottomOver);

    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/prev0", m_scrPrev);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/next0", m_scrNext);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/base", m_scrBase);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/thumb0", m_scrThumb);

    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/prev1", m_scrPrevPressed);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/next1", m_scrNextPressed);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/thumb1", m_scrThumbPressed);

    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/prev2", m_scrPrevMouseOver);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/next2", m_scrNextMouseOver);
    LoadCanvasByUol("UI/Basic.img/VScr4/enabled/thumb2", m_scrThumbMouseOver);

    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/title1", m_title1);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/title2", m_title2);

    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/commonl", m_gaugeCommonL);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/commonc", m_gaugeCommonC);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/commonr", m_gaugeCommonR);

    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/selfl", m_gaugeSelfL);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/selfc", m_gaugeSelfC);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/gauge/selfr", m_gaugeSelfR);

    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/iconCommonAtk", m_iconCommonAtk);
    ok &= LoadCanvasByUol("UI/UIWindow.img/DamageRank/iconUnknownSkill", m_iconUnknownSkill);

    LoadCanvasByAnyUol(m_btnReset, { "UI/UIWindow.img/DamageRank/BtReset/normal/0" });
    LoadCanvasByAnyUol(m_btnResetMouseOver, { "UI/UIWindow.img/DamageRank/BtReset/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnResetPressed, { "UI/UIWindow.img/DamageRank/BtReset/pressed/0" });
    LoadCanvasByAnyUol(m_btnResetDisabled, { "UI/UIWindow.img/DamageRank/BtReset/disabled/0" });
    LogCanvasInfo("btnReset", m_btnReset);
    LogCanvasInfo("btnResetMouseOver", m_btnResetMouseOver);
    LogCanvasInfo("btnResetPressed", m_btnResetPressed);
    LogCanvasInfo("btnResetDisabled", m_btnResetDisabled);

    LoadCanvasByAnyUol(m_btnSwitch, { "UI/UIWindow.img/DamageRank/BtSwitch/normal/0" });
    LoadCanvasByAnyUol(m_btnSwitchMouseOver, { "UI/UIWindow.img/DamageRank/BtSwitch/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnSwitchPressed, { "UI/UIWindow.img/DamageRank/BtSwitch/pressed/0" });
    LoadCanvasByAnyUol(m_btnSwitchDisabled, { "UI/UIWindow.img/DamageRank/BtSwitch/disabled/0" });
    LogCanvasInfo("btnSwitch", m_btnSwitch);
    LogCanvasInfo("btnSwitchMouseOver", m_btnSwitchMouseOver);
    LogCanvasInfo("btnSwitchPressed", m_btnSwitchPressed);
    LogCanvasInfo("btnSwitchDisabled", m_btnSwitchDisabled);

    LoadCanvasByAnyUol(m_btnAuto, { "UI/UIWindow.img/DamageRank/BtAuto/normal/0" });
    LoadCanvasByAnyUol(m_btnAutoMouseOver, { "UI/UIWindow.img/DamageRank/BtAuto/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnAutoPressed, { "UI/UIWindow.img/DamageRank/BtAuto/pressed/0" });
    LoadCanvasByAnyUol(m_btnAutoDisabled, { "UI/UIWindow.img/DamageRank/BtAuto/disabled/0" });
    LogCanvasInfo("btnAuto", m_btnAuto);
    LogCanvasInfo("btnAutoMouseOver", m_btnAutoMouseOver);
    LogCanvasInfo("btnAutoPressed", m_btnAutoPressed);
    LogCanvasInfo("btnAutoDisabled", m_btnAutoDisabled);

    LoadCanvasByAnyUol(m_btnMin, { "UI/Basic.img/BtMin/normal/0" });
    LoadCanvasByAnyUol(m_btnMinMouseOver, { "UI/Basic.img/BtMin/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnMinPressed, { "UI/Basic.img/BtMin/pressed/0" });
    LogCanvasInfo("btnMin", m_btnMin);
    LogCanvasInfo("btnMinMouseOver", m_btnMinMouseOver);
    LogCanvasInfo("btnMinPressed", m_btnMinPressed);

    LoadCanvasByAnyUol(m_btnMax, { "UI/Basic.img/BtMax/normal/0" });
    LoadCanvasByAnyUol(m_btnMaxMouseOver, { "UI/Basic.img/BtMax/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnMaxPressed, { "UI/Basic.img/BtMax/pressed/0" });
    LogCanvasInfo("btnMax", m_btnMax);
    LogCanvasInfo("btnMaxMouseOver", m_btnMaxMouseOver);
    LogCanvasInfo("btnMaxPressed", m_btnMaxPressed);

    LoadCanvasByAnyUol(m_btnClose, { "UI/Basic.img/BtClose/normal/0" });
    LoadCanvasByAnyUol(m_btnCloseMouseOver, { "UI/Basic.img/BtClose/mouseOver/0" });
    LoadCanvasByAnyUol(m_btnClosePressed, { "UI/Basic.img/BtClose/pressed/0" });
    LogCanvasInfo("btnClose", m_btnClose);
    LogCanvasInfo("btnCloseMouseOver", m_btnCloseMouseOver);
    LogCanvasInfo("btnClosePressed", m_btnClosePressed);

    m_assetsReady = ok;
    return m_assetsReady;
}

IWzCanvasPtr CUIDamageRank::GetSkillIconCanvas(int skillId) {
    // Negative skill ids are virtual damage categories.
    // Example: -1 = DoT Damage.
    // Do not try Skill/<job>.img lookup.
    if (skillId < 0) {
        return IWzCanvasPtr();
    }

    if (skillId == 0) {
        return m_iconCommonAtk ? m_iconCommonAtk : m_iconUnknownSkill;
    }

    auto it = m_skillIconCache.find(skillId);
    if (it != m_skillIconCache.end()) {
        return it->second ? it->second : m_iconUnknownSkill;
    }

    IWzCanvasPtr icon;

    // v83/v95 style candidate:
    // Skill/<job>.img/skill/<skillId>/icon
    const int skillRoot = skillId / 10000;

    char uol[256];
    sprintf_s(uol, "Skill/%d.img/skill/%d/icon", skillRoot, skillId);

    if (!LoadCanvasByUol(uol, icon) || !icon) {

        // Cache fallback too, so we don't retry every redraw.
        m_skillIconCache[skillId] = IWzCanvasPtr();
        return m_iconUnknownSkill;
    }
    m_skillIconCache[skillId] = icon;
    return icon;
}

bool CUIDamageRank::CreateLayer() {
    if (m_layer) {
        return true;
    }

    auto gr = get_gr();
    if (!gr) {
        return false;
    }

    IWzCanvasPtr canvas = BuildCanvas();
    if (!canvas) {
        return false;
    }

    // Create at local 0,0 first. Do not bake screen position into creation coords.
    m_layer = gr->CreateLayer(
            0,
            0,
            kLayerWidth,
            kLayerHeightExpanded,
            kLayerZ,
            static_cast<IUnknown*>(canvas),
            vtMissing);

    if (!m_layer) {
        return false;
    }

    IUnknownPtr hudOrigin = DamageRankHud::GetScreenOriginLt();
    if (hudOrigin) {
        m_layer->origin = static_cast<IUnknown*>(hudOrigin);
    }

    m_layer->width = kLayerWidth;
    m_layer->height = kLayerHeightExpanded;
    m_layer->color = 0xFFFFFFFF;
    m_layer->visible = 0;

    // Position relative to HUD origin, not world.
    m_layer->RelMove(m_posX, m_posY);

    SyncDamageRankHitShell(m_posX, m_posY, kLayerWidth, GetCurrentLayerHeight(), m_visible);
    return true;
}

void CUIDamageRank::SetVisible(bool visible) {
    m_visible = visible;

    if (m_layer) {
        m_layer->visible = visible ? 1 : 0;
    }

    if (!visible) {
        m_bottomRollDragging = false;
        m_bottomRollHover = false;
        m_scrollThumbDragging = false;
        m_scrollPressedArmed = false;
        m_hoveredScrollPart = ScrollPart::None;
        m_pressedScrollPart = ScrollPart::None;
        SetGameCursorState(0);

        SetPlayerGaugeLayersVisible(false);
    } else if (m_mode == Mode::Player && !m_minimized) {
        SetPlayerGaugeLayersVisible(true);
    }

    SyncDamageRankHitShell(m_posX, m_posY, kLayerWidth, GetCurrentLayerHeight(), m_visible);
}

bool CUIDamageRank::IsVisible() const {
    return m_visible;
}

bool CUIDamageRank::IsScreenPointInside(int screenX, int screenY) const {
    if (!m_visible) {
        return false;
    }

    const int height = GetCurrentLayerHeight();
    const int localX = screenX - m_posX;
    const int localY = screenY - m_posY;

    return localX >= 0 &&
           localY >= 0 &&
           localX < kLayerWidth &&
           localY < height;
}

bool CUIDamageRank::IsInputCaptured() const {
    return m_dragging ||
           m_bottomRollDragging ||
           m_scrollThumbDragging ||
           m_scrollPressedArmed ||
           m_pressedButtonArmed;
}

void CUIDamageRank::ResetOnStageChange() {
    m_visible = false;
    m_dragging = false;
    m_bottomRollDragging = false;
    m_bottomRollHover = false;
    m_scrollThumbDragging = false;
    m_scrollPressedArmed = false;
    m_hoveredScrollPart = ScrollPart::None;
    m_pressedScrollPart = ScrollPart::None;
    m_pressedButtonArmed = false;

    ClearPlayerGaugeLayers();
    SyncDamageRankHitShell(0, 0, 0, 0, false);

    if (m_layer) {
        m_layer->visible = 0;
    }
    m_layer = nullptr;
    m_created = false;
}

void CUIDamageRank::OnMapTransition() {
    if (!m_visible) {
        return;
    }

    const bool wasMinimized = m_minimized;
    const Mode savedMode = m_mode;

    m_dragging = false;
    m_bottomRollDragging = false;
    m_bottomRollHover = false;
    m_scrollThumbDragging = false;
    m_scrollPressedArmed = false;
    m_hoveredScrollPart = ScrollPart::None;
    m_pressedScrollPart = ScrollPart::None;
    m_pressedButtonArmed = false;

    ClearPlayerGaugeLayers();
    if (m_layer) {
        m_layer->visible = 0;
    }
    m_layer = nullptr;
    m_created = false;

    DamageRankHud::RefreshOrigin();

    if (!EnsureCreated()) {
        m_visible = false;
        SyncDamageRankHitShell(0, 0, 0, 0, false);
        return;
    }

    m_minimized = wasMinimized;
    SetMode(savedMode);
    SetVisible(true);
    Redraw();
}

void CUIDamageRank::SetMode(Mode mode) {
    m_mode = mode;
}

CUIDamageRank::Mode CUIDamageRank::GetMode() const {
    return m_mode;
}

int CUIDamageRank::GetPlayerDisplayRows(size_t playerCount) const {
    const int count = static_cast<int>(playerCount);

    if (count <= 0) {
        return 0;
    }

    const int autoRows = GetPlayerVisibleRows(playerCount);

    if (m_autoMode) {
        return autoRows;
    }

    int rows = m_playerDisplayRows;

    if (rows <= 0) {
        rows = autoRows;
    }

    if (rows < 1) {
        rows = 1;
    }

    if (rows > kMaxPlayerRows) {
        rows = kMaxPlayerRows;
    }

    // Scrollbar is not implemented yet, so do not expose empty rows.
    if (rows > count) {
        rows = count;
    }

    return rows;
}

void CUIDamageRank::ClampPlayerDisplayRows() {
    const size_t playerCount =
            CDamageRankData::GetInstance().SnapshotPlayers().size();

    const int count = static_cast<int>(playerCount);

    if (count <= 0) {
        m_playerDisplayRows = 0;
        return;
    }

    if (m_playerDisplayRows <= 0) {
        m_playerDisplayRows = GetPlayerVisibleRows(playerCount);
    }

    if (m_playerDisplayRows < 1) {
        m_playerDisplayRows = 1;
    }

    if (m_playerDisplayRows > kMaxPlayerRows) {
        m_playerDisplayRows = kMaxPlayerRows;
    }

    if (m_playerDisplayRows > count) {
        m_playerDisplayRows = count;
    }
}

int CUIDamageRank::GetSkillDisplayRows(size_t skillCount) const {
    const int count = static_cast<int>(skillCount);

    if (count <= 0) {
        return 0;
    }

    const int autoRows = GetSkillVisibleRows(skillCount);

    if (m_autoMode) {
        return autoRows;
    }

    int rows = m_skillDisplayRows;

    if (rows <= 0) {
        rows = autoRows;
    }

    if (rows < 1) {
        rows = 1;
    }

    if (rows > kMaxSkillRows) {
        rows = kMaxSkillRows;
    }

    // Scrollbar is not implemented yet, so do not expose empty rows.
    if (rows > count) {
        rows = count;
    }

    return rows;
}

void CUIDamageRank::ClampSkillDisplayRows() {
    const size_t skillCount =
            CDamageRankData::GetInstance().SnapshotSkills().size();

    const int count = static_cast<int>(skillCount);

    if (count <= 0) {
        m_skillDisplayRows = 0;
        return;
    }

    if (m_skillDisplayRows <= 0) {
        m_skillDisplayRows = GetSkillVisibleRows(skillCount);
    }

    if (m_skillDisplayRows < 1) {
        m_skillDisplayRows = 1;
    }

    if (m_skillDisplayRows > kMaxSkillRows) {
        m_skillDisplayRows = kMaxSkillRows;
    }

    if (m_skillDisplayRows > count) {
        m_skillDisplayRows = count;
    }
}

int CUIDamageRank::GetCurrentRollItemCount() const {
    if (m_mode == Mode::Player) {
        return static_cast<int>(
                CDamageRankData::GetInstance().SnapshotPlayers().size());
    }

    return static_cast<int>(
            CDamageRankData::GetInstance().SnapshotSkills().size());
}

int CUIDamageRank::GetCurrentRollPitch() const {
    return (m_mode == Mode::Player) ? kPlayerRowPitch : kSkillRowHeight;
}

int CUIDamageRank::GetCurrentRollRows() const {
    if (m_mode == Mode::Player) {
        return GetPlayerDisplayRows(
                CDamageRankData::GetInstance().SnapshotPlayers().size());
    }

    return GetSkillDisplayRows(
            CDamageRankData::GetInstance().SnapshotSkills().size());
}

void CUIDamageRank::SetCurrentRollRows(int rows) {
    if (m_mode == Mode::Player) {
        m_playerDisplayRows = rows;
    } else {
        m_skillDisplayRows = rows;
    }
}

void CUIDamageRank::ClampCurrentRollRows() {
    if (m_mode == Mode::Player) {
        ClampPlayerDisplayRows();
    } else {
        ClampSkillDisplayRows();
    }
}

bool CUIDamageRank::HitTestBottomRollArea(int localX, int localY) const {
    if (!m_visible || m_minimized) {
        return false;
    }

    const int height = GetCurrentLayerHeight();

    return localX >= 0 &&
           localX < kLayerWidth &&
           localY >= height - kBottomRollHitHeight &&
           localY < height;
}

void CUIDamageRank::UpdateBottomRollCursor() {
    if (!m_visible || m_minimized) {
        SetGameCursorState(0);
        return;
    }

    if (m_bottomRollDragging) {
        SetGameCursorState(9);
        return;
    }

    if (m_bottomRollHover) {
        SetGameCursorState(7);
        return;
    }

    SetGameCursorState(0);
}

void CUIDamageRank::UpdateScrollCursor() {
    if (!m_visible || m_minimized || !HasCurrentScroll()) {
        SetGameCursorState(0);
        return;
    }

    if (m_scrollThumbDragging || m_scrollPressedArmed) {
        SetGameCursorState(9);
        return;
    }

    if (m_hoveredScrollPart != ScrollPart::None) {
        SetGameCursorState(7);
        return;
    }

    SetGameCursorState(0);
}

int CUIDamageRank::GetCurrentScrollOffset() const {
    return (m_mode == Mode::Player) ? m_playerScrollOffset : m_skillScrollOffset;
}

void CUIDamageRank::SetCurrentScrollOffset(int offset) {
    if (m_mode == Mode::Player) {
        m_playerScrollOffset = offset;
    } else {
        m_skillScrollOffset = offset;
    }
}

int CUIDamageRank::GetCurrentMaxScrollOffset() const {
    const int itemCount = GetCurrentRollItemCount();
    const int rows = GetCurrentRollRows();

    const int maxOffset = itemCount - rows;
    return maxOffset > 0 ? maxOffset : 0;
}

void CUIDamageRank::ClampCurrentScrollOffset() {
    const int maxOffset = GetCurrentMaxScrollOffset();

    int offset = GetCurrentScrollOffset();

    if (offset < 0) {
        offset = 0;
    }

    if (offset > maxOffset) {
        offset = maxOffset;
    }

    SetCurrentScrollOffset(offset);
}

bool CUIDamageRank::HasCurrentScroll() const {
    if (!m_visible || m_minimized) {
        return false;
    }

    // Original-like behavior: scrollbar appears when Auto is OFF.
    if (m_autoMode) {
        return false;
    }

    return GetCurrentMaxScrollOffset() > 0;
}

bool CUIDamageRank::ShouldHideScrollThumb() const {
    if (!HasCurrentScroll()) {
        return true;
    }

    // Original behavior:
    // Hide thumb when display count is less than 3.
    // Player row = 1 display unit.
    // Skill row = 2 display units because skill row is 42px.
    const int displayUnits =
            (m_mode == Mode::Player)
                    ? GetCurrentRollRows()
                    : GetCurrentRollRows() * 2;

    return displayUnits < 3;
}

RECT CUIDamageRank::GetScrollBarRect() const {
    RECT rc = { 0, 0, 0, 0 };

    if (!HasCurrentScroll()) {
        return rc;
    }

    const int height = GetCurrentLayerHeight();

    rc.left = kScrollX;
    rc.top = kRowsStartY;
    rc.right = kScrollX + kScrollWidth;
    rc.bottom = height - kLayerBottomHeight;

    if (rc.bottom < rc.top) {
        rc.bottom = rc.top;
    }

    return rc;
}

RECT CUIDamageRank::GetScrollThumbRect() const {
    RECT bar = GetScrollBarRect();

    RECT thumb = {
        bar.left,
        bar.top + kScrollButtonHeight,
        bar.right,
        bar.top + kScrollButtonHeight
    };

    if (!HasCurrentScroll()) {
        return thumb;
    }

    const int itemCount = GetCurrentRollItemCount();
    const int rows = GetCurrentRollRows();
    const int maxOffset = GetCurrentMaxScrollOffset();

    const int barHeight = bar.bottom - bar.top;
    const int trackTop = bar.top + kScrollButtonHeight;
    const int trackBottom = bar.bottom - kScrollButtonHeight;
    const int trackHeight = trackBottom - trackTop;

    if (itemCount <= 0 || rows <= 0 || barHeight <= 0 || trackHeight <= 0) {
        return thumb;
    }

    int thumbHeight = 25;

    if (m_scrThumb) {
        thumbHeight = static_cast<int>(m_scrThumb->Getheight());
    }

    if (thumbHeight > trackHeight) {
        thumbHeight = trackHeight;
    }

    const int movable = trackHeight - thumbHeight;

    int thumbTop = trackTop;
    if (maxOffset > 0 && movable > 0) {
        thumbTop += (movable * GetCurrentScrollOffset()) / maxOffset;
    }

    thumb.top = thumbTop;
    thumb.bottom = thumbTop + thumbHeight;

    return thumb;
}

int CUIDamageRank::GetScrollOffsetFromThumbY(int thumbTop) const {
    const RECT bar = GetScrollBarRect();
    const RECT thumb = GetScrollThumbRect();

    const int maxOffset = GetCurrentMaxScrollOffset();
    if (maxOffset <= 0) {
        return 0;
    }

    const int trackTop = bar.top + kScrollButtonHeight;
    const int trackBottom = bar.bottom - kScrollButtonHeight;
    const int trackHeight = trackBottom - trackTop;
    const int thumbHeight = thumb.bottom - thumb.top;
    const int movable = trackHeight - thumbHeight;

    if (movable <= 0) {
        return 0;
    }

    int y = thumbTop - trackTop;

    if (y < 0) {
        y = 0;
    }

    if (y > movable) {
        y = movable;
    }

    return (y * maxOffset + movable / 2) / movable;
}

bool CUIDamageRank::HitTestScrollBar(int localX, int localY) const {
    const RECT rc = GetScrollBarRect();

    return localX >= rc.left &&
           localX < rc.right &&
           localY >= rc.top &&
           localY < rc.bottom;
}

bool CUIDamageRank::HitTestScrollThumb(int localX, int localY) const {
    if (ShouldHideScrollThumb()) {
        return false;
    }

    const RECT rc = GetScrollThumbRect();

    return localX >= rc.left &&
           localX < rc.right &&
           localY >= rc.top &&
           localY < rc.bottom;
}

CUIDamageRank::ScrollPart CUIDamageRank::HitTestScrollPart(int localX, int localY) const {
    if (!HasCurrentScroll()) {
        return ScrollPart::None;
    }

    const RECT bar = GetScrollBarRect();

    if (localX < bar.left ||
            localX >= bar.right ||
            localY < bar.top ||
            localY >= bar.bottom) {
        return ScrollPart::None;
    }

    if (localY < bar.top + kScrollButtonHeight) {
        return ScrollPart::Prev;
    }

    if (localY >= bar.bottom - kScrollButtonHeight) {
        return ScrollPart::Next;
    }

    if (!ShouldHideScrollThumb() && HitTestScrollThumb(localX, localY)) {
        return ScrollPart::Thumb;
    }

    return ScrollPart::Track;
}

int CUIDamageRank::GetCurrentLayerHeight() const {
    if (m_minimized) {
        return kLayerHeightMinimized;
    }

    if (m_mode == Mode::Player) {
        const size_t playerCount =
                CDamageRankData::GetInstance().SnapshotPlayers().size();

        const int rows = GetPlayerDisplayRows(playerCount);
        return kRowsStartY + (kPlayerRowPitch * rows) + kLayerBottomHeight;
    }

    const size_t skillCount =
            CDamageRankData::GetInstance().SnapshotSkills().size();

    const int rows = GetSkillDisplayRows(skillCount);
    return kRowsStartY + (kSkillRowHeight * rows) + kLayerBottomHeight;
}

void CUIDamageRank::DrawOutlinedText(IWzCanvasPtr canvas, int x, int y, const std::wstring& text) {
    if (!canvas || !m_fontMain || !m_fontOutline || text.empty()) {
        return;
    }

    Ztl_bstr_t zText(text.c_str());
    Ztl_variant_t alpha((long)180);

    canvas->DrawTextA(x - 1, y - 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x - 1, y, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x - 1, y + 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x, y - 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x, y + 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x + 1, y - 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x + 1, y, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x + 1, y + 1, zText, m_fontOutline, alpha);
    canvas->DrawTextA(x, y, zText, m_fontMain);
}

void CUIDamageRank::DrawTiledBackground(IWzCanvasPtr canvas) {
    if (!canvas) {
        return;
    }

    const int totalHeight = GetCurrentLayerHeight();

    // transparent clear first
    canvas->DrawRectangle(0, 0, kLayerWidth, totalHeight, 0x00FFFFFF);

    if (m_minimized) {
        if (m_bgMin) {
            BlitCanvas(canvas, 0, 0, m_bgMin);
        } else {
            canvas->DrawRectangle(0, 0, kLayerWidth, totalHeight, 0xCC111318);
        }
        return;
    }

    if (m_bgMax) {
        BlitCanvas(canvas, 0, 0, m_bgMax);
    } else {
        canvas->DrawRectangle(0, 0, kLayerWidth, totalHeight, 0xCC111318);
    }

    const int bottomY = totalHeight - kLayerBottomHeight;

    int y = kRowsStartY;
    while (y < bottomY) {
        if (m_bgCenter) {
            BlitCanvas(canvas, 0, y, m_bgCenter);
        }
        y += kPlayerRowHeight;
    }

    IWzCanvasPtr bottomCanvas =
            (m_bottomRollHover || m_bottomRollDragging) && m_bgBottomOver
                    ? m_bgBottomOver
                    : m_bgBottom;

    if (bottomCanvas) {
        BlitCanvas(canvas, 0, bottomY, bottomCanvas);
    }
}

void CUIDamageRank::DrawScrollBar(IWzCanvasPtr canvas) {
    if (!canvas || !HasCurrentScroll()) {
        return;
    }

    const RECT bar = GetScrollBarRect();
    const RECT thumb = GetScrollThumbRect();

    const int barH = bar.bottom - bar.top;
    if (barH <= 0) {
        return;
    }

    const bool prevPressed =
            m_scrollPressedArmed && m_pressedScrollPart == ScrollPart::Prev;
    const bool nextPressed =
            m_scrollPressedArmed && m_pressedScrollPart == ScrollPart::Next;
    const bool thumbPressed =
            (m_scrollPressedArmed && m_pressedScrollPart == ScrollPart::Thumb) ||
            m_scrollThumbDragging;

    const bool prevHover =
            !prevPressed && m_hoveredScrollPart == ScrollPart::Prev;
    const bool nextHover =
            !nextPressed && m_hoveredScrollPart == ScrollPart::Next;
    const bool thumbHover =
            !thumbPressed && m_hoveredScrollPart == ScrollPart::Thumb;

    IWzCanvasPtr prevCanvas =
            prevPressed && m_scrPrevPressed
                    ? m_scrPrevPressed
                    : (prevHover && m_scrPrevMouseOver ? m_scrPrevMouseOver : m_scrPrev);

    IWzCanvasPtr nextCanvas =
            nextPressed && m_scrNextPressed
                    ? m_scrNextPressed
                    : (nextHover && m_scrNextMouseOver ? m_scrNextMouseOver : m_scrNext);

    IWzCanvasPtr thumbCanvas =
            thumbPressed && m_scrThumbPressed
                    ? m_scrThumbPressed
                    : (thumbHover && m_scrThumbMouseOver ? m_scrThumbMouseOver : m_scrThumb);

    const int trackTop = bar.top + kScrollButtonHeight;
    const int trackBottom = bar.bottom - kScrollButtonHeight;

    // Base first.
    if (m_scrBase) {
        for (int y = trackTop; y < trackBottom; y += kScrollButtonHeight) {
            BlitCanvas(canvas, bar.left, y, m_scrBase);
        }
    } else {
        canvas->DrawRectangle(
                bar.left,
                trackTop,
                kScrollWidth,
                trackBottom - trackTop,
                0x30000000);
    }

    // Thumb second.
    if (!ShouldHideScrollThumb()) {
        if (thumbCanvas) {
            BlitCanvas(canvas, thumb.left, thumb.top, thumbCanvas);
        } else {
            canvas->DrawRectangle(
                    thumb.left,
                    thumb.top,
                    thumb.right - thumb.left,
                    thumb.bottom - thumb.top,
                    0x90FFFFFF);
        }
    }

    // Buttons last, so base/thumb never crush arrows.
    if (prevCanvas) {
        BlitCanvas(canvas, bar.left, bar.top, prevCanvas);
    }

    if (nextCanvas) {
        BlitCanvas(canvas, bar.left, bar.bottom - kScrollButtonHeight, nextCanvas);
    }
}

RECT CUIDamageRank::GetButtonRect(ButtonId id) const {
    RECT rc = { 0, 0, 0, 0 };
    switch (id) {
    case kBtnReset:
        rc.left = kResetX;
        rc.top = 4;
        rc.right = kResetX + 27; // asset width
        rc.bottom = 21;          
        break;

    case kBtnSwitch:
        rc.left = kSwitchX;
        rc.top = 4;
        rc.right = kSwitchX + 30; // asset width
        rc.bottom = 20;
        break;

    case kBtnAuto:
        rc.left = kAutoX;
        rc.top = 4;
        rc.right = kAutoX + 21; // asset width
        rc.bottom = 20;
        break;

    case kBtnMinMax:
        rc.left = kMinX;
        rc.top = 4;
        rc.right = kMinX + 12;
        rc.bottom = 20;
        break;

    case kBtnClose:
        rc.left = kCloseX;
        rc.top = 4;
        rc.right = kCloseX + 12;
        rc.bottom = 20;
        break;

    default:
        break;
    }
    return rc;
}

bool CUIDamageRank::HitTestHeaderButton(int localX, int localY, ButtonId& outId) const {
    const ButtonId ids[] = { kBtnReset, kBtnSwitch, kBtnAuto, kBtnMinMax, kBtnClose };
    for (ButtonId id : ids) {
        const RECT rc = GetButtonRect(id);
        if (localX >= rc.left && localX < rc.right && localY >= rc.top && localY < rc.bottom) {
            outId = id;
            return true;
        }
    }
    return false;
}

bool CUIDamageRank::HitTestHeaderDragArea(int localX, int localY) const {
    if (localY < 0 || localY >= 24) {
        return false;
    }

    ButtonId ignored = kBtnReset;
    if (HitTestHeaderButton(localX, localY, ignored)) {
        return false;
    }

    return localX >= 0 && localX < kLayerWidth;
}

void CUIDamageRank::DrawHeaderButton(
        IWzCanvasPtr canvas,
        int x,
        int y,
        int w,
        int h,
        const IWzCanvasPtr& icon,
        const wchar_t* fallbackText,
        unsigned int fillColor) {
    if (icon && BlitCanvas(canvas, x, y, icon)) {
        return;
    }

    canvas->DrawRectangle(x, y, w, h, fillColor);
    DrawOutlinedText(canvas, x + 3, y + 1, fallbackText);
}

bool CUIDamageRank::IsButtonVisualDisabled(ButtonId id) const {
   
    if (m_minimized) {
        if (id == kBtnReset || id == kBtnSwitch || id == kBtnAuto) {
            return true;
        }
    }

    return false;
}

bool CUIDamageRank::IsButtonInteractionDisabled(ButtonId id) const {
    if (m_minimized) {
        if (id == kBtnReset || id == kBtnSwitch || id == kBtnAuto) {
            return true;
        }
    }

    return false;
}

IWzCanvasPtr CUIDamageRank::GetButtonCanvasForDraw(ButtonId id) const {
    const bool isPressed =
            m_pressedButtonArmed &&
            m_hoveredButtonValid &&
            m_pressedButton == id &&
            m_hoveredButton == id;

    const bool isHover =
            !isPressed &&
            m_hoveredButtonValid &&
            m_hoveredButton == id;

    switch (id) {
    case kBtnReset:
        if (m_minimized) {
            return m_btnResetDisabled ? m_btnResetDisabled : m_btnReset;
        }
        if (isPressed && m_btnResetPressed)
            return m_btnResetPressed;
        if (isHover && m_btnResetMouseOver)
            return m_btnResetMouseOver;
        return m_btnReset;

    case kBtnSwitch:
        if (m_minimized) {
            return m_btnSwitchDisabled ? m_btnSwitchDisabled : m_btnSwitch;
        }
        if (isPressed && m_btnSwitchPressed)
            return m_btnSwitchPressed;
        if (isHover && m_btnSwitchMouseOver)
            return m_btnSwitchMouseOver;
        return m_btnSwitch;

    case kBtnAuto:
        if (m_minimized) {
            return m_btnAutoDisabled ? m_btnAutoDisabled : m_btnAuto;
        }

        if (!m_autoMode) {
            if (isPressed && m_btnAutoPressed)
                return m_btnAutoPressed;
            return m_btnAutoDisabled ? m_btnAutoDisabled : m_btnAuto;
        }

        if (isPressed && m_btnAutoPressed)
            return m_btnAutoPressed;
        if (isHover && m_btnAutoMouseOver)
            return m_btnAutoMouseOver;
        return m_btnAuto;

    case kBtnMinMax:
        if (m_minimized) {
            if (isPressed && m_btnMaxPressed)
                return m_btnMaxPressed;
            if (isHover && m_btnMaxMouseOver)
                return m_btnMaxMouseOver;
            return m_btnMax;
        } else {
            if (isPressed && m_btnMinPressed)
                return m_btnMinPressed;
            if (isHover && m_btnMinMouseOver)
                return m_btnMinMouseOver;
            return m_btnMin;
        }

    case kBtnClose:
        if (isPressed && m_btnClosePressed)
            return m_btnClosePressed;
        if (isHover && m_btnCloseMouseOver)
            return m_btnCloseMouseOver;
        return m_btnClose;

    default:
        return IWzCanvasPtr();
    }
}

void CUIDamageRank::DrawHeader(IWzCanvasPtr canvas) {
    if (!canvas) {
        return;
    }

    RECT rc = GetButtonRect(kBtnReset);
    DrawHeaderButton(
            canvas,
            rc.left,
            rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            GetButtonCanvasForDraw(kBtnReset),
            L"R",
            0x80405058);

    const int titleX = rc.right + 5;
    const int titleY = 3;
    DrawOutlinedText(canvas, titleX, titleY, L"Damage Statistic");

    rc = GetButtonRect(kBtnSwitch);
    DrawHeaderButton(
            canvas,
            rc.left,
            rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            GetButtonCanvasForDraw(kBtnSwitch),
            L"S",
            0x80405058);

    rc = GetButtonRect(kBtnAuto);
    DrawHeaderButton(
            canvas,
            rc.left,
            rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            GetButtonCanvasForDraw(kBtnAuto),
            L"A",
            m_autoMode ? 0xA0407058 : 0x80405058);

    rc = GetButtonRect(kBtnMinMax);
    DrawHeaderButton(
            canvas,
            rc.left,
            rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            GetButtonCanvasForDraw(kBtnMinMax),
            m_minimized ? L"+" : L"-",
            0x80405058);

    rc = GetButtonRect(kBtnClose);
    DrawHeaderButton(
            canvas,
            rc.left,
            rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            GetButtonCanvasForDraw(kBtnClose),
            L"X",
            0x80405058);
}

void CUIDamageRank::DrawTitleBand(IWzCanvasPtr canvas) {
    if (!canvas || m_minimized) {
        return;
    }

    IWzCanvasPtr title = (m_mode == Mode::Player) ? m_title1 : m_title2;
    if (!BlitCanvas(canvas, 8, kTitleY, title)) {
        DrawOutlinedText(canvas, 12, 29, m_mode == Mode::Player ? L"title1" : L"title2");
    }
}
void CUIDamageRank::StopVectorAnimation(IWzVector2DPtr vector) {
    if (!vector) {
        return;
    }

    int x = 0;
    int y = 0;
    vector->get_rx(&x);
    vector->get_ry(&y);

    Ztl_variant_t origin = vector->Getorigin();
    vector->Putorigin(vtMissing);
    vector->Putorigin(origin);
    vector->RelMove(x, y);
}

IWzGr2DLayerPtr CUIDamageRank::CreateChildLayerFromCanvas(
        IWzCanvasPtr canvas,
        int x,
        int y,
        int width,
        int height,
        int z) {
    if (!canvas || !m_layer) {
        return IWzGr2DLayerPtr();
    }

    auto gr = get_gr();
    if (!gr) {
        return IWzGr2DLayerPtr();
    }

    IWzGr2DLayerPtr layer = gr->CreateLayer(
            0,
            0,
            width,
            height,
            z,
            static_cast<IUnknown*>(canvas),
            vtMissing);

    if (!layer) {
        return IWzGr2DLayerPtr();
    }

    try {
        Ztl_variant_t overlay(static_cast<IUnknown*>(m_layer.GetInterfacePtr()));
        layer->Putoverlay(overlay);

        IWzVector2DPtr origin = m_layer->Getlt();
        if (origin) {
            Ztl_variant_t vOrigin(static_cast<IUnknown*>(origin.GetInterfacePtr()));
            layer->Putorigin(vOrigin);
        }

        layer->Putcolor(0xFFFFFFFF);
        layer->visible =
                (m_visible && !m_minimized && m_mode == Mode::Player) ? 1 : 0;

        layer->RelMove(x, y);
    } catch (...) {

        if (layer) {
            layer->visible = 0;
        }

        return IWzGr2DLayerPtr();
    }

    return layer;
}

void CUIDamageRank::DrawTextCanvasContent(
        IWzCanvasPtr canvas,
        int width,
        int height,
        const std::wstring& text,
        bool selfStyle) {
    if (!canvas) {
        return;
    }

    canvas->DrawRectangle(0, 0, width, height, 0x00FFFFFF);

    if (text.empty()) {
        return;
    }

    // Keep current font style.
    // This only changes how the existing canvas is refreshed.
    if (selfStyle) {
        DrawOutlinedText(canvas, 1, 0, text);
    } else if (m_fontOutline) {
        canvas->DrawTextA(0, 0, Ztl_bstr_t(text.c_str()), m_fontOutline);
    }
}

void CUIDamageRank::UpdateTextLayerCanvas(
        IWzGr2DLayerPtr layer,
        int height,
        const std::wstring& text,
        bool selfStyle) {
    if (!layer) {
        return;
    }

    try {
        IWzCanvasPtr canvas = layer->Getcanvas(Ztl_variant_t((long)0));
        if (!canvas) {
            return;
        }

        const int width = static_cast<int>(canvas->Getwidth());
        DrawTextCanvasContent(canvas, width, height, text, selfStyle);
    } catch (...) {
    }
}

IWzCanvasPtr CUIDamageRank::BuildTextCanvas(
        int width,
        int height,
        const std::wstring& text,
        bool selfStyle) {
    IWzCanvasPtr canvas;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", canvas, nullptr);
    if (!canvas) {
        return IWzCanvasPtr();
    }

    canvas->Create(width, height, vtMissing, vtMissing);
    DrawTextCanvasContent(canvas, width, height, text, selfStyle);

    return canvas;
}

void CUIDamageRank::ReplaceLayerCanvas(IWzGr2DLayerPtr layer, IWzCanvasPtr canvas) {
    if (!layer || !canvas) {
        return;
    }

    layer->RemoveCanvas(0);
    layer->InsertCanvas(canvas, 0, 255, 255, 100, 100);
}
void CUIDamageRank::SetPlayerGaugeLayersVisible(bool visible) {
    const int flag = visible ? 1 : 0;

    for (auto& pair : m_playerGaugeLayers) {
        auto& g = pair.second;

        if (g.gaugeL) {
            g.gaugeL->visible = flag;
        }
        if (g.gaugeC) {
            g.gaugeC->visible = flag;
        }
        if (g.gaugeR) {
            g.gaugeR->visible = flag;
        }
        if (g.nameText) {
            g.nameText->visible = flag;
        }
        if (g.damageText) {
            g.damageText->visible = flag;
        }
    }
}

void CUIDamageRank::ClearPlayerGaugeLayers() {
    for (auto& pair : m_playerGaugeLayers) {
        auto& g = pair.second;

        if (g.gaugeL) {
            g.gaugeL->visible = 0;
        }
        if (g.gaugeC) {
            g.gaugeC->visible = 0;
        }
        if (g.gaugeR) {
            g.gaugeR->visible = 0;
        }
        if (g.nameText) {
            g.nameText->visible = 0;
        }
        if (g.damageText) {
            g.damageText->visible = 0;
        }
    }

    m_playerGaugeLayers.clear();
}
void CUIDamageRank::SyncPlayerGaugeLayers() {
    if (!m_layer || !m_visible || m_minimized || m_mode != Mode::Player) {
        SetPlayerGaugeLayersVisible(false);
        return;
    }

    EnsureFonts();
    EnsureAssetsLoaded();

    std::vector<CDamageRankData::PlayerEntry> players =
            CDamageRankData::GetInstance().SnapshotPlayers();

    std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.totalDamage > rhs.totalDamage;
    });

    unsigned long long topDamage = 1ULL;
    if (!players.empty() && players.front().totalDamage > 0ULL) {
        topDamage = players.front().totalDamage;
    }

    const char* localNameRaw = GetLocalCharacterNameRaw();
    const std::string localName =
            localNameRaw ? std::string(localNameRaw) : std::string();

    ClampPlayerDisplayRows();
    ClampCurrentScrollOffset();

    const size_t visibleRows =
            static_cast<size_t>(GetPlayerDisplayRows(players.size()));

    const size_t startIndex =
            static_cast<size_t>(m_autoMode ? 0 : m_playerScrollOffset);

    const size_t available =
            players.size() > startIndex ? players.size() - startIndex : 0;

    const size_t count =
            available < visibleRows ? available : visibleRows;

    std::map<int, bool> active;

    const int gaugeStartX = 6;
    const int nameStartX = gaugeStartX + 4;

    const bool scrollShown = HasCurrentScroll();
    const int gaugeMaxWidth = scrollShown ? 194 : 209;
    const int damageEndX = scrollShown ? 194 : 209;

    // Layer height stays 21, but row position advances by 23.
    const int lineHeight = kPlayerRowHeight;
    const int rowPitch = kPlayerRowPitch;

    const long now = static_cast<long>(GetClientUpdateTime());

    for (size_t row = 0; row < count; ++row) {
        const auto& entry = players[startIndex + row];
        active[entry.charId] = true;

        const int y = kRowsStartY + static_cast<int>(row) * rowPitch;

        const bool selfStyle =
                (!localName.empty() && entry.name == localName);

        double ratio = 0.0;
        if (topDamage > 0ULL) {
            ratio =
                    static_cast<double>(entry.totalDamage) /
                    static_cast<double>(topDamage);
        }

        if (ratio < 0.0) {
            ratio = 0.0;
        }
        if (ratio > 1.0) {
            ratio = 1.0;
        }

        int gaugeWidth = static_cast<int>(static_cast<double>(gaugeMaxWidth) * ratio);
        if (gaugeWidth < 1 && entry.totalDamage > 0ULL) {
            gaugeWidth = 1;
        }

        auto& g = m_playerGaugeLayers[entry.charId];

        const bool needRecreateForStyle =
                g.hasStyle && g.selfStyle != selfStyle;

        if (needRecreateForStyle) {
            if (g.gaugeL)
                g.gaugeL->visible = 0;
            if (g.gaugeC)
                g.gaugeC->visible = 0;
            if (g.gaugeR)
                g.gaugeR->visible = 0;
            g.gaugeL = IWzGr2DLayerPtr();
            g.gaugeC = IWzGr2DLayerPtr();
            g.gaugeR = IWzGr2DLayerPtr();
        }

        g.hasStyle = true;
        g.selfStyle = selfStyle;

        IWzCanvasPtr gaugeL = selfStyle ? m_gaugeSelfL : m_gaugeCommonL;
        IWzCanvasPtr gaugeC = selfStyle ? m_gaugeSelfC : m_gaugeCommonC;
        IWzCanvasPtr gaugeR = selfStyle ? m_gaugeSelfR : m_gaugeCommonR;

        if (!gaugeL || !gaugeC || !gaugeR) {
            continue;
        }

        const int spawnY =
                (g.targetY != -1) ? g.targetY : y;


        const int spawnWidth =
                (g.targetWidth != -1) ? g.targetWidth : 1;

        if (!g.gaugeL) {
            g.gaugeL = CreateChildLayerFromCanvas(
                    gaugeL,
                    gaugeStartX,
                    spawnY,
                    1,
                    lineHeight,
                    kLayerZ + 20);
        }

        if (!g.gaugeC) {
            g.gaugeC = CreateChildLayerFromCanvas(
                    gaugeC,
                    gaugeStartX + 1,
                    spawnY,
                    gaugeMaxWidth,
                    lineHeight,
                    kLayerZ + 20);

            if (g.gaugeC && g.gaugeC->Getrb()) {
                g.gaugeC->Getrb()->Putrx(spawnWidth);
            }
        }

        if (!g.gaugeR) {
            g.gaugeR = CreateChildLayerFromCanvas(
                    gaugeR,
                    gaugeStartX + spawnWidth + 1,
                    spawnY,
                    1,
                    lineHeight,
                    kLayerZ + 20);
        }

        std::wstring nameText = ToWide(TrimName(entry.name, 11));
        const std::wstring damageText = FormatComma(entry.totalDamage);

        const int nameTextWidth = CalcTextWidth(m_fontOutline, nameText) + 20;
        const int damageWidth = CalcTextWidth(m_fontOutline, damageText);

        if (!g.nameText) {
            IWzCanvasPtr textCanvas = BuildTextCanvas(110, lineHeight, nameText, selfStyle);
            g.nameText = CreateChildLayerFromCanvas(
                    textCanvas,
                    nameStartX,
                    spawnY + 2,
                    110,
                    lineHeight,
                    kLayerZ + 30);
        }

        if (!g.damageText) {
            IWzCanvasPtr damageCanvas = BuildTextCanvas(100, lineHeight, damageText, selfStyle);

            int damageStartX = gaugeStartX + spawnWidth + 7;
            if (damageStartX < nameTextWidth) {
                damageStartX = nameTextWidth;
            }
            if (damageStartX > damageEndX - damageWidth) {
                damageStartX = damageEndX - damageWidth;
            }

            g.damageText = CreateChildLayerFromCanvas(
                    damageCanvas,
                    damageStartX,
                    spawnY + 2,
                    100,
                    lineHeight,
                    kLayerZ + 30);
        }

        UpdateTextLayerCanvas(g.nameText, lineHeight, nameText, selfStyle);
        UpdateTextLayerCanvas(g.damageText, lineHeight, damageText, selfStyle);

        int damageTargetX = gaugeStartX + gaugeWidth + 7;
        if (damageTargetX < nameTextWidth) {
            damageTargetX = nameTextWidth;
        }
        if (damageTargetX > damageEndX - damageWidth) {
            damageTargetX = damageEndX - damageWidth;
        }

        const bool rankChanged = (g.targetY != y);
        const bool widthChanged = (g.targetWidth != gaugeWidth);
        const bool damageWidthChanged = (g.lastDamageWidth != damageWidth);

        if (rankChanged || widthChanged || damageWidthChanged) {
            g.targetY = y;
            g.targetWidth = gaugeWidth;
            g.lastDamageWidth = damageWidth;

        if (g.animEndTime <= now || rankChanged) {
                g.animEndTime = now + 500;
            }

            Ztl_variant_t moveTime(g.animEndTime);

            if (g.gaugeL && g.gaugeL->Getlt()) {
                IWzVector2DPtr v = g.gaugeL->Getlt();
                StopVectorAnimation(v);
                v->RelMove(gaugeStartX, y, moveTime);
            }

            if (g.gaugeC && g.gaugeC->Getlt()) {
                IWzVector2DPtr v = g.gaugeC->Getlt();
                StopVectorAnimation(v);
                v->RelMove(gaugeStartX + 1, y, moveTime);
            }

            if (g.gaugeC && g.gaugeC->Getrb()) {
                IWzVector2DPtr v = g.gaugeC->Getrb();
                StopVectorAnimation(v);
                v->RelMove(gaugeWidth, v->Getry(), moveTime);
            }

            if (g.gaugeR && g.gaugeR->Getlt()) {
                IWzVector2DPtr v = g.gaugeR->Getlt();
                StopVectorAnimation(v);
                v->RelMove(gaugeStartX + gaugeWidth + 1, y, moveTime);
            }

            if (g.nameText && g.nameText->Getlt()) {
                IWzVector2DPtr v = g.nameText->Getlt();
                StopVectorAnimation(v);
                v->RelMove(nameStartX, y + 2, moveTime);
            }

            if (g.damageText && g.damageText->Getlt()) {
                IWzVector2DPtr v = g.damageText->Getlt();
                StopVectorAnimation(v);
                v->RelMove(damageTargetX, y + 2, moveTime);
            }
        }

        if (g.gaugeL)
            g.gaugeL->visible = 1;
        if (g.gaugeC)
            g.gaugeC->visible = 1;
        if (g.gaugeR)
            g.gaugeR->visible = 1;
        if (g.nameText)
            g.nameText->visible = 1;
        if (g.damageText)
            g.damageText->visible = 1;
    }

    for (auto it = m_playerGaugeLayers.begin(); it != m_playerGaugeLayers.end();) {
        if (active.find(it->first) == active.end()) {
            auto& g = it->second;
            if (g.gaugeL)
                g.gaugeL->visible = 0;
            if (g.gaugeC)
                g.gaugeC->visible = 0;
            if (g.gaugeR)
                g.gaugeR->visible = 0;
            if (g.nameText)
                g.nameText->visible = 0;
            if (g.damageText)
                g.damageText->visible = 0;

            auto eraseIt = it;
            ++it;
            m_playerGaugeLayers.erase(eraseIt);
        } else {
            ++it;
        }
    }
}
void CUIDamageRank::DrawGaugeBar(
        IWzCanvasPtr canvas,
        int x,
        int y,
        int width,
        bool selfStyle,
        double ratio) {
    if (!canvas || width <= 2) {
        return;
    }

    IWzCanvasPtr left = selfStyle ? m_gaugeSelfL : m_gaugeCommonL;
    IWzCanvasPtr center = selfStyle ? m_gaugeSelfC : m_gaugeCommonC;
    IWzCanvasPtr right = selfStyle ? m_gaugeSelfR : m_gaugeCommonR;

    if (ratio < 0.0) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }

    if (!left || !center || !right) {
        const unsigned int edgeColor = selfStyle ? 0xFFE7B73A : 0xFF9F6A32;
        const unsigned int bgColor = 0xFF2A2E39;
        const unsigned int fgColor = selfStyle ? 0xFFFFCC33 : 0xFFB85C2E;

        const int fillWidth = static_cast<int>(static_cast<double>(width - 2) * ratio);

        canvas->DrawRectangle(x, y, width, 19, 0x20101010);
        canvas->DrawRectangle(x, y + 8, width, 3, edgeColor);
        canvas->DrawRectangle(x + 1, y + 8, width - 2, 3, bgColor);

        if (fillWidth > 0) {
            canvas->DrawRectangle(x + 1, y + 8, fillWidth, 3, fgColor);
        }
        return;
    }

    BlitCanvas(canvas, x, y, left);
    for (int i = 1; i < width - 1; ++i) {
        BlitCanvas(canvas, x + i, y, center);
    }
    BlitCanvas(canvas, x + width - 1, y, right);

    const int fillWidth = static_cast<int>(static_cast<double>(width) * ratio);
    for (int i = 0; i < fillWidth; ++i) {
        if (i == 0) {
            BlitCanvas(canvas, x, y, left);
        } else if (i == width - 1) {
            BlitCanvas(canvas, x + i, y, right);
        } else {
            BlitCanvas(canvas, x + i, y, center);
        }
    }
}


void CUIDamageRank::DrawPlayerPage(IWzCanvasPtr canvas) {
    // Player rows are rendered by SyncPlayerGaugeLayers().
    // Keep this empty so the base canvas only contains background/header/title.
}

void CUIDamageRank::DrawSkillPage(IWzCanvasPtr canvas) {
    if (m_minimized) {
        return;
    }

    std::vector<CDamageRankData::SkillEntry> skills =
            CDamageRankData::GetInstance().SnapshotSkills();

    std::sort(skills.begin(), skills.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.totalDamage > rhs.totalDamage;
    });

    const unsigned long long totalAllDamage = [&skills]() -> unsigned long long {
        unsigned long long sum = 0;
        for (const auto& entry : skills) {
            sum += entry.totalDamage;
        }
        return sum > 0 ? sum : 1ULL;
    }();

    int y = kRowsStartY;

    ClampSkillDisplayRows();
    ClampCurrentScrollOffset();

    const size_t visibleRows =
            static_cast<size_t>(GetSkillDisplayRows(skills.size()));

    const size_t startIndex =
            static_cast<size_t>(m_autoMode ? 0 : m_skillScrollOffset);

    const size_t available =
            skills.size() > startIndex ? skills.size() - startIndex : 0;

    const size_t count =
            available < visibleRows ? available : visibleRows;

    const bool scrollShown = HasCurrentScroll();

    const int rowX = 6;
    const int rowOuterW = scrollShown ? 194 : 210;
    const int rowInnerW = rowOuterW - 2;

    const int textRightX = scrollShown ? 192 : 212;
    const int textMinX = scrollShown ? 126 : 146;

    for (size_t i = 0; i < count; ++i) {
        const auto& entry = skills[startIndex + i];

        canvas->DrawRectangle(rowX, y, rowOuterW, 40, 0x50909090);
        canvas->DrawRectangle(rowX + 1, y + 1, rowInnerW, 38, 0x08FFFFFF);
        canvas->DrawRectangle(rowX, y + 39, rowOuterW, 1, 0x40909090);

        const bool isDotDamage = (entry.skillId == -1);

        if (!isDotDamage) {
            IWzCanvasPtr skillIcon = GetSkillIconCanvas(entry.skillId);
            if (!BlitCanvas(canvas, 10, y + 4, skillIcon)) {
                canvas->DrawRectangle(10, y + 4, 32, 32, 0x30101010);
            }
        }

        const std::wstring skillLabel =
                isDotDamage
                        ? L"DoT Damage"
                        : (entry.skillName.empty()
                                          ? (L"Skill " + std::to_wstring(entry.skillId))
                                          : ToWide(TrimName(entry.skillName, 11)));

        const std::wstring damageText = FormatComma(entry.totalDamage);

        const std::wstring countText =
                isDotDamage
                        ? L""
                        : (std::to_wstring(entry.count) + L" times");

        const double percent =
                (static_cast<double>(entry.totalDamage) * 100.0) /
                static_cast<double>(totalAllDamage);

        wchar_t percentBuf[32] = {};
        swprintf_s(percentBuf, L"%.0f%%", percent);
        const std::wstring percentText(percentBuf);

        DrawOutlinedText(canvas, 48, y + 5, skillLabel);

        if (!countText.empty()) {
            DrawOutlinedText(canvas, 48, y + 22, countText);
        }

        int damageX = textRightX - CalcTextWidth(m_fontMain, damageText);
        if (damageX < textMinX) {
            damageX = textMinX;
        }
        DrawOutlinedText(canvas, damageX, y + 5, damageText);

        int percentX = textRightX - CalcTextWidth(m_fontMain, percentText);
        if (percentX < textMinX) {
            percentX = textMinX;
        }
        DrawOutlinedText(canvas, percentX, y + 22, percentText);

        y += kSkillRowHeight;
    }
}

IWzCanvasPtr CUIDamageRank::BuildCanvas() {
    EnsureFonts();
    EnsureAssetsLoaded();

    IWzCanvasPtr canvas;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", canvas, nullptr);
    if (!canvas) {
        return IWzCanvasPtr();
    }

    const int height = GetCurrentLayerHeight();
    canvas->Create(kLayerWidth, height, vtMissing, vtMissing);

    DrawTiledBackground(canvas);
    DrawHeader(canvas);

    if (!m_minimized) {
        DrawTitleBand(canvas);

        if (m_mode == Mode::Player) {
            DrawPlayerPage(canvas);
        } else {
            DrawSkillPage(canvas);
        }

        DrawScrollBar(canvas);
    }

    return canvas;
}

void CUIDamageRank::UpdateLayerPlacement() {
    if (!m_layer) {
        return;
    }

    // Do not assign x/y directly here.
    // Those are likely being interpreted in world/map space.
    m_layer->RelMove(m_posX, m_posY);

    SyncDamageRankHitShell(m_posX, m_posY, kLayerWidth, GetCurrentLayerHeight(), m_visible);
}

void CUIDamageRank::UpdateLayerSize() {
    if (!m_layer) {
        return;
    }

    m_layer->width = kLayerWidth;
    m_layer->height = GetCurrentLayerHeight();
}

void CUIDamageRank::Redraw() {
    if (!m_layer) {
        return;
    }

    UpdateLayerSize();
    UpdateLayerPlacement();

    IWzCanvasPtr canvas = BuildCanvas();
    if (!canvas) {
        return;
    }

    m_layer->RemoveCanvas(0);
    m_layer->InsertCanvas(canvas, 0, 255, 255, 100, 100);

    UpdateLayerPlacement();

    if (m_mode == Mode::Player && !m_minimized && m_visible) {
        SyncPlayerGaugeLayers();
    } else {
        SetPlayerGaugeLayersVisible(false);
    }
}

bool CUIDamageRank::AnimateTick() {
    if (!m_visible || m_minimized || m_mode != Mode::Player || !m_layer) {
        return false;
    }

    const DWORD now = GetTickCount();
    if (m_lastAnimTick != 0 && now - m_lastAnimTick < 16) {
        return false;
    }
    m_lastAnimTick = now;

    bool changed = false;

    for (auto& pair : m_playerAnim) {
        auto& anim = pair.second;

        const double delta = anim.targetRatio - anim.displayRatio;

        if (delta > -0.003 && delta < 0.003) {
            if (anim.displayRatio != anim.targetRatio) {
                anim.displayRatio = anim.targetRatio;
                changed = true;
            }
            continue;
        }

        if (delta > 0.0) {
            // Grow faster.
            double step = delta * 0.35 + 0.015;
            if (step > delta) {
                step = delta;
            }
            anim.displayRatio += step;
            changed = true;
        } else {
            // Shrink softer.
            double step = (-delta) * 0.22 + 0.008;
            if (step > -delta) {
                step = -delta;
            }
            anim.displayRatio -= step;
            changed = true;
        }

        if (anim.displayRatio < 0.0) {
            anim.displayRatio = 0.0;
        }
        if (anim.displayRatio > 1.0) {
            anim.displayRatio = 1.0;
        }
    }

    if (changed) {
        Redraw();
    }

    return changed;
}

void CUIDamageRank::OnHeaderButton(ButtonId id) {
    PlayUISound(id == kBtnClose ? L"MenuDown" : L"BtMouseClick");

    switch (id) {
    case kBtnReset:
        m_playerAnim.clear();
        m_playerDisplayRows = 0;
        m_skillDisplayRows = 0;
        m_playerScrollOffset = 0;
        m_skillScrollOffset = 0;
        m_scrollThumbDragging = false;
        m_scrollPressedArmed = false;
        m_hoveredScrollPart = ScrollPart::None;
        m_pressedScrollPart = ScrollPart::None;
        m_bottomRollDragging = false;
        m_bottomRollHover = false;
        ClearPlayerGaugeLayers();
        CDamageRankData::GetInstance().Reset();
        SendControlReset();
        SendControlOpen();
        Redraw();
        break;

    case kBtnSwitch:
        if (!m_minimized) {
            m_mode = (m_mode == Mode::Player) ? Mode::Skill : Mode::Player;
            Redraw();
        }
        break;

    case kBtnAuto: {
        const int currentRows = GetCurrentRollRows();

        m_autoMode = !m_autoMode;

        if (!m_autoMode) {
            SetCurrentRollRows(currentRows);
            ClampCurrentRollRows();
            ClampCurrentScrollOffset();
        } else {
            SetCurrentScrollOffset(0);
        }

        Redraw();
        break;
    }

    case kBtnMinMax:
        m_minimized = !m_minimized;

        if (m_minimized) {
            m_bottomRollDragging = false;
            m_bottomRollHover = false;
            m_scrollThumbDragging = false;
            m_scrollPressedArmed = false;
            m_hoveredScrollPart = ScrollPart::None;
            m_pressedScrollPart = ScrollPart::None;
            SetGameCursorState(0);
        }

        Redraw();
        break;

    case kBtnClose:
        SendControlClose();
        SetVisible(false);
        m_minimized = false;
        m_mode = Mode::Player;
        m_dragging = false;
        m_bottomRollDragging = false;
        m_bottomRollHover = false;
        SetGameCursorState(0);
        m_pressedButtonArmed = false;
        m_playerScrollOffset = 0;
        m_skillScrollOffset = 0;
        m_scrollThumbDragging = false;
        m_scrollPressedArmed = false;
        m_hoveredScrollPart = ScrollPart::None;
        m_pressedScrollPart = ScrollPart::None;
        break;

    default:
        break;
    }
}

bool CUIDamageRank::HandleMouseLButtonDown(int screenX, int screenY) {
    if (!m_visible) {
        return false;
    }

    const int height = GetCurrentLayerHeight();
    const int localX = screenX - m_posX;
    const int localY = screenY - m_posY;

    if (localX < 0 || localY < 0 || localX >= kLayerWidth || localY >= height) {
        return false;
    }

    ButtonId id = kBtnReset;
    if (HitTestHeaderButton(localX, localY, id)) {
        if (IsButtonInteractionDisabled(id)) {
            return true;
        }

        m_pressedButton = id;
        m_pressedButtonArmed = true;
        m_dragging = false;

        m_hoveredButton = id;
        m_hoveredButtonValid = true;
        Redraw();
        return true;
    }

    if (HitTestHeaderDragArea(localX, localY)) {
        m_dragging = true;
        m_pressedButtonArmed = false;
        m_dragOffsetX = localX;
        m_dragOffsetY = localY;
        return true;
    }

    const ScrollPart scrollPart = HitTestScrollPart(localX, localY);
    if (scrollPart != ScrollPart::None) {
        m_dragging = false;
        m_pressedButtonArmed = false;
        m_bottomRollDragging = false;
        m_bottomRollHover = false;

        m_scrollPressedArmed = true;
        m_pressedScrollPart = scrollPart;
        m_hoveredScrollPart = scrollPart;

        // Scrollbar uses normal cursor. 7/9 are only for bottom roll resize.
        UpdateScrollCursor();

        const RECT thumb = GetScrollThumbRect();

        if (scrollPart == ScrollPart::Prev) {
            SetCurrentScrollOffset(GetCurrentScrollOffset() - 1);
            ClampCurrentScrollOffset();
            Redraw();
            return true;
        }

        if (scrollPart == ScrollPart::Next) {
            SetCurrentScrollOffset(GetCurrentScrollOffset() + 1);
            ClampCurrentScrollOffset();
            Redraw();
            return true;
        }

        if (scrollPart == ScrollPart::Thumb) {
            m_scrollThumbDragging = true;
            m_scrollThumbGrabOffsetY = localY - thumb.top;
            Redraw();
            return true;
        }

        if (scrollPart == ScrollPart::Track) {
            const int thumbHeight = thumb.bottom - thumb.top;
            const int newOffset =
                    GetScrollOffsetFromThumbY(localY - thumbHeight / 2);

            SetCurrentScrollOffset(newOffset);
            ClampCurrentScrollOffset();
            Redraw();
            return true;
        }
    }

    if (HitTestBottomRollArea(localX, localY)) {
        const int itemCount = GetCurrentRollItemCount();

        int rows = GetCurrentRollRows();
        if (rows <= 0 && itemCount > 0) {
            rows = 1;
        }

        m_autoMode = false;
        SetCurrentRollRows(rows);
        ClampCurrentRollRows();

        m_dragging = false;
        m_pressedButtonArmed = false;
        m_bottomRollDragging = true;
        m_bottomRollHover = true;

        m_rollDragStartY = screenY;
        m_rollDragStartRows = GetCurrentRollRows();

        UpdateBottomRollCursor();

        Redraw();
        return true;
    }

    return false;
}

void CUIDamageRank::UpdateHoveredButtonFromScreenPoint(int screenX, int screenY) {
    m_hoveredButtonValid = false;

    if (!m_visible) {
        return;
    }

    const int height = GetCurrentLayerHeight();
    const int localX = screenX - m_posX;
    const int localY = screenY - m_posY;

    if (localX < 0 || localY < 0 || localX >= kLayerWidth || localY >= height) {
        return;
    }

    ButtonId id = kBtnReset;
    if (HitTestHeaderButton(localX, localY, id)) {
        if (IsButtonVisualDisabled(id)) {
            return;
        }

        m_hoveredButton = id;
        m_hoveredButtonValid = true;
    }
}

bool CUIDamageRank::HandleMouseMove(int screenX, int screenY) {
    if (!m_visible) {
        return false;
    }

    if (m_scrollThumbDragging) {
        const int localY = screenY - m_posY;

        m_scrollPressedArmed = true;
        m_pressedScrollPart = ScrollPart::Thumb;
        m_hoveredScrollPart = ScrollPart::Thumb;

        // Scrollbar itself keeps normal cursor.
        UpdateScrollCursor();

        const int thumbTop = localY - m_scrollThumbGrabOffsetY;
        const int newOffset = GetScrollOffsetFromThumbY(thumbTop);

        if (newOffset != GetCurrentScrollOffset()) {
            SetCurrentScrollOffset(newOffset);
            ClampCurrentScrollOffset();
            Redraw();
        }

        return true;
    }

    if (m_bottomRollDragging) {
        const int itemCount = GetCurrentRollItemCount();

        if (itemCount <= 0) {
            SetCurrentRollRows(0);
            UpdateBottomRollCursor();
            Redraw();
            return true;
        }

        const int pitch = GetCurrentRollPitch();
        const int deltaY = screenY - m_rollDragStartY;
        const int rowDelta = deltaY / pitch;

        int newRows = m_rollDragStartRows + rowDelta;

        const int maxRows =
                (m_mode == Mode::Player) ? kMaxPlayerRows : kMaxSkillRows;

        if (newRows < 1) {
            newRows = 1;
        }

        if (newRows > maxRows) {
            newRows = maxRows;
        }

        if (newRows > itemCount) {
            newRows = itemCount;
        }

        const int oldRows = GetCurrentRollRows();

        if (newRows != oldRows) {
            SetCurrentRollRows(newRows);
            ClampCurrentRollRows();
            ClampCurrentScrollOffset();

            Redraw();
        }

        UpdateBottomRollCursor();
        return true;
    }

    if (m_dragging) {
        m_posX = screenX - m_dragOffsetX;
        m_posY = screenY - m_dragOffsetY;

        if (m_posX < 0) {
            m_posX = 0;
        }
        if (m_posY < 0) {
            m_posY = 0;
        }

        UpdateLayerPlacement();
        return true;
    }

    const bool oldValid = m_hoveredButtonValid;
    const ButtonId oldId = m_hoveredButton;
    const bool oldBottomHover = m_bottomRollHover;
    const ScrollPart oldScrollPart = m_hoveredScrollPart;

    UpdateHoveredButtonFromScreenPoint(screenX, screenY);

    const int height = GetCurrentLayerHeight();
    const int localX = screenX - m_posX;
    const int localY = screenY - m_posY;

    m_hoveredScrollPart = HitTestScrollPart(localX, localY);

    m_bottomRollHover =
            m_hoveredScrollPart == ScrollPart::None &&
            localX >= 0 &&
            localY >= 0 &&
            localX < kLayerWidth &&
            localY < height &&
            HitTestBottomRollArea(localX, localY);

    if (m_hoveredScrollPart != ScrollPart::None || m_scrollPressedArmed || m_scrollThumbDragging) {
        UpdateScrollCursor();
    } else {
        UpdateBottomRollCursor();
    }

    const bool changed =
            (oldValid != m_hoveredButtonValid) ||
            (oldValid && m_hoveredButtonValid && oldId != m_hoveredButton) ||
            (oldBottomHover != m_bottomRollHover) ||
            (oldScrollPart != m_hoveredScrollPart);

    if (changed) {
        if (m_hoveredButtonValid) {
            PlayUISound(L"BtMouseOver");
        }
        Redraw();
    }

    return m_hoveredButtonValid ||
           m_bottomRollHover ||
           m_hoveredScrollPart != ScrollPart::None;
}

bool CUIDamageRank::HandleMouseWheel(int screenX, int screenY, int wheelDelta) {

    if (!m_visible || m_minimized || !HasCurrentScroll()) {
        return false;
    }

    const int localX = screenX - m_posX;
    const int localY = screenY - m_posY;
    const int height = GetCurrentLayerHeight();

    if (localX < 0 ||
            localX >= kLayerWidth ||
            localY < 0 ||
            localY >= height) {
        return false;
    }

    int steps = wheelDelta / WHEEL_DELTA;
    if (steps == 0) {
        steps = (wheelDelta > 0) ? 1 : -1;
    }

    const int oldOffset = GetCurrentScrollOffset();

    SetCurrentScrollOffset(oldOffset - steps);
    ClampCurrentScrollOffset();

    if (GetCurrentScrollOffset() != oldOffset) {
        Redraw();
    }

    return true;
}

bool CUIDamageRank::HandleMouseLButtonUp(int screenX, int screenY) {
    if (!m_visible) {
        return false;
    }

    bool handled = false;
    
    if (m_scrollThumbDragging || m_scrollPressedArmed) {
        m_scrollThumbDragging = false;
        m_scrollPressedArmed = false;
        m_pressedScrollPart = ScrollPart::None;

        const int localX = screenX - m_posX;
        const int localY = screenY - m_posY;

        m_hoveredScrollPart = HitTestScrollPart(localX, localY);

        UpdateScrollCursor();

        handled = true;
        Redraw();
    }

    if (m_bottomRollDragging) {
        m_bottomRollDragging = false;

        const int localX = screenX - m_posX;
        const int localY = screenY - m_posY;

        m_bottomRollHover = HitTestBottomRollArea(localX, localY);

        handled = true;

        UpdateBottomRollCursor();
        Redraw();
    }

    if (m_dragging) {
        m_dragging = false;
        handled = true;
    }

    if (m_pressedButtonArmed) {
        const int height = GetCurrentLayerHeight();
        const int localX = screenX - m_posX;
        const int localY = screenY - m_posY;

        bool clicked = false;
        ButtonId clickedId = kBtnReset;

        if (localX >= 0 && localY >= 0 && localX < kLayerWidth && localY < height) {
            ButtonId id = kBtnReset;
            if (HitTestHeaderButton(localX, localY, id) && id == m_pressedButton) {
                clicked = true;
                clickedId = id;
                handled = true;
            }
        }

        m_pressedButtonArmed = false;
        UpdateHoveredButtonFromScreenPoint(screenX, screenY);

        if (clicked) {
            OnHeaderButton(clickedId);
        } else if (m_visible) {
            Redraw();
        }
    }

    return handled;
}

void CUIDamageRank::OnTrackerReset() {
    m_playerAnim.clear();
    m_playerDisplayRows = 0;
    m_skillDisplayRows = 0;
    m_playerScrollOffset = 0;
    m_skillScrollOffset = 0;

    m_scrollThumbDragging = false;
    m_scrollPressedArmed = false;
    m_hoveredScrollPart = ScrollPart::None;
    m_pressedScrollPart = ScrollPart::None;

    m_bottomRollDragging = false;
    m_bottomRollHover = false;

    ClearPlayerGaugeLayers();
    CDamageRankData::GetInstance().Reset();
}

void CUIDamageRank::OnTrackerPlayer(
        int charId,
        int jobId,
        const std::string& name,
        unsigned long long totalDamage) {

    CDamageRankData::GetInstance().UpsertPlayer(
            charId,
            name.empty() ? (std::string("CID ") + std::to_string(charId)) : name,
            jobId,
            totalDamage);

    if (m_visible && !m_minimized) {
        ClampCurrentScrollOffset();
        Redraw();
    }
}

void CUIDamageRank::OnTrackerSkill(
        int skillId,
        const std::string& skillName,
        unsigned long long deltaDamage,
        unsigned long long totalDamage,
        unsigned long long maxDamage,
        unsigned long long minDamage,
        unsigned int count) {
    CDamageRankData::GetInstance().UpsertSkill(
            skillId,
            skillName,
            deltaDamage,
            totalDamage,
            maxDamage,
            minDamage,
            count);

    if (m_visible && !m_minimized) {
        ClampCurrentScrollOffset();
        Redraw();
    }
}

void DptUi_OnTrackerReset() {
    CUIDamageRank::GetInstance().OnTrackerReset();
}

void DptUi_OnTrackerPlayer(int charId, int jobId, const std::string& name, unsigned long long totalDamage) {
    CUIDamageRank::GetInstance().OnTrackerPlayer(charId, jobId, name, totalDamage);
}

void DptUi_OnTrackerSkill(
        int skillId,
        const std::string& skillName,
        unsigned long long deltaDamage,
        unsigned long long totalDamage,
        unsigned long long maxDamage,
        unsigned long long minDamage,
        unsigned int count) {
    CUIDamageRank::GetInstance().OnTrackerSkill(
            skillId,
            skillName,
            deltaDamage,
            totalDamage,
            maxDamage,
            minDamage,
            count);
}

void DptUi_AnimateTick() {
    CUIDamageRank::GetInstance().AnimateTick();
}