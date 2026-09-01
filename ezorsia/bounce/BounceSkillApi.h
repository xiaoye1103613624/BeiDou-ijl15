#pragma once

// Beginner purple bounce skills (1050 横向闪跃 / 1054 垂直闪跃).
// Vanilla DoActiveSkill does not route these IDs → silent no-cast (icon only).
// FlashJump cave + DoBoundJump dispatch ported from MapleRoot / MXD hyperskill (minimal).
void AttachBounceSkillMod();
