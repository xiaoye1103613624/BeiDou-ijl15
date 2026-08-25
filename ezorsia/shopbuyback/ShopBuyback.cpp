#include "stdafx.h"
#include "ShopBuyback.h"
#include "Memory.h"
#include "compat/ClientAddresses.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/WzLib/IWzCanvas.h"
#include "compat/WzLib/IWzResMan.h"
#include "compat/ztl/zcom.h"

#include <windows.h>

// ===========================================================================
// Reverse-engineered CShopDlg surface (Exodus v83). See ShopBuyback.h for what
// this module does and why.
//
// The "All" label above the shop's stock is NOT a button - it is a CCtrlTab
// strip spanning the whole panel (created at 0x753724: id 0x3EA, style 2, x 5,
// y 95, w 222; style 2 -> Basic.img/Tab3, 21px). The strip holds at most two
// tabs, because CShopDlg's per-tab scroll array at +0x108 has two entries, and
// the index picks the commodity array (0 -> +0xC4, else +0xC8). So the Buy Back
// button is NOT a tab: it is drawn by hand past the strip's right edge, and the
// strip's own width is narrowed so its clicks reach CShopDlg::OnEvent.
//
//   CShopDlg::SetShopDlg    0x007529AD  __thiscall(CInPacket*) - fills the window
//   CShopDlg::OnChildNotify 0x00754005  __thiscall(nId, nMsg, nIndex)
//   CShopDlg::SendClose     0x00753248  __thiscall()  - sends NPC_SHOP mode 3
//   CWnd::Destroy           0x009E00AF  __thiscall()
//   CCtrlTab::AddTab        0x004DE0E2  __thiscall(selSprite, unselSprite, data)
//   open exclusive dialog   0x00BE7910  (must be null for CShopDlg to be created)
//   CShopDlg primary vtable 0x00AFE110
//
// Tab control layout: +0x1C total width, +0x34 in-use flag, +0x38 fixed tab
// width, +0x3C selected index, +0x48 tab count, +0x4C/+0x50 entry list.
// ===========================================================================

namespace ShopBuyback {
namespace {

    constexpr DWORD kAddr_SetShopDlg    = 0x007529AD;
    constexpr DWORD kAddr_OnEvent       = 0x00754110;   // __thiscall(msg, ?, x, y), on this+4
    constexpr DWORD kAddr_DrawList      = 0x0075577A;   // __thiscall(IWzCanvas*)
    constexpr DWORD kAddr_OnChildNotify = 0x00754005;
    constexpr DWORD kAddr_SendClose     = 0x00753248;
    constexpr DWORD kAddr_CWnd_Destroy  = 0x009E00AF;
    constexpr DWORD kAddr_OpenDlg       = 0x00BE7910;
    constexpr DWORD kAddr_ResMan        = 0x00BF14E8;

    // Exact-class check, no virtual dispatch: the global above can hold any dialog of
    // that base class, so identity is compared against CShopDlg's own vtable.
    constexpr DWORD kVtbl_ShopDlg_Primary = 0x00AFE110;


    constexpr int kOff_TabSelected   = 0x3C;
    constexpr int kOff_TabCount      = 0x48;   // SetSelected bounds-checks against it

    constexpr int kCtrlId_BuyTab   = 0x3EA;  // the native "All" / "Must" strip

    // The button is drawn by hand, not by a control. A CCtrlTab of our own rendered
    // nicely but owned its whole rectangle for hit testing (PtInRect(0,0,+0x1C,+0x20) in
    // sub_4DFECE) while doing nothing with the click, so only the fringe outside it
    // reached the dialog - and waiving that test hid the control, because the draw path
    // consults it too. Drawing it ourselves keeps the click and the pressed state in
    // our hands, and leaves the native strip's two tab slots completely alone.
    //
    // Geometry taken from the client, not estimated. The strip is created at
    // 0x753724 with CreateCtrl(id 0x3EA, style 2, x 5, y 0x5F, w 0xDE), and
    // CreateCtrl's style table at 0x4DD7A6 maps style 2 -> height 0x15, which is
    // Basic.img/Tab3 (21px, 6px caps). The panel itself ends at x 227 - the shop
    // background's red underline runs x 6..226 on the buy side - so the button is
    // right-aligned to 226 rather than parked at a guessed offset.
    constexpr int kBarX0 = 5, kBarX1 = 227;        // the strip's own span
    constexpr int kTabW = 56, kTabY = 95, kTabH = 21;
    constexpr int kTabX = kBarX1 - 1 - kTabW;      // flush with the panel's right edge
    constexpr int kCapW = 6;                       // Basic.img/Tab3 end caps
    constexpr int kLabelW = 45, kLabelH = 15;      // Shop/TabBuy/*/4
    constexpr int kLabelX = kTabX + (kTabW - kLabelW) / 2;
    constexpr int kLabelY = kTabY + (kTabH - kLabelH) / 2;

    // The native strip is created 222 wide from x 5, so it covers 5..227 and would eat
    // every click over the button. Narrowing +0x1C genuinely shrinks its clickable area
    // (same PtInRect), and its own tabs are 50px each, so nothing of the client's moves.
    constexpr int kNativeMaxW = kTabX - kBarX0 - 4;

    // Click box: the whole button plus a little slack.
    constexpr int kHitX0 = kTabX - 4, kHitX1 = kTabX + kTabW + 4;
    constexpr int kHitY0 = kTabY - 3, kHitY1 = kTabY + kTabH + 2;

    constexpr int kMsg_LButtonDown = 0x201;

    constexpr int   kOff_TabWidthTotal  = 0x1C;
    constexpr int   kOff_TabInUse       = 0x34;
    constexpr int   kOff_TabFixedWidth  = 0x38;
    constexpr int   kNativeTabW         = 0x32;  // what the client writes at 0x7530B3
    constexpr int kNotify_TabClick = 0x1F4;  // "selection changed", arg3 = new index
    constexpr int kTabIndex_Shop   = 0;      // "All" is always first in the native strip
    constexpr int kTabIndex_Must   = 1;
    // A tab whose user data is 0 is skipped by the strip's own mouse handler
    // (sub_4DD82C: "cmp [entry+0x1C], 0 / je next"), so it must be non-zero to click.
    constexpr int kTabData_Must    = 1;

    constexpr int kOff_TabCtrl   = 0x9C;     // CShopDlg -> the native strip
    constexpr int kOff_MustArray = 0xC8;     // the level/job filtered commodity array
    constexpr int kOff_SellStrip = 0xA4;     // sell side's five-tab strip (id 0x3EB)
    constexpr DWORD kAddr_SetSelected = 0x004DE4A1;   // __thiscall(nIndex), notifies parent

    constexpr DWORD kAddr_AddTab      = 0x004DE0E2;   // (selSprite, unselSprite, data)
    constexpr DWORD kAddr_CtrlTabDraw = 0x004DD903;   // CCtrlTab vtable slot 7


    // ---- protocol -----------------------------------------------------------
    constexpr unsigned short kOpcode_ShopAction = 0x3D;   // RecvOpcode.NPC_SHOP
    constexpr unsigned char  kMode_ShowBuyback  = 4;
    constexpr unsigned char  kMode_ShowShop     = 5;

    // Which list the shop window is showing. Only the server sets this, so a refused
    // switch (nothing sold yet) cannot leave the window out of step.
    bool g_bBuybackMode = false;

    // Whether the server holds anything to buy back. Informational only.
    bool g_bHasSoldItems = false;

    // Set while tearing a shop window down to make room for a replacement, so the
    // dialog's own "shop closed" packet cannot reach the server and clear the shop.
    bool g_bReplacingShop = false;

    // ---- sprite loading -----------------------------------------------------
    IWzCanvasPtr LoadCanvas(const wchar_t* path) {
        IWzCanvasPtr pCanvas;
        IWzResMan* pResMan = *reinterpret_cast<IWzResMan**>(kAddr_ResMan);
        if (!pResMan) return pCanvas;
        VARIANT vP, vA, vR;
        VariantInit(&vP); VariantInit(&vA); VariantInit(&vR);
        Ztl_bstr_t uol(path);
        HRESULT hr = pResMan->raw_GetObject(uol, vP, vA, &vR);
        VariantClear(&vP); VariantClear(&vA);
        if (SUCCEEDED(hr)) {
            IUnknown* pUnk = nullptr;
            if (vR.vt == VT_UNKNOWN) pUnk = vR.punkVal;
            else if (vR.vt == VT_DISPATCH) pUnk = vR.pdispVal;
            if (pUnk) {
                IWzCanvas* pRaw = nullptr;
                if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&pRaw))))
                    pCanvas.Attach(pRaw);
            }
        }
        VariantClear(&vR);
        return pCanvas;
    }

    IWzCanvasPtr g_pLabelSel;     // Shop/TabBuy/enabled/4  - "Buy Back", highlighted
    IWzCanvasPtr g_pLabelUnsel;   // Shop/TabBuy/disabled/4 - "Buy Back", idle
    // Basic.img/Tab3 - the 9-slice the client builds every shop tab from, confirmed by
    // pixels: its fill0/fill1 columns match the real "Etc"/"Use" tabs exactly.
    IWzCanvasPtr g_pLeft[2], g_pFill[2], g_pRight[2];   // [0] = idle, [1] = pressed
    // Raw, explicitly nulled pointers rather than IWzCanvasPtr: the trace caught the
    // second smart pointer holding 0xFFFFFFFF before it was ever assigned, which made
    // "if (!p) load it" skip the load and the AddRef fault. These are cached for the
    // process lifetime and only ever handed to AddTab, so refcounting them by hand is
    // both sufficient and one less thing to get wrong.
    IWzCanvas* g_pMustSel   = nullptr;   // Shop/TabBuy/enabled/1  - "Must"
    IWzCanvas* g_pMustUnsel = nullptr;   // Shop/TabBuy/disabled/1
    int          g_nLoadTries = 0;

    // The shop's own buy-side strip, so its draw can be told apart from every other
    // CCtrlTab in the client.
    void* g_pStrip = nullptr;

    // Whether the NPC's own window carried a "Must" tab, so the buyback view can keep it.
    bool g_bShopHadMust = false;

    // Which native tab the player asked for, applied once the replacement window exists.
    int g_nPendingSelect = -1;

    // Whether WE put the second tab there. A shop stocking rechargeables owns that slot
    // with its own "Must" tab, and its clicks must not be mistaken for ours.
    bool g_bTabAdded = false;

    // Retried per shop rather than latched after one pass: the first attempt can land
    // while the window is still being built and come back empty.
    void EnsureTabSprites() {
        if (g_pLabelSel && g_pLabelUnsel && g_pLeft[0] && g_pLeft[1]) return;
        if (++g_nLoadTries > 30) return;
        if (!g_pLabelSel)   g_pLabelSel   = LoadCanvas(L"UI/UIWindow.img/Shop/TabBuy/enabled/4");
        if (!g_pLabelUnsel) g_pLabelUnsel = LoadCanvas(L"UI/UIWindow.img/Shop/TabBuy/disabled/4");
        if (!g_pLeft[0])  g_pLeft[0]  = LoadCanvas(L"UI/Basic.img/Tab3/left0");
        if (!g_pLeft[1])  g_pLeft[1]  = LoadCanvas(L"UI/Basic.img/Tab3/left1");
        if (!g_pFill[0])  g_pFill[0]  = LoadCanvas(L"UI/Basic.img/Tab3/fill0");
        if (!g_pFill[1])  g_pFill[1]  = LoadCanvas(L"UI/Basic.img/Tab3/fill1");
        if (!g_pRight[0]) g_pRight[0] = LoadCanvas(L"UI/Basic.img/Tab3/right0");
        if (!g_pRight[1]) g_pRight[1] = LoadCanvas(L"UI/Basic.img/Tab3/right1");
    }

    // A pointer worth calling a virtual on: in-bounds for the client's heap and aligned.
    bool LooksLikeObject(void* p) {
        const DWORD v = reinterpret_cast<DWORD>(p);
        return v >= 0x00010000 && v < 0x7FFF0000 && (v & 3) == 0;
    }

    void EnsureMustSprites() {
        if (!LooksLikeObject(g_pMustSel)) {
            IWzCanvasPtr p = LoadCanvas(L"UI/UIWindow.img/Shop/TabBuy/enabled/1");
            g_pMustSel = p.Detach();          // keep the reference the load gave us
        }
        if (!LooksLikeObject(g_pMustUnsel)) {
            IWzCanvasPtr p = LoadCanvas(L"UI/UIWindow.img/Shop/TabBuy/disabled/1");
            g_pMustUnsel = p.Detach();
        }
    }

    // ---- outgoing requests --------------------------------------------------
    void SendShopMode(unsigned char mode) {
        void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
        if (!sock) {
            return;
        }
        COutPacket packet(kOpcode_ShopAction);
        packet.Encode1(mode);
        reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket)(sock, packet);
    }

    // ---- adding the tab -----------------------------------------------------
    typedef void(__fastcall* t_AddTab)(void* pCtrl, void* edx,
                                       IWzCanvas* pSel, IWzCanvas* pUnsel, int nData);

    // Zero sizes = "blit at the source's natural size", the idiom the other ezorsia
    // dialogs use; asking a WZ canvas for get_width/get_height returns 0 here.
    void Blit(IWzCanvas* pDst, IWzCanvas* pSrc, int x, int y) {
        if (pDst && pSrc) pDst->CopyEx(x, y, pSrc, CA_OVERWRITE, 0, 0, 0, 0, 0, 0);
    }

    void DrawBuybackButton(IWzCanvas* pCanvas) {
        if (!pCanvas) return;
        EnsureTabSprites();
        const int st = g_bBuybackMode ? 1 : 0;      // pressed while its list is showing

        IWzCanvas* pL = g_pLeft[st]  ? g_pLeft[st].GetInterfacePtr()  : nullptr;
        IWzCanvas* pF = g_pFill[st]  ? g_pFill[st].GetInterfacePtr()  : nullptr;
        IWzCanvas* pR = g_pRight[st] ? g_pRight[st].GetInterfacePtr() : nullptr;
        IWzCanvasPtr& lab = g_bBuybackMode
            ? (g_pLabelSel   ? g_pLabelSel   : g_pLabelUnsel)
            : (g_pLabelUnsel ? g_pLabelUnsel : g_pLabelSel);
        IWzCanvas* pLabel = lab ? lab.GetInterfacePtr() : nullptr;
        if (!pL || !pF || !pR || !pLabel) return;

        Blit(pCanvas, pL, kTabX, kTabY);
        // fill is 1px wide: tile it, since a natural-size blit is the one form that works
        for (int i = kCapW; i < kTabW - kCapW; ++i) Blit(pCanvas, pF, kTabX + i, kTabY);
        Blit(pCanvas, pR, kTabX + kTabW - kCapW, kTabY);
        Blit(pCanvas, pLabel, kLabelX, kLabelY);
        g_bTabAdded = true;
    }

    // Which inventory tab (Equip/Use/Etc/Set-up/Cash) the sell side was showing, carried
    // across the window replacement. It is not the +0xF4 field - the trace showed that
    // one stuck at 0 - but the selection of the sell side's own five-tab CCtrlTab at
    // +0xA4, filled by the loop at 0x753D63. Restoring it through the control's
    // SetSelected replays the notify, so the list rebuilds exactly as on a real click.
    int g_nSavedInvTab = -1;

    void SaveInvTab(void* pShopDlg) {
        g_nSavedInvTab = -1;
        __try {
            char* pSell = *reinterpret_cast<char**>(reinterpret_cast<char*>(pShopDlg) + kOff_SellStrip);
            if (pSell) g_nSavedInvTab = *reinterpret_cast<int*>(pSell + kOff_TabSelected);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void RestoreInvTab(void* pShopDlg) {
        const int nWanted = g_nSavedInvTab;
        g_nSavedInvTab = -1;
        if (nWanted <= 0) return;                 // 0 is where a fresh window already is
        __try {
            char* pSell = *reinterpret_cast<char**>(reinterpret_cast<char*>(pShopDlg) + kOff_SellStrip);
            if (!pSell) return;
            const int nCount = *reinterpret_cast<int*>(pSell + kOff_TabCount);
            if (nWanted >= nCount) return;        // SetSelected would reject it anyway
            typedef void(__thiscall* t_SetSelected)(void*, int);
            reinterpret_cast<t_SetSelected>(kAddr_SetSelected)(pSell, nWanted);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Does this dialog hold a "Must" list? Exactly the client's own test at 0x752D7F:
    // the recommended-items array, then its ZArray element count at [-4].
    bool HasMustList(void* pShopDlg) {
        void* pArr = *reinterpret_cast<void**>(reinterpret_cast<char*>(pShopDlg) + kOff_MustArray);
        return pArr && *(reinterpret_cast<int*>(pArr) - 1) != 0;
    }

    void AddBuybackTab(void* pShopDlg) {
        if (!pShopDlg) return;
        EnsureTabSprites();

        void* pNative = *reinterpret_cast<void**>(reinterpret_cast<char*>(pShopDlg) + kOff_TabCtrl);
        if (!pNative) return;
        char* n = reinterpret_cast<char*>(pNative);
        g_pStrip = pNative;

        // Narrow the native strip so clicks over the button reach CShopDlg::OnEvent.
        int* pW = reinterpret_cast<int*>(n + kOff_TabWidthTotal);
        if (*pW > kNativeMaxW) *pW = kNativeMaxW;

        const bool bMustNow = HasMustList(pShopDlg);

        if (!g_bBuybackMode) {
            // Remember the shape of the NPC's own window, so the buyback view can be
            // rebuilt to match it.
            g_bShopHadMust = bMustNow;
        } else if (g_bShopHadMust && !bMustNow) {
            // The buyback list is a different set of items, so the client's level/job
            // filter puts nothing in +0xC8 and it never adds the "Must" tab - the strip
            // silently loses a tab the moment the buyback list opens. Put it back, with
            // the same sprites and the same 50px width the client uses at 0x7530B3.
            //
            // Order matters: the client writes +0x34/+0x38 before its own AddTab lays the
            // entry out, and a strip left on auto-divide would size the tabs from the
            // already-narrowed +0x1C.
            EnsureMustSprites();
            if (LooksLikeObject(g_pMustSel) && LooksLikeObject(g_pMustUnsel)) {
                *reinterpret_cast<int*>(n + kOff_TabInUse)      = 1;
                *reinterpret_cast<int*>(n + kOff_TabFixedWidth) = kNativeTabW;
                // AddTab stores the pointers and releases them when the strip dies (the
                // client hands it ZRefs by value, already addref'd at 0x428DCC), so match
                // that or the second shop would run on freed canvases.
                g_pMustSel->AddRef();
                g_pMustUnsel->AddRef();
                reinterpret_cast<t_AddTab>(kAddr_AddTab)(
                    pNative, nullptr, g_pMustSel, g_pMustUnsel, kTabData_Must);
            }
        }

        // The client selects its own "Must" tab when it adds one (SetSelected(1) at
        // 0x7530BF). In buyback mode that draws +0xC8 - only the level-appropriate slice
        // of the sold items - so put the selection back on the full list. The same poke
        // honours a click on "All", which would otherwise land back on "Must".
        if (g_bBuybackMode || g_nPendingSelect == kTabIndex_Shop)
            *reinterpret_cast<int*>(n + kOff_TabSelected) = kTabIndex_Shop;
        g_nPendingSelect = -1;
    }

    // ---- closing an open shop window ---------------------------------------
    // Mirrors the client's own teardown (the storage-close handler at 0x0074009E):
    // CWnd::Destroy, then the deleting destructor on the +8 subobject.
    void CloseOpenShopDlg() {
        void* pDlg = *reinterpret_cast<void**>(kAddr_OpenDlg);
        if (!pDlg) return;

        __try {
            if (*reinterpret_cast<DWORD*>(pDlg) != kVtbl_ShopDlg_Primary) return;

            SaveInvTab(pDlg);       // read it before the window it lives in is torn down
            g_bReplacingShop = true;

            typedef void(__thiscall* t_Destroy)(void*);
            reinterpret_cast<t_Destroy>(kAddr_CWnd_Destroy)(pDlg);

            void* pAfter = *reinterpret_cast<void**>(kAddr_OpenDlg);
            if (pAfter) {
                char* pSub8 = reinterpret_cast<char*>(pAfter) + 8;
                void** vtbl8 = *reinterpret_cast<void***>(pSub8);
                if (vtbl8 && vtbl8[0]) {
                    typedef void(__thiscall* t_Delete)(void*, int);
                    reinterpret_cast<t_Delete>(vtbl8[0])(pSub8, 1);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Leave the window alone; the client's own guard then refuses the new one,
            // which is a missing refresh rather than a crash.
        }
        g_bReplacingShop = false;
    }

    // ---- hooks --------------------------------------------------------------
    typedef void(__fastcall* t_SetShopDlg)(void* pThis, void* edx, void* pInPacket);
    t_SetShopDlg SetShopDlg_orig = reinterpret_cast<t_SetShopDlg>(kAddr_SetShopDlg);

    void __fastcall SetShopDlg_hook(void* pThis, void* edx, void* pInPacket) {
        SetShopDlg_orig(pThis, edx, pInPacket);
        // Separately guarded: a fault while restoring the tab strip used to abort the
        // rest of the work silently, which is how the inventory tab quietly stopped
        // being restored as well.
        __try { AddBuybackTab(pThis); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { RestoreInvTab(pThis); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    typedef void(__fastcall* t_OnChildNotify)(void* pThis, void* edx, int nId, int nMsg, int nIndex);
    t_OnChildNotify OnChildNotify_orig = reinterpret_cast<t_OnChildNotify>(kAddr_OnChildNotify);

    void __fastcall OnChildNotify_hook(void* pThis, void* edx, int nId, int nMsg, int nIndex) {
        __try {
        if (nMsg == kNotify_TabClick) {
            // While the buyback list is up, neither native tab has a list behind it -
            // "All" would draw the empty +0xC4 and "Must" the empty +0xC8. Both mean
            // "go back to the NPC's stock"; remember which one, so the rebuilt window
            // lands on the tab that was actually clicked.
            if (nId == kCtrlId_BuyTab && g_bBuybackMode &&
                (nIndex == kTabIndex_Shop || nIndex == kTabIndex_Must)) {
                g_nPendingSelect = nIndex;
                SendShopMode(kMode_ShowShop);
                return;
            }
        }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        OnChildNotify_orig(pThis, edx, nId, nMsg, nIndex);
    }

    // The control renders the button, but its own hit test never sees the click: mouse
    // input reaches the dialog, and the strip that used to swallow this area is now
    // narrowed, so the click is picked up here by rect instead.
    typedef void(__fastcall* t_OnEvent)(void* pSub4, void* edx, int nMsg, int a2, int x, int y);
    t_OnEvent OnEvent_orig = reinterpret_cast<t_OnEvent>(kAddr_OnEvent);

    void __fastcall OnEvent_hook(void* pSub4, void* edx, int nMsg, int a2, int x, int y) {
        if (nMsg == kMsg_LButtonDown && g_bTabAdded &&
            x >= kHitX0 && x < kHitX1 && y >= kHitY0 && y < kHitY1) {
            if (!g_bBuybackMode) SendShopMode(kMode_ShowBuyback);
            return;             // consumed - never let it fall through to an item row
        }
        OnEvent_orig(pSub4, edx, nMsg, a2, x, y);
    }

    // CCtrlTab draws a tab highlighted on a plain equality test - sub_4DD903 does
    // "cmp <index>, [this+0x3C] / sete" at 0x4DDBA2 - so an index no tab holds leaves
    // every tab idle. It cannot simply be parked there, because CShopDlg reads the same
    // field to pick which commodity array and which scroll position to use
    // (index == 0 -> +0xC4, and [this + index*4 + 0x108], which -1 would read off the
    // front of). So it is swapped in for the duration of the strip's draw only.
    typedef void(__fastcall* t_CtrlTabDraw)(void* pThis, void* edx, int a1, int a2, int a3);
    t_CtrlTabDraw CtrlTabDraw_orig = reinterpret_cast<t_CtrlTabDraw>(kAddr_CtrlTabDraw);

    void __fastcall CtrlTabDraw_hook(void* pThis, void* edx, int a1, int a2, int a3) {
        int* pSel = nullptr;
        int  nSaved = 0;
        __try {
            if (g_bBuybackMode && pThis && pThis == g_pStrip) {
                pSel = reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + kOff_TabSelected);
                nSaved = *pSel;
                *pSel = -1;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { pSel = nullptr; }

        CtrlTabDraw_orig(pThis, edx, a1, a2, a3);

        if (pSel) {
            __try { *pSel = nSaved; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    typedef void(__fastcall* t_DrawList)(void* pThis, void* edx, IWzCanvas* pCanvas);
    t_DrawList DrawList_orig = reinterpret_cast<t_DrawList>(kAddr_DrawList);

    void __fastcall DrawList_hook(void* pThis, void* edx, IWzCanvas* pCanvas) {
        DrawList_orig(pThis, edx, pCanvas);
        __try { DrawBuybackButton(pCanvas); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    typedef void(__fastcall* t_SendClose)(void* pThis, void* edx);
    t_SendClose SendClose_orig = reinterpret_cast<t_SendClose>(kAddr_SendClose);

    void __fastcall SendClose_hook(void* pThis, void* edx) {
        // Swallowed only while WE are swapping one shop window for another: the server
        // still thinks the shop is open, and telling it otherwise would clear it.
        if (g_bReplacingShop) return;
        SendClose_orig(pThis, edx);
    }

} // namespace

// Layout: [opcode:2][buybackListShown:1][hasSoldItems:1]
void HandleModePacket(CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    const size_t saved = packet->GetOffset();
    unsigned short opcode = 0;
    if (!packet->TryPeekOpcode(opcode)) {
        return;
    }
    packet->SetOffset(saved + 2);
    if (!packet->CanRead(2)) {
        packet->SetOffset(saved);
        return;
    }
    g_bBuybackMode = packet->Decode<unsigned char>() != 0;
    g_bHasSoldItems = packet->Decode<unsigned char>() != 0;
}

void OnBeforeOpenShop() {
    g_nLoadTries = 0;               // a fresh load budget for every shop
    g_bTabAdded = false;            // no clicks over an area not drawn on yet
    g_pStrip = nullptr;             // the old strip dies with the old window
    EnsureTabSprites();

    // A shop packet while a shop window is already up is a tab switch: replace the
    // window instead of letting the client throw on its one-at-a-time guard.
    if (*reinterpret_cast<void**>(kAddr_OpenDlg)) CloseOpenShopDlg();
}

void ApplyHooks(bool bEnable) {
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&SetShopDlg_orig),
                    reinterpret_cast<void*>(SetShopDlg_hook));
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&OnChildNotify_orig),
                    reinterpret_cast<void*>(OnChildNotify_hook));
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&DrawList_orig),
                    reinterpret_cast<void*>(DrawList_hook));
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&CtrlTabDraw_orig),
                    reinterpret_cast<void*>(CtrlTabDraw_hook));
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&OnEvent_orig),
                    reinterpret_cast<void*>(OnEvent_hook));
    Memory::SetHook(bEnable, reinterpret_cast<void**>(&SendClose_orig),
                    reinterpret_cast<void*>(SendClose_hook));
}

} // namespace ShopBuyback
