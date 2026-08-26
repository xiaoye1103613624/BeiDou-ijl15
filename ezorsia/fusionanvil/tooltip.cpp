#include "stdafx.h"
#include "Client.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include "compat/ztl/zcom.h"
#include "../setitem/equiptooltip_style.h"
#include "../setitem/SetItemApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../equipgrowth/EquipGrowthApi.h"
#include "FusionAnvilApi.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "itemoption_tip_data.inc"

// 1 = enter-green baseline (no EQUIP_TIP stamps / no color-strip symbols in .obj).
#ifndef GREEN_ENTER_BASELINE
#define GREEN_ENTER_BASELINE 0
#endif

// Temporary equip-marker UI mock — disable when real marker packets exist.
#ifndef MOCK_EQUIP_MARKERS
#define MOCK_EQUIP_MARKERS 1
#endif

// v083 zh-CN client renders narrow strings as GBK/CP936. UTF-8 source literals
// (e.g. L"幻化" from a UTF-8 .cpp) display as mojibake (澶艳 / 骞诲寇).
namespace {
// 外观 : / (幻化) in GBK
static const char kLabelAppearance[] = "\xCD\xE2\xB9\xD3 :";
static const char kSuffixTransmog[]  = " (\xBB\xC3\xBB\xAF)";
static const char kWordTransmog[]    = "\xBB\xC3\xBB\xAF";

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[64] = {};
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
    // Cash SN 5xxxxxx overlaps pets (5000xxx → String/Pet.img). Never probe
    // Custom/String/Cash.img for pets — GetObjectA throws STG_E_FILENOTFOUND
    // (0x80030002) and the client shows "error code : -2147287038" then exits.
    if (nItemID / 1000 == 5000) {
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

    try {
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
    } catch (...) {
        // Missing Custom cash string — leave result empty (vanilla behavior).
    }
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
    // 灵韵觉醒 fields (after anvil extension).
    MEMBER_AT(int, 0xFD, nEquipSkillID)
    MEMBER_AT(int, 0x101, nEquipSkillLevel)
    MEMBER_AT(unsigned long long, 0x105, tEquipSkillExpire)
    // Hyper / Potential + Bonus + Soul/Socket — size 0x140 (Phase10 socket3).
    MEMBER_AT(unsigned char, 0x10D, nEnhance)
    MEMBER_AT(unsigned char, 0x10E, nPotentialGrade)
    MEMBER_AT(unsigned short, 0x10F, nInfusion)
    MEMBER_AT(int, 0x110, nPotential1)
    MEMBER_AT(int, 0x114, nPotential2)
    MEMBER_AT(int, 0x118, nPotential3)
    MEMBER_AT(unsigned char, 0x11C, nBonusPotentialGrade)
    MEMBER_AT(int, 0x120, nBonusPotential1)
    MEMBER_AT(int, 0x124, nBonusPotential2)
    MEMBER_AT(int, 0x128, nBonusPotential3)
    MEMBER_AT(int, 0x12C, nSoulId)
    MEMBER_AT(int, 0x130, nSoulOption)
    MEMBER_AT(int, 0x134, nSocket1)
    MEMBER_AT(int, 0x138, nSocket2)
    MEMBER_AT(int, 0x13C, nSocket3)
    // 破界等级 — short @ 0x140 (struct grew to 0x142).
    MEMBER_AT(unsigned short, 0x140, nBreakthrough)
    // 宝石镶嵌等级 宝X — 服务端写入 bonus 尾段 reserved short 低字节(0~16)。
    MEMBER_AT(unsigned char, 0x11E, nGemInlay)
};


// Extended GW_ItemSlotEquip fields live at [0xF9, 0x140). Vanilla ctor only
// initializes through 0xF9; Cash Shop / local template equips never run the
// Decode hook that fills the tail — leftover heap garbage then shows as fake
// 幻化 / 灵韵 / 过期 / [当前等级: huge] / Hyper +20. Sanitize on read.
static constexpr int kMaxEquipSkillLevel = 30;
static constexpr int kMaxHyperEnhance = 25;

static bool IsPlausibleAnvilItemId(int itemId) {
    // Skin must be a Character equip id (incl. pet equips 18xxxxxx).
    return itemId >= 1000000 && itemId < 2000000;
}

static bool IsPlausibleEquipSkill(int skillId, int skillLv) {
    return skillId > 0 && skillLv > 0 && skillLv <= kMaxEquipSkillLevel;
}

static int SafeGetItemId(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    int itemId = 0;
    __try { itemId = static_cast<int>(pe->nItemID); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return itemId;
}

// 注能等级 ⚡ — 服务端写 nInfusion 低字节(0~10); 本地/模板装备未走 Decode hook,
// 尾部残留堆垃圾需跨安全校验(0~10)，仿 SafeGetHyper 的 clamp。
static int SafeGetInfusion(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    unsigned int raw = 0;
    __try { raw = static_cast<unsigned int>(pe->nInfusion & 0xFF); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (raw <= 0 || raw > 10) {
        return 0;
    }
    return static_cast<int>(raw);
}

// 破界等级 — short @ 0x140 (0~50); 未走 Decode hook 的本地/模板装备尾部
// 残留堆垃圾需跨安全校验(0~50)，仿 SafeGetInfusion。
static int SafeGetBreakthrough(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    unsigned int raw = 0;
    __try { raw = static_cast<unsigned int>(pe->nBreakthrough); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (raw <= 0 || raw > 50) {
        return 0;
    }
    return static_cast<int>(raw);
}

// 宝石镶嵌等级 宝X — 服务端写 nGemInlay 低字节(0~16); 未走 Decode hook 的
// 本地/模板装备尾部残留堆垃圾需跨安全校验(0~16)，仿 SafeGetInfusion。
static int SafeGetGemInlay(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    unsigned int raw = 0;
    __try { raw = static_cast<unsigned int>(pe->nGemInlay & 0xFF); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (raw <= 0 || raw > 16) {
        return 0;
    }
    return static_cast<int>(raw);
}

static int SafeGetAnvilItemId(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    int nAnvilItemID = 0;
    __try { nAnvilItemID = pe->nAnvilItemID; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!IsPlausibleAnvilItemId(nAnvilItemID)) {
        return 0;
    }
    return nAnvilItemID;
}

static void SafeGetEquipSkill(GW_ItemSlotEquip* pe, int* outId, int* outLv, unsigned long long* outExpire) {
    if (outId) *outId = 0;
    if (outLv) *outLv = 0;
    if (outExpire) *outExpire = 0;
    if (!pe) return;
    int sid = 0;
    int lv = 0;
    unsigned long long exp = 0;
    __try {
        sid = pe->nEquipSkillID;
        lv = pe->nEquipSkillLevel;
        exp = pe->tEquipSkillExpire;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    // Absurd skillLv (e.g. 1404742386) = uninit int; drop whole spirit block.
    if (!IsPlausibleEquipSkill(sid, lv)) {
        return;
    }
    if (outId) *outId = sid;
    if (outLv) *outLv = lv;
    if (outExpire) *outExpire = exp;
}

static void SafeGetEquipSkill(GW_ItemSlotEquip* pe, int* outId, int* outLv) {
    SafeGetEquipSkill(pe, outId, outLv, nullptr);
}

static void SafeGetHyperPotential(
    GW_ItemSlotEquip* pe,
    unsigned char* outEnhance,
    unsigned char* outGrade,
    int* outP1,
    int* outP2,
    int* outP3)
{
    if (outEnhance) *outEnhance = 0;
    if (outGrade) *outGrade = 0;
    if (outP1) *outP1 = 0;
    if (outP2) *outP2 = 0;
    if (outP3) *outP3 = 0;
    if (!pe) return;
    unsigned char enhance = 0;
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    int skillId = 0;
    int skillLv = 0;
    int anvil = 0;
    __try {
        enhance = pe->nEnhance;
        grade = pe->nPotentialGrade;
        p1 = pe->nPotential1;
        p2 = pe->nPotential2;
        p3 = pe->nPotential3;
        skillId = pe->nEquipSkillID;
        skillLv = pe->nEquipSkillLevel;
        anvil = pe->nAnvilItemID;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    // Sibling extension fields look like heap garbage → ignore Hyper/Pot too.
    if (skillLv < 0 || skillLv > kMaxEquipSkillLevel) {
        return;
    }
    if (anvil != 0 && !IsPlausibleAnvilItemId(anvil)) {
        return;
    }
    if (skillId != 0 && !IsPlausibleEquipSkill(skillId, skillLv)) {
        return;
    }
    // Do NOT clamp enhance>10 up to 10 — that turned CS garbage into fake ★10/+20.
    if (enhance > kMaxHyperEnhance) {
        enhance = 0;
    }
    if (outEnhance) *outEnhance = enhance;
    if (outGrade) *outGrade = grade;
    if (outP1) *outP1 = p1;
    if (outP2) *outP2 = p2;
    if (outP3) *outP3 = p3;
}

static void SafeGetBonusPotential(
    GW_ItemSlotEquip* pe,
    unsigned char* outGrade,
    int* outP1,
    int* outP2,
    int* outP3)
{
    if (outGrade) *outGrade = 0;
    if (outP1) *outP1 = 0;
    if (outP2) *outP2 = 0;
    if (outP3) *outP3 = 0;
    if (!pe) return;
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    int skillLv = 0;
    int anvil = 0;
    __try {
        grade = pe->nBonusPotentialGrade;
        p1 = pe->nBonusPotential1;
        p2 = pe->nBonusPotential2;
        p3 = pe->nBonusPotential3;
        skillLv = pe->nEquipSkillLevel;
        anvil = pe->nAnvilItemID;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (skillLv < 0 || skillLv > kMaxEquipSkillLevel) {
        return;
    }
    if (anvil != 0 && !IsPlausibleAnvilItemId(anvil)) {
        return;
    }
    if (outGrade) *outGrade = grade;
    if (outP1) *outP1 = p1;
    if (outP2) *outP2 = p2;
    if (outP3) *outP3 = p3;
}

static void SafeGetSoulSocket(
    GW_ItemSlotEquip* pe, int* outSoulId, int* outSoulOption,
    int* outSocket1, int* outSocket2 = nullptr, int* outSocket3 = nullptr)
{
    if (outSoulId) *outSoulId = 0;
    if (outSoulOption) *outSoulOption = 0;
    if (outSocket1) *outSocket1 = 0;
    if (outSocket2) *outSocket2 = 0;
    if (outSocket3) *outSocket3 = 0;
    if (!pe) return;
    int soulId = 0, soulOption = 0, socket1 = 0, socket2 = 0, socket3 = 0;
    int skillLv = 0;
    int anvil = 0;
    __try {
        soulId = pe->nSoulId;
        soulOption = pe->nSoulOption;
        socket1 = pe->nSocket1;
        socket2 = pe->nSocket2;
        socket3 = pe->nSocket3;
        skillLv = pe->nEquipSkillLevel;
        anvil = pe->nAnvilItemID;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (skillLv < 0 || skillLv > kMaxEquipSkillLevel) {
        return;
    }
    if (anvil != 0 && !IsPlausibleAnvilItemId(anvil)) {
        return;
    }
    if (outSoulId) *outSoulId = soulId;
    if (outSoulOption) *outSoulOption = soulOption;
    if (outSocket1) *outSocket1 = socket1;
    if (outSocket2) *outSocket2 = socket2;
    if (outSocket3) *outSocket3 = socket3;
}

static const char* PotentialGradeNameGbk(unsigned char g) {
    // 无 / 普通 / 稀有 / 史诗 / 独特 / 传说 — cube-era official names (server enum)
    switch (g) {
    case 1: return "\xC6\xD5\xCD\xA8";
    case 2: return "\xCF\xA1\xD3\xD0";
    case 3: return "\xCA\xB7\xCA\xAB";
    case 4: return "\xB6\xC0\xCC\xD8";
    case 5: return "\xB4\xAB\xCB\xB5";
    default: return "\xCE\xDE";
    }
}

// Fig3 / modern CMS tip letter grades mapped from server 1..5.
// No SSS in our MAX_GRADE=5; 传说→SS. Official cube names remain in PotentialGradeNameGbk.
static const char* PotentialGradeLetterGbk(unsigned char g) {
    switch (g) {
    case 1: return "C\xBC\xB6";   // C级
    case 2: return "B\xBC\xB6";   // B级
    case 3: return "A\xBC\xB6";   // A级
    case 4: return "S\xBC\xB6";   // S级
    case 5: return "SS\xBC\xB6";  // SS级
    default: return "?\xBC\xB6";
    }
}

// GBK labels for ItemOption stat keys (server XML has no string templates).
static const char* ItemOptionKeyLabelGbk(const char* key) {
    if (!key) return nullptr;
    if (strcmp(key, "incSTR") == 0) return "\xC1\xA6\xC1\xBF";                 // 力量
    if (strcmp(key, "incDEX") == 0) return "\xC3\xF4\xBD\xDD";                 // 敏捷
    if (strcmp(key, "incINT") == 0) return "\xD6\xC7\xC1\xA6";                 // 智力
    if (strcmp(key, "incLUK") == 0) return "\xD4\xCB\xC6\xF8";                 // 运气
    if (strcmp(key, "incSTRlv") == 0) return "\xC1\xA6\xC1\xBF/\xB5\xC8\xBC\xB6"; // 力量/等级
    if (strcmp(key, "incDEXlv") == 0) return "\xC3\xF4\xBD\xDD/\xB5\xC8\xBC\xB6";
    if (strcmp(key, "incINTlv") == 0) return "\xD6\xC7\xC1\xA6/\xB5\xC8\xBC\xB6";
    if (strcmp(key, "incLUKlv") == 0) return "\xD4\xCB\xC6\xF8/\xB5\xC8\xBC\xB6";
    if (strcmp(key, "incMHPlv") == 0) return "\xD7\xEE\xB4\xF3HP/\xB5\xC8\xBC\xB6";
    if (strcmp(key, "incEXPr") == 0) return "\xBE\xAD\xD1\xE9\xD6\xB5%"; // 经验值%
    if (strcmp(key, "incAsrR") == 0) return "\xD7\xB4\xCC\xAC\xD2\xEC\xB3\xA3"; // 状态异常抗
    if (strcmp(key, "incTerR") == 0) return "\xCA\xF4\xD0\xD4\xD2\xEC\xB3\xA3"; // 属性异常抗
    if (strcmp(key, "reduceCooltime") == 0) return "\xC0\xE4\xC8\xB4\xCB\xB3\xBC\xE1\xC9\xD9"; // 冷却时间减少
    if (strcmp(key, "attackType") == 0) return "\xB9\xA5\xBB\xF7\xC0\xE0\xD0\xCD"; // 攻击类型
    if (strcmp(key, "incSTRr") == 0) return "\xC1\xA6\xC1\xBF%";
    if (strcmp(key, "incDEXr") == 0) return "\xC3\xF4\xBD\xDD%";
    if (strcmp(key, "incINTr") == 0) return "\xD6\xC7\xC1\xA6%";
    if (strcmp(key, "incLUKr") == 0) return "\xD4\xCB\xC6\xF8%";
    if (strcmp(key, "incMHP") == 0) return "\xD7\xEE\xB4\xF3HP";           // 最大HP
    if (strcmp(key, "incMMP") == 0) return "\xD7\xEE\xB4\xF3MP";           // 最大MP
    if (strcmp(key, "incMHPr") == 0) return "\xD7\xEE\xB4\xF3HP%";
    if (strcmp(key, "incMMPr") == 0) return "\xD7\xEE\xB4\xF3MP%";
    if (strcmp(key, "incPAD") == 0) return "\xB9\xA5\xBB\xF7\xC1\xA6";         // 攻击力
    if (strcmp(key, "incMAD") == 0) return "\xC4\xA7\xB7\xA8\xC1\xA6";         // 魔法力
    if (strcmp(key, "incPDD") == 0) return "\xB7\xC0\xD3\xF9\xC1\xA6";         // 防御力
    if (strcmp(key, "incMDD") == 0) return "\xC4\xA7\xB7\xA8\xB7\xC0\xD3\xF9"; // 魔法防御
    if (strcmp(key, "incPADr") == 0) return "\xB9\xA5\xBB\xF7\xC1\xA6%";
    if (strcmp(key, "incMADr") == 0) return "\xC4\xA7\xB7\xA8\xC1\xA6%";
    if (strcmp(key, "incPDDr") == 0) return "\xB7\xC0\xD3\xF9\xC1\xA6%";
    if (strcmp(key, "incMDDr") == 0) return "\xC4\xA7\xB7\xA8\xB7\xC0\xD3\xF9%";
    if (strcmp(key, "incACC") == 0) return "\xC3\xFC\xD6\xD0";                 // 命中
    if (strcmp(key, "incEVA") == 0) return "\xBB\xD8\xB1\xDC";                 // 回避
    if (strcmp(key, "incACCr") == 0) return "\xC3\xFC\xD6\xD0%";
    if (strcmp(key, "incEVAr") == 0) return "\xBB\xD8\xB1\xDC%";
    if (strcmp(key, "incSpeed") == 0) return "\xD2\xC6\xB6\xAF\xCB\xD9\xB6\xC8"; // 移动速度
    if (strcmp(key, "incJump") == 0) return "\xCC\xF8\xD4\xBE";               // 跳跃
    if (strcmp(key, "incCr") == 0) return "\xB1\xA9\xBB\xF7\xC2\xCA";           // 爆击率
    if (strcmp(key, "incCriticaldamage") == 0
        || strcmp(key, "incCriticalDamage") == 0
        || strcmp(key, "incCriticaldamageMin") == 0
        || strcmp(key, "incCriticaldamageMax") == 0) {
        return "\xB1\xA9\xBB\xF7\xC9\xCB\xBA\xA6"; // 爆击伤害
    }
    if (strcmp(key, "incDAMr") == 0) return "\xC9\xCB\xBA\xA6%";               // 伤害%
    if (strcmp(key, "incAllStat") == 0 || strcmp(key, "incALLStat") == 0) {
        return "\xCB\xF9\xD3\xD0\xCA\xF4\xD0\xD4"; // 所有属性
    }
    if (strcmp(key, "boss") == 0) return "Boss\xC9\xCB\xBA\xA6"; // Boss伤害 (often with incDAMr)
    if (strcmp(key, "ignoreDAM") == 0) return "\xBA\xF6\xC2\xD4\xC9\xCB\xBA\xA6";
    if (strcmp(key, "ignoreDAMr") == 0) return "\xBA\xF6\xC2\xD4\xC9\xCB\xBA\xA6%";
    if (strcmp(key, "incMesoProp") == 0) return "\xBD\xF0\xB1\xD2\xB5\xF7\xC2\xCA"; // 金币掉落
    if (strcmp(key, "incRewardProp") == 0) return "\xCE\xEF\xC6\xB7\xB5\xF7\xC2\xCA"; // 物品掉落
    if (strcmp(key, "incAllskill") == 0) return "\xCB\xF9\xD3\xD0\xBC\xBC\xC4\xDC"; // 所有技能
    if (strcmp(key, "mpconReduce") == 0) return "MP\xCF\xFA\xBA\xC4";           // MP消耗
    if (strcmp(key, "mpRestore") == 0) return "MP\xBB\xD6\xB8\xB4";
    if (strcmp(key, "RecoveryHP") == 0) return "HP\xBB\xD6\xB8\xB4";
    if (strcmp(key, "RecoveryMP") == 0) return "MP\xBB\xD6\xB8\xB4";
    if (strcmp(key, "RecoveryUP") == 0) return "\xBB\xD6\xB8\xB4\xD0\xA7\xB9\xFB";
    if (strcmp(key, "DAMreflect") == 0) return "\xB7\xB4\xC9\xCB";             // 反伤
    if (strcmp(key, "prop") == 0) return "\xB8\xC5\xC2\xCA";                   // 概率
    if (strcmp(key, "time") == 0) return "\xCA\xB1\xBC\xE4";                   // 时间
    if (strcmp(key, "HP") == 0) return "HP";
    if (strcmp(key, "MP") == 0) return "MP";
    return nullptr;
}

static bool ItemOptionKeyIsPercent(const char* key) {
    if (!key) return false;
    size_t n = strlen(key);
    return n > 0 && key[n - 1] == 'r';
}

static const ItemOptTipEntry* FindItemOptTip(int optionId) {
    int lo = 0;
    int hi = kItemOptTipTableCount - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int id = kItemOptTipTable[mid].optionId;
        if (id == optionId) return &kItemOptTipTable[mid];
        if (id < optionId) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

static const ItemOptLevelEntry* PickItemOptLevel(const ItemOptTipEntry* tip, int potLevel) {
    if (!tip || !tip->levels || tip->nLevels <= 0) return nullptr;
    const ItemOptLevelEntry* best = nullptr;
    for (int i = 0; i < tip->nLevels; ++i) {
        const ItemOptLevelEntry& e = tip->levels[i];
        if (e.level == potLevel) return &e;
        if (e.level <= potLevel) best = &e;
    }
    if (best) return best;
    return &tip->levels[0];
}

static int SafeGetEquipReqLevel(int itemId) {
    if (itemId <= 0) return 1;
    try {
        IWzPropertyPtr pItem = CItemInfo::GetInstance()->GetItemInfo(itemId);
        if (!pItem) return 1;
        Ztl_variant_t vInfo;
        if (FAILED(pItem->get_item(const_cast<wchar_t*>(L"info"), &vInfo))) return 1;
        IWzPropertyPtr pInfo(vInfo.GetUnknown(false, false));
        if (!pInfo) return 1;
        Ztl_variant_t vReq;
        if (FAILED(pInfo->get_item(const_cast<wchar_t*>(L"reqLevel"), &vReq))) return 1;
        int req = get_int32(vReq, 1);
        if (req < 1) req = 1;
        return req;
    } catch (...) {
        return 1;
    }
}

static int PotLevelFromReq(int reqLevel) {
    int lv = (reqLevel + 9) / 10;
    if (lv < 1) lv = 1;
    if (lv > 20) lv = 20;
    return lv;
}

// Format one option into GBK tip text. Fallback "#id" if unknown.
// Options that only have prop/time (e.g. 901~905) used to yield empty string → blank 星岩/潜能行.
static std::string FormatItemOptionTipGbk(int optionId, int potLevel) {
    if (optionId <= 0) return std::string();
    const ItemOptTipEntry* tip = FindItemOptTip(optionId);
    if (!tip) {
        char buf[48];
        // GBK: 未知效果 #
        sprintf_s(buf, "\xCE\xB4\xD6\xAA\xD0\xA7\xB9\xFB #%d", optionId);
        return std::string(buf);
    }
    const ItemOptLevelEntry* lv = PickItemOptLevel(tip, potLevel);
    if (!lv || !lv->stats || lv->nStats <= 0) {
        char buf[48];
        sprintf_s(buf, "\xCE\xB4\xD6\xAA\xD0\xA7\xB9\xFB #%d", optionId);
        return std::string(buf);
    }
    std::string out;
    int prop = 0, timeSec = 0;
    // Collect primary stats (skip prop/time). Collapse STR+DEX+INT+LUK (same value)
    // into one「所有属性」line — option 22802 etc. looked like "5 lines" after cube.
    const ItemOptStatEntry* primary[16];
    int nPrimary = 0;
    for (int i = 0; i < lv->nStats && nPrimary < 16; ++i) {
        const ItemOptStatEntry& st = lv->stats[i];
        if (st.key && strcmp(st.key, "prop") == 0) {
            prop = st.value;
            continue;
        }
        if (st.key && strcmp(st.key, "time") == 0) {
            timeSec = st.value;
            continue;
        }
        primary[nPrimary++] = &st;
    }
    auto isFourStatKey = [](const char* key, bool wantPct) -> int {
        // returns bit: STR=1 DEX=2 INT=4 LUK=8
        if (!key) return 0;
        if (wantPct) {
            if (strcmp(key, "incSTRr") == 0) return 1;
            if (strcmp(key, "incDEXr") == 0) return 2;
            if (strcmp(key, "incINTr") == 0) return 4;
            if (strcmp(key, "incLUKr") == 0) return 8;
        } else {
            if (strcmp(key, "incSTR") == 0) return 1;
            if (strcmp(key, "incDEX") == 0) return 2;
            if (strcmp(key, "incINT") == 0) return 4;
            if (strcmp(key, "incLUK") == 0) return 8;
        }
        return 0;
    };
    auto tryCollapseAllStat = [&](bool wantPct) -> bool {
        if (nPrimary < 4) return false;
        int mask = 0;
        int commonVal = 0;
        bool haveVal = false;
        int used[16] = {};
        for (int i = 0; i < nPrimary; ++i) {
            int bit = isFourStatKey(primary[i]->key, wantPct);
            if (!bit) continue;
            if (mask & bit) return false; // duplicate
            if (!haveVal) {
                commonVal = primary[i]->value;
                haveVal = true;
            } else if (primary[i]->value != commonVal) {
                return false;
            }
            mask |= bit;
            used[i] = 1;
        }
        if (mask != 15) return false; // need all four
        // GBK: 所有属性 / 所有属性%
        const char* allLabel = wantPct
            ? "\xCB\xF9\xD3\xD0\xCA\xF4\xD0\xD4%"
            : "\xCB\xF9\xD3\xD0\xCA\xF4\xD0\xD4";
        char num[24];
        if (wantPct) sprintf_s(num, "+%d%%", commonVal);
        else sprintf_s(num, "+%d", commonVal);
        if (!out.empty()) out += "\n";
        out += allLabel;
        out += " : ";
        out += num;
        for (int i = 0; i < nPrimary; ++i) {
            if (used[i]) continue;
            const ItemOptStatEntry& st = *primary[i];
            if (!out.empty()) out += "\n";
            const char* label = ItemOptionKeyLabelGbk(st.key);
            const bool pct = ItemOptionKeyIsPercent(st.key)
                || (st.key && (strcmp(st.key, "incCr") == 0
                    || strcmp(st.key, "incMesoProp") == 0 || strcmp(st.key, "incRewardProp") == 0
                    || strcmp(st.key, "ignoreDAMr") == 0 || strcmp(st.key, "incDAMr") == 0));
            char nbuf[24];
            if (pct) sprintf_s(nbuf, "+%d%%", st.value);
            else sprintf_s(nbuf, "+%d", st.value);
            if (label) {
                out += label;
                out += " : ";
                out += nbuf;
            } else {
                out += st.key ? st.key : "?";
                out += " : ";
                out += nbuf;
            }
        }
        return true;
    };
    if (!tryCollapseAllStat(true) && !tryCollapseAllStat(false)) {
        for (int i = 0; i < nPrimary; ++i) {
            const ItemOptStatEntry& st = *primary[i];
            // One attribute per tip row (comma-join overflows companion width → 「运气」截断).
            if (!out.empty()) out += "\n";
            const char* label = ItemOptionKeyLabelGbk(st.key);
            char num[24];
            const bool pct = ItemOptionKeyIsPercent(st.key)
                || (st.key && (strcmp(st.key, "incCr") == 0
                    || strcmp(st.key, "incMesoProp") == 0 || strcmp(st.key, "incRewardProp") == 0
                    || strcmp(st.key, "ignoreDAMr") == 0 || strcmp(st.key, "incDAMr") == 0));
            if (pct) {
                sprintf_s(num, "+%d%%", st.value);
            } else {
                sprintf_s(num, "+%d", st.value);
            }
            if (label) {
                out += label;
                out += " : ";
                out += num;
            } else {
                out += st.key ? st.key : "?";
                out += " : ";
                out += num;
            }
        }
    }
    if (out.empty()) {
        // GBK: 特殊触发 概率N% 持续Ns  (#id)
        char buf[96];
        if (prop > 0 || timeSec > 0) {
            sprintf_s(buf,
                "\xCC\xD8\xCA\xE2\xB4\xA5\xB7\xA2 \xB8\xC5\xC2\xCA%d%% \xB3\xD6\xD0\xF8%ds (#%d)",
                prop > 0 ? prop : 0, timeSec > 0 ? timeSec : 0, optionId);
        } else {
            sprintf_s(buf, "\xCE\xB4\xD6\xAA\xD0\xA7\xB9\xFB #%d", optionId);
        }
        return std::string(buf);
    }
    return out;
}

// GBK: 灵韵
static const char kLabelSpirit[] = "\xC1\xE9\xD4\xCF :";
// GBK: 潜能 — fig3-style short header label
static const char kLabelPotentialShort[] = "\xC7\xB1\xC4\xDC";
// GBK: 潜在能力 — legacy long label (kept for reference)
static const char kLabelPotentialAbility[] = "\xC7\xB1\xD4\xDA\xC4\xDC\xC1\xA6";
// GBK: 附加潜能
static const char kLabelBonusPotential[] = "\xB8\xBD\xBC\xD3\xC7\xB1\xC4\xDC";
// GBK: 未鉴定的潜能 (Phase7 hidden state)
static const char kLabelUnrevealedPotential[] = "\xCE\xB4\xBC\xF8\xB6\xA8\xB5\xC4\xC7\xB1\xC4\xDC";
// GBK: 灵魂宝珠 (珠=D6 E9; was wrongly 轴=D6 E1)
static const char kLabelSoulOrb[] = "\xC1\xE9\xBB\xEA\xB1\xA6\xD6\xE9";
// GBK: 星岩
static const char kLabelSocket[] = "\xD0\xC7\xD1\xD2";

static std::string LookupSkillFieldGbk(int skillId, const wchar_t* field) {
    if (skillId <= 0 || !field) {
        return std::string();
    }
    try {
        wchar_t path[64] = {};
        swprintf_s(path, L"String/Skill.img/%07d", skillId);
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

static Ztl_bstr_t LookupSkillFieldBstr(int skillId, const wchar_t* field) {
    if (skillId <= 0 || !field) {
        return Ztl_bstr_t(L"");
    }
    try {
        wchar_t path[64] = {};
        swprintf_s(path, L"String/Skill.img/%07d", skillId);
        IWzPropertyPtr property = get_rm()->GetObjectA(path).GetUnknown();
        if (!property) {
            return Ztl_bstr_t(L"");
        }
        Ztl_variant_t value = property->item[field];
        if (value.vt == VT_BSTR && V_BSTR(&value)) {
            return Ztl_bstr_t(V_BSTR(&value));
        }
    } catch (...) {
    }
    return Ztl_bstr_t(L"");
}

static std::string LookupSkillNameGbk(int skillId) {
    return LookupSkillFieldGbk(skillId, L"name");
}

static IWzCanvasPtr LoadSkillIconCanvas(int skillId) {
    IWzCanvasPtr out;
    if (skillId <= 0) {
        return out;
    }
    auto rm = get_rm();
    if (!rm) {
        return out;
    }
    try {
        char uol[128];
        sprintf_s(uol, "Skill/%d.img/skill/%d/icon", skillId / 10000, skillId);
        Ztl_variant_t v1(vtMissing);
        Ztl_variant_t v2(vtMissing);
        Ztl_variant_t obj = rm->GetObjectA(Ztl_bstr_t(uol), v1, v2);
        IUnknown* unk = obj.GetUnknown(false, false);
        if (!unk) {
            return out;
        }
        IWzCanvas* raw = nullptr;
        if (FAILED(unk->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw))) || !raw) {
            return out;
        }
        out = raw;
        raw->Release();
    } catch (...) {
    }
    return out;
}

// Estimate wrapped line count for GBK tip body (~28 chars / line @ tip width).
static int EstimateWrapLines(const std::string& s, int charsPerLine) {
    if (s.empty() || charsPerLine <= 0) {
        return 0;
    }
    int lines = 1;
    int col = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n') {
            ++lines;
            col = 0;
            continue;
        }
        int w = (c >= 0x80) ? 2 : 1; // rough GBK width
        if (i + 1 < s.size() && c >= 0x80) {
            // consume trail byte as same cell
        }
        col += (c >= 0x80) ? 1 : 1; // count chars loosely
        if (col >= charsPerLine) {
            ++lines;
            col = 0;
        }
        if (c >= 0x80 && i + 1 < s.size()) {
            ++i; // skip GBK trail
        }
    }
    return (std::max)(1, lines);
}

static int CalcSpiritTipExtraHeight(int skillId, int skillLv, unsigned long long expire) {
    if (skillId <= 0 || skillLv <= 0) {
        return 0;
    }
    int h = 0;
    h += 22; // skill name
    if (expire != 0) {
        h += 16; // expire line
    }
    h += 10; // separator
    std::string desc = LookupSkillFieldGbk(skillId, L"desc");
    if (desc.empty()) {
        wchar_t hl[16];
        swprintf_s(hl, L"h%d", skillLv);
        desc = LookupSkillFieldGbk(skillId, hl);
    }
    const int descLines = EstimateWrapLines(desc, 22);
    const int descBlock = (std::max)(68, descLines * 14 + 4);
    h += descBlock;
    h += 10; // separator
    h += 16; // [Current Level]
    wchar_t hl[16];
    swprintf_s(hl, L"h%d", skillLv);
    std::string lvDesc = LookupSkillFieldGbk(skillId, hl);
    h += EstimateWrapLines(lvDesc, 28) * 14 + 8;
    return h;
}

static void DrawWrappedGbk(
    IWzCanvasPtr canvas, IWzFontPtr font, int x, int y, int maxChars, const std::string& gbk, int lineH)
{
    if (!canvas || !font || gbk.empty()) {
        return;
    }
    std::string line;
    int col = 0;
    int cy = y;
    auto flush = [&]() {
        if (line.empty()) {
            return;
        }
        canvas->DrawTextA(x, cy, GbkToBstr(line.c_str()), font, Ztl_variant_t(), Ztl_variant_t());
        line.clear();
        col = 0;
        cy += lineH;
    };
    for (size_t i = 0; i < gbk.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(gbk[i]);
        if (c == '\n') {
            flush();
            continue;
        }
        if (c >= 0x80 && i + 1 < gbk.size()) {
            if (col + 1 >= maxChars) {
                flush();
            }
            line.push_back(static_cast<char>(c));
            line.push_back(gbk[i + 1]);
            ++i;
            col += 1;
        } else {
            if (col + 1 >= maxChars) {
                flush();
            }
            line.push_back(static_cast<char>(c));
            col += 1;
        }
    }
    flush();
}

// ---------------------------------------------------------------------------
// 083 IDA map (BeiDou.exe.i64, imagebase 0x400000) — verified 2026-08-03 MCP.
// Hex-Rays may label these __stdcall; prologues use ecx=this + retn N.
// ---------------------------------------------------------------------------
// VA            | Symbol                         | Convention (hook as __fastcall)
// 0x008ECA0C    | SetToolTip_Equip_Basic         | thiscall; retn 4;  (this, GW_ItemSlotEquip*)
// 0x008E7836    | PrintValue                     | thiscall; retn 10h; (this, nType, nValue, ZXString, bShowAlways)
//                                               | → AddInfoEx(14, 0x10, …) vanilla
// 0x008F39E1    | AddInfoEx                      | thiscall; (this, nType, nSubType, sCtx, sSub, bDot, nAlign)
// 0x008ED0D2    | DrawToolTip_Equip              | thiscall; retn 8;  (this, layout/a2, GW_ItemSlotEquip*)
// 0x008F36A1    | GetFontByType                  | thiscall; retn 8;  case N → this+off
// 0x008E8252    | SetToolTip_Equip (full)        | calls Basic @0x8E8E67 + Draw @0x8E9EA2
// 0x008E7975    | SetToolTip_Equip_Simple        | calls Basic @0x8E7B1C + Draw @0x8E8035
// GetFontByType offsets (ASM): 2→+0x428  14→+0x460  15→+0x464  16→+0x468
// ---------------------------------------------------------------------------
static constexpr uintptr_t kAddr_GetItemName            = 0x005CF63E;
static constexpr uintptr_t kAddr_SetToolTip_Equip_Basic = 0x008ECA0C;
static constexpr uintptr_t kAddr_DrawToolTip_Equip      = 0x008ED0D2;
static constexpr uintptr_t kAddr_get_basic_font         = 0x0098A707;
static constexpr uintptr_t kAddr_PrintValue             = 0x008E7836;
static constexpr uintptr_t kAddr_StringPoolGet          = 0x0079E993;
static constexpr uintptr_t kAddr_StringPoolInstance     = 0x0079E805;
static constexpr uintptr_t kAddr_GetFontByType          = 0x008F36A1;
static constexpr uintptr_t kAddr_AddInfoEx              = 0x008F39E1;

static auto get_basic_font =
    reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

static int g_spiritTipExtraH = 0;

// ---------------------------------------------------------------------------
// Equip stat breakdown — §10.3 palette (萧曳083玩法.md).
// AddInfoEx = 2 fonts/line → monochrome paren on main line; colored strip via
// DrawTextA under tip (extra height), one strip per stat with sources.
// ---------------------------------------------------------------------------
static constexpr int kHyperStarMaxTip = 25;
static constexpr bool kTipPacketIncludesHyperPotential = true;
static constexpr int kFontTipLabel = 14;   // GetFontByType case 14 → +0x460
static constexpr int kFontTipYellow = 2;   // case 2 → +0x428
static constexpr int kFontTipPurple = 15;  // case 15 → +0x464
// Was wrongly 0x464/0x468 (types 15/16). Remap must hit the slots AddInfoEx(14,15) resolves.
static constexpr uintptr_t kOffFontTipLabel = 0x460;
static constexpr uintptr_t kOffFontTipPurple = 0x464;

// §10.3 ARGB
static constexpr unsigned long kArgbBase = 0xFFFFFFFFu;
static constexpr unsigned long kArgbScroll = 0xFFFFAA40u;   // 橙 强化
static constexpr unsigned long kArgbChaos = 0xFF2DB84Du;    // 深绿 混沌
static constexpr unsigned long kArgbHyper = 0xFFE8C840u;    // 金黄 星力
static constexpr unsigned long kArgbPot = 0xFFC070E8u;      // 紫 主潜能
static constexpr unsigned long kArgbBonus = 0xFF48D0D8u;    // 青 附加
static constexpr unsigned long kArgbSoul = 0xFFE060C0u;     // 品红 灵魂
static constexpr unsigned long kArgbSocket = 0xFF90A0B0u;   // 银灰 星岩
static constexpr unsigned long kArgbReforge = 0xFFFF6B6Bu;  // 珊瑚 洗炼
static constexpr unsigned long kGrowthBonusArgb = 0xFF48D0D8u;

static constexpr unsigned long kPotGradeArgb[6] = {
    0xFFAAAAAAu, 0xFFB0B0B0u, 0xFF5BA8E0u, 0xFFC070E8u, 0xFFE8C040u, 0xFF6ED86Eu,
};
static constexpr unsigned long kPotHeaderBarMain = 0xFFE8C840u;
static constexpr unsigned long kPotHeaderBarBonus = 0xFF48D0D8u;
static constexpr unsigned long kPotHeaderBarSoul = 0xFFC070E8u;
static constexpr unsigned long kPotHeaderBarSocket = 0xFF90A0B0u;
static constexpr unsigned long kPotHeaderTextDark = 0xFF101010u;
static constexpr unsigned long kPotParenGreenArgb = 0xFF6ED86Eu;
static constexpr int kFontTipGreen = 15; // remap slot shared with purple when needed


typedef HRESULT(__thiscall* PotWzFontCreate_t)(
    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto PotWzFontCreate = reinterpret_cast<PotWzFontCreate_t>(0x0046341A);

static IWzFontPtr g_potGradeFonts[6];
static CUIToolTip* g_potRemapTip = nullptr;
static void* g_potSavedFont14 = nullptr;
static void* g_potSavedFont15 = nullptr;

static void RestorePotentialFontRemap() {
    if (!g_potRemapTip) {
        return;
    }
    char* base = reinterpret_cast<char*>(g_potRemapTip);
    *reinterpret_cast<void**>(base + kOffFontTipLabel) = g_potSavedFont14;
    *reinterpret_cast<void**>(base + kOffFontTipPurple) = g_potSavedFont15;
    g_potRemapTip = nullptr;
    g_potSavedFont14 = nullptr;
    g_potSavedFont15 = nullptr;
}

static IWzFontPtr EnsurePotentialGradeFont(unsigned char grade) {
    unsigned char g = grade;
    if (g > 5) {
        g = 0;
    }
    if (g_potGradeFonts[g]) {
        return g_potGradeFonts[g];
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_potGradeFonts[g], nullptr);
        if (!g_potGradeFonts[g]) {
            return nullptr;
        }
        const Ztl_variant_t style(L"");
        if (FAILED(PotWzFontCreate(
                g_potGradeFonts[g], L"Dotum", 12, kPotGradeArgb[g], style))) {
            g_potGradeFonts[g] = nullptr;
            return nullptr;
        }
        return g_potGradeFonts[g];
    } catch (...) {
        g_potGradeFonts[g] = nullptr;
        return nullptr;
    }
}

// Remap tip font slots 14/15 to grade Dotum so AddInfoEx Draw picks grade color.
static void ApplyPotentialFontRemap(CUIToolTip* tip, unsigned char grade) {
    if (!tip) {
        return;
    }
    IWzFontPtr font = EnsurePotentialGradeFont(grade > 0 ? grade : 1);
    if (!font) {
        return;
    }
    if (g_potRemapTip == tip) {
        char* base = reinterpret_cast<char*>(tip);
        void* raw = static_cast<IWzFont*>(font);
        *reinterpret_cast<void**>(base + kOffFontTipLabel) = raw;
        *reinterpret_cast<void**>(base + kOffFontTipPurple) = raw;
        return;
    }
    RestorePotentialFontRemap();
    char* base = reinterpret_cast<char*>(tip);
    g_potSavedFont14 = *reinterpret_cast<void**>(base + kOffFontTipLabel);
    g_potSavedFont15 = *reinterpret_cast<void**>(base + kOffFontTipPurple);
    void* raw = static_cast<IWzFont*>(font);
    *reinterpret_cast<void**>(base + kOffFontTipLabel) = raw;
    *reinterpret_cast<void**>(base + kOffFontTipPurple) = raw;
    g_potRemapTip = tip;
}

static constexpr unsigned kPoolStatIds[15] = {
    2042, 2043, 2044, 2045, 2046, 2047,
    657, 658, 659, 660, 661, 662, 663, 664, 665
};
static const char* kPotFlatKeys[15] = {
    "incSTR", "incDEX", "incINT", "incLUK", "incMHP", "incMMP",
    "incPAD", "incMAD", "incPDD", "incMDD", "incACC", "incEVA",
    nullptr, "incSpeed", "incJump"
};
// Percent potential keys (server panel applies %; tip shows in brackets).
static const char* kPotPercentKeys[15] = {
    "incSTRr", "incDEXr", "incINTr", "incLUKr", "incMHPr", "incMMPr",
    "incPADr", "incMADr", "incPDDr", "incMDDr", "incACCr", "incEVAr",
    nullptr, nullptr, nullptr
};

static GW_ItemSlotEquip* g_breakdownPe = nullptr;
static bool g_inEquipBasicTip = false;
static char g_statLabelBuf[15][64] = {};
static size_t g_statLabelLen[15] = {};
static bool g_statLabelsReady = false;

static void EnsureEquipStatLabels() {
    if (g_statLabelsReady) {
        return;
    }
    void* pool = nullptr;
    try {
        pool = reinterpret_cast<void*(__cdecl*)()>(kAddr_StringPoolInstance)();
    } catch (...) {
        return;
    }
    if (!pool) {
        return;
    }
    auto getStr = reinterpret_cast<ZXString<char>*(__fastcall*)(
        void*, void*, ZXString<char>*, unsigned int, char)>(kAddr_StringPoolGet);
    for (int i = 0; i < 15; ++i) {
        try {
            ZXString<char> s;
            getStr(pool, nullptr, &s, kPoolStatIds[i], 0);
            const char* p = static_cast<const char*>(s);
            if (!p) {
                continue;
            }
            size_t n = strlen(p);
            while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == ':' || p[n - 1] == '\t')) {
                --n;
            }
            if (n >= sizeof(g_statLabelBuf[i])) {
                n = sizeof(g_statLabelBuf[i]) - 1;
            }
            memcpy(g_statLabelBuf[i], p, n);
            g_statLabelBuf[i][n] = 0;
            g_statLabelLen[i] = n;
        } catch (...) {
        }
    }
    g_statLabelsReady = true;
}

static int MatchEquipStatByLabel(const char* prop) {
    if (!prop || !*prop) {
        return -1;
    }
    EnsureEquipStatLabels();
    for (int i = 0; i < 15; ++i) {
        if (g_statLabelLen[i] == 0) {
            continue;
        }
        if (strncmp(prop, g_statLabelBuf[i], g_statLabelLen[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int ClampEnhanceStars(unsigned char enhance) {
    int n = static_cast<int>(enhance);
    if (n < 0 || n > kHyperStarMaxTip) {
        return 0;
    }
    return n;
}

static int HyperBonusForStat(int statIdx, int stars, int itemId) {
    if (stars <= 0) return 0;
    // Sync HyperEnhanceTable.java: ★1-5 +2 allstat, ★6-10 +3; weapon atk 3/4/5, armor 0
    int all = 0, atk = 0;
    const bool weapon = (itemId / 1000000 == 1) && (
        (itemId / 10000) == 130 || (itemId / 10000) == 131 || (itemId / 10000) == 132
        || (itemId / 10000) == 133 || (itemId / 10000) == 137 || (itemId / 10000) == 138
        || (itemId / 10000) == 140 || (itemId / 10000) == 141 || (itemId / 10000) == 142
        || (itemId / 10000) == 143 || (itemId / 10000) == 144 || (itemId / 10000) == 145
        || (itemId / 10000) == 146 || (itemId / 10000) == 147 || (itemId / 10000) == 148
        || (itemId / 10000) == 149);
    for (int s = 1; s <= stars && s <= kHyperStarMaxTip; ++s) {
        all += (s <= 5) ? 2 : 3;
        if (weapon) {
            if (s <= 5) atk += 3;
            else if (s <= 9) atk += 4;
            else atk += 5;
        }
    }
    if (statIdx >= 0 && statIdx <= 3) return all; // STR DEX INT LUK
    if (statIdx == 6 || statIdx == 7) return atk; // WATK MATK
    return 0;
}

static short TipSafeShort(ZtlSecure<short>& v) {
    try {
        return static_cast<short>(v);
    } catch (...) {
        return 0;
    }
}

/** CItemInfo default (底板); scroll delta = packetBoard − default. */
static int EquipDefaultForStat(int itemId, int statIdx) {
    if (itemId <= 0 || statIdx < 0 || statIdx >= 15) {
        return 0;
    }
    try {
        CItemInfo::EQUIPITEM* eq = CItemInfo::GetInstance()->GetEquipItem(itemId);
        if (!eq) {
            return 0;
        }
        switch (statIdx) {
        case 0: return TipSafeShort(eq->niSTR);
        case 1: return TipSafeShort(eq->niDEX);
        case 2: return TipSafeShort(eq->niINT);
        case 3: return TipSafeShort(eq->niLUK);
        case 4: return TipSafeShort(eq->niMaxHP);
        case 5: return TipSafeShort(eq->niMaxMP);
        case 6: return TipSafeShort(eq->niPAD);
        case 7: return TipSafeShort(eq->niMAD);
        case 8: return TipSafeShort(eq->niPDD);
        case 9: return TipSafeShort(eq->niMDD);
        case 10: return TipSafeShort(eq->niACC);
        case 11: return TipSafeShort(eq->niEVA);
        case 12: return TipSafeShort(eq->niCraft);
        case 13: return TipSafeShort(eq->niSpeed);
        case 14: return TipSafeShort(eq->niJump);
        default: return 0;
        }
    } catch (...) {
        return 0;
    }
}

static void CollectOptsGroup(
    GW_ItemSlotEquip* pe, int* optsOut, int* countOut, int group /*0 main 1 bonus 2 soul 3 socket*/)
{
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    SafeGetHyperPotential(pe, nullptr, &grade, &p1, &p2, &p3);
    unsigned char bGrade = 0;
    int b1 = 0, b2 = 0, b3 = 0;
    SafeGetBonusPotential(pe, &bGrade, &b1, &b2, &b3);
    int soulId = 0, soulOption = 0, socket1 = 0, socket2 = 0, socket3 = 0;
    SafeGetSoulSocket(pe, &soulId, &soulOption, &socket1, &socket2, &socket3);
    int raw[4] = {};
    int nRaw = 0;
    if (group == 0) {
        raw[0] = p1; raw[1] = p2; raw[2] = p3; nRaw = 3;
    } else if (group == 1) {
        raw[0] = b1; raw[1] = b2; raw[2] = b3; nRaw = 3;
    } else if (group == 2) {
        raw[0] = soulOption; nRaw = 1;
    } else {
        raw[0] = socket1; raw[1] = socket2; raw[2] = socket3; nRaw = 3;
    }
    int n = 0;
    for (int i = 0; i < nRaw; ++i) {
        if (raw[i] > 0) {
            optsOut[n++] = raw[i];
        }
    }
    *countOut = n;
}

static void CollectPotentialOpts(GW_ItemSlotEquip* pe, int* optsOut /*[10]*/, int* countOut) {
    int n = 0;
    int tmp[4] = {};
    int tn = 0;
    for (int g = 0; g < 4; ++g) {
        CollectOptsGroup(pe, tmp, &tn, g);
        for (int i = 0; i < tn && n < 10; ++i) {
            optsOut[n++] = tmp[i];
        }
    }
    *countOut = n;
}

static int SumOptKeyFromOpts(const int* opts, int n, int potLevel, const char* key) {
    if (!opts || n <= 0 || !key) {
        return 0;
    }
    int sum = 0;
    for (int oi = 0; oi < n; ++oi) {
        const ItemOptTipEntry* tip = FindItemOptTip(opts[oi]);
        const ItemOptLevelEntry* lv = PickItemOptLevel(tip, potLevel);
        if (!lv || !lv->stats) {
            continue;
        }
        for (int si = 0; si < lv->nStats; ++si) {
            const ItemOptStatEntry& st = lv->stats[si];
            if (st.key && strcmp(st.key, key) == 0) {
                sum += st.value;
            }
        }
    }
    return sum;
}

static int SumOptKey(GW_ItemSlotEquip* pe, const char* key) {
    if (!pe || !key) {
        return 0;
    }
    int opts[10] = {};
    int n = 0;
    CollectPotentialOpts(pe, opts, &n);
    const int potLevel = PotLevelFromReq(SafeGetEquipReqLevel(SafeGetItemId(pe)));
    return SumOptKeyFromOpts(opts, n, potLevel, key);
}

static int SumOptKeyGroup(GW_ItemSlotEquip* pe, int group, const char* key) {
    if (!pe || !key) {
        return 0;
    }
    int opts[4] = {};
    int n = 0;
    CollectOptsGroup(pe, opts, &n, group);
    const int potLevel = PotLevelFromReq(SafeGetEquipReqLevel(SafeGetItemId(pe)));
    return SumOptKeyFromOpts(opts, n, potLevel, key);
}

static int PotentialFlatForStat(GW_ItemSlotEquip* pe, int statIdx) {
    if (!pe || statIdx < 0 || statIdx >= 15 || !kPotFlatKeys[statIdx]) {
        return 0;
    }
    return SumOptKey(pe, kPotFlatKeys[statIdx]);
}

static int PotentialPercentForStat(GW_ItemSlotEquip* pe, int statIdx) {
    if (!pe || statIdx < 0 || statIdx >= 15 || !kPotPercentKeys[statIdx]) {
        return 0;
    }
    return SumOptKey(pe, kPotPercentKeys[statIdx]);
}

static int FlatForGroupStat(GW_ItemSlotEquip* pe, int group, int statIdx) {
    if (!pe || statIdx < 0 || statIdx >= 15 || !kPotFlatKeys[statIdx]) {
        return 0;
    }
    return SumOptKeyGroup(pe, group, kPotFlatKeys[statIdx]);
}

static IWzFontPtr EnsureMockFont(unsigned long color, int size);

struct TipSeg {
    char text[24];
    unsigned long argb;
};

struct TipColorStrip {
    CUIToolTip* tip;
    TipSeg segs[12];
    int nSeg;
};

static TipColorStrip g_tipStrips[48];
static int g_tipStripCount = 0;
static int g_tipStripExtraH = 0;
static constexpr int kTipStripLineH = 14;

#if !GREEN_ENTER_BASELINE
// Survives /OPT:REF — tip-only builds must not rely on fusionanvil.obj stamps.
// GREEN_ENTER_BASELINE=1 omits these so enter-green frozen objs stay tip-clean.
extern "C" __declspec(dllexport) const char* EquipTip_GetStamp() {
    return "EQUIP_TIP_TOOLTIP_ONLY_20260803";
}
extern "C" __declspec(dllexport) const char* EquipTip_GetColorStamp() {
    return "EQUIP_TIP_COLOR_20260803";
}
#endif

static void ClearTipColorStrips() {
    g_tipStripCount = 0;
    g_tipStripExtraH = 0;
}

static void PushTipSeg(TipColorStrip& strip, const char* fmt, int v, unsigned long argb) {
    if (strip.nSeg >= 12 || v == 0) {
        return;
    }
    TipSeg& s = strip.segs[strip.nSeg++];
    s.argb = argb;
    if (v > 0) {
        sprintf_s(s.text, "+%d", v);
    } else {
        sprintf_s(s.text, "%d", v);
    }
}

static void EmitStatBreakdownLine(
    CUIToolTip* tip,
    int nType,
    int baseStat,
    int scrollDelta,
    int chaosDelta,
    int flameDelta,
    int growthBonus,
    int hyperBonus,
    int potMain,
    int potBonus,
    int soulBonus,
    int socketBonus,
    int potPercent,
    ZXString<char> sProperty)
{
    const int board = baseStat + scrollDelta + chaosDelta + flameDelta + growthBonus;
    const int total = board + hyperBonus + potMain + potBonus + soulBonus + socketBonus;
    ZXString<char> sLeft;

    // Compact monochrome paren: 底板 强化 混沌 火花 成长 星力 潜能 附加 灵魂 星岩
    char paren[160] = {};
    size_t off = 0;
    auto ap = [&](const char* fmt, int v) {
        if (off + 20 >= sizeof(paren)) {
            return;
        }
        off += static_cast<size_t>(sprintf_s(paren + off, sizeof(paren) - off, fmt, v));
    };
    ap("%d", baseStat);
    if (scrollDelta != 0) {
        ap(scrollDelta > 0 ? "+%d" : "%d", scrollDelta);
    }
    if (chaosDelta != 0) {
        ap(chaosDelta > 0 ? "+%d" : "%d", chaosDelta);
    }
    if (flameDelta != 0) {
        ap(flameDelta > 0 ? "+%d" : "%d", flameDelta);
    }
    // Growth split lives in EquipGrowth companion tip — never paren/strip on main tip.
    if (hyperBonus > 0) {
        ap("+%d", hyperBonus);
    }
    if (potMain > 0) {
        ap("+%d", potMain);
    }
    if (potBonus > 0) {
        ap("+%d", potBonus);
    }
    if (soulBonus > 0) {
        ap("+%d", soulBonus);
    }
    if (socketBonus > 0) {
        ap("+%d", socketBonus);
    }
    if (potPercent > 0) {
        ap("+%d%%", potPercent);
    }

    const bool hasBreak = (scrollDelta != 0 || chaosDelta != 0 || flameDelta != 0 || hyperBonus > 0
            || potMain > 0 || potBonus > 0 || soulBonus > 0 || socketBonus > 0 || potPercent > 0);
    if (!hasBreak) {
        if (nType == CUIToolTip::PT_VALUE) {
            sLeft.Format("%s %d", static_cast<const char*>(sProperty), total);
        } else if (total >= 0) {
            sLeft.Format("%s +%d", static_cast<const char*>(sProperty), total);
        } else {
            sLeft.Format("%s %d", static_cast<const char*>(sProperty), total);
        }
        tip->AddInfoEx(kFontTipLabel, kFontTipYellow, sLeft, ZXString<char>(), 1, 1001);
        return;
    }

    if (nType == CUIToolTip::PT_VALUE) {
        sLeft.Format("%s %d (%s)", static_cast<const char*>(sProperty), total, paren);
    } else if (total >= 0) {
        sLeft.Format("%s +%d (%s)", static_cast<const char*>(sProperty), total, paren);
    } else {
        sLeft.Format("%s %d (%s)", static_cast<const char*>(sProperty), total, paren);
    }
    // Prefer Hyper yellow, else pot purple for AddInfoEx right-slot (fallback).
    if (hyperBonus > 0) {
        tip->AddInfoEx(kFontTipLabel, kFontTipYellow, sLeft, ZXString<char>(), 1, 1001);
    } else {
        tip->AddInfoEx(kFontTipLabel, kFontTipPurple, sLeft, ZXString<char>(), 1, 1001);
    }

    // Colored strip drawn in DrawToolTip_Equip_hook
    if (g_tipStripCount < 48 && tip) {
        TipColorStrip& strip = g_tipStrips[g_tipStripCount++];
        strip.tip = tip;
        strip.nSeg = 0;
        {
            TipSeg& s = strip.segs[strip.nSeg++];
            s.argb = kArgbBase;
            sprintf_s(s.text, "%d", baseStat);
        }
        PushTipSeg(strip, "+%d", scrollDelta, kArgbScroll);
        PushTipSeg(strip, "%d", chaosDelta, kArgbChaos);
        PushTipSeg(strip, "+%d", flameDelta, kArgbChaos); // 火花同深绿（对齐 265 tip 绿）
        // Growth color strip → companion tip only (avoid main-tip 7+30 pollution/overflow).
        PushTipSeg(strip, "+%d", hyperBonus, kArgbHyper);
        PushTipSeg(strip, "+%d", potMain, kArgbPot);
        PushTipSeg(strip, "+%d", potBonus, kArgbBonus);
        PushTipSeg(strip, "+%d", soulBonus, kArgbSoul);
        PushTipSeg(strip, "+%d", socketBonus, kArgbSocket);
        if (strip.nSeg > 1) {
            // +4 bottom pad so colored strip sits inside MakeLayer double-outline (h-2),
            // not painted past the tip frame (looks like duplicate overflow).
            tip->m_nHeight += kTipStripLineH + 4;
            g_tipStripExtraH += kTipStripLineH + 4;
        } else {
            --g_tipStripCount;
        }
    }
}

static auto Original_PrintValue =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, int, ZXString<char>, int)>(kAddr_PrintValue);

void __fastcall Hook_PrintValue_HyperBreakdown(
    CUIToolTip* tip,
    void* /*edx*/,
    int nType,
    int nValue,
    ZXString<char> sProperty,
    int bShowAlways)
{
    if (EquipCompare::IsDeltaPrintValueActive()) {
        Original_PrintValue(tip, nType, nValue, sProperty, bShowAlways);
        return;
    }

    if (!g_inEquipBasicTip || !g_breakdownPe || !tip ||
        (nType != CUIToolTip::PT_INC && nType != CUIToolTip::PT_VALUE)) {
        Original_PrintValue(tip, nType, nValue, sProperty, bShowAlways);
        return;
    }

    const char* prop = static_cast<const char*>(sProperty);
    const int idx = MatchEquipStatByLabel(prop);
    if (idx < 0) {
        Original_PrintValue(tip, nType, nValue, sProperty, bShowAlways);
        return;
    }

    unsigned char enhance = 0;
    SafeGetHyperPotential(g_breakdownPe, &enhance, nullptr, nullptr, nullptr, nullptr);
    const int stars = ClampEnhanceStars(enhance);
    const int itemId = SafeGetItemId(g_breakdownPe);
    const int hyperBonus = HyperBonusForStat(idx, stars, itemId);
    const int potMain = FlatForGroupStat(g_breakdownPe, 0, idx);
    const int potBonusFlat = FlatForGroupStat(g_breakdownPe, 1, idx);
    const int soulBonus = FlatForGroupStat(g_breakdownPe, 2, idx);
    const int socketBonus = FlatForGroupStat(g_breakdownPe, 3, idx);
    const int potBonus = potMain + potBonusFlat + soulBonus + socketBonus;
    const int potPercent = PotentialPercentForStat(g_breakdownPe, idx);
    int growthBonus = EquipGrowth::GetGrowthBonusForStat(itemId, idx);
    const int flameDelta = EquipGrowth::GetFlameBonusForStat(itemId, idx);
    const int chaosDelta = static_cast<int>(FusionAnvil_GetChaosStat(g_breakdownPe, idx));

    int board = nValue;
    if (kTipPacketIncludesHyperPotential) {
        board = nValue - hyperBonus - potBonus;
        if (board < 0) {
            board = 0;
        }
    }
    // Main tip: always show TOTAL only. Board splits (base+scroll / growth / flame)
    // belong in the growth companion tip — never paint "7+30" paren or color strip
    // on the original equip tip (pollutes chrome / duplicates below the frame).
    (void)board;
    (void)growthBonus;
    (void)flameDelta;
    (void)chaosDelta;
    if (hyperBonus <= 0 && potBonus <= 0 && potPercent <= 0) {
        Original_PrintValue(tip, nType, nValue, sProperty, bShowAlways);
        return;
    }

    if (!bShowAlways && nValue <= 0 && hyperBonus <= 0 && potBonus <= 0 && potPercent <= 0) {
        return;
    }

    // Hyper / Potential still use colored companion-style strip on main tip;
    // board is a single total (no base/scroll/growth/flame paren).
    EmitStatBreakdownLine(tip, nType, board, 0, 0, 0, 0, hyperBonus,
            potMain, potBonusFlat, soulBonus, socketBonus, potPercent, sProperty);
}

#if !GREEN_ENTER_BASELINE
static void DrawTipColorStrips(CUIToolTip* tip) {
    if (!tip || !tip->m_pLayer || g_tipStripCount <= 0) {
        return;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        IWzCanvasPtr canvas = tip->m_pLayer->Getcanvas(vIdx);
        if (!canvas) {
            return;
        }
        int canvasH = 0;
        try {
            canvasH = static_cast<int>(canvas->Getheight());
        } catch (...) {
            canvasH = tip->m_nHeight;
        }
        // Keep strip glyphs inside fill (above bottom double-outline at h-2).
        const int yMax = (canvasH > 4 ? canvasH - 4 : tip->m_nHeight - 4);
        int y = tip->m_nHeight - g_spiritTipExtraH - g_tipStripExtraH + 2;
        if (y < 2) {
            y = 2;
        }
        for (int i = 0; i < g_tipStripCount; ++i) {
            TipColorStrip& strip = g_tipStrips[i];
            if (strip.tip != tip || strip.nSeg <= 0) {
                continue;
            }
            if (y + 12 > yMax) {
                break; // do not paint past tip frame
            }
            int x = 12;
            for (int s = 0; s < strip.nSeg; ++s) {
                IWzFontPtr font = EnsureMockFont(strip.segs[s].argb, 11);
                if (!font) {
                    continue;
                }
                wchar_t wbuf[32] = {};
                MultiByteToWideChar(CP_ACP, 0, strip.segs[s].text, -1, wbuf, 32);
                const Ztl_bstr_t bs(wbuf);
                const int tw = static_cast<int>(font->CalcTextWidth(bs, Ztl_variant_t()));
                canvas->DrawTextA(x, y, bs, font, Ztl_variant_t(), Ztl_variant_t());
                x += tw + 4;
            }
            y += kTipStripLineH;
        }
    } catch (...) {
    }
}
#endif

// Growth segment color kept for companions — alias above.

static void DrawSpiritSkillBlock(CUIToolTip* tip, GW_ItemSlotEquip* pe) {
    if (!tip || !pe || g_spiritTipExtraH <= 0 || !tip->m_pLayer) {
        return;
    }
    int skillId = 0;
    int skillLv = 0;
    unsigned long long expire = 0;
    SafeGetEquipSkill(pe, &skillId, &skillLv, &expire);
    if (skillId <= 0 || skillLv <= 0) {
        return;
    }
    // Require a real String/Skill.img name — CS uninit often yields "#575".
    Ztl_bstr_t skillNameBstr = LookupSkillFieldBstr(skillId, L"name");
    const wchar_t* skillNameWide = skillNameBstr;
    if (!skillNameWide || !*skillNameWide) {
        return;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        IWzCanvasPtr canvas = tip->m_pLayer->Getcanvas(vIdx);
        if (!canvas) {
            return;
        }

        IWzFontPtr fontName;
        IWzFontPtr fontBody;
        // font 1 在部分 tip 上对中文名几乎不可见；统一用 body 字体画名称
        get_basic_font(std::addressof(fontBody), 0);
        get_basic_font(std::addressof(fontName), 0);
        if (!fontBody) {
            return;
        }
        if (!fontName) {
            fontName = fontBody;
        }

        int y = tip->m_nHeight - g_spiritTipExtraH + 6;
        const int tipW = tip->m_nWidth;

        // 技能名：WZ BSTR 直绘，避免 GBK 往返丢字（「灵韵 : 冒险岛勇士」）
        {
            Ztl_bstr_t label = GbkToBstr("\xC1\xE9\xD4\xCF : "); // 灵韵 :
            canvas->DrawTextA(12, y, label, fontName, Ztl_variant_t(), Ztl_variant_t());

            Ztl_bstr_t nameBstr = skillNameBstr;
            // 「灵韵 : 」约 48px，名称跟在后面
            canvas->DrawTextA(12 + 48, y, nameBstr, fontName, Ztl_variant_t(), Ztl_variant_t());
            y += 20;
        }

        if (expire != 0) {
            // Show raw expire hint (permanent uses 0)
            canvas->DrawTextA(
                12, y, GbkToBstr("\xB9\xFD\xC6\xDA"), // 过期
                fontBody, Ztl_variant_t(), Ztl_variant_t());
            y += 16;
        }

        // Separator
        canvas->raw_DrawRectangle(6, y, tipW - 12, 1, 0xFFFFFFFFu);
        y += 10;

        // Icon + desc
        IWzCanvasPtr icon = LoadSkillIconCanvas(skillId);
        const int iconX = 10;
        const int iconY = y;
        if (icon) {
            Ztl_variant_t empty;
            canvas->CopyEx(
                iconX, iconY, icon, CANVAS_ALPHATYPE::CA_OVERWRITE,
                32, 32, 0, 0, 32, 32, empty);
        }

        std::string desc = LookupSkillFieldGbk(skillId, L"desc");
        const int descX = icon ? 50 : 12;
        DrawWrappedGbk(canvas, fontBody, descX, y, icon ? 22 : 28, desc, 14);
        y += (std::max)(icon ? 36 : 0, EstimateWrapLines(desc, icon ? 22 : 28) * 14) + 6;

        // Separator
        canvas->raw_DrawRectangle(6, y, tipW - 12, 1, 0xFFFFFFFFu);
        y += 8;

        // Current level
        {
            char lvLine[64];
            // [当前等级: N]
            sprintf_s(lvLine, "[\xB5\xB1\xC7\xB0\xB5\xC8\xBC\xB6: %d]", skillLv);
            canvas->DrawTextA(12, y, GbkToBstr(lvLine), fontBody, Ztl_variant_t(), Ztl_variant_t());
            y += 16;
        }

        wchar_t hl[16];
        swprintf_s(hl, L"h%d", skillLv);
        std::string lvDesc = LookupSkillFieldGbk(skillId, hl);
        DrawWrappedGbk(canvas, fontBody, 12, y, 28, lvDesc, 14);
    } catch (...) {
    }
}


static auto CUIToolTip__SetToolTip_Equip_Basic =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, GW_ItemSlotEquip*)>(kAddr_SetToolTip_Equip_Basic);

void __fastcall CUIToolTip__SetToolTip_Equip_Basic_hook(
    CUIToolTip* pThis, void* /*edx*/, GW_ItemSlotEquip* pe)
{
    if (!pThis || !pe) {
        return;
    }

    // PrintValue hook reads g_breakdownPe to append Hyper/Potential colored split.
    ClearTipColorStrips();
    g_breakdownPe = pe;
    EquipTooltipStyle_SetContextItemId(SafeGetItemId(pe));
    g_inEquipBasicTip = true;
    // Keep vanilla zh-CN stat/type labels from String.wz/ToolTipHelp.img.
    CUIToolTip__SetToolTip_Equip_Basic(pThis, pe);
    g_inEquipBasicTip = false;
    g_breakdownPe = nullptr;
    EquipTooltipStyle_SetContextItemId(0);

    // Fusion Anvil: append skin item name only; do not replace base tooltip text.
    const int nAnvilItemID = SafeGetAnvilItemId(pe);
    if (nAnvilItemID != 0) {
        ZXString<char> sSkinName;
        reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
            kAddr_GetItemName)(CItemInfo::GetInstance(), &sSkinName, nAnvilItemID);
        if (!sSkinName.IsEmpty()) {
            ZXString<char> sLine;
            sLine.Format("%s%s", static_cast<const char*>(sSkinName), kSuffixTransmog);
            pThis->AddInfoEx(14, 15, kLabelAppearance, sLine, 1, 1001);
        }
    }

    // Potential → bottom companion tip (旁挂底条), not AddInfoEx in main tip.
    // Hyper ★ → top companion. Main tip paints Hyper/Potential color split from packet total.

    // 灵韵：抬高 tip，留给 Draw 阶段画「图标+说明」区块（对齐参考装备加技能）
    g_spiritTipExtraH = 0;
    int skillId = 0;
    int skillLv = 0;
    unsigned long long expire = 0;
    SafeGetEquipSkill(pe, &skillId, &skillLv, &expire);
    if (skillId > 0 && skillLv > 0 && !LookupSkillNameGbk(skillId).empty()) {
        g_spiritTipExtraH = CalcSpiritTipExtraHeight(skillId, skillLv, expire);
        if (g_spiritTipExtraH > 0) {
            pThis->m_nHeight += g_spiritTipExtraH;
        }
    }
}


// ===========================================================================
// Transmog corner icon — draw the skin item's small icon in the top-right
// corner of the equip tooltip canvas after the engine has rendered it.
// ===========================================================================

static auto CUIToolTip__DrawToolTip_Equip =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, GW_ItemSlotEquip*)>(kAddr_DrawToolTip_Equip);

#if MOCK_EQUIP_MARKERS
// A′ Hyper companion tip — floats ABOVE main equip tip (套装属性-style).
// Stars come from real nEnhance @ 0x10D (1–10); do NOT paint into native canvas.
static constexpr unsigned long kMockStarColor = 0xFFFFCC33; // warm gold
static constexpr unsigned long kMockRowColor = 0xFFFFFFFF;
static constexpr size_t kMarkerTipBufSize = 0x600;
// Official-ish Star Force row: more top air, slightly smaller glyphs, gap /5.
static constexpr int kMarkerTipPadTop = 10;
// Bottom companion: last option line must not sit flush on the white border.
static constexpr int kMarkerTipPadBottom = 14;
static constexpr int kMarkerTipLineH = 14;
static constexpr int kMarkerTipLineGap = 2;
static constexpr int kMarkerTipHeightSlack = 4;
static constexpr int kMarkerTipMarginX = 10;
static constexpr int kMarkerGapAbove = 0;
// Extra gap between main equip tip bottom and potential companion top.
static constexpr int kMarkerGapBelowMain = 3;
static constexpr int kMarkerBorderOverlap = 2;
static constexpr unsigned long kMarkerTipFillColor = 0xA0000000u;
static constexpr unsigned long kMarkerTipOutlineColor = 0xFFFFFFFFu;
static constexpr int kMarkerEquipTopScrubH = 2;
static constexpr int kHyperStarMax = 25; // sync PotentialHyperConfig.MAX_ENHANCE / 095
static constexpr int kHyperStarsPerRow = 10; // 一行最多 10 星，超出换行
static constexpr int kStarDrawMaxPx = 10;   // slightly smaller than full WZ sprite
static constexpr int kStarGapNormal = 1;    // between adjacent stars
static constexpr int kStarGapGroup = 6;     // extra gap after every 5th star
static constexpr int kInfusionMax = 10;     // 注能等级上限 0~10
static constexpr int kInfusionPerRow = 10;  // 注能 ⚡ 一行最多 10 个

static int ClampHyperStars(unsigned char enhance);

typedef HRESULT(__thiscall* WzFontCreate_t)(
    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(0x0046341A);

enum class MarkerLineKind : int {
    Stars = 0,
    GbkText = 1,
    Infusion = 2,
};

struct MarkerLine {
    MarkerLineKind kind;
    int starCount;       // Stars: filled count 1–10
    const char* gbkText; // GbkText only
};

struct MarkerVisualLine {
    MarkerLineKind kind;
    int starCount;
    std::wstring text; // GbkText
};

static alignas(8) char g_markerTipBuf[kMarkerTipBufSize];
static bool g_markerTipInited = false;
static CUIToolTip* g_markerMainTip = nullptr;
static int g_markerLastItemId = 0;
static int g_markerLastEnhance = 0;
static int g_markerLastInfusion = 0;
static int g_markerLastGem = 0;
static int g_markerLastBreakthrough = 0;
static int g_markerLastX = -1;
static int g_markerLastY = -1;
static int g_markerLastW = 0;
static int g_markerLastH = 0;
static int g_markerLastVisualCount = 0;

static auto Original_ClearToolTip =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*)>(ClientAddresses::kToolTipClear);

static IWzCanvasPtr LoadWzCanvas(const wchar_t* uol) {
    IWzCanvasPtr canvas;
    if (!uol || !*uol || !get_rm()) {
        return canvas;
    }
    try {
        Ztl_variant_t value = get_rm()->GetObjectA(const_cast<wchar_t*>(uol));
        IUnknown* unknown = value.GetUnknown(false, false);
        if (unknown) {
            IWzCanvas* raw = nullptr;
            if (SUCCEEDED(unknown->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw)))
                && raw) {
                canvas = raw;
                raw->Release();
            }
        }
    } catch (...) {
    }
    return canvas;
}

static IWzFontPtr EnsureMockFont(unsigned long color, int size = 10) {
    IWzFontPtr font;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
        if (font) {
            const Ztl_variant_t style(L"");
            if (FAILED(WzFontCreate(font, L"Dotum", static_cast<unsigned long>(size), color, style))) {
                font = nullptr;
            }
        }
    } catch (...) {
        font = nullptr;
    }
    if (!font) {
        try {
            get_basic_font(std::addressof(font), 0);
        } catch (...) {
        }
    }
    return font;
}

static void DrawCenteredWide(
    IWzCanvasPtr canvas, int tipWidth, int y, const wchar_t* text, IWzFontPtr font)
{
    if (!canvas || !text || !text[0] || !font || tipWidth <= 0) {
        return;
    }
    try {
        const Ztl_bstr_t bs(text);
        const unsigned tw = font->CalcTextWidth(bs, Ztl_variant_t());
        const int x = (tipWidth - static_cast<int>(tw)) / 2;
        canvas->DrawTextA(x, y, bs, font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

// IWzFont::CalcTextWidth — same COM Measure as IDA font width helpers.
static int MeasureTextWidth(IWzFontPtr font, const wchar_t* text) {
    if (!font || !text) {
        return 0;
    }
    try {
        return static_cast<int>(font->CalcTextWidth(Ztl_bstr_t(text), Ztl_variant_t()));
    } catch (...) {
        return 0;
    }
}

// Dotum 12 fullwidth Han ≈ 12px. Capacity at tipW:
//   maxHan = floor((tipW - 2*kMarkerTipMarginX) / hanW)
// tipW≈245 → floor((245-16)/12) = 19 汉字/行.
static int MaxHanCharsPerLine(IWzFontPtr font, int tipW) {
    const int avail = tipW - 2 * kMarkerTipMarginX;
    if (avail <= 0) {
        return 1;
    }
    int hanW = MeasureTextWidth(font, L"\x6C49"); // 汉
    if (hanW <= 0) {
        hanW = 12;
    }
    return (std::max)(1, avail / hanW);
}

static int ContentMaxWidth(int tipW) {
    return (std::max)(1, tipW - 2 * kMarkerTipMarginX);
}

static std::wstring GbkToWide(const char* gbk) {
    std::wstring out;
    if (!gbk || !gbk[0]) {
        return out;
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, gbk, -1, nullptr, 0);
    if (n <= 1) {
        return out;
    }
    out.resize(static_cast<size_t>(n - 1));
    MultiByteToWideChar(CP_ACP, 0, gbk, -1, &out[0], n);
    return out;
}

// Split so each piece width <= maxW; keep whole wchar units (no mid-glyph cuts).
static std::vector<std::wstring> WrapWideToWidth(
    IWzFontPtr font, const std::wstring& text, int maxW)
{
    std::vector<std::wstring> parts;
    if (text.empty()) {
        return parts;
    }
    if (!font || maxW <= 0) {
        parts.push_back(text);
        return parts;
    }

    size_t i = 0;
    while (i < text.size()) {
        size_t end = i + 1;
        while (end <= text.size()) {
            const std::wstring chunk = text.substr(i, end - i);
            if (MeasureTextWidth(font, chunk.c_str()) > maxW) {
                break;
            }
            ++end;
        }
        if (end == i + 1) {
            // Single glyph wider than maxW — still emit it (avoids infinite loop).
            parts.push_back(text.substr(i, 1));
            ++i;
        } else {
            parts.push_back(text.substr(i, end - i - 1));
            i = end - 1;
        }
    }
    return parts;
}

// Row width for n stars at drawW with normal gaps + group gaps every 5.
static int StarRowWidth(int n, int drawW) {
    if (n <= 0 || drawW <= 0) {
        return 0;
    }
    int w = n * drawW + (n - 1) * kStarGapNormal;
    if (n > 5) {
        w += kStarGapGroup;
    }
    if (n > 10) {
        w += kStarGapGroup;
    }
    return w;
}

// Prefer CharacterEff star sprite (scaled down); shrink further if tip is narrow.
static bool TryDrawWzStarRow(
    IWzCanvasPtr canvas, int tipWidth, int y, int filled, int* outH)
{
    if (outH) {
        *outH = kMarkerTipLineH;
    }
    filled = ClampHyperStars(static_cast<unsigned char>(filled > 0 ? filled : 0));
    if (filled <= 0) {
        return false;
    }
    IWzCanvasPtr star = LoadWzCanvas(L"Effect/CharacterEff.img/1114000/2/0");
    if (!star) {
        return false;
    }
    unsigned sw = 0;
    unsigned sh = 0;
    try {
        sw = star->Getwidth();
        sh = star->Getheight();
    } catch (...) {
        return false;
    }
    if (sw == 0 || sh == 0) {
        return false;
    }

    int drawW = static_cast<int>(sw);
    int drawH = static_cast<int>(sh);
    if (drawW > kStarDrawMaxPx) {
        drawH = (std::max)(1, drawH * kStarDrawMaxPx / drawW);
        drawW = kStarDrawMaxPx;
    }

    const int maxW = ContentMaxWidth(tipWidth);
    // Shrink until 10-star row fits (avoid packed text fallback looking like extra ★).
    while (drawW > 6 && StarRowWidth(filled, drawW) > maxW) {
        --drawW;
        drawH = (std::max)(1, static_cast<int>(sh) * drawW / static_cast<int>(sw));
    }
    const int rowW = StarRowWidth(filled, drawW);
    if (rowW > maxW || rowW <= 0) {
        return false;
    }

    int x = (tipWidth - rowW) / 2;
    try {
        for (int i = 0; i < filled; ++i) {
            canvas->CopyEx(
                x, y, star, CANVAS_ALPHATYPE::CA_REMOVEALPHA,
                drawW, drawH, 0, 0, static_cast<int>(sw), static_cast<int>(sh),
                Ztl_variant_t());
            x += drawW + kStarGapNormal;
            // Official Star Force: visual gap after 5th and 10th star.
            if ((i + 1) % 5 == 0 && (i + 1) < filled) {
                x += kStarGapGroup;
            }
        }
    } catch (...) {
        return false;
    }
    if (outH) {
        *outH = (std::max)(kMarkerTipLineH, drawH + 2);
    }
    return true;
}

// Text fallback: ★ with space every 5 (max 10).
static std::wstring BuildStarWide(int filled) {
    std::wstring s;
    filled = ClampHyperStars(static_cast<unsigned char>(filled > 0 ? filled : 0));
    if (filled <= 0) {
        return s;
    }
    s.reserve(static_cast<size_t>(filled + 2));
    for (int i = 0; i < filled; ++i) {
        if (i > 0 && (i % 5) == 0) {
            s.push_back(L' ');
        }
        s.push_back(L'\x2605'); // BLACK STAR
    }
    return s;
}

// 注能 ⚡ 行 — 用闪电字形(借鉴星之力行式布局)，满 5 空一格，最多 10 个。
static std::wstring BuildInfusionWide(int level) {
    std::wstring s;
    if (level <= 0 || level > kInfusionPerRow) {
        return s;
    }
    s.reserve(static_cast<size_t>(level + 2));
    for (int i = 0; i < level; ++i) {
        if (i > 0 && (i % 5) == 0) {
            s.push_back(L' ');
        }
        s.push_back(L'\x26A1'); // BLACK LIGHTNING ⚡
    }
    return s;
}

static IWzCanvasPtr GetMarkerTipCanvas(CUIToolTip* tip) {
    if (!tip || !tip->m_pLayer) {
        return nullptr;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        return tip->m_pLayer->Getcanvas(vIdx);
    } catch (...) {
        return nullptr;
    }
}

// MakeLayer @ 0x8F3141 with bDoubleOutline: bottom inner white at y=h-2,
// corner whites at y=h-1. Wipe both rows before redrawing a single join line.
static void ScrubCompanionBottomOutline(IWzCanvasPtr canvas, int tipW, int tipH) {
    if (!canvas || tipW <= 0 || tipH < 2) {
        return;
    }
    try {
        canvas->DrawRectangle(0, tipH - 2, tipW, 2, kMarkerTipFillColor);
    } catch (...) {
    }
}

// One white separator at companion bottom inner Y (h-2): matches MakeLayer
// bottom inner stroke. Inner width (2, w-4) keeps left/right frame intact.
// With overlap=2, world Y = mainY (equip tip top edge) — above owner text.
static void DrawCompanionJoinWhiteLine(IWzCanvasPtr canvas, int tipW, int tipH) {
    if (!canvas || tipW < 5 || tipH < 2) {
        return;
    }
    try {
        canvas->DrawRectangle(2, tipH - 2, tipW - 4, 1, kMarkerTipOutlineColor);
    } catch (...) {
    }
}

// Equip MakeLayer top: (0,0)/(w-1,0) corners + inner line (2,1,w-4,1).
// Fill only the top 2px strip — never touch owner "XX的" / item name below.
static void ScrubEquipTopOutline(IWzCanvasPtr canvas, int tipW) {
    if (!canvas || tipW <= 0) {
        return;
    }
    try {
        canvas->DrawRectangle(0, 0, tipW, kMarkerEquipTopScrubH, kMarkerTipFillColor);
    } catch (...) {
    }
}

static int ComputeCompanionTipY(int mainY, int tipH) {
    // tipY = mainY - tipH + overlap → companion bottom covers equip top chrome.
    int tipY = mainY - tipH - kMarkerGapAbove + kMarkerBorderOverlap;
    if (tipY < 0) {
        tipY = 0;
    }
    return tipY;
}

static CUIToolTip* EnsureMarkerTip() {
    if (!g_markerTipInited) {
        g_markerTipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
            g_markerTipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_markerTipBuf);
}

static void HideMarkerTip() {
    g_markerMainTip = nullptr;
    g_markerLastItemId = 0;
    g_markerLastEnhance = 0;
    g_markerLastInfusion = 0;
    g_markerLastGem = 0;
    g_markerLastBreakthrough = 0;
    g_markerLastX = -1;
    g_markerLastY = -1;
    g_markerLastW = 0;
    g_markerLastH = 0;
    g_markerLastVisualCount = 0;
    if (g_markerTipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
            g_markerTipBuf);
    }
}

static bool HasMarkerTipLayer() {
    if (!g_markerTipInited) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_markerTipBuf);
        return tip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadMainTipScreenPos(CUIToolTip* mainTip, int& outX, int& outY, int& outW) {
    outX = 0;
    outY = 0;
    outW = 0;
    if (!mainTip) {
        return false;
    }
    __try {
        outW = mainTip->m_nWidth;
        if (!mainTip->m_pLayer || outW <= 0) {
            return false;
        }
        outX = mainTip->m_pLayer->rx;
        outY = mainTip->m_pLayer->ry;
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outX = 0;
        outY = 0;
        outW = 0;
        return false;
    }
}

// Content height only (no String2 chrome guess):
//   tipH_content = padTop + N*lineH + (N-1)*gap + padBottom
static int ComputeMarkerTipContentH(int visualLineCount) {
    if (visualLineCount <= 0) {
        return kMarkerTipPadTop + kMarkerTipPadBottom + kMarkerTipLineH;
    }
    const int gaps = (visualLineCount > 1) ? (visualLineCount - 1) * kMarkerTipLineGap : 0;
    return kMarkerTipPadTop + kMarkerTipPadBottom + visualLineCount * kMarkerTipLineH + gaps;
}

static int ClampHyperStars(unsigned char enhance) {
    int n = static_cast<int>(enhance);
    // Reject out-of-range (uninit heap); never clamp 11..255 → 10.
    if (n < 0 || n > kHyperStarMax) {
        return 0;
    }
    return n;
}

static std::vector<MarkerLine> CollectHyperStarLines(GW_ItemSlotEquip* pe) {
    std::vector<MarkerLine> lines;
    unsigned char enhance = 0;
    SafeGetHyperPotential(pe, &enhance, nullptr, nullptr, nullptr, nullptr);
    int stars = ClampHyperStars(enhance);
    // 一行 10 星；超过另起一行（★11–20、★21–25…）
    while (stars > 0) {
        const int row = (stars > kHyperStarsPerRow) ? kHyperStarsPerRow : stars;
        lines.push_back({MarkerLineKind::Stars, row, nullptr});
        stars -= row;
    }
    // 注能 ⚡ 等级：挂在同一 companion 里（Hyper 之上/下)，仅注入>0 时追加。
    const int infusion = SafeGetInfusion(pe);
    if (infusion > 0) {
        lines.push_back({MarkerLineKind::Infusion, infusion, nullptr});
    }
    // 宝石镶嵌 宝X：仅>0 时追加一行 GbkText（服务端 reserved short 低字节）。
    const int gemInlay = SafeGetGemInlay(pe);
    if (gemInlay > 0) {
        char gbk[16] = {0};
        sprintf_s(gbk, "\xB1\xA6%d", gemInlay); // 宝X (GBK 宝=0xB1A6)
        lines.push_back({MarkerLineKind::GbkText, 0, gbk});
    }
    // 破界+N：仅>0 时追加一行 GbkText（服务端独立尾short）。
    const int breakthrough = SafeGetBreakthrough(pe);
    if (breakthrough > 0) {
        char gbk[16] = {0};
        sprintf_s(gbk, "\xC6\xC6\xBD\xE7+%d", breakthrough); // 破界+N (GBK)
        lines.push_back({MarkerLineKind::GbkText, 0, gbk});
    }
    return lines;
}

static std::vector<MarkerVisualLine> ExpandMarkerVisualLines(
    const std::vector<MarkerLine>& lines, IWzFontPtr rowFont, int tipW)
{
    std::vector<MarkerVisualLine> visual;
    const int maxW = ContentMaxWidth(tipW);
    for (const MarkerLine& line : lines) {
        if (line.kind == MarkerLineKind::Stars) {
            visual.push_back({MarkerLineKind::Stars, line.starCount, L""});
            continue;
        }
        if (line.kind == MarkerLineKind::Infusion) {
            visual.push_back({MarkerLineKind::Infusion, line.starCount, L""});
            continue;
        }
        if (!line.gbkText) {
            continue;
        }
        const std::wstring wide = GbkToWide(line.gbkText);
        if (wide.empty()) {
            continue;
        }
        const int tw = MeasureTextWidth(rowFont, wide.c_str());
        if (!rowFont || tw <= maxW) {
            visual.push_back({MarkerLineKind::GbkText, 0, wide});
            continue;
        }
        for (const std::wstring& part : WrapWideToWidth(rowFont, wide, maxW)) {
            visual.push_back({MarkerLineKind::GbkText, 0, part});
        }
    }
    return visual;
}

static void RenderMarkerVisualLines(
    IWzCanvasPtr canvas, int tipWidth, const std::vector<MarkerVisualLine>& lines,
    IWzFontPtr starFont, IWzFontPtr rowFont)
{
    if (!canvas || tipWidth <= 0 || lines.empty()) {
        return;
    }

    int y = kMarkerTipPadTop;
    for (size_t i = 0; i < lines.size(); ++i) {
        const MarkerVisualLine& line = lines[i];
        int usedH = kMarkerTipLineH;
        if (line.kind == MarkerLineKind::Stars) {
            int starH = kMarkerTipLineH;
            const int n = ClampHyperStars(static_cast<unsigned char>(
                line.starCount > 0 ? line.starCount : 0));
            if (n > 0
                && !TryDrawWzStarRow(canvas, tipWidth, y, n, &starH)
                && starFont) {
                const std::wstring stars = BuildStarWide(n);
                DrawCenteredWide(canvas, tipWidth, y, stars.c_str(), starFont);
            }
            usedH = starH;
        } else if (line.kind == MarkerLineKind::Infusion && rowFont) {
            const int lvl = (line.starCount > 0 && line.starCount <= kInfusionPerRow)
                ? line.starCount : 0;
            if (lvl > 0) {
                const std::wstring bolts = BuildInfusionWide(lvl);
                if (!bolts.empty()) {
                    DrawCenteredWide(canvas, tipWidth, y, bolts.c_str(), rowFont);
                }
            }
        } else if (line.kind == MarkerLineKind::GbkText && !line.text.empty() && rowFont) {
            DrawCenteredWide(canvas, tipWidth, y, line.text.c_str(), rowFont);
        }
        y += usedH;
        if (i + 1 < lines.size()) {
            y += kMarkerTipLineGap;
        }
    }
}

// Calibrate SetToolTip_String2 blank-line probe to shrink-fit targetH (slack 0–4px).
// Prior bug: always used lineCount+2 newlines then trusted oversized m_nHeight.
static int CreateMarkerTipLayerAtHeight(
    CUIToolTip* tip, int tipX, int tipY, int tipW, int targetH)
{
    if (!tip || tipW <= 0 || targetH <= 0) {
        return 0;
    }

    ZXString<char> zTitle("");
    auto setLines = [&](int n) -> int {
        std::string s((std::max)(0, n), '\n');
        ZXString<char> zDesc(s.c_str());
        tip->ClearToolTip();
        tip->SetToolTip_String2(tipX, tipY, zTitle, zDesc, 0, 0, 0, tipW, 1, 0);
        return tip->m_nHeight;
    };

    const int h2 = setLines(2);
    const int h12 = setLines(12);
    const int perLine = (h12 > h2) ? (std::max)(1, (h12 - h2) / 10) : 14;
    const int base = h2 - 2 * perLine;
    int need = (std::max)(1, (targetH - base + perLine - 1) / perLine);
    setLines(need);

    // Grow until canvas covers content.
    for (int i = 0; i < 6 && tip->m_nHeight < targetH; ++i) {
        setLines(++need);
    }
    // Shrink excess empty band (keep <= slack).
    for (int i = 0; i < 8
         && need > 1
         && tip->m_nHeight > targetH + kMarkerTipHeightSlack; ++i) {
        const int prevH = tip->m_nHeight;
        setLines(--need);
        if (tip->m_nHeight < targetH) {
            setLines(++need); // restore last height that still fits content
            break;
        }
        if (tip->m_nHeight >= prevH) {
            break; // probe stopped changing
        }
    }

    return tip->m_nHeight > 0 ? tip->m_nHeight : targetH;
}

// Sidecar tip: Hyper ★ row ABOVE main equip tip (any equip with nEnhance > 0).
static void UpdateHyperStarCompanionTip(CUIToolTip* mainTip, GW_ItemSlotEquip* pe) {
    unsigned char enhance = 0;
    SafeGetHyperPotential(pe, &enhance, nullptr, nullptr, nullptr, nullptr);
    const int starCount = ClampHyperStars(enhance);
    const int infusion = SafeGetInfusion(pe);
    const int gemInlay = SafeGetGemInlay(pe);
    const int breakthrough = SafeGetBreakthrough(pe);
    const int itemId = SafeGetItemId(pe);
    if (!mainTip || itemId <= 0 || (starCount <= 0 && infusion <= 0 && gemInlay <= 0 && breakthrough <= 0)) {
        HideMarkerTip();
        return;
    }

    int mainX = 0;
    int mainY = 0;
    int mainW = 0;
    if (!ReadMainTipScreenPos(mainTip, mainX, mainY, mainW)) {
        HideMarkerTip();
        return;
    }

    const int tipW = mainW;
    if (tipW <= 0) {
        HideMarkerTip();
        return;
    }
    int tipX = mainX;
    if (tipX < 0) {
        tipX = 0;
    }

    IWzFontPtr starFont = EnsureMockFont(kMockStarColor);
    IWzFontPtr rowFont = EnsureMockFont(kMockRowColor);

    const std::vector<MarkerLine> logical = CollectHyperStarLines(pe);
    if (logical.empty()) {
        HideMarkerTip();
        return;
    }
    const std::vector<MarkerVisualLine> visual =
        ExpandMarkerVisualLines(logical, rowFont, tipW);
    const int visualCount = static_cast<int>(visual.size());
    const int targetH = ComputeMarkerTipContentH(visualCount);

    try {
        CUIToolTip* tip = EnsureMarkerTip();

        auto scrubMainTop = [&]() {
            IWzCanvasPtr mainCanvas = GetMarkerTipCanvas(mainTip);
            if (mainCanvas) {
                ScrubEquipTopOutline(mainCanvas, tipW);
            }
        };

        // Reuse: same item / stars / infusion / gem / geometry — only re-dock + scrub seam.
        if (itemId == g_markerLastItemId && starCount == g_markerLastEnhance
            && infusion == g_markerLastInfusion
            && gemInlay == g_markerLastGem
            && breakthrough == g_markerLastBreakthrough
            && tipX == g_markerLastX && tipW == g_markerLastW
            && visualCount == g_markerLastVisualCount
            && HasMarkerTipLayer() && tip->m_pLayer) {
            const int h = tip->m_nHeight > 0 ? tip->m_nHeight
                : (g_markerLastH > 0 ? g_markerLastH : targetH);
            const int tipY = ComputeCompanionTipY(mainY, h);
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
            scrubMainTop();
            g_markerMainTip = mainTip;
            g_markerLastY = tipY;
            g_markerLastH = h;
            return;
        }

        int tipY = ComputeCompanionTipY(mainY, targetH);
        const int h = CreateMarkerTipLayerAtHeight(tip, tipX, tipY, tipW, targetH);

        tipY = ComputeCompanionTipY(mainY, h);
        if (tip->m_pLayer) {
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
        }

        IWzCanvasPtr canvas = GetMarkerTipCanvas(tip);
        if (!canvas) {
            HideMarkerTip();
            return;
        }
        ScrubCompanionBottomOutline(canvas, tipW, h);
        DrawCompanionJoinWhiteLine(canvas, tipW, h);
        scrubMainTop();
        RenderMarkerVisualLines(canvas, tipW, visual, starFont, rowFont);

        g_markerMainTip = mainTip;
        g_markerLastItemId = itemId;
        g_markerLastEnhance = starCount;
        g_markerLastInfusion = infusion;
        g_markerLastGem = gemInlay;
        g_markerLastBreakthrough = breakthrough;
        g_markerLastX = tipX;
        g_markerLastY = tipY;
        g_markerLastW = tipW;
        g_markerLastH = h;
        g_markerLastVisualCount = visualCount;
    } catch (...) {
        HideMarkerTip();
    }
}

// ---------------------------------------------------------------------------
// B′ Potential companion tip — floats BELOW main equip tip (官服底条旁挂).
// Grade-colored「潜在能力」+ option name lines only (no flat-stat math).
// ---------------------------------------------------------------------------
static alignas(8) char g_potTipBuf[kMarkerTipBufSize];
static bool g_potTipInited = false;
static CUIToolTip* g_potMainTip = nullptr;
static int g_potLastItemId = 0;
static unsigned char g_potLastGrade = 0;
static int g_potLastP1 = 0, g_potLastP2 = 0, g_potLastP3 = 0;
static unsigned char g_potLastBonusGrade = 0;
static int g_potLastB1 = 0, g_potLastB2 = 0, g_potLastB3 = 0;
static int g_potLastSoulId = 0, g_potLastSoulOption = 0, g_potLastSocket1 = 0, g_potLastSocket2 = 0, g_potLastSocket3 = 0;
static int g_potLastX = -1;
static int g_potLastY = -1;
static int g_potLastW = 0;
static int g_potLastH = 0;
static int g_potLastLineCount = 0;
static int g_potLastMainH = 0;

static CUIToolTip* EnsurePotTip() {
    if (!g_potTipInited) {
        g_potTipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
            g_potTipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_potTipBuf);
}

static void HidePotTip() {
    g_potMainTip = nullptr;
    g_potLastItemId = 0;
    g_potLastGrade = 0;
    g_potLastP1 = g_potLastP2 = g_potLastP3 = 0;
    g_potLastBonusGrade = 0;
    g_potLastB1 = g_potLastB2 = g_potLastB3 = 0;
    g_potLastSoulId = g_potLastSoulOption = g_potLastSocket1 = g_potLastSocket2 = g_potLastSocket3 = 0;
    g_potLastX = -1;
    g_potLastY = -1;
    g_potLastW = 0;
    g_potLastH = 0;
    g_potLastLineCount = 0;
    g_potLastMainH = 0;
    if (g_potTipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
            g_potTipBuf);
    }
}

static bool HasPotTipLayer() {
    if (!g_potTipInited) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_potTipBuf);
        return tip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadMainTipScreenRect(
    CUIToolTip* mainTip, int& outX, int& outY, int& outW, int& outH)
{
    outX = outY = outW = outH = 0;
    if (!mainTip) {
        return false;
    }
    __try {
        outW = mainTip->m_nWidth;
        outH = mainTip->m_nHeight;
        if (!mainTip->m_pLayer || outW <= 0 || outH <= 0) {
            return false;
        }
        outX = mainTip->m_pLayer->rx;
        outY = mainTip->m_pLayer->ry;
        return outW > 0 && outH > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int ComputeBottomCompanionTipY(int mainY, int mainH) {
    // Slight overlap on seam + extra air under the original equip tip.
    int tipY = mainY + mainH - kMarkerBorderOverlap + kMarkerGapBelowMain;
    if (tipY < 0) {
        tipY = 0;
    }
    return tipY;
}

static void ScrubCompanionTopOutline(IWzCanvasPtr canvas, int tipW) {
    if (!canvas || tipW <= 0) {
        return;
    }
    try {
        canvas->DrawRectangle(0, 0, tipW, 2, kMarkerTipFillColor);
    } catch (...) {
    }
}

static void DrawBottomCompanionJoinWhiteLine(IWzCanvasPtr canvas, int tipW) {
    if (!canvas || tipW <= 0) {
        return;
    }
    try {
        canvas->DrawRectangle(2, 0, tipW - 4, 1, kMarkerTipOutlineColor);
    } catch (...) {
    }
}

static void ScrubEquipBottomOutline(IWzCanvasPtr canvas, int tipW, int tipH) {
    if (!canvas || tipW <= 0 || tipH <= 0) {
        return;
    }
    try {
        canvas->DrawRectangle(0, tipH - 2, tipW, 2, kMarkerTipFillColor);
    } catch (...) {
    }
}

static void DrawLeftWide(
    IWzCanvasPtr canvas, int tipWidth, int y, const wchar_t* text, IWzFontPtr font)
{
    if (!canvas || !text || !text[0] || !font || tipWidth <= 0) {
        return;
    }
    try {
        const int x = kMarkerTipMarginX;
        canvas->DrawTextA(x, y, Ztl_bstr_t(text), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

enum class PotTipRowKind : int {
    MainHeader = 0,
    BonusHeader = 1,
    SoulHeader = 2,
    SocketHeader = 3,
    Option = 4,
    Sep = 5,
};

struct PotTipRow {
    PotTipRowKind kind;
    unsigned char grade; // for Main/Bonus header coloring
    std::wstring text;
};

static std::vector<PotTipRow> CollectPotTipRows(GW_ItemSlotEquip* pe, unsigned char* outColorGrade) {
    std::vector<PotTipRow> rows;
    if (outColorGrade) {
        *outColorGrade = 0;
    }
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    SafeGetHyperPotential(pe, nullptr, &grade, &p1, &p2, &p3);
    unsigned char bGrade = 0;
    int b1 = 0, b2 = 0, b3 = 0;
    SafeGetBonusPotential(pe, &bGrade, &b1, &b2, &b3);
    int soulId = 0, soulOption = 0, socket1 = 0, socket2 = 0, socket3 = 0;
    SafeGetSoulSocket(pe, &soulId, &soulOption, &socket1, &socket2, &socket3);
    const bool hasMain = (grade > 0 || p1 > 0 || p2 > 0 || p3 > 0);
    const bool hasBonus = (bGrade > 0 || b1 > 0 || b2 > 0 || b3 > 0);
    const bool hasSoul = (soulId > 0 || soulOption > 0);
    const bool hasSocket = (socket1 > 0 || socket2 > 0 || socket3 > 0);
    if (!hasMain && !hasBonus && !hasSoul && !hasSocket) {
        return rows;
    }
    unsigned char colorGrade = grade;
    if (bGrade > colorGrade) {
        colorGrade = bGrade;
    }
    if (outColorGrade) {
        *outColorGrade = colorGrade;
    }

    const int potLevel = PotLevelFromReq(SafeGetEquipReqLevel(SafeGetItemId(pe)));
    auto pushFormattedOption = [&](int optionId, unsigned char lineGrade) {
        if (optionId <= 0) {
            return;
        }
        std::string blob = FormatItemOptionTipGbk(optionId, potLevel);
        if (blob.empty()) {
            return;
        }
        size_t start = 0;
        while (start <= blob.size()) {
            // memchr avoids MSVC ___std_find_trivial link flake (toolset/CRT skew).
            const char* base = blob.data() + start;
            const size_t remain = blob.size() - start;
            const void* p = remain ? std::memchr(base, '\n', remain) : nullptr;
            const size_t nl = p ? static_cast<size_t>(static_cast<const char*>(p) - blob.data())
                                : std::string::npos;
            std::string line = (nl == std::string::npos)
                ? blob.substr(start)
                : blob.substr(start, nl - start);
            if (!line.empty()) {
                PotTipRow row{};
                row.kind = PotTipRowKind::Option;
                row.grade = lineGrade;
                row.text = GbkToWide(line.c_str());
                rows.push_back(row);
            }
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
    };
    auto pushOpts = [&](const int* opts, unsigned char lineGrade) {
        for (int oi = 0; oi < 3; ++oi) {
            pushFormattedOption(opts[oi], lineGrade);
        }
    };

    if (hasMain) {
        // Fig3: 「潜能 : SS级」(+ cube name in brackets for clarity)
        char header[96];
        sprintf_s(header, "%s : %s",
            kLabelPotentialShort,
            PotentialGradeLetterGbk(grade > 0 ? grade : 1));
        PotTipRow h{};
        h.kind = PotTipRowKind::MainHeader;
        h.grade = grade > 0 ? grade : 1;
        h.text = GbkToWide(header);
        rows.push_back(h);
        // Phase7: grade>0 且三条 option 均≤0 → 隐藏未鉴定
        const bool mainHidden = (grade > 0 && p1 <= 0 && p2 <= 0 && p3 <= 0);
        if (mainHidden) {
            PotTipRow sealed{};
            sealed.kind = PotTipRowKind::Option;
            sealed.grade = grade > 0 ? grade : 1;
            sealed.text = GbkToWide(kLabelUnrevealedPotential);
            rows.push_back(sealed);
        } else {
            const int opts[3] = { p1, p2, p3 };
            pushOpts(opts, grade > 0 ? grade : 1);
        }
    }

    if (hasBonus) {
        if (!rows.empty()) {
            PotTipRow sep{};
            sep.kind = PotTipRowKind::Sep;
            rows.push_back(sep);
        }
        char header[96];
        sprintf_s(header, "%s : %s",
            kLabelBonusPotential,
            PotentialGradeLetterGbk(bGrade > 0 ? bGrade : 1));
        PotTipRow h{};
        h.kind = PotTipRowKind::BonusHeader;
        h.grade = bGrade > 0 ? bGrade : 1;
        h.text = GbkToWide(header);
        rows.push_back(h);
        const int opts[3] = { b1, b2, b3 };
        pushOpts(opts, bGrade > 0 ? bGrade : 1);
    }

    // Soul / socket: dedicated blocks AFTER pot lines (not jammed as fake pot options).
    // Text-only — do not DrawItemIcon here (wrong origin shows as stray colored squares).
    if (hasSoul) {
        if (!rows.empty()) {
            PotTipRow sep{};
            sep.kind = PotTipRowKind::Sep;
            rows.push_back(sep);
        }
        PotTipRow h{};
        h.kind = PotTipRowKind::SoulHeader;
        h.grade = 0;
        if (soulId > 0) {
            ZXString<char> soulName;
            reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
                kAddr_GetItemName)(CItemInfo::GetInstance(), &soulName, soulId);
            const char* nm = static_cast<const char*>(soulName);
            char buf[128];
            if (nm && nm[0]) {
                sprintf_s(buf, "%s : %s", kLabelSoulOrb, nm);
            } else {
                sprintf_s(buf, "%s", kLabelSoulOrb);
            }
            h.text = GbkToWide(buf);
        } else {
            h.text = GbkToWide(kLabelSoulOrb);
        }
        rows.push_back(h);
        if (soulOption > 0) {
            pushFormattedOption(soulOption, 0);
        }
    }

    if (hasSocket) {
        if (!rows.empty()) {
            PotTipRow sep{};
            sep.kind = PotTipRowKind::Sep;
            rows.push_back(sep);
        }
        PotTipRow h{};
        h.kind = PotTipRowKind::SocketHeader;
        h.text = GbkToWide(kLabelSocket);
        rows.push_back(h);
        pushFormattedOption(socket1, 0);
        if (socket2 > 0) pushFormattedOption(socket2, 0);
        if (socket3 > 0) pushFormattedOption(socket3, 0);
    }
    return rows;
}

static std::vector<std::wstring> CollectPotentialWideLines(GW_ItemSlotEquip* pe, unsigned char* outGrade) {
    std::vector<std::wstring> lines;
    unsigned char grade = 0;
    const std::vector<PotTipRow> rows = CollectPotTipRows(pe, &grade);
    if (outGrade) {
        *outGrade = grade;
    }
    for (const PotTipRow& r : rows) {
        if (r.kind == PotTipRowKind::Sep) {
            lines.push_back(L"");
        } else {
            lines.push_back(r.text);
        }
    }
    return lines;
}

static void RenderPotentialBottomLines(
    IWzCanvasPtr canvas, int tipWidth, const std::vector<std::wstring>& lines, IWzFontPtr font)
{
    // Thin wrapper — full path uses RenderPotTipRows.
    if (!canvas || tipWidth <= 0 || lines.empty() || !font) {
        return;
    }
    int y = kMarkerTipPadTop;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].empty()) {
            DrawLeftWide(canvas, tipWidth, y, lines[i].c_str(), font);
        }
        y += kMarkerTipLineH;
        if (i + 1 < lines.size()) {
            y += kMarkerTipLineGap;
        }
    }
}

static constexpr int kPotHeaderBarH = 16;

static void RenderPotTipRows(
    IWzCanvasPtr canvas, int tipWidth, const std::vector<PotTipRow>& rows)
{
    if (!canvas || tipWidth <= 0 || rows.empty()) {
        return;
    }
    IWzFontPtr optFont = EnsureMockFont(0xFFFFFFFFu, 12);
    IWzFontPtr darkFont = EnsureMockFont(kPotHeaderTextDark, 12);
    int y = kMarkerTipPadTop;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PotTipRow& row = rows[i];
        if (row.kind == PotTipRowKind::Sep) {
            // 095 DrawOptionInfo: solid white 1px separator across tip.
            try {
                canvas->DrawRectangle(6, y + 4, tipWidth - 12, 1, 0xFFFFFFFFu);
            } catch (...) {
            }
            y += 10;
            continue;
        }

        const bool isHeader =
            row.kind == PotTipRowKind::MainHeader
            || row.kind == PotTipRowKind::BonusHeader
            || row.kind == PotTipRowKind::SoulHeader
            || row.kind == PotTipRowKind::SocketHeader;

        if (isHeader) {
            unsigned long bar = kPotHeaderBarMain;
            if (row.kind == PotTipRowKind::BonusHeader) {
                bar = kPotHeaderBarBonus;
            } else if (row.kind == PotTipRowKind::SoulHeader) {
                bar = kPotHeaderBarSoul;
            } else if (row.kind == PotTipRowKind::SocketHeader) {
                bar = kPotHeaderBarSocket;
            }
            try {
                canvas->DrawRectangle(4, y, tipWidth - 8, kPotHeaderBarH, bar);
            } catch (...) {
            }
            // Grade-colored letter badge (small icon substitute).
            if (row.kind == PotTipRowKind::MainHeader || row.kind == PotTipRowKind::BonusHeader) {
                const unsigned char g =
                    (row.grade >= 1 && row.grade <= 5) ? row.grade : 1;
                const unsigned long gCol = kPotGradeArgb[g];
                const int badgeW = (g == 5) ? 18 : 12;
                try {
                    canvas->DrawRectangle(6, y + 2, badgeW, 12, gCol);
                } catch (...) {
                }
                IWzFontPtr letterFont = EnsurePotentialGradeFont(g);
                if (letterFont) {
                    try {
                        // White-ish letter via dark font on bright plate is poor;
                        // use white mock font for badge glyph.
                        IWzFontPtr white = EnsureMockFont(0xFFFFFFFFu, 11);
                        if (white) {
                            const wchar_t* letter =
                                (g == 1) ? L"C" :
                                (g == 2) ? L"B" :
                                (g == 3) ? L"A" :
                                (g == 4) ? L"S" : L"SS";
                            canvas->DrawTextA(
                                7, y + 1, Ztl_bstr_t(letter), white,
                                Ztl_variant_t(), Ztl_variant_t());
                        }
                    } catch (...) {
                    }
                }
                (void)letterFont;
            }
            IWzFontPtr hdrFont = darkFont ? darkFont : optFont;
            const int textX = (row.kind == PotTipRowKind::MainHeader
                || row.kind == PotTipRowKind::BonusHeader)
                ? ((row.grade == 5) ? 28 : 22)
                : kMarkerTipMarginX;
            if (hdrFont && !row.text.empty()) {
                try {
                    canvas->DrawTextA(
                        textX, y + 1, Ztl_bstr_t(row.text.c_str()), hdrFont,
                        Ztl_variant_t(), Ztl_variant_t());
                } catch (...) {
                }
            }
            y += kPotHeaderBarH + 2;
        } else {
            // Option lines — white text + grade-colored bullet (095-ish).
            const unsigned long bullet =
                (row.grade >= 1 && row.grade <= 5)
                    ? kPotGradeArgb[row.grade]
                    : 0xFF90EE90u;
            if (optFont && !row.text.empty()) {
                try {
                    canvas->DrawRectangle(
                        kMarkerTipMarginX + 1, y + 4, 6, 6, bullet);
                    canvas->DrawTextA(
                        kMarkerTipMarginX + 12, y, Ztl_bstr_t(row.text.c_str()), optFont,
                        Ztl_variant_t(), Ztl_variant_t());
                } catch (...) {
                }
            }
            y += kMarkerTipLineH;
        }
        if (i + 1 < rows.size() && rows[i + 1].kind != PotTipRowKind::Sep) {
            y += kMarkerTipLineGap;
        }
    }
}

static int ComputePotTipContentH(const std::vector<PotTipRow>& rows) {
    int h = kMarkerTipPadTop + kMarkerTipPadBottom + kMarkerTipHeightSlack;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PotTipRow& row = rows[i];
        if (row.kind == PotTipRowKind::Sep) {
            h += 10;
        } else if (row.kind == PotTipRowKind::MainHeader
            || row.kind == PotTipRowKind::BonusHeader
            || row.kind == PotTipRowKind::SoulHeader
            || row.kind == PotTipRowKind::SocketHeader) {
            h += kPotHeaderBarH + 2;
        } else {
            h += kMarkerTipLineH;
        }
        if (i + 1 < rows.size() && rows[i + 1].kind != PotTipRowKind::Sep) {
            h += kMarkerTipLineGap;
        }
    }
    if (h < 36) {
        h = 36;
    }
    return h;
}

static void UpdatePotentialBottomCompanionTip(CUIToolTip* mainTip, GW_ItemSlotEquip* pe) {
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    SafeGetHyperPotential(pe, nullptr, &grade, &p1, &p2, &p3);
    unsigned char bGrade = 0;
    int b1 = 0, b2 = 0, b3 = 0;
    SafeGetBonusPotential(pe, &bGrade, &b1, &b2, &b3);
    int soulId = 0, soulOption = 0, socket1 = 0, socket2 = 0, socket3 = 0;
    SafeGetSoulSocket(pe, &soulId, &soulOption, &socket1, &socket2, &socket3);
    const int itemId = SafeGetItemId(pe);
    const bool hasMain = (grade > 0 || p1 > 0 || p2 > 0 || p3 > 0);
    const bool hasBonus = (bGrade > 0 || b1 > 0 || b2 > 0 || b3 > 0);
    const bool hasSoul = (soulId > 0 || soulOption > 0);
    const bool hasSocket = (socket1 > 0 || socket2 > 0 || socket3 > 0);
    if (!mainTip || itemId <= 0 || (!hasMain && !hasBonus && !hasSoul && !hasSocket)) {
        HidePotTip();
        return;
    }

    int mainX = 0, mainY = 0, mainW = 0, mainH = 0;
    if (!ReadMainTipScreenRect(mainTip, mainX, mainY, mainW, mainH)) {
        HidePotTip();
        return;
    }

    const int tipW = mainW;
    if (tipW <= 0) {
        HidePotTip();
        return;
    }
    int tipX = mainX;
    if (tipX < 0) {
        tipX = 0;
    }

    unsigned char colorGrade = 0;
    const std::vector<PotTipRow> rows = CollectPotTipRows(pe, &colorGrade);
    if (rows.empty()) {
        HidePotTip();
        return;
    }

    const int lineCount = static_cast<int>(rows.size());
    const int targetH = ComputePotTipContentH(rows);

    try {
        CUIToolTip* tip = EnsurePotTip();

        auto scrubMainBottom = [&]() {
            IWzCanvasPtr mainCanvas = GetMarkerTipCanvas(mainTip);
            if (mainCanvas) {
                ScrubEquipBottomOutline(mainCanvas, tipW, mainH);
            }
        };

        if (itemId == g_potLastItemId && grade == g_potLastGrade
            && p1 == g_potLastP1 && p2 == g_potLastP2 && p3 == g_potLastP3
            && bGrade == g_potLastBonusGrade
            && b1 == g_potLastB1 && b2 == g_potLastB2 && b3 == g_potLastB3
            && soulId == g_potLastSoulId && soulOption == g_potLastSoulOption
            && socket1 == g_potLastSocket1 && socket2 == g_potLastSocket2
            && socket3 == g_potLastSocket3
            && tipX == g_potLastX && tipW == g_potLastW
            && lineCount == g_potLastLineCount && mainH == g_potLastMainH
            && HasPotTipLayer() && tip->m_pLayer) {
            const int h = tip->m_nHeight > 0 ? tip->m_nHeight
                : (g_potLastH > 0 ? g_potLastH : targetH);
            const int tipY = ComputeBottomCompanionTipY(mainY, mainH);
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
            scrubMainBottom();
            g_potMainTip = mainTip;
            g_potLastY = tipY;
            g_potLastH = h;
            return;
        }

        int tipY = ComputeBottomCompanionTipY(mainY, mainH);
        const int h = CreateMarkerTipLayerAtHeight(tip, tipX, tipY, tipW, targetH);
        tipY = ComputeBottomCompanionTipY(mainY, mainH);
        if (tip->m_pLayer) {
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
        }

        IWzCanvasPtr canvas = GetMarkerTipCanvas(tip);
        if (!canvas) {
            HidePotTip();
            return;
        }
        ScrubCompanionTopOutline(canvas, tipW);
        DrawBottomCompanionJoinWhiteLine(canvas, tipW);
        scrubMainBottom();
        RenderPotTipRows(canvas, tipW, rows);

        g_potMainTip = mainTip;
        g_potLastItemId = itemId;
        g_potLastGrade = grade;
        g_potLastP1 = p1;
        g_potLastP2 = p2;
        g_potLastP3 = p3;
        g_potLastBonusGrade = bGrade;
        g_potLastB1 = b1;
        g_potLastB2 = b2;
        g_potLastB3 = b3;
        g_potLastSoulId = soulId;
        g_potLastSoulOption = soulOption;
        g_potLastSocket1 = socket1;
        g_potLastSocket2 = socket2;
        g_potLastSocket3 = socket3;
        g_potLastX = tipX;
        g_potLastY = tipY;
        g_potLastW = tipW;
        g_potLastH = h;
        g_potLastLineCount = lineCount;
        g_potLastMainH = mainH;
    } catch (...) {
        HidePotTip();
    }
}

void __fastcall CUIToolTip__ClearToolTip_Marker_hook(CUIToolTip* pThis, void* /*edx*/) {
    if (pThis && pThis == g_markerMainTip) {
        HideMarkerTip();
    }
    if (pThis && pThis == g_potMainTip) {
        HidePotTip();
    }
    // Font remap was for in-main AddInfoEx; keep restore harmless if unused.
    if (pThis && pThis == g_potRemapTip) {
        RestorePotentialFontRemap();
    }
    Original_ClearToolTip(pThis);
}
#endif // MOCK_EQUIP_MARKERS

void __fastcall CUIToolTip__DrawToolTip_Equip_hook(
    CUIToolTip* pThis, void* /*edx*/, int a2, GW_ItemSlotEquip* pe)
{
    // Always draw full vanilla equip tip (custom canvas OFF until info parity).
    // Optional custom path only when EQUIP_TIP_STYLE_CUSTOM_CANVAS=1 and succeeds.
    const bool custom = EquipTooltipStyle_TryDrawCustom(pThis, pe, g_spiritTipExtraH);
    if (!custom) {
        CUIToolTip__DrawToolTip_Equip(pThis, a2, pe);
    } else {
        // Skipping vanilla also skips the SetItem chain hop — refresh set companion.
        SetItem_OnEquipTipDrawn(pThis, pe);
    }

#if MOCK_EQUIP_MARKERS
    // Separate windows: Hyper ★ ABOVE main tip; Potential BELOW (旁挂底条).
    UpdateHyperStarCompanionTip(pThis, pe);
    UpdatePotentialBottomCompanionTip(pThis, pe);
#endif

    // 灵韵技能区块（图标 + 说明 + 当前等级）画在 tip 底部预留高度内
    DrawSpiritSkillBlock(pThis, pe);
#if !GREEN_ENTER_BASELINE
    DrawTipColorStrips(pThis);
#endif

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

std::string ItemOptionTip_FormatGbk(int optionId, int potLevel) {
    return FormatItemOptionTipGbk(optionId, potLevel);
}

unsigned char ItemOptionTip_InferGrade(int pot1, int pot2, int pot3) {
    auto band = [](int id) -> unsigned char {
        if (id <= 0) {
            return 0;
        }
        const int b = id / 10000;
        if (b >= 4) {
            return 5; // 传说
        }
        if (b == 3) {
            return 4; // 独特
        }
        if (b == 2) {
            return 3; // 史诗
        }
        if (b == 1) {
            return 2; // 稀有
        }
        return 1; // 普通
    };
    unsigned char g = band(pot1);
    const unsigned char g2 = band(pot2);
    const unsigned char g3 = band(pot3);
    if (g2 > g) {
        g = g2;
    }
    if (g3 > g) {
        g = g3;
    }
    return g;
}

int ItemOptionTip_PotLevelFromReq(int reqLevel) {
    return PotLevelFromReq(reqLevel);
}

void FusionAnvil_BindDrawToolTipEquipTarget(void** outPtr) {
    if (!outPtr) {
        return;
    }
    *outPtr = reinterpret_cast<void*>(CUIToolTip__DrawToolTip_Equip);
}

void AttachFusionAnvilTooltipHooks() {
    if (!Client::enableFusionAnvilTooltipHooks) {
        return;
    }
    if (g_tooltipHooksAttached) {
        return;
    }
    g_tooltipHooksAttached = true;

    ATTACH_HOOK(Original_GetItemString, Hook_GetItemString);
    ATTACH_HOOK(CUIToolTip__SetToolTip_Equip_Basic, CUIToolTip__SetToolTip_Equip_Basic_hook);
    ATTACH_HOOK(Original_PrintValue, Hook_PrintValue_HyperBreakdown);
    ATTACH_HOOK(CUIToolTip__DrawToolTip_Equip, CUIToolTip__DrawToolTip_Equip_hook);
#if MOCK_EQUIP_MARKERS
    ATTACH_HOOK(Original_ClearToolTip, CUIToolTip__ClearToolTip_Marker_hook);
#endif
}
