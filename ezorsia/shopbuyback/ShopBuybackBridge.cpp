#include "stdafx.h"
#include "ShopBuyback.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace ShopBuyback {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            static_cast<unsigned short>(kOpcode_Mode),
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != static_cast<unsigned short>(kOpcode_Mode)) {
                    return false;
                }
                HandleModePacket(packet);
                return true;
            });

    PacketDispatcher::RegisterHandler(
            static_cast<unsigned short>(kOpcode_OpenShop),
            [](void* /*clientSocket*/, CompatInPacket* /*packet*/, unsigned short opcode) {
                if (opcode != static_cast<unsigned short>(kOpcode_OpenShop)) {
                    return false;
                }
                OnBeforeOpenShop();
                return false;
            });
}
} // namespace ShopBuyback
