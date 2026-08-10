#pragma once

// Enable custom Hero V-skill pilot (1121015) cast via skillbook / hotkey.
// Vanilla CUserLocal::DoActiveSkill only hardcodes 1121000/02/06/08/10 (+1121011 via
// is_heros_will_skill). Unknown 112xxxx IDs fall through to silent fail (no SPECIAL_MOVE).
void AttachVSkillCastMod();
