#include "stdafx.h"
#include "MiracleCubeApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_miracleCubeHooksAttached = false;
} // namespace

namespace MiracleCube {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kMiracleCubeResultOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kMiracleCubeResultOpcode) {
                    return false;
                }
                MiracleCube::HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_miracleCubeHooksAttached) {
        return;
    }
    g_miracleCubeHooksAttached = true;
    AttachMiracleCubeMod();
}
} // namespace
