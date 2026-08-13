#include "stdafx.h"
#include "ColoringPrismApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_coloringPrismHooksAttached = false;
} // namespace

namespace ColoringPrism {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kColoringPrismRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kColoringPrismRecvOpcode) {
                    return false;
                }
                ColoringPrism_HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_coloringPrismHooksAttached) {
        return;
    }
    g_coloringPrismHooksAttached = true;
    AttachColoringPrismMod();
}
} // namespace ColoringPrism
