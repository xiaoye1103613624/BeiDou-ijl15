#pragma once

class CompatInPacket;

/** Phase10 LP: MiracleCube / HyperMiracleCube result window. */
constexpr unsigned short kMiracleCubeResultOpcode = 0x17A;

/**
 * Popup disabled by default (DisablePopup default=1).
 * Cube results go to chat via server dropMessage (self-only).
 * Set [miraclecube] DisablePopup=0 in config.ini to re-enable popup for debug.
 * Legacy 0x17A packets are still decoded but ignored when disabled.
 */

namespace MiracleCube {
void HandleServerPacket(CompatInPacket* packet);
void RegisterPacketHandler();
void EnsureHooks();
void AttachMiracleCubeMod();
} // namespace MiracleCube
