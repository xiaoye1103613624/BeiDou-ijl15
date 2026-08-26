#pragma once

struct CompatInPacket;

void MonsterBook_RegisterPacketHandler();
void MonsterBook_AttachHooks();
void MonsterBook_OnTick();

void AttachMonsterBookMod();
void AttachMonsterBookFoundInMod();
void AttachMonsterBookDropsMod();
void MonsterBookDrops_OnClientTick();
void MonsterBookDrops_OnPacket(CompatInPacket* pPacket);
void AttachMonsterBookSearchMod();
void MonsterBookSearch_OnClientTick();
void MonsterBookSearch_OnPacket(CompatInPacket* pPacket);
void MonsterBookSearch_SetItemResultView(bool bOn);
bool MonsterBookSearch_IsItemResultView();
