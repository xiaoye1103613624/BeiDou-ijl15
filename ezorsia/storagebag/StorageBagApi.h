#pragma once

class CompatInPacket;

constexpr unsigned short kBagWindowSendOpcode = 0x3724;
constexpr unsigned short kBagWindowRecvOpcode = 0x3725;

void AttachStorageBagMod();
bool StorageBag_HandleMouseMessage(unsigned int& msg, unsigned long wParam, long lParam, long* plResult);

namespace StorageBag {
void HandleServerPacket(CompatInPacket* packet);
void RegisterPacketHandler();
void EnsureHooks();
}
