#include "stdafx.h"
#include "SideToolbarApi.h"
#include "../WorldMapInfo.h"
#include "../Client.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/field.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
bool g_sideToolbarEnabled = false;
constexpr unsigned short kSidebarConfigOpcode = 0x3733;

void SidebarLog(const char* /*msg*/) {
    // tip-drawonly: no fopen(sidebar_debug) — absent from green 6D612F01; enter GS family.
}

std::string DecodeMapleString(CompatInPacket* packet) {
    std::string s;
    if (!packet || !packet->CanRead(2)) {
        return s;
    }
    const uint16_t len = packet->Decode<uint16_t>();
    if (len == 0 || !packet->CanRead(len)) {
        return s;
    }
    const unsigned char* p = packet->Current();
    if (p) {
        s.assign(reinterpret_cast<const char*>(p), len);
    }
    packet->SetOffset(packet->GetOffset() + len);
    return s;
}

bool HandleSidebarConfigPacket(CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr || opcode != kSidebarConfigOpcode) {
        return false;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != opcode) {
        return false;
    }
    packet->Decode<uint16_t>(); // consume opcode
    if (!packet->CanRead(1)) {
        return true;
    }
    const int count = static_cast<int>(packet->Decode<uint8_t>());
    SideToolbar::ResetServerToolConfigDefaults();
    for (int i = 0; i < count; ++i) {
        if (!packet->CanRead(2)) {
            break;
        }
        const int toolIndex = static_cast<int>(packet->Decode<uint8_t>());
        const bool visible = packet->Decode<uint8_t>() != 0;
        const std::string title = DecodeMapleString(packet);
        const std::string desc = DecodeMapleString(packet);
        SideToolbar::ApplyServerToolConfig(
                toolIndex,
                visible,
                title.c_str(),
                desc.c_str());
    }
    SideToolbar::InvalidateUi();
    return true;
}
} // namespace

namespace SideToolbar {
// Field-enter is late enough for ResMan. Creating only from OnTick was fragile:
// if BossHP's UserLocal::Update hook fails, ModRegistry::OnClientTick never runs
// and the toolbar is never created.
void EnsureHooks() {
    g_sideToolbarEnabled = true;
    char line[192];
    sprintf_s(line, "EnsureHooks: create now disableWorldMap=%d",
              Client::disableWorldMap ? 1 : 0);
    SidebarLog(line);
    // WorldMap force-attach soft-disabled (enter MapHelper / 0x8007000D bisect).
    SidebarLog("EnsureHooks: SKIP WorldMap force-attach (enter MapHelper bisect)");
    // Only create once in-field — login screen must stay clean.
    if (get_field()) {
        AttachSideToolbarMod();
    }
}

void OnTick() {
    if (!g_sideToolbarEnabled) {
        return;
    }
    // Login / char-select: never keep or recreate toolbar.
    if (!get_field()) {
        DestroyForLogout();
        return;
    }
    // Retry if first create failed (ResMan not ready yet).
    static int s_ticks = 0;
    if (s_ticks < 120) {
        ++s_ticks;
        if ((s_ticks % 15) == 0) {
            AttachSideToolbarMod();
        }
    }
}

void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kSidebarConfigOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                return HandleSidebarConfigPacket(packet, opcode);
            });
}
} // namespace SideToolbar
