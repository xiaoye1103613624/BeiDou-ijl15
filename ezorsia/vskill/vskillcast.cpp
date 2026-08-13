// vskillcast.cpp — allow custom skill 1121015 (Aura Weapon pilot) to cast
//
// Root cause (IDA CUserLocal::DoActiveSkill @ 0x9445B0):
//   Hero 4th-job dispatch hardcodes 1121000/01/02/06/08/10 only.
//   1121015 hits switch default → LABEL_428 → LABEL_1009 (restore HP/MP, return 0).
//   No SPECIAL_MOVE packet is ever sent — WZ level/effect can be perfect and still dead.
//
// Fix: treat 1121015 like Heros Will for *client routing only* — early branch calls
//   DoActiveSkill_StatChange (same path as 1121002 Stance / 1121010 Enrage).
// Server StatEffect.isHerosWill() does NOT include 1121015, so buff logic stays pad/time.

#include "stdafx.h"
#include "VSkillCastApi.h"
#include "compat/hook.h"

#include <cstdint>

namespace {

// WARNING: 0x006ED5EC is NOT IsHerosWillSkill on current BeiDou.exe — that VA is a
// C++ ctor (writes vtable 0xAF04A8). Hooking it at DllMain caused boot AV @ 0x006ED5F1.
// AttachVSkillCastMod is disabled in LazyCompatInit until IDA finds the real VA.
constexpr uintptr_t kIsHerosWillSkill = 0x006ED5EC;  // WRONG — do not re-enable without IDA
constexpr int kAuraWeaponPilot = 1121015;

using IsHerosWillSkill_t = int(__cdecl*)(int nSkillID);
IsHerosWillSkill_t Real_is_heros_will_skill =
    reinterpret_cast<IsHerosWillSkill_t>(kIsHerosWillSkill);

bool g_hookAttached = false;

int __cdecl Hook_is_heros_will_skill(int nSkillID) {
    if (nSkillID == kAuraWeaponPilot) {
        return 1;
    }
    return Real_is_heros_will_skill(nSkillID);
}

} // namespace

void AttachVSkillCastMod() {
    if (g_hookAttached) {
        return;
    }
    g_hookAttached = true;
    ATTACH_HOOK(Real_is_heros_will_skill, Hook_is_heros_will_skill);
}
