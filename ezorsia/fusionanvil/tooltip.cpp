#include "stdafx.h"
#include "compat/hook.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include <vector>

// v083 zh-CN client renders narrow strings as GBK/CP936. UTF-8 source literals
// (e.g. L"幻化" from a UTF-8 .cpp) display as mojibake (澶艳 / 骞诲寇).
namespace {
// 外观 : / (幻化) in GBK
static const char kLabelAppearance[] = "\xCD\xE2\xB9\xD3 :";
static const char kSuffixTransmog[]  = " (\xBB\xC3\xBB\xAF)";
static const char kWordTransmog[]    = "\xBB\xC3\xBB\xAF";

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[32] = {};
    if (MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}
} // namespace

// ===========================================================================
// Custom.wz cash-item string fallback
// CItemInfo eagerly caches String/<category>.img into a ZMap at +0x54 during
// startup. GetItemString @ 0x5CF6FE reads only that cache, so cash IDs added
// later under Custom.wz/String/Cash.img aren't visible. Hook falls back to a
// live GetObjectA lookup so tooltips resolve without mutating the cache.
// ===========================================================================
static constexpr uintptr_t kAddr_GetItemString = 0x005CF6FE;
static constexpr uintptr_t kAddr_ZXString_Cat  = 0x00428D30;

typedef ZXString<char>* (__thiscall* t_GetItemString)(
    void* pThis, ZXString<char>* result, int nItemID, const char* sPropName);
static auto Original_GetItemString =
    reinterpret_cast<t_GetItemString>(kAddr_GetItemString);

ZXString<char>* __fastcall Hook_GetItemString(
    void* pThis, void* /*edx*/, ZXString<char>* result, int nItemID, const char* sPropName)
{
    Original_GetItemString(pThis, result, nItemID, sPropName);

    if (!result->IsEmpty() || !sPropName) {
        return result;
    }
    if (nItemID < 5000000 || nItemID >= 6000000) {
        return result;
    }

    wchar_t wProp[64];
    if (MultiByteToWideChar(CP_ACP, 0, sPropName, -1, wProp, _countof(wProp)) <= 0) {
        return result;
    }
    wchar_t wPath[192];
    _snwprintf_s(wPath, _countof(wPath), _TRUNCATE, L"Custom/String/Cash.img/%d/%s", nItemID, wProp);

    Ztl_variant_t vObj = get_rm()->GetObjectA(wPath);
    if (vObj.vt != VT_BSTR || !V_BSTR(&vObj)) {
        return result;
    }

    BSTR bs = V_BSTR(&vObj);
    int nWide = static_cast<int>(SysStringLen(bs));
    if (nWide <= 0) {
        return result;
    }
    int nNarrow = WideCharToMultiByte(CP_ACP, 0, bs, nWide, nullptr, 0, nullptr, nullptr);
    if (nNarrow <= 0) {
        return result;
    }
    std::vector<char> buf(nNarrow + 1, 0);
    WideCharToMultiByte(CP_ACP, 0, bs, nWide, buf.data(), nNarrow, nullptr, nullptr);

    reinterpret_cast<void(__thiscall*)(void*, const char*, int)>(kAddr_ZXString_Cat)(
        result, buf.data(), nNarrow);
    return result;
}


class GW_ItemSlotEquip {
public:
    MEMBER_AT(TSecType<int>, 0xC, nItemID)
    MEMBER_AT(ZtlSecurePacked<unsigned char>, 0x28, nRUC)
    MEMBER_AT(ZtlSecure<short>, 0x34, niSTR)
    MEMBER_AT(ZtlSecure<short>, 0x3C, niDEX)
    MEMBER_AT(ZtlSecure<short>, 0x44, niINT)
    MEMBER_AT(ZtlSecure<short>, 0x4C, niLUK)
    MEMBER_AT(ZtlSecure<short>, 0x54, niMaxHP)
    MEMBER_AT(ZtlSecure<short>, 0x5C, niMaxMP)
    MEMBER_AT(ZtlSecure<short>, 0x64, niPAD)
    MEMBER_AT(ZtlSecure<short>, 0x6C, niMAD)
    MEMBER_AT(ZtlSecure<short>, 0x74, niPDD)
    MEMBER_AT(ZtlSecure<short>, 0x7C, niMDD)
    MEMBER_AT(ZtlSecure<short>, 0x84, niACC)
    MEMBER_AT(ZtlSecure<short>, 0x8C, niEVA)
    MEMBER_AT(ZtlSecure<short>, 0x94, niCraft)
    MEMBER_AT(ZtlSecure<short>, 0x9C, niSpeed)
    MEMBER_AT(ZtlSecure<short>, 0xA4, niJump)
    MEMBER_AT(ZtlSecure<short>, 0xAC, nAttribute)
    // Fusion Anvil transmog: int at offset 0xF9 holds the "skin" item id.
    MEMBER_AT(int, 0xF9, nAnvilItemID)
};


static int SafeGetAnvilItemId(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    int nAnvilItemID = 0;
    __try { nAnvilItemID = pe->nAnvilItemID; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return nAnvilItemID;
}


static constexpr uintptr_t kAddr_GetItemName            = 0x005CF63E;
static constexpr uintptr_t kAddr_SetToolTip_Equip_Basic = 0x008ECA0C;
static constexpr uintptr_t kAddr_DrawToolTip_Equip      = 0x008ED0D2;
static constexpr uintptr_t kAddr_get_basic_font         = 0x0098A707;

static auto CUIToolTip__SetToolTip_Equip_Basic =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, GW_ItemSlotEquip*)>(kAddr_SetToolTip_Equip_Basic);

void __fastcall CUIToolTip__SetToolTip_Equip_Basic_hook(
    CUIToolTip* pThis, void* /*edx*/, GW_ItemSlotEquip* pe)
{
    if (!pThis || !pe) {
        return;
    }

    // Keep vanilla zh-CN stat/type labels from String.wz/ToolTipHelp.img.
    CUIToolTip__SetToolTip_Equip_Basic(pThis, pe);

    // Fusion Anvil: append skin item name only; do not replace base tooltip text.
    const int nAnvilItemID = SafeGetAnvilItemId(pe);
    if (nAnvilItemID != 0) {
        ZXString<char> sSkinName;
        reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
            kAddr_GetItemName)(CItemInfo::GetInstance(), &sSkinName, nAnvilItemID);

        ZXString<char> sLine;
        sLine.Format("%s%s", static_cast<const char*>(sSkinName), kSuffixTransmog);
        pThis->AddInfoEx(14, 15, kLabelAppearance, sLine, 1, 1001);
    }
}


// ===========================================================================
// Transmog corner icon — draw the skin item's small icon in the top-right
// corner of the equip tooltip canvas after the engine has rendered it.
// ===========================================================================

static auto CUIToolTip__DrawToolTip_Equip =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, GW_ItemSlotEquip*)>(kAddr_DrawToolTip_Equip);

static auto get_basic_font =
    reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

void __fastcall CUIToolTip__DrawToolTip_Equip_hook(
    CUIToolTip* pThis, void* /*edx*/, int a2, GW_ItemSlotEquip* pe)
{
    CUIToolTip__DrawToolTip_Equip(pThis, a2, pe);
    const int nAnvilItemID = SafeGetAnvilItemId(pe);
    if (!pe || !nAnvilItemID || !pThis || !pThis->m_pLayer) {
        return;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        IWzCanvasPtr pCanvas = pThis->m_pLayer->Getcanvas(vIdx);
        if (!pCanvas) {
            return;
        }

        // Top-right corner — icons anchor at bottom-left, so y is the baseline.
        int iconX = pThis->m_nWidth - 32 - 14;
        int iconBaselineY = 6 + 32;
        CItemInfo::GetInstance()->DrawItemIconForSlot(
            pCanvas, nAnvilItemID, iconX, iconBaselineY, 0, 0, 0, 1, 0, 1);

        IWzFontPtr pFont;
        get_basic_font(std::addressof(pFont), 0);
        if (pFont) {
            pCanvas->DrawTextA(
                iconX - 4, iconBaselineY + 2,
                GbkToBstr(kWordTransmog),
                pFont, Ztl_variant_t(), Ztl_variant_t());
        }
    } catch (...) {}
}


namespace {
bool g_tooltipHooksAttached = false;
} // namespace

void AttachFusionAnvilTooltipHooks() {
    if (g_tooltipHooksAttached) {
        return;
    }
    g_tooltipHooksAttached = true;

    ATTACH_HOOK(Original_GetItemString, Hook_GetItemString);
    ATTACH_HOOK(CUIToolTip__SetToolTip_Equip_Basic, CUIToolTip__SetToolTip_Equip_Basic_hook);
    ATTACH_HOOK(CUIToolTip__DrawToolTip_Equip, CUIToolTip__DrawToolTip_Equip_hook);
}
