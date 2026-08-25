#pragma once

#include "compat/wvs/Packet.h"

// Buyback tab for the NPC shop window.
namespace ShopBuyback {
    static constexpr int kOpcode_Mode = 0x168;
    static constexpr int kOpcode_OpenShop = 0x131;

    void HandleModePacket(CompatInPacket* packet);
    void OnBeforeOpenShop();
    void ApplyHooks(bool bEnable);
    void RegisterPacketHandler();
}
