#pragma once

namespace SetItem {
void RegisterPacketHandler();
void EnsureHooks();
void EnsureUiHooks();
void RequestSetItemBonus(int setId);
void RedrawActivePanel();
const char* GetSetItemSkillBonusText(int setId);
bool IsSetEnabled(int setId);
int GetSkillBonusLevel(int skillId);
} // namespace SetItem
