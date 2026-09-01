// LimitBreakBridge.cpp — apply per-character damage cap from server LP 0x3730.
//
// IDA (BeiDou.exe @ 0x400000): verified existing Client.cpp patch sites (unchanged addresses):
//   0x008C3304  mov eax, imm32   (panel / damage int imm; stock 199999)
//   0x00AFE8A0  double           (on-screen damage display cap; stock 199999.0)
// No existing hook addresses were changed; only runtime immediates/data are rewritten.

#include "stdafx.h"
#include "WindowTitleApi.h"
#include "Memory.h"
#include "compat/ClientAddresses.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

#include <cstdint>

namespace {

constexpr uintptr_t kAddr_DamageCapImm = 0x008C3304 + 1;
constexpr uintptr_t kAddr_AtkOutCapDouble = 0x00AFE8A0;

void ApplyDamageCapMemory(long long value) {
    if (value < 0) {
        value = 0;
    }
    const unsigned int asInt =
            value > static_cast<long long>(0x7FFFFFFF) ? 0x7FFFFFFFu
                                                       : static_cast<unsigned int>(value);
    Memory::WriteInt(kAddr_DamageCapImm, asInt);
    Memory::WriteDouble(kAddr_AtkOutCapDouble, static_cast<double>(value));
}

bool HandleLimitBreakPacket(CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr || opcode != CustomSendOpcode::kLimitBreakSync) {
        return false;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
        return false;
    }
    packet->Decode<uint16_t>();
    const long long limitBreak = static_cast<long long>(packet->Decode<uint64_t>());
    WindowTitle::ApplyLimitBreak(limitBreak);
    return true;
}

} // namespace

namespace WindowTitle {

void ApplyLimitBreak(long long value) {
    SetLimitBreak(value);
    ApplyDamageCapMemory(value);
}

void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            CustomSendOpcode::kLimitBreakSync,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                return HandleLimitBreakPacket(packet, opcode);
            });
}

} // namespace WindowTitle
