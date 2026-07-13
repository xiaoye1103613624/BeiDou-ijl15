#pragma once

class CompatInPacket;

constexpr unsigned short kUserInfoExSendOpcode = 0x3726;
constexpr unsigned short kUserInfoExRecvOpcode = 0x3727;

namespace UserInfoDetail {
void RegisterPacketHandler();
void AttachHooks();
}
