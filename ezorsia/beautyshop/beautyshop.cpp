#include "stdafx.h"
#include "BeautyShopApi.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <memory>

static void BeautyLog(const char* sFormat, ...) {
    char msg[1024];
    va_list args;
    va_start(args, sFormat);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, sFormat, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[1200];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strcat_s(path, MAX_PATH, "beauty_debug.txt");

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(h, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(h);
    }
}

static PVOID g_vehHandle = nullptr;
static volatile LONG g_crashCount = 0;
static volatile LONG g_inHandler = 0;
static LONG CALLBACK BeautyCrashVeh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedCompareExchange(&g_inHandler, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedIncrement(&g_crashCount) > 8) {
        InterlockedExchange(&g_inHandler, 0);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    char modName[MAX_PATH] = "?";
    uintptr_t off = 0;
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &hMod) && hMod) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(hMod, full, MAX_PATH)) {
            char* b = strrchr(full, '\\');
            _snprintf_s(modName, MAX_PATH, _TRUNCATE, "%s", b ? b + 1 : full);
        }
        off = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
    }
    const char* acc = "";
    uintptr_t target = 0;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        acc = ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read";
        target = ep->ExceptionRecord->ExceptionInformation[1];
    }
    BeautyLog("*** CRASH code=0x%08X at %s+0x%X (abs=0x%08X) %s target=0x%08X",
              static_cast<unsigned>(code), modName, static_cast<unsigned>(off),
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr)), acc, static_cast<unsigned>(target));

    CONTEXT* ctx = ep->ContextRecord;
    BeautyLog("    Eip=0x%08X Esp=0x%08X Ebp=0x%08X", ctx->Eip, ctx->Esp, ctx->Ebp);
    uintptr_t dllBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("ezorsia.dll"));
    const DWORD* sp = reinterpret_cast<const DWORD*>(ctx->Esp);
    int logged = 0;
    for (int i = 0; i < 512 && logged < 24; ++i) {
        uintptr_t v = sp[i];
        if (v >= 0x00401000 && v < 0x00A92000) {
            BeautyLog("    stack+0x%-4X MapleStory.exe+0x%X", i * 4, static_cast<unsigned>(v - 0x00400000));
            ++logged;
        } else if (dllBase && v >= dllBase && v < dllBase + 0x100000) {
            BeautyLog("    stack+0x%-4X ezorsia.dll+0x%X", i * 4, static_cast<unsigned>(v - dllBase));
            ++logged;
        }
    }
    BeautyLog("    (end of stack scan, %d frames)", logged);

    InterlockedExchange(&g_inHandler, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

static constexpr int32_t kBeautyShopItemID = 5920000;
static constexpr int     kOpcode_SaveBeauty = 0x174;

static constexpr int kTab_Hair       = 0;
static constexpr int kTab_Face       = 1;
static constexpr int kTab_Skin       = 2;
static constexpr int kAction_Request = 0;
static constexpr int kAction_Save    = 1;
static constexpr int kAction_Apply   = 2;
static constexpr int kAction_Delete  = 3;
static constexpr int kAction_Unlock  = 4;
static constexpr int kResp_Open      = 0;
static constexpr int kResp_Data      = 1;

static constexpr int kMaxSlots = 6;

static constexpr uintptr_t kAddr_ClientSocket_Instance        = 0x00BE7914;
static constexpr uintptr_t kAddr_ClientSocket_SendPacket      = 0x0049637B;
static constexpr uintptr_t kAddr_ClientSocket_ProcessPacket   = 0x004965F1;
static constexpr uintptr_t kAddr_play_ui_sound                = 0x00989588;
static constexpr uintptr_t kAddr_SendConsumeCash              = 0x00A0A63F;
static constexpr uintptr_t kAddr_SendEtcCash                  = 0x00A1DC5B;
static constexpr uintptr_t kAddr_get_consume_cash_item_type   = 0x004863D5;

static constexpr uintptr_t kAddr_CWvsContext_Instance = 0x00BE7918;
static constexpr uintptr_t kAddr_CurrentStage         = 0x00BEDED4;
static constexpr int        kOff_CharacterDataInCtx   = 2094 * 4;
static constexpr uintptr_t kAddr_ZRefCAvatar_Alloc    = 0x00428967;
static constexpr uintptr_t kAddr_ZRef_Release         = 0x00428C15;
static constexpr uintptr_t kAddr_AvatarLook_ctor      = 0x004283FE;
static constexpr uintptr_t kAddr_CAvatar_Init         = 0x0045149F;
static constexpr int kAvatarStandAction = 5;

static constexpr int kAL_Gender  = 0x0C;
static constexpr int kAL_Skin    = 0x0D;
static constexpr int kAL_Face    = 0x11;
static constexpr int kAL_Hair    = 0x19;
static constexpr int kAL_WeaponSticker = 0x15;
static constexpr int kAL_Hat          = 0x1D;
static constexpr int kAL_FaceAcc      = 0x21;
static constexpr int kAL_Weapon       = 0x45;
static constexpr int kAL_WeaponUnseen = 0x115;

static constexpr int kAvatarFeetDX = 60;
static constexpr int kAvatarFeetDY = 87;
static constexpr int kAvatarScale  = 100;

typedef void*(__thiscall* t_ZRefCAvatar_Alloc)(void* zref);
typedef long (__thiscall* t_ZRef_Release)(void* zref, int);
typedef void*(__thiscall* t_AvatarLook_ctor)(void* buf, void* charData);
typedef void (__thiscall* t_CAvatar_Init)(void* avatar, void* look, int ma,
                                          void* origin, void* overlay,
                                          int z, int x, int y, int scale, int emotion);
static auto ZRefCAvatar_Alloc = reinterpret_cast<t_ZRefCAvatar_Alloc>(kAddr_ZRefCAvatar_Alloc);
static auto ZRef_Release      = reinterpret_cast<t_ZRef_Release>(kAddr_ZRef_Release);
static auto AvatarLook_ctor   = reinterpret_cast<t_AvatarLook_ctor>(kAddr_AvatarLook_ctor);
static auto CAvatar_Init      = reinterpret_cast<t_CAvatar_Init>(kAddr_CAvatar_Init);

static unsigned ComputeLookHash(void* charData) {
    if (!charData) return 0;
    unsigned h = 2166136261u;
    __try {
        unsigned char buf[0x210];
        memset(buf, 0, sizeof(buf));
        AvatarLook_ctor(buf, charData);
        for (int i = 0x0C; i < 0x205; ++i) {
            h = (h ^ buf[i]) * 16777619u;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        h = 0;
    }
    if (h == 0) h = 1;
    return h;
}

static auto get_basic_font =
    reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(0x0098A707);

static bool g_enableNameLabels = true;
static bool g_enablePreviewAvatars = true;

static void ResolveBeautyName(int itemID, bool isHair, wchar_t* out, size_t cap) {
    if (cap) out[0] = L'\0';
    if (!g_enableNameLabels) return;
    if (itemID <= 0 || cap == 0) return;
    wchar_t path[160];
    _snwprintf_s(path, _countof(path), _TRUNCATE,
                 L"String/Eqp.img/Eqp/%s/%d/name", isHair ? L"Hair" : L"Face", itemID);
    try {
        Ztl_variant_t v = get_rm()->GetObjectA(path);
        Ztl_variant_t vs;
        if (V_VT(&v) != VT_EMPTY && V_VT(&v) != VT_ERROR &&
            SUCCEEDED(ZComAPI::ZComVariantChangeType(&vs, &v, 0, VT_BSTR)) &&
            V_BSTR(&vs)) {
            wcsncpy_s(out, cap, V_BSTR(&vs), _TRUNCATE);
        }
    } catch (...) {
    }
}

static void DrawFittedName(IWzCanvasPtr pCanvas, const wchar_t* text,
                           int bandLeft, int bandRight, int y) {
    if (!pCanvas || !text || !text[0]) return;
    int bandW = bandRight - bandLeft;
    if (bandW < 8) return;

    static IWzFontPtr s_basic, s_small;
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        try {
            get_basic_font(std::addressof(s_basic), 0);
            int bestH = s_basic ? static_cast<IWzFont*>(s_basic)->Getheight() : 0;
            if (bestH <= 0) bestH = 100;
            for (int t = 1; t < 10; ++t) {
                IWzFontPtr f;
                get_basic_font(std::addressof(f), t);
                if (!f) continue;
                int h = static_cast<IWzFont*>(f)->Getheight();
                if (h > 0 && h < bestH) { bestH = h; s_small = f; }
            }
        } catch (...) {
        }
    }

    try {
        IWzFont* pf = s_basic;
        if (!pf) return;
        wchar_t buf[80];
        wcsncpy_s(buf, _countof(buf), text, _TRUNCATE);

        unsigned int w = pf->CalcTextWidth(Ztl_bstr_t(buf), Ztl_variant_t());
        if ((int)w > bandW && s_small) {
            pf = s_small;
            w = pf->CalcTextWidth(Ztl_bstr_t(buf), Ztl_variant_t());
        }
        if ((int)w > bandW) {
            int fit = pf->CalcLongestText(Ztl_bstr_t(buf), bandW - 8, Ztl_variant_t());
            if (fit < 1) fit = 1;
            if (fit < (int)wcslen(buf)) {
                if (fit > (int)_countof(buf) - 2) fit = _countof(buf) - 2;
                buf[fit] = L'\x2026';
                buf[fit + 1] = L'\0';
            }
            w = pf->CalcTextWidth(Ztl_bstr_t(buf), Ztl_variant_t());
        }

        int nx = bandLeft + (bandW - (int)w) / 2;
        if (nx < bandLeft) nx = bandLeft;
        pCanvas->DrawTextA(nx, y, Ztl_bstr_t(buf), pf, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static auto play_ui_sound = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto ClientSocket_SendPacket = reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(kAddr_ClientSocket_SendPacket);

typedef int(__cdecl* t_CUtilDlg_YesNo)(ZXString<char>, const wchar_t*, void*, int, int);
static auto CUtilDlg_YesNo = reinterpret_cast<t_CUtilDlg_YesNo>(0x00992BFD);
static constexpr int kYesNo_Yes = 6;

using t_TT_Ctor      = void*(__thiscall*)(void*);
using t_TT_Dtor      = void (__thiscall*)(void*);
using t_TT_SetString = void (__thiscall*)(void*, int, int, const char*);
using t_TT_Clear     = void (__thiscall*)(void*);
static auto pTT_Ctor      = reinterpret_cast<t_TT_Ctor>(0x008E49B5);
static auto pTT_Dtor      = reinterpret_cast<t_TT_Dtor>(0x008E6BA3);
static auto pTT_SetString = reinterpret_cast<t_TT_SetString>(0x008E6E7D);
static auto pTT_Clear     = reinterpret_cast<t_TT_Clear>(0x008E6E23);

static void SendBeautyPacket(const COutPacket& oPacket) {
    void* pClientSocket = *reinterpret_cast<void**>(kAddr_ClientSocket_Instance);
    if (pClientSocket) {
        ClientSocket_SendPacket(pClientSocket, oPacket);
    }
}

struct BeautyPacketReader {
    CInPacket* p;
    explicit BeautyPacketReader(CInPacket* pPacket) : p(pPacket) {}
    unsigned char* base() const { return reinterpret_cast<unsigned char*>(p); }
    unsigned char* data() const { return *reinterpret_cast<unsigned char**>(base() + 0x8); }
    unsigned int& offset() const { return *reinterpret_cast<unsigned int*>(base() + 0x14); }
    unsigned short length() const { return *reinterpret_cast<unsigned short*>(base() + 0xC); }
    bool CanRead(size_t n) const { return offset() + n <= length(); }
    unsigned char Decode1() {
        if (!CanRead(1)) return 0;
        unsigned char v = data()[offset()];
        offset() += 1;
        return v;
    }
    int Decode4() {
        if (!CanRead(4)) return 0;
        int v = *reinterpret_cast<int*>(data() + offset());
        offset() += 4;
        return v;
    }
    void Skip2() { offset() += 2; }
};

struct SlotData {
    IWzCanvasPtr pLayer = nullptr;
    IWzCanvasPtr pEnableHM = nullptr;
    int x = 0;
    int y = 0;
    int enableX = 0;
    int enableY = 0;

    bool bIsSaved = false;

    int nGender = 0;
    int nSkin = 0;
    int nHair = 0;
    int nFace = 0;
    int nHat = 0;
    int nTop = 0;
    int nBottom = 0;
    int nShoes = 0;
    int nWeapon = 0;
    int nCashWeapon = 0;

    int hairId = 0;
    bool hasHairData = false;
    int faceId = 0;
    bool hasFaceData = false;
    bool hasSkinData = false;

    wchar_t szName[64] = {0};

    IWzCanvasPtr pDeleteBtn[4];
    RECT rcDeleteBtn{};
    int deleteBtnState = 2;
    int deleteBtnOriginX = 0;
    int deleteBtnOriginY = 0;

    IWzCanvasPtr pApplyHairBtn[4];
    IWzCanvasPtr pApplyFaceBtn[4];
    IWzCanvasPtr pApplySkinBtn[4];
    RECT rcApplyBtn{};
    int applyBtnState = 2;

    IWzCanvasPtr pSaveHairBtn[4];
    IWzCanvasPtr pSaveFaceBtn[4];
    IWzCanvasPtr pSaveSkinBtn[4];
};

class CBeautyShop : public CWnd {
public:
    ZALLOC_GLOBAL

    inline static CBeautyShop* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    IWzCanvasPtr m_pBgCanvas;
    IWzCanvasPtr m_pBgCanvas2;
    IWzCanvasPtr m_pTabHairN, m_pTabHairS;
    IWzCanvasPtr m_pTabFaceN, m_pTabFaceS;
    IWzCanvasPtr m_pTabSkinN, m_pTabSkinS;
    IWzCanvasPtr m_pDisableSlot;
    IWzCanvasPtr m_pBtCloseN, m_pBtCloseM, m_pBtCloseP;

    int m_nCurrentTab;
    int m_nSelectedSlot;
    int m_nUnlockedSlots;
    bool m_bDataReady = false;
    int m_nPressedApplySlot = -1;

    alignas(8) unsigned char m_ttBuf[0x600];
    bool m_bTtInit = false;
    int m_nTtSlot = -1;
    int m_nWndX = 0, m_nWndY = 0;
    unsigned m_lastLookHash = 0;
    void* m_pOpenStage = nullptr;
    int m_nCloseBtnState;
    int m_bDragging;
    int m_nDragAnchorX;
    int m_nDragAnchorY;

    int m_anHair[6];
    int m_anFace[6];
    int m_anSkin[6];

    unsigned int m_avatarRef[6][2];
    void*        m_pSlotAvatar[6];

    SlotData m_hairSlots[6];
    SlotData m_faceSlots[6];
    SlotData m_skinSlots[6];

    RECT m_rcTabHair, m_rcTabFace, m_rcTabSkin;
    RECT m_rcBtClose;
    RECT m_rcSlot[6];

    explicit CBeautyShop(int nPOS, int nID);
    virtual ~CBeautyShop() override;

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int OnMouseMove(int rx, int ry) override;
    virtual void OnDestroy() override;
    virtual void Update() override;
    virtual int OnSetFocus(int /*bFocus*/) override { return 0; }
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        const bool isKeyUp = (lParam & 0x80000000) != 0;
        if (!isKeyUp && wParam == VK_ESCAPE) {
            Destroy();
            return;
        }
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
        if (ctx) {
            reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(0x00A07431)(ctx, wParam, lParam);
        }
    }

    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }

    bool IsSlotLocked(int i) const { return i >= m_nUnlockedSlots; }

    SlotData* SlotsForTab(int tab) {
        if (tab == kTab_Face) return m_faceSlots;
        if (tab == kTab_Skin) return m_skinSlots;
        return m_hairSlots;
    }
    static bool SlotHasData(const SlotData& s, int tab) {
        if (tab == kTab_Face) return s.hasFaceData;
        if (tab == kTab_Skin) return s.hasSkinData;
        return s.hasHairData;
    }
    static IWzCanvasPtr* ApplyBtnsForTab(SlotData& s, int tab) {
        if (tab == kTab_Face) return s.pApplyFaceBtn;
        if (tab == kTab_Skin) return s.pApplySkinBtn;
        return s.pApplyHairBtn;
    }
    static IWzCanvasPtr* SaveBtnsForTab(SlotData& s, int tab) {
        if (tab == kTab_Face) return s.pSaveFaceBtn;
        if (tab == kTab_Skin) return s.pSaveSkinBtn;
        return s.pSaveHairBtn;
    }

    void SelectTab(int nTab);
    void SelectSlot(int nSlot);
    void RebuildAvatars();
    void ReleaseAvatars();
    void BuildSlotAvatar(int i, void* charData);
    void EnsureTooltip();
    void TooltipShowName(int slot, int rx, int ry, const wchar_t* name);
    void TooltipHide();
    void SaveBeauty();
    void HandleServerResponse(CInPacket* pPacket);
    void HandleServerResponseCompat(class CompatInPacket* pPacket);
    void SendSavePacket(int slot, int type);
    void SendApplyPacket(int slot);
    void SendDeletePacket(int slot, int type);
    static IWzCanvasPtr LoadSprite(const wchar_t* sPath);
};

IWzCanvasPtr CBeautyShop::LoadSprite(const wchar_t* sPath) {
    IWzCanvasPtr pCanvas;
    try {
        pCanvas = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(sPath)));
    } catch (...) {}
    return pCanvas;
}

CBeautyShop::CBeautyShop(int nPOS, int nID)
    : m_nCurrentTab(0), m_nSelectedSlot(-1), m_nUnlockedSlots(0), m_nCloseBtnState(0),
      m_bDragging(0), m_nDragAnchorX(0), m_nDragAnchorY(0) {
    memset(m_anHair, 0, sizeof(m_anHair));
    memset(m_anFace, 0, sizeof(m_anFace));
    memset(m_anSkin, 0, sizeof(m_anSkin));
    memset(m_avatarRef, 0, sizeof(m_avatarRef));
    memset(m_pSlotAvatar, 0, sizeof(m_pSlotAvatar));

    ms_pInstance = this;

    int w = 397, h = 370;
    int x = (get_screen_width() - w) / 2;
    int y = (get_screen_height() - h) / 2;
    CreateWnd(x, y, w, h, 10, 1, nullptr, 1);
    m_nWndX = x;
    m_nWndY = y;
    m_pOpenStage = *reinterpret_cast<void**>(kAddr_CurrentStage);

    play_ui_sound(L"MenuUp");

    m_pBgCanvas = LoadSprite(L"UI/New.img/beautyRoom/backgrnd");
    m_pBgCanvas2 = LoadSprite(L"UI/New.img/beautyRoom/backgrnd2");

    m_pTabHairN = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/normal/0");
    m_pTabHairS = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/selected/0");
    m_pTabFaceN = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/normal/1");
    m_pTabFaceS = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/selected/1");
    m_pTabSkinN = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/normal/3");
    m_pTabSkinS = LoadSprite(L"UI/New.img/beautyRoom/tab:Tab/selected/3");

    m_pDisableSlot = LoadSprite(L"UI/New.img/beautyRoom/Slot/disableSlot");

    m_pBtCloseN = LoadSprite(L"UI/New.img/beautyRoom/BtClose/normal/0");
    m_pBtCloseM = LoadSprite(L"UI/New.img/beautyRoom/BtClose/mouseOver/0");
    m_pBtCloseP = LoadSprite(L"UI/New.img/beautyRoom/BtClose/pressed/0");
    m_rcBtClose = {375, 4, 375 + 13, 4 + 13};

    m_rcTabHair = {10, 23, 10 + 43, 42};
    m_rcTabFace = {53, 23, 53 + 43, 42};
    m_rcTabSkin = {96, 23, 96 + 43, 42};

    int slotW = 121, slotH = 153;
    int startX = 15, startY = 44;
    int gapX = 2, gapY = 2;

    IWzCanvasPtr pEnableHM = LoadSprite(L"UI/New.img/beautyRoom/Slot/enableHM");
    IWzCanvasPtr pEnableFM = LoadSprite(L"UI/New.img/beautyRoom/Slot/enableFM");
    IWzCanvasPtr pEnableSM = LoadSprite(L"UI/New.img/beautyRoom/Slot/enableSM");

    IWzCanvasPtr pDeleteBtn[4];
    pDeleteBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:Delete/disabled/0");
    pDeleteBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:Delete/mouseOver/0");
    pDeleteBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:Delete/normal/0");
    pDeleteBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:Delete/pressed/0");

    IWzCanvasPtr pApplyHairBtn[4];
    pApplyHairBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyHair/disabled/0");
    pApplyHairBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyHair/mouseOver/0");
    pApplyHairBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyHair/normal/0");
    pApplyHairBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyHair/pressed/0");

    IWzCanvasPtr pApplyFaceBtn[4];
    pApplyFaceBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyFace/disabled/0");
    pApplyFaceBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyFace/mouseOver/0");
    pApplyFaceBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyFace/normal/0");
    pApplyFaceBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplyFace/pressed/0");

    IWzCanvasPtr pSaveHairBtn[4];
    pSaveHairBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveHair/disabled/0");
    pSaveHairBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveHair/mouseOver/0");
    pSaveHairBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveHair/normal/0");
    pSaveHairBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveHair/pressed/0");
    BeautyLog("sprites SaveHair disabled=%d normal=%d pressed=%d",
              pSaveHairBtn[0] ? 1 : 0, pSaveHairBtn[2] ? 1 : 0, pSaveHairBtn[3] ? 1 : 0);

    IWzCanvasPtr pSaveFaceBtn[4];
    pSaveFaceBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveFace/disabled/0");
    pSaveFaceBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveFace/mouseOver/0");
    pSaveFaceBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveFace/normal/0");
    pSaveFaceBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveFace/pressed/0");

    IWzCanvasPtr pApplySkinBtn[4];
    pApplySkinBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplySkin/disabled/0");
    pApplySkinBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplySkin/mouseOver/0");
    pApplySkinBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplySkin/normal/0");
    pApplySkinBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:ApplySkin/pressed/0");

    IWzCanvasPtr pSaveSkinBtn[4];
    pSaveSkinBtn[0] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveSkin/disabled/0");
    pSaveSkinBtn[1] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveSkin/mouseOver/0");
    pSaveSkinBtn[2] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveSkin/normal/0");
    pSaveSkinBtn[3] = LoadSprite(L"UI/New.img/beautyRoom/Slot/button:SaveSkin/pressed/0");

    auto InitSlots = [&](SlotData slots[], IWzCanvasPtr pEnable) {
        IWzCanvasPtr pFirstLayer = nullptr;
        for (int i = 0; i < 6; ++i) {
            int col = i % 3;
            int row = i / 3;

            int sx = startX + col * (slotW + gapX);
            int sy = startY + row * (slotH + gapY);
            m_rcSlot[i] = {sx, sy, sx + slotW, sy + slotH};

            int centerX = sx + slotW / 2;
            int centerY = sy + slotH / 2;

            if (i == 0) {
                pFirstLayer = LoadSprite(L"UI/New.img/beautyRoom/Slot/layer:slot");
            }
            slots[i].pLayer = pFirstLayer;
            slots[i].pEnableHM = pEnable;

            for (int j = 0; j < 4; j++) {
                slots[i].pDeleteBtn[j] = pDeleteBtn[j];
                slots[i].pApplyHairBtn[j] = pApplyHairBtn[j];
                slots[i].pApplyFaceBtn[j] = pApplyFaceBtn[j];
                slots[i].pApplySkinBtn[j] = pApplySkinBtn[j];
                slots[i].pSaveHairBtn[j] = pSaveHairBtn[j];
                slots[i].pSaveFaceBtn[j] = pSaveFaceBtn[j];
                slots[i].pSaveSkinBtn[j] = pSaveSkinBtn[j];
            }

            int deleteBtnX = sx + slotW - 25;
            int deleteBtnY = sy + 15;
            slots[i].deleteBtnOriginX = deleteBtnX;
            slots[i].deleteBtnOriginY = deleteBtnY;
            if (pDeleteBtn[2]) {
                UINT uW = 0, uH = 0;
                pDeleteBtn[2]->get_width(&uW);
                pDeleteBtn[2]->get_height(&uH);
                int rcLeft = deleteBtnX - (int)pDeleteBtn[2]->cx;
                int rcTop = deleteBtnY - (int)pDeleteBtn[2]->cy;
                slots[i].rcDeleteBtn = {rcLeft, rcTop, rcLeft + (int)uW, rcTop + (int)uH};
            } else {
                slots[i].rcDeleteBtn = {deleteBtnX, deleteBtnY, deleteBtnX + 16, deleteBtnY + 16};
            }

            int applyBtnW = 60, applyBtnH = 20;
            if (pApplyHairBtn[0]) {
                pApplyHairBtn[0]->get_width((UINT*)&applyBtnW);
                pApplyHairBtn[0]->get_height((UINT*)&applyBtnH);
            }
            int applyBtnX = sx + (slotW - applyBtnW) / 2;
            int applyBtnY = sy + slotH - applyBtnH - 8;
            slots[i].rcApplyBtn = {applyBtnX, applyBtnY, applyBtnX + applyBtnW, applyBtnY + applyBtnH};
            slots[i].applyBtnState = 2;
            slots[i].deleteBtnState = 2;

            slots[i].x = centerX;
            slots[i].y = centerY;
            slots[i].hasHairData = false;
            slots[i].hasFaceData = false;
        }
    };

    InitSlots(m_hairSlots, pEnableHM);
    InitSlots(m_faceSlots, pEnableFM);
    InitSlots(m_skinSlots, pEnableSM);

    // Show slots immediately. Server DATA (0x174) overwrites when it arrives.
    // Without this, m_bDataReady stays false and Save/Apply never paint if the
    // reply is dropped / handler missed / server not yet restarted.
    m_nUnlockedSlots = kMaxSlots;
    m_bDataReady = true;
    BeautyLog("OpenBeautyShop: local unlock=%d dataReady=1 (await server 0x174)", m_nUnlockedSlots);

    COutPacket oPacket(kOpcode_SaveBeauty);
    oPacket.Encode1(kAction_Request);
    void* sock = *reinterpret_cast<void**>(kAddr_ClientSocket_Instance);
    BeautyLog("OpenBeautyShop: send REQUEST sock=%p", sock);
    SendBeautyPacket(oPacket);
}

CBeautyShop::~CBeautyShop() {
    ReleaseAvatars();
    if (ms_pInstance == this) {
        ms_pInstance = nullptr;
    }
}

void CBeautyShop::OnDestroy() {
    auto CleanSlots = [](SlotData slots[]) {
        for (int i = 0; i < 6; ++i) {
            slots[i].pLayer = nullptr;
            slots[i].pEnableHM = nullptr;
            for (int j = 0; j < 4; j++) {
                slots[i].pDeleteBtn[j] = nullptr;
                slots[i].pApplyHairBtn[j] = nullptr;
                slots[i].pApplyFaceBtn[j] = nullptr;
                slots[i].pApplySkinBtn[j] = nullptr;
                slots[i].pSaveHairBtn[j] = nullptr;
                slots[i].pSaveFaceBtn[j] = nullptr;
                slots[i].pSaveSkinBtn[j] = nullptr;
            }
        }
    };
    ReleaseAvatars();

    if (m_bTtInit) {
        try { pTT_Clear(m_ttBuf); } catch (...) {}
        try { pTT_Dtor(m_ttBuf); } catch (...) {}
        m_bTtInit = false;
        m_nTtSlot = -1;
    }
    CleanSlots(m_hairSlots);
    CleanSlots(m_faceSlots);
    CleanSlots(m_skinSlots);
    m_pTabSkinN = m_pTabSkinS = nullptr;
    m_pBgCanvas2 = nullptr;
    m_pDisableSlot = nullptr;
    m_pBtCloseN = m_pBtCloseM = m_pBtCloseP = nullptr;

    if (ms_pInstance == this) {
        ms_pInstance = nullptr;
    }
    CWnd::OnDestroy();
}

void CBeautyShop::Update() {

    if (*reinterpret_cast<void**>(kAddr_CurrentStage) != m_pOpenStage) {
        Destroy();
        return;
    }

    if (m_bDataReady && g_enablePreviewAvatars) {
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
        void* charData = ctx ? *reinterpret_cast<void**>(
            reinterpret_cast<char*>(ctx) + kOff_CharacterDataInCtx) : nullptr;
        if (charData) {
            unsigned h = ComputeLookHash(charData);
            if (h != m_lastLookHash) {
                RebuildAvatars();
            }
        }
    }
    InvalidateRect(nullptr);
}

void CBeautyShop::Draw(const RECT* pRect) {
    if (this != ms_pInstance) return;
    CWnd::Draw(pRect);
    IWzCanvasPtr pCanvas = GetCanvas();
    if (!pCanvas) return;

    auto BlitAtPosition = [&](IWzCanvasPtr pSprite, int bx, int by) {
        if (!pSprite) return;
        pCanvas->CopyEx(bx, by, pSprite, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
    };

    if (m_pBgCanvas) {
        pCanvas->CopyEx(0, 0, m_pBgCanvas, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
    }
    if (m_pBgCanvas2) {
        pCanvas->CopyEx(8, 25, m_pBgCanvas2, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
    }

    bool hairSel = (m_nCurrentTab == kTab_Hair);
    BlitAtPosition(hairSel ? m_pTabHairS : m_pTabHairN, 10, hairSel ? 23 : 25);
    bool faceSel = (m_nCurrentTab == kTab_Face);
    BlitAtPosition(faceSel ? m_pTabFaceS : m_pTabFaceN, 53, faceSel ? 23 : 25);
    bool skinSel = (m_nCurrentTab == kTab_Skin);
    BlitAtPosition(skinSel ? m_pTabSkinS : m_pTabSkinN, 96, skinSel ? 23 : 25);

    SlotData* slots = SlotsForTab(m_nCurrentTab);

    for (int i = 0; m_bDataReady && i < 6; ++i) {
        SlotData& slot = slots[i];
        int slotX = m_rcSlot[i].left;
        int slotY = m_rcSlot[i].top;

        if (slot.pLayer) {
            pCanvas->CopyEx(slotX, slotY, slot.pLayer, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, Ztl_variant_t());
        }

        if (IsSlotLocked(i)) {
            if (m_pDisableSlot) {
                pCanvas->CopyEx(slotX, slotY, m_pDisableSlot, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, Ztl_variant_t());
            }
            continue;
        }

        if (!slot.bIsSaved && slot.pEnableHM) {
            int hmX = slot.x - 35;
            int hmY = slot.y - 45;
            pCanvas->CopyEx(hmX, hmY, slot.pEnableHM, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, Ztl_variant_t());
        }

        bool hasDataForCurrentTab = SlotHasData(slot, m_nCurrentTab);

        IWzCanvasPtr pApplyBtn;
        if (hasDataForCurrentTab) {
            pApplyBtn = ApplyBtnsForTab(slot, m_nCurrentTab)[slot.applyBtnState];
        } else {
            pApplyBtn = SaveBtnsForTab(slot, m_nCurrentTab)[slot.applyBtnState];
        }
        if (pApplyBtn) {
            pCanvas->CopyEx(slot.rcApplyBtn.left, slot.rcApplyBtn.top, pApplyBtn, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, Ztl_variant_t());
        }

        if (hasDataForCurrentTab) {
            IWzCanvasPtr pDel = slot.pDeleteBtn[slot.deleteBtnState];
            if (pDel) {
                pCanvas->CopyEx(slot.deleteBtnOriginX, slot.deleteBtnOriginY, pDel, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, Ztl_variant_t());
            }
        }

        if (g_enableNameLabels && hasDataForCurrentTab && slot.szName[0]) {
            DrawFittedName(pCanvas, slot.szName,
                           m_rcSlot[i].left + 2, m_rcSlot[i].right - 2,
                           m_rcSlot[i].top + 106);
        }
    }

    IWzCanvasPtr pClose = (m_nCloseBtnState == 2) ? m_pBtCloseP : (m_nCloseBtnState == 1) ? m_pBtCloseM : m_pBtCloseN;
    BlitAtPosition(pClose, m_rcBtClose.left, m_rcBtClose.top);
}

void CBeautyShop::SelectTab(int nTab) {
    if (m_nCurrentTab == nTab) return;
    m_nCurrentTab = nTab;
    play_ui_sound(L"Tab");
    RebuildAvatars();
    InvalidateRect(nullptr);
}

void CBeautyShop::SelectSlot(int nSlot) {
    if (m_nSelectedSlot == nSlot) return;
    m_nSelectedSlot = nSlot;
    InvalidateRect(nullptr);
}

void CBeautyShop::ReleaseAvatars() {
    for (int i = 0; i < 6; ++i) {
        if (m_avatarRef[i][1]) {
            __try {
                ZRef_Release(&m_avatarRef[i], 0);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        m_avatarRef[i][0] = 0;
        m_avatarRef[i][1] = 0;
        m_pSlotAvatar[i] = nullptr;
    }
}

void CBeautyShop::BuildSlotAvatar(int i, void* charData) {
    if (i < 0 || i >= 6 || !charData) return;

    SlotData& s = SlotsForTab(m_nCurrentTab)[i];
    if (!s.bIsSaved) return;

    IWzGr2DLayer* pLayer = m_pLayer.GetInterfacePtr();
    if (!pLayer) return;

    __try {
        void* pAvatar = ZRefCAvatar_Alloc(&m_avatarRef[i]);
        if (!pAvatar) return;

        unsigned char buf[0x210];
        memset(buf, 0, sizeof(buf));
        AvatarLook_ctor(buf, charData);

        if (m_nCurrentTab == kTab_Hair) {
            *reinterpret_cast<int*>(buf + kAL_Hair) = s.nHair;
        } else if (m_nCurrentTab == kTab_Face) {
            *reinterpret_cast<int*>(buf + kAL_Face) = s.nFace;
        } else {
            *reinterpret_cast<int*>(buf + kAL_Skin) = s.nSkin;
        }

        if (m_nCurrentTab != kTab_Skin) {
            *reinterpret_cast<int*>(buf + kAL_Hat)     = 0;
            *reinterpret_cast<int*>(buf + kAL_FaceAcc) = 0;
        }

        pLayer->AddRef();
        pLayer->AddRef();
        int ax = m_rcSlot[i].left + kAvatarFeetDX;
        int ay = m_rcSlot[i].top + kAvatarFeetDY;
        CAvatar_Init(pAvatar, buf, kAvatarStandAction, pLayer, pLayer,
                     1, ax, ay, kAvatarScale, 0);

        m_pSlotAvatar[i] = pAvatar;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BeautyLog("BuildSlotAvatar(%d) faulted", i);
        m_pSlotAvatar[i] = nullptr;
    }
}

void CBeautyShop::RebuildAvatars() {
    ReleaseAvatars();
    if (!g_enablePreviewAvatars) return;

    void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
    if (!ctx) return;
    void* charData = *reinterpret_cast<void**>(
        reinterpret_cast<char*>(ctx) + kOff_CharacterDataInCtx);
    if (!charData) return;

    m_lastLookHash = ComputeLookHash(charData);
    for (int i = 0; i < 6; ++i) {
        if (IsSlotLocked(i)) continue;
        BuildSlotAvatar(i, charData);
    }
}

void CBeautyShop::EnsureTooltip() {
    if (m_bTtInit) return;
    try {
        pTT_Ctor(m_ttBuf);
        m_bTtInit = true;
    } catch (...) {
        m_bTtInit = false;
    }
}

void CBeautyShop::TooltipShowName(int slot, int rx, int ry, const wchar_t* name) {
    if (!name || !name[0]) return;
    EnsureTooltip();
    if (!m_bTtInit) return;

    if (slot == m_nTtSlot) return;
    char ansi[96];
    int n = WideCharToMultiByte(CP_ACP, 0, name, -1, ansi, sizeof(ansi), nullptr, nullptr);
    if (n <= 0) { ansi[0] = '\0'; }
    try {
        pTT_SetString(m_ttBuf, m_nWndX + rx + 14, m_nWndY + ry + 20, ansi);
        m_nTtSlot = slot;
    } catch (...) {
    }
}

void CBeautyShop::TooltipHide() {
    if (!m_bTtInit) return;
    if (m_nTtSlot < 0) return;
    try { pTT_Clear(m_ttBuf); } catch (...) {}
    m_nTtSlot = -1;
}

void CBeautyShop::SaveBeauty() {
    if (m_nSelectedSlot < 0 || m_nSelectedSlot >= 6) return;
    if (IsSlotLocked(m_nSelectedSlot)) return;
    SendSavePacket(m_nSelectedSlot, m_nCurrentTab);
    play_ui_sound(L"Lock");
}

void CBeautyShop::SendSavePacket(int slot, int type) {
    COutPacket oPacket(kOpcode_SaveBeauty);
    oPacket.Encode1(kAction_Save);
    oPacket.Encode1(slot);
    oPacket.Encode1(type);
    SendBeautyPacket(oPacket);
}

void CBeautyShop::SendApplyPacket(int slot) {
    COutPacket oPacket(kOpcode_SaveBeauty);
    oPacket.Encode1(kAction_Apply);
    oPacket.Encode1(slot);
    oPacket.Encode1(m_nCurrentTab);
    SendBeautyPacket(oPacket);
}

void CBeautyShop::SendDeletePacket(int slot, int type) {
    COutPacket oPacket(kOpcode_SaveBeauty);
    oPacket.Encode1(kAction_Delete);
    oPacket.Encode1(slot);
    oPacket.Encode1(type);
    SendBeautyPacket(oPacket);
}

struct CompatBeautyReader {
    CompatInPacket* p;
    explicit CompatBeautyReader(CompatInPacket* pkt) : p(pkt) {}
    unsigned char Decode1() { return p->Decode<unsigned char>(); }
    int Decode4() { return static_cast<int>(p->Decode<int32_t>()); }
};

template <typename Reader>
static void HandleServerResponseImpl(CBeautyShop* shop, Reader& r) {
    shop->m_bDataReady = true;
    const int serverUnlock = static_cast<int>(r.Decode1());
    // Never let server DATA re-lock all slots (buttons vanish). Floor at 6 for private server.
    int unlocked = serverUnlock;
    if (unlocked <= 0) {
        unlocked = kMaxSlots;
    }
    if (unlocked > kMaxSlots) {
        unlocked = kMaxSlots;
    }
    shop->m_nUnlockedSlots = unlocked;
    BeautyLog("DATA unlocked server=%d applied=%d", serverUnlock, shop->m_nUnlockedSlots);

    unsigned char hairCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        shop->m_anHair[i] = (i < hairCount) ? r.Decode4() : 0;
    }

    unsigned char faceCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        shop->m_anFace[i] = (i < faceCount) ? r.Decode4() : 0;
    }

    unsigned char skinCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        shop->m_anSkin[i] = (i < skinCount) ? r.Decode4() : 0;
    }

    unsigned char hairSavedCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        bool saved = (i < hairSavedCount) && (r.Decode1() != 0);
        shop->m_hairSlots[i].hasHairData = saved;
        shop->m_hairSlots[i].bIsSaved = saved;
        if (saved) shop->m_hairSlots[i].hairId = shop->m_anHair[i];
    }

    unsigned char faceSavedCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        bool saved = (i < faceSavedCount) && (r.Decode1() != 0);
        shop->m_faceSlots[i].hasFaceData = saved;
        shop->m_faceSlots[i].bIsSaved = saved;
        if (saved) shop->m_faceSlots[i].faceId = shop->m_anFace[i];
    }

    unsigned char skinSavedCount = r.Decode1();
    for (int i = 0; i < 6; ++i) {
        bool saved = (i < skinSavedCount) && (r.Decode1() != 0);
        shop->m_skinSlots[i].hasSkinData = saved;
        shop->m_skinSlots[i].bIsSaved = saved;
    }

    unsigned char fullDataCount = r.Decode1();
    for (int i = 0; i < fullDataCount; ++i) {
        int tab = r.Decode1();
        int slot = r.Decode1();
        int gender = r.Decode1();
        int skin = r.Decode4();
        int hair = r.Decode4();
        int face = r.Decode4();
        int hat = r.Decode4();
        int top = r.Decode4();
        int bottom = r.Decode4();
        int shoes = r.Decode4();
        int weapon = r.Decode4();
        int cashWeapon = r.Decode4();

        SlotData* arr = nullptr;
        if (tab == kTab_Hair) arr = shop->m_hairSlots;
        else if (tab == kTab_Face) arr = shop->m_faceSlots;
        else if (tab == kTab_Skin) arr = shop->m_skinSlots;
        if (!arr || slot < 0 || slot >= 6) continue;

        SlotData& s = arr[slot];
        s.nGender = gender;
        s.nSkin = skin;
        s.nHair = hair;
        s.nFace = face;
        s.nHat = hat;
        s.nTop = top;
        s.nBottom = bottom;
        s.nShoes = shoes;
        s.nWeapon = weapon;
        s.nCashWeapon = cashWeapon;
        s.bIsSaved = true;
        if (tab == kTab_Hair) {
            s.hasHairData = true;
            s.hairId = hair;
            ResolveBeautyName(hair, true, s.szName, _countof(s.szName));
        } else if (tab == kTab_Face) {
            s.hasFaceData = true;
            s.faceId = face;
            ResolveBeautyName(face, false, s.szName, _countof(s.szName));
        } else {
            s.hasSkinData = true;
            s.szName[0] = L'\0';
        }
    }

    shop->RebuildAvatars();
    shop->InvalidateRect(nullptr);
}

void CBeautyShop::HandleServerResponse(CInPacket* pPacket) {
    BeautyPacketReader r(pPacket);
    HandleServerResponseImpl(this, r);
}

void CBeautyShop::HandleServerResponseCompat(CompatInPacket* pPacket) {
    CompatBeautyReader r(pPacket);
    HandleServerResponseImpl(this, r);
}

void CBeautyShop::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    if (this != ms_pInstance) return;

    POINT pt{rx, ry};
    if (msg == WM_LBUTTONDOWN) {

        if (PtInRect(&m_rcBtClose, pt)) {
            m_nCloseBtnState = 2;
            play_ui_sound(L"BtMouseClick");
            InvalidateRect(nullptr);
            return;
        }

        if (PtInRect(&m_rcTabHair, pt)) {
            SelectTab(kTab_Hair);
            return;
        }
        if (PtInRect(&m_rcTabFace, pt)) {
            SelectTab(kTab_Face);
            return;
        }
        if (PtInRect(&m_rcTabSkin, pt)) {
            SelectTab(kTab_Skin);
            return;
        }

        for (int i = 0; i < 6; ++i) {
            if (!PtInRect(&m_rcSlot[i], pt)) continue;
            if (IsSlotLocked(i)) return;
            SelectSlot(i);
            play_ui_sound(L"Click");

            SlotData& slot = SlotsForTab(m_nCurrentTab)[i];
            bool hasDataForCurrentTab = SlotHasData(slot, m_nCurrentTab);

            if (PtInRect(&slot.rcApplyBtn, pt)) {
                slot.applyBtnState = 3;
                m_nPressedApplySlot = i;
                InvalidateRect(nullptr);
                return;
            }

            if (hasDataForCurrentTab && PtInRect(&slot.rcDeleteBtn, pt)) {
                play_ui_sound(L"BtMouseClick");
                int delSlot = i;
                int delType = m_nCurrentTab;
                ZXString<char> zmsg("Are you sure want to delete this?");
                int r = 0;
                try { r = CUtilDlg_YesNo(zmsg, nullptr, nullptr, 0, 0); }
                catch (...) { r = 0; }

                if (ms_pInstance == this && r == kYesNo_Yes) {
                    SendDeletePacket(delSlot, delType);
                    play_ui_sound(L"Unlock");
                }
                return;
            }
            return;
        }

        if (m_pLayer) {
            m_bDragging = 1;
            m_nDragAnchorX = rx;
            m_nDragAnchorY = ry;
        }
    } else if (msg == WM_LBUTTONUP) {
        m_bDragging = 0;

        if (m_nPressedApplySlot >= 0) {
            int ps = m_nPressedApplySlot;
            m_nPressedApplySlot = -1;
            if (ps >= 0 && ps < 6 && !IsSlotLocked(ps)) {
                SlotData& s = SlotsForTab(m_nCurrentTab)[ps];
                bool over = PtInRect(&s.rcApplyBtn, pt) != 0;
                s.applyBtnState = over ? 1 : 2;
                if (over) {
                    if (SlotHasData(s, m_nCurrentTab)) {
                        SendApplyPacket(ps);
                        play_ui_sound(L"Apply");
                    } else {
                        m_nSelectedSlot = ps;
                        SaveBeauty();
                    }
                }
            }
            InvalidateRect(nullptr);
            return;
        }

        if (m_nCloseBtnState == 2) {
            m_nCloseBtnState = 0;
            if (PtInRect(&m_rcBtClose, pt)) {
                play_ui_sound(L"MenuDown");
                Destroy();
                return;
            }
            InvalidateRect(nullptr);
        }
    }

    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

int CBeautyShop::OnMouseMove(int rx, int ry) {
    if (this != ms_pInstance) return 0;
    if (m_bDragging) {
        int dx = rx - m_nDragAnchorX;
        int dy = ry - m_nDragAnchorY;
        if ((dx != 0 || dy != 0) && m_pLayer) {
            m_pLayer->RelOffset(dx, dy, Ztl_variant_t(), Ztl_variant_t());
            m_nWndX += dx;
            m_nWndY += dy;
        }
        TooltipHide();
        return 1;
    }

    POINT pt{rx, ry};

    int closeNow = PtInRect(&m_rcBtClose, pt) ? 1 : 0;
    if (m_nCloseBtnState != 2 && m_nCloseBtnState != closeNow) {
        m_nCloseBtnState = closeNow;
        if (closeNow) play_ui_sound(L"BtMouseOver");
    }

    SlotData* slots = SlotsForTab(m_nCurrentTab);

    for (int i = 0; i < 6; ++i) {
        if (IsSlotLocked(i)) continue;
        SlotData& slot = slots[i];
        bool hasDataForCurrentTab = SlotHasData(slot, m_nCurrentTab);
        if (PtInRect(&slot.rcApplyBtn, pt)) {
            if (slot.applyBtnState == 2) slot.applyBtnState = 1;
        } else if (slot.applyBtnState == 1) {
            slot.applyBtnState = 2;
        }

        if (hasDataForCurrentTab && PtInRect(&slot.rcDeleteBtn, pt)) {
            if (slot.deleteBtnState == 2) {
                slot.deleteBtnState = 1;
                play_ui_sound(L"BtMouseOver");
            }
        } else if (slot.deleteBtnState == 1) {
            slot.deleteBtnState = 2;
        }
    }

    int nameSlot = -1;
    for (int i = 0; i < 6; ++i) {
        if (IsSlotLocked(i)) continue;
        SlotData& slot = slots[i];
        if (!slot.szName[0] || !SlotHasData(slot, m_nCurrentTab)) continue;
        RECT rcName = {m_rcSlot[i].left + 8, m_rcSlot[i].top + 104,
                       m_rcSlot[i].right - 3, m_rcSlot[i].top + 120};
        if (PtInRect(&rcName, pt)) { nameSlot = i; break; }
    }
    if (nameSlot >= 0) {
        TooltipShowName(nameSlot, rx, ry, slots[nameSlot].szName);
    } else {
        TooltipHide();
    }

    return 0;
}

static void OpenBeautyShop() {
    if (CBeautyShop::ms_pInstance) {
        return;
    }
    BeautyLog("OpenBeautyShop: creating window");
    new CBeautyShop(0, kBeautyShopItemID);
    BeautyLog("OpenBeautyShop: window created");
}

void BeautyShop_SendUnlockSlot(int nPOS) {
    BeautyLog("SendUnlockSlot pos=%d", nPOS);
    COutPacket oPacket(kOpcode_SaveBeauty);
    oPacket.Encode1(kAction_Unlock);
    oPacket.Encode2(static_cast<unsigned short>(nPOS));
    SendBeautyPacket(oPacket);
}

void BeautyShop_OpenWindow() {
    OpenBeautyShop();
}

void BeautyShop_CloseWindow() {
    if (CBeautyShop::ms_pInstance) {
        CBeautyShop::ms_pInstance->Destroy();
    }
}

void BeautyShop_ToggleWindow() {
    if (BeautyShop_IsOpen()) {
        BeautyShop_CloseWindow();
    } else {
        BeautyShop_OpenWindow();
    }
}

bool BeautyShop_IsOpen() {
    return CBeautyShop::ms_pInstance != nullptr;
}

static int g_cachedUnlockedSlots = -1;

int BeautyShop_GetCachedUnlockedSlots() {
    return g_cachedUnlockedSlots;
}

void BeautyShop_HandleServerPacket(class CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    const size_t savedOff = packet->GetOffset();
    const unsigned long size = packet->Size();
    unsigned short peeked = 0;
    bool onOpcode = packet->TryPeekOpcode(peeked) && peeked == kOpcode_SaveBeauty;
    if (!onOpcode) {
        // Cursor may already be past header, or opcode only at abs 0/4.
        packet->SetOffset(0);
        onOpcode = packet->TryPeekOpcode(peeked) && peeked == kOpcode_SaveBeauty;
        if (!onOpcode && size >= 6) {
            packet->SetOffset(4);
            onOpcode = packet->TryPeekOpcode(peeked) && peeked == kOpcode_SaveBeauty;
        }
        if (!onOpcode) {
            packet->SetOffset(savedOff);
            BeautyLog("recv 0x174 MISS off=%zu size=%lu peek=0x%X",
                      savedOff, size, static_cast<unsigned>(peeked));
            return;
        }
    }
    packet->Decode<uint16_t>();
    const uint8_t respType = packet->Decode<uint8_t>();
    BeautyLog("recv 0x174 respType=%d offWas=%zu size=%lu",
              static_cast<int>(respType), savedOff, size);
    if (respType == kResp_Open) {
        OpenBeautyShop();
    } else if (respType == kResp_Data) {
        if (CBeautyShop::ms_pInstance) {
            CBeautyShop::ms_pInstance->HandleServerResponseCompat(packet);
            g_cachedUnlockedSlots = CBeautyShop::ms_pInstance->m_nUnlockedSlots;
        } else {
            const int unlocked = static_cast<int>(packet->Decode<uint8_t>());
            g_cachedUnlockedSlots = unlocked > kMaxSlots ? kMaxSlots : unlocked;
            BeautyLog("recv 0x174 unlockedSlots=%d (ui closed)", g_cachedUnlockedSlots);
            play_ui_sound(L"Unlock");
        }
    }
}

void AttachBeautyShopMod() {
    if (!g_vehHandle) {
        g_vehHandle = AddVectoredExceptionHandler(1, BeautyCrashVeh);
    }
    BeautyLog("=== AttachBeautyShopMod: crash logger installed ===");
}
