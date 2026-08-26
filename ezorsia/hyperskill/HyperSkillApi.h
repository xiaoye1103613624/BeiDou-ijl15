#pragma once

/** MapleRoot-style hyper (5th) skills: all job lines DoActiveSkill + LT/RB area + GBK job names. */
void AttachHyperSkillMod();

/** MapleRoot SkillEdits satellites: LtRb_Eval, FlashJumpAll(1054), Corsair shipSkills. */
void AttachMrSkillSatellites();

/** Slim live Skill books for K-open; promote _full on hyper cast only. */
void AttachHyperBookLoader();
void HyperBookLoader_OnTick();

extern "C" void __cdecl HyperBookLoader_PromoteForSkill(int skillId);
