#include "stdafx.h"
#include "ColoringPrismApi.h"
#include "coloringprism.h"
#include "weapontint.h"
#include "itemeff.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_coloringPrismHooksAttached = false;
} // namespace

void ColoringPrism_OnTick() {
    WeaponTint_Tick();
}

namespace ColoringPrism {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kColoringPrismRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kColoringPrismRecvOpcode || !packet) {
                    return false;
                }
                // Dispatcher has already consumed the opcode; weapontint expects
                // the cursor still AT the opcode word — rewind two bytes.
                const size_t off = packet->GetOffset();
                if (off >= 2) {
                    packet->SetOffset(off - 2);
                }
                // CompatInPacket is the CInPacket alias used by weapontint (pch.h).
                WeaponTint_HandleSync(reinterpret_cast<CInPacket*>(packet));
                return true;
            });
}

void EnsureHooks() {
    if (g_coloringPrismHooksAttached) {
        return;
    }
    g_coloringPrismHooksAttached = true;
    AttachWeaponTintMod();
    AttachColoringPrismMod();
    AttachItemEffectMod();
}
} // namespace ColoringPrism
