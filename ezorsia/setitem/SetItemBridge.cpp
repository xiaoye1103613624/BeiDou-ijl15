#include "stdafx.h"
#include "SetItemApi.h"
#include "SetItemData.h"
#include "compat/ClientAddresses.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"
#include <map>
#include <set>
#include <string>

void AttachSetItemMod();
void AttachSetItemUiHooks();
void SetItem_RedrawActivePanel();
bool SetItem_TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH);

namespace SetItemData {
int g_finalDamagePercent = 0;
int g_damageSkinId = 0;
int g_damR = 0;
int g_bossDamR = 0;
int g_normalDamR = 0;
int g_ignorePDR = 0;
int g_ignoreMDR = 0;
int g_critRate = 0;
int g_critDam = 0;
int g_padR = 0;
int g_madR = 0;
int g_itemDropProp = 0;
int g_mesoDropProp = 0;
int g_damageReduce = 0;
} // namespace SetItemData

namespace {
bool g_setItemHooksAttached = false;
std::set<int> g_pendingSetRequests;
std::map<int, std::string> g_setSkillBonusText;
std::map<int, bool> g_setEnabled;
std::map<int, int> g_skillBonusLevels;

void SendSetItemRequest(CompatOutPacketBuilder& builder) {
    void* socket = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (!socket) {
        return;
    }
    CompatOutPacket packet = builder.Build();
    reinterpret_cast<void(__fastcall*)(void*, void*, CompatOutPacket*)>(
            ClientAddresses::kSendPacket)(socket, nullptr, &packet);
}

std::string DecodePacketString(CompatInPacket* packet) {
    const uint16_t len = packet->Decode<uint16_t>();
    std::string s;
    if (len == 0 || !packet->CanRead(len)) {
        return s;
    }
    const unsigned char* p = packet->Current();
    if (p) {
        s.assign(reinterpret_cast<const char*>(p), len);
    }
    packet->SetOffset(packet->GetOffset() + len);
    return s;
}

bool HandleSetItemInbound(void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr) {
        return false;
    }

    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
        return false;
    }
    packet->Decode<uint16_t>();

    switch (opcode) {
    case CustomSendOpcode::kSetItemFinalDamage: {
        SetItemData::g_finalDamagePercent = static_cast<int>(packet->Decode<uint16_t>());
        SetItemData::g_damageSkinId = static_cast<int>(packet->Decode<uint32_t>());
        if (packet->CanRead(18)) {
            SetItemData::g_damR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_bossDamR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_normalDamR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_ignorePDR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_ignoreMDR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_critRate = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_critDam = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_padR = static_cast<int>(packet->Decode<uint16_t>());
            SetItemData::g_madR = static_cast<int>(packet->Decode<uint16_t>());
            if (packet->CanRead(2)) {
                const uint16_t n = packet->Decode<uint16_t>();
                for (uint16_t i = 0; i < n && packet->CanRead(2); ++i) {
                    packet->Decode<uint16_t>();
                }
            }
            // 尾部：物品掉落% / 金币掉落% / 伤害减免%
            if (packet->CanRead(6)) {
                SetItemData::g_itemDropProp = static_cast<int>(packet->Decode<uint16_t>());
                SetItemData::g_mesoDropProp = static_cast<int>(packet->Decode<uint16_t>());
                SetItemData::g_damageReduce = static_cast<int>(packet->Decode<uint16_t>());
            }
        }
        return true;
    }
    case CustomSendOpcode::kSetItemSkillBonus: {
        const uint16_t count = packet->Decode<uint16_t>();
        for (uint16_t i = 0; i < count; ++i) {
            const int setId = static_cast<int>(packet->Decode<uint32_t>());
            const bool enabled = packet->Decode<uint8_t>() != 0;
            std::string text = DecodePacketString(packet);
            if (enabled) {
                g_setEnabled[setId] = true;
                g_setSkillBonusText[setId] = std::move(text);
            } else {
                g_setEnabled[setId] = false;
                g_setSkillBonusText.erase(setId);
            }
            g_pendingSetRequests.erase(setId);
        }
        SetItem::RedrawActivePanel();
        return true;
    }
    case CustomSendOpcode::kSetSkillBonus: {
        const uint16_t count = packet->Decode<uint16_t>();
        g_skillBonusLevels.clear();
        for (uint16_t i = 0; i < count; ++i) {
            const int skillId = static_cast<int>(packet->Decode<uint32_t>());
            const int addLevel = static_cast<int>(packet->Decode<uint32_t>());
            g_skillBonusLevels[skillId] = addLevel;
        }
        return true;
    }
    default:
        return false;
    }
}

void RegisterInboundHandlers() {
    PacketDispatcher::RegisterHandler(CustomSendOpcode::kSetItemFinalDamage, HandleSetItemInbound);
    PacketDispatcher::RegisterHandler(CustomSendOpcode::kSetItemSkillBonus, HandleSetItemInbound);
    PacketDispatcher::RegisterHandler(CustomSendOpcode::kSetSkillBonus, HandleSetItemInbound);
}
} // namespace

namespace SetItem {
void RegisterPacketHandler() {
    RegisterInboundHandlers();
}

void EnsureHooks() {
    if (g_setItemHooksAttached) {
        return;
    }
    g_setItemHooksAttached = true;
    AttachSetItemMod();
}

void EnsureUiHooks() {
    AttachSetItemUiHooks();
}

void RedrawActivePanel() {
    SetItem_RedrawActivePanel();
}

void RequestSetItemBonus(int setId) {
    if (setId <= 0) {
        return;
    }
    const char* cached = GetSetItemSkillBonusText(setId);
    if (cached && cached[0] != '\0') {
        return;
    }
    if (g_pendingSetRequests.count(setId) > 0) {
        return;
    }
    g_pendingSetRequests.insert(setId);

    CompatOutPacketBuilder builder(CustomRecvOpcode::kSetItemBonusRequest);
    builder.WriteInt(setId);
    SendSetItemRequest(builder);
}

const char* GetSetItemSkillBonusText(int setId) {
    const auto it = g_setSkillBonusText.find(setId);
    if (it == g_setSkillBonusText.end()) {
        return "";
    }
    return it->second.c_str();
}

bool IsSetEnabled(int setId) {
    const auto it = g_setEnabled.find(setId);
    return it != g_setEnabled.end() && it->second;
}

bool IsSetDisabled(int setId) {
    const auto it = g_setEnabled.find(setId);
    return it != g_setEnabled.end() && !it->second;
}

int GetSkillBonusLevel(int skillId) {
    const auto it = g_skillBonusLevels.find(skillId);
    return it == g_skillBonusLevels.end() ? 0 : it->second;
}

bool TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    return SetItem_TryGetActiveSetTooltipRect(outX, outY, outW, outH);
}
} // namespace SetItem
