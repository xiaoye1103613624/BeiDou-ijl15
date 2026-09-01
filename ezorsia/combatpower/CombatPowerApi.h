#pragma once

namespace CombatPower {
void RegisterModule();
void SetCombatPower(int characterId, long long power);
long long GetCombatPower(int characterId);
void ClearCombatPower(int characterId);
} // namespace CombatPower
