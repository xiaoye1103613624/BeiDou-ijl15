#include "stdafx.h"
#include "MonsterBookApi.h"
#include "MonsterBookConstants.h"
#include "../compat/PacketDispatcher.h"
#include "../compat/wvs/Packet.h"

namespace {
constexpr unsigned short kMonsterBookResultOpcode = 0x372C;

bool HandleMonsterBookResult(void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
    if (packet == nullptr || opcode != kMonsterBookResultOpcode) {
        return false;
    }
#if USE_MONSTER_BOOK_DROPS
    try {
        MonsterBookDrops_OnPacket(packet);
    } catch (...) {
    }
#endif
#if USE_MONSTER_BOOK_SEARCH
    try {
        MonsterBookSearch_OnPacket(packet);
    } catch (...) {
    }
#endif
    return true;
}
} // namespace

void MonsterBook_RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(kMonsterBookResultOpcode, HandleMonsterBookResult);
}

void MonsterBook_AttachHooks() {
#if USE_MONSTER_BOOK_OPEN
    AttachMonsterBookMod();
#endif
#if USE_MONSTER_BOOK_FOUNDIN
    AttachMonsterBookFoundInMod();
#endif
#if USE_MONSTER_BOOK_DROPS
    AttachMonsterBookDropsMod();
#endif
#if USE_MONSTER_BOOK_SEARCH
    AttachMonsterBookSearchMod();
#endif
}

void MonsterBook_OnTick() {
#if USE_MONSTER_BOOK_DROPS
    try {
        MonsterBookDrops_OnClientTick();
    } catch (...) {
    }
#endif
#if USE_MONSTER_BOOK_SEARCH
    try {
        MonsterBookSearch_OnClientTick();
    } catch (...) {
    }
#endif
}
