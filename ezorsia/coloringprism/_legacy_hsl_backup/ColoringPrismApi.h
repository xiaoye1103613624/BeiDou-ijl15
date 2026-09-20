#pragma once

class CompatInPacket;

constexpr unsigned short kColoringPrismRecvOpcode = 0x184;
constexpr unsigned short kColoringPrismSendOpcode = 0x11D;

void AttachColoringPrismMod();

void ColoringPrism_RequestOpen();
void ColoringPrism_OpenWindow();
void ColoringPrism_CloseWindow();
bool ColoringPrism_IsOpen();
void ColoringPrism_HandleServerPacket(CompatInPacket* packet);
void ColoringPrism_OnTick();

namespace ColoringPrism {
void RegisterPacketHandler();
void EnsureHooks();
}
