#include "stdafx.h"
#include "UserInfoDetailApi.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/secure.h"
#include "compat/ztl/ztl.h"

namespace {
constexpr unsigned long kUserInfoCtxPtr              = 0x00BEDCCC;
constexpr int           kUserInfoCtxCharIdOffset     = 1636;
constexpr int           kUserInfoCtxEquipArrayOffset = 1796;

constexpr unsigned long kCWndOnMouseMove             = 0x00424446;
constexpr unsigned long kCUIUserInfoDetailDraw       = 0x008FD4C0;
constexpr unsigned long kCUIUserInfoDetailDtor       = 0x008FF36F;

constexpr unsigned long kTSecTypeGetData             = 0x0042873D;
constexpr unsigned long kItemToolTipParamCtor        = 0x00483EED;
constexpr unsigned long kCItemInfoGetItemSlot        = 0x005D5D95;
constexpr unsigned long kScrollCtrlGetChild          = 0x0064BCCC;
constexpr unsigned long kScrollCtrlGetOffset         = 0x0064B1A1;
constexpr unsigned long kCUIToolTipShow              = 0x008F5B20;

struct RemoteEquipStats {
    int targetCharId = -1;
    int itemId = 0;
    int anvilItemId = 0;
    int equipSkillId = 0;
    int equipSkillLevel = 0;
    unsigned long long equipSkillExpire = 0;
    short str = 0;
    short dex = 0;
    short intel = 0;
    short luk = 0;
    short maxHp = 0;
    short maxMp = 0;
    short pad = 0;
    short mad = 0;
    short pdd = 0;
    short mdd = 0;
    short acc = 0;
    short eva = 0;
    short craft = 0;
    short speed = 0;
    short jump = 0;
    unsigned char ruc = 0;

    void Clear() {
        *this = RemoteEquipStats{};
    }
};

struct GW_ItemSlotBase : public ZRefCounted {
    virtual ~GW_ItemSlotBase() = 0;
    MEMBER_AT(TSecType<int>, 0xC, nItemID)
};

class GW_ItemSlotEquip : public GW_ItemSlotBase {
public:
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
    MEMBER_AT(int, 0xF9, nAnvilItemID)
    MEMBER_AT(int, 0xFD, nEquipSkillID)
    MEMBER_AT(int, 0x101, nEquipSkillLevel)
    MEMBER_AT(unsigned long long, 0x105, tEquipSkillExpire)
};

static auto TSecTypeGetData =
    reinterpret_cast<int(__thiscall*)(void*, int)>(kTSecTypeGetData);
static auto ItemToolTipParamCtor =
    reinterpret_cast<void(__thiscall*)(void*)>(kItemToolTipParamCtor);
static auto CItemInfoGetItemSlot =
    reinterpret_cast<void*(__thiscall*)(CItemInfo*, void*, int)>(kCItemInfoGetItemSlot);
static auto ScrollCtrlGetChild =
    reinterpret_cast<void*(__thiscall*)(void*, int)>(kScrollCtrlGetChild);
static auto ScrollCtrlGetOffset =
    reinterpret_cast<int(__thiscall*)(void*)>(kScrollCtrlGetOffset);
static auto CUIToolTipShow = reinterpret_cast<void(__thiscall*)(
    void*, int, int, GW_ItemSlotBase*, void*, int, int, int, unsigned int)>(kCUIToolTipShow);
static auto CUIToolTipCtor =
    reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor);
static auto CUIToolTipClear =
    reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear);
static auto ClientSocketSendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

static auto OnMouseMoveOrig =
    reinterpret_cast<int(__thiscall*)(void*, int, int)>(kCWndOnMouseMove);
static auto DrawOrig =
    reinterpret_cast<int*(__thiscall*)(void*, void*)>(kCUIUserInfoDetailDraw);
static auto DtorOrig =
    reinterpret_cast<void(__thiscall*)(void*)>(kCUIUserInfoDetailDtor);

bool g_detailActive = false;
RemoteEquipStats g_remoteStats{};
int g_lastRequestCharId = 0;
int g_lastRequestItemId = 0;
char g_tooltipMem[0x280]{};
bool g_tooltipReady = false;
bool g_hooksAttached = false;

void SendEquipDetailRequest(int charId, int itemId) {
    if (charId <= 0 || itemId <= 0) {
        return;
    }
    if (charId == g_lastRequestCharId && itemId == g_lastRequestItemId) {
        return;
    }
    g_lastRequestCharId = charId;
    g_lastRequestItemId = itemId;
    g_remoteStats.Clear();

    COutPacket oPacket(kUserInfoExSendOpcode);
    oPacket.Encode1(1);
    oPacket.Encode4(charId);
    oPacket.Encode4(itemId);

    void* clientSocket = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (clientSocket) {
        ClientSocketSendPacket(clientSocket, oPacket);
    }
}

void ApplyRemoteStats(GW_ItemSlotEquip* equip, int itemId) {
    if (!equip || g_remoteStats.itemId != itemId) {
        return;
    }
    equip->get_niSTR() = g_remoteStats.str;
    equip->get_niDEX() = g_remoteStats.dex;
    equip->get_niINT() = g_remoteStats.intel;
    equip->get_niLUK() = g_remoteStats.luk;
    equip->get_niMaxHP() = g_remoteStats.maxHp;
    equip->get_niMaxMP() = g_remoteStats.maxMp;
    equip->get_niPAD() = g_remoteStats.pad;
    equip->get_niMAD() = g_remoteStats.mad;
    equip->get_niPDD() = g_remoteStats.pdd;
    equip->get_niMDD() = g_remoteStats.mdd;
    equip->get_niACC() = g_remoteStats.acc;
    equip->get_niEVA() = g_remoteStats.eva;
    equip->get_niCraft() = g_remoteStats.craft;
    equip->get_niSpeed() = g_remoteStats.speed;
    equip->get_niJump() = g_remoteStats.jump;
    equip->get_nRUC() = g_remoteStats.ruc;
    if (g_remoteStats.anvilItemId != 0) {
        equip->nAnvilItemID = g_remoteStats.anvilItemId;
    }
    equip->nEquipSkillID = g_remoteStats.equipSkillId;
    equip->nEquipSkillLevel = g_remoteStats.equipSkillLevel;
    equip->tEquipSkillExpire = g_remoteStats.equipSkillExpire;
}

bool TryShowEquipTooltip(void* self, int x, int y) {
    const unsigned long ctx = *reinterpret_cast<unsigned long*>(kUserInfoCtxPtr);
    if (!ctx) {
        return false;
    }

    unsigned char* equipArray = *reinterpret_cast<unsigned char**>(ctx + kUserInfoCtxEquipArrayOffset);
    if (!equipArray) {
        return false;
    }

    const int equipCount = *reinterpret_cast<int*>(equipArray - 4);
    if (equipCount <= 0) {
        return false;
    }

    const int targetCharId = *reinterpret_cast<int*>(ctx + kUserInfoCtxCharIdOffset);
    void* detail = reinterpret_cast<char*>(self) - 4;
    void* scrollCtrl = ScrollCtrlGetChild(reinterpret_cast<char*>(detail) + 0x74, 0);
    if (!scrollCtrl) {
        return false;
    }
    const int scrollOffset = ScrollCtrlGetOffset(scrollCtrl);

    void** vtable = *reinterpret_cast<void***>(self);
    auto getAbsLeft = reinterpret_cast<int(__thiscall*)(void*)>(vtable[11]);
    auto getAbsTop = reinterpret_cast<int(__thiscall*)(void*)>(vtable[12]);
    const int absLeft = getAbsLeft(self);
    const int absTop = getAbsTop(self);

    for (int row = 0; row < 3; ++row) {
        const int yTop = 40 * row + 32;
        if (x < 17 || x > 49 || y < yTop || y > yTop + 32) {
            continue;
        }

        const int realRow = scrollOffset + row;
        if (realRow < 0 || realRow >= equipCount) {
            return false;
        }

        const int itemId = TSecTypeGetData(equipArray + 12 * realRow, 0);
        if (itemId == 0) {
            return false;
        }

        unsigned char zrefBuf[8]{};
        CItemInfoGetItemSlot(CItemInfo::GetInstance(), zrefBuf, itemId);
        auto* itemSlot = *reinterpret_cast<GW_ItemSlotBase**>(zrefBuf + 4);
        if (!itemSlot) {
            return false;
        }

        SendEquipDetailRequest(targetCharId, itemId);
        ApplyRemoteStats(reinterpret_cast<GW_ItemSlotEquip*>(itemSlot), itemId);

        if (!g_tooltipReady) {
            memset(g_tooltipMem, 0, sizeof(g_tooltipMem));
            CUIToolTipCtor(g_tooltipMem);
            g_tooltipReady = true;
        }
        CUIToolTipClear(g_tooltipMem);

        unsigned char paramBuf[0x2C]{};
        ItemToolTipParamCtor(paramBuf);
        CUIToolTipShow(
            g_tooltipMem,
            absLeft + x + 20,
            absTop + y + 20,
            itemSlot,
            paramBuf,
            0,
            1,
            0,
            0);
        return true;
    }
    return false;
}

int __fastcall OnMouseMoveHook(void* self, void* /*edx*/, int x, int y) {
    const int ret = OnMouseMoveOrig(self, x, y);
    if (!g_detailActive) {
        return ret;
    }

    if (TryShowEquipTooltip(self, x, y)) {
        return ret;
    }

    if (g_tooltipReady) {
        CUIToolTipClear(g_tooltipMem);
    }
    g_lastRequestCharId = 0;
    g_lastRequestItemId = 0;
    g_remoteStats.Clear();
    return ret;
}

int* __fastcall DrawHook(void* self, void* /*edx*/, void* arg0) {
    g_detailActive = true;
    g_lastRequestCharId = 0;
    g_lastRequestItemId = 0;
    g_remoteStats.Clear();
    return DrawOrig(self, arg0);
}

void __fastcall DtorHook(void* self, void* /*edx*/) {
    if (g_tooltipReady) {
        CUIToolTipClear(g_tooltipMem);
    }
    g_detailActive = false;
    g_lastRequestCharId = 0;
    g_lastRequestItemId = 0;
    g_remoteStats.Clear();
    DtorOrig(self);
}
} // namespace

namespace UserInfoDetail {
void HandleServerPacket(CompatInPacket* packet) {
    if (!packet || !packet->CanRead(3)) {
        return;
    }
    const unsigned char type = packet->Decode<unsigned char>();
    if (type != 1 || !packet->CanRead(4 + 4 + 4 + 15 * 2 + 1)) {
        return;
    }

    RemoteEquipStats stats{};
    stats.targetCharId = packet->Decode<int>();
    stats.itemId = packet->Decode<int>();
    stats.anvilItemId = packet->Decode<int>();
    stats.equipSkillId = packet->Decode<int>();
    stats.equipSkillLevel = packet->Decode<int>();
    const unsigned int expireLo = packet->Decode<unsigned int>();
    const unsigned int expireHi = packet->Decode<unsigned int>();
    stats.equipSkillExpire =
        (static_cast<unsigned long long>(expireHi) << 32) | expireLo;
    stats.str = static_cast<short>(packet->Decode<unsigned short>());
    stats.dex = static_cast<short>(packet->Decode<unsigned short>());
    stats.intel = static_cast<short>(packet->Decode<unsigned short>());
    stats.luk = static_cast<short>(packet->Decode<unsigned short>());
    stats.maxHp = static_cast<short>(packet->Decode<unsigned short>());
    stats.maxMp = static_cast<short>(packet->Decode<unsigned short>());
    stats.pad = static_cast<short>(packet->Decode<unsigned short>());
    stats.mad = static_cast<short>(packet->Decode<unsigned short>());
    stats.pdd = static_cast<short>(packet->Decode<unsigned short>());
    stats.mdd = static_cast<short>(packet->Decode<unsigned short>());
    stats.acc = static_cast<short>(packet->Decode<unsigned short>());
    stats.eva = static_cast<short>(packet->Decode<unsigned short>());
    stats.craft = static_cast<short>(packet->Decode<unsigned short>());
    stats.speed = static_cast<short>(packet->Decode<unsigned short>());
    stats.jump = static_cast<short>(packet->Decode<unsigned short>());
    stats.ruc = packet->Decode<unsigned char>();
    g_remoteStats = stats;
}

void AttachHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    ATTACH_HOOK(OnMouseMoveOrig, OnMouseMoveHook);
    ATTACH_HOOK(DrawOrig, DrawHook);
    ATTACH_HOOK(DtorOrig, DtorHook);
}
} // namespace UserInfoDetail
