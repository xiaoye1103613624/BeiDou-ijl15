#include "stdafx.h"
#include "WeatherApi.h"
#include "weather.h"
#include "lamps.h"
#include "compat/PacketDispatcher.h"

namespace {
constexpr unsigned short kWeatherSyncOpcode = 0x373D;
constexpr unsigned short kLampPreviewOpcode = 0x373F;
}

bool Weather_IsFieldActive() {
    return Weather::IsFieldActive();
}

bool Weather_HasFallingSky() {
    return Weather::HasFallingSky();
}

void Weather_RegisterPacketHandlers() {
    PacketDispatcher::RegisterHandler(
        kWeatherSyncOpcode,
        [](void*, CompatInPacket* packet, unsigned short) -> bool {
            Weather_HandleWorldState(packet);
            return true;
        });
    PacketDispatcher::RegisterHandler(
        kLampPreviewOpcode,
        [](void*, CompatInPacket* packet, unsigned short) -> bool {
            Lamp_HandlePreviewPacket(packet);
            return true;
        });
}
