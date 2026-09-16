#pragma once

namespace SetItemData {
extern int g_finalDamagePercent;
extern int g_damageSkinId;

/** Full combat/panel sync from SET_ITEM_FINAL_DAMAGE (0x175). */
struct CombatPanelStats {
    int finalDamagePercent = 0;
    int damageSkinId = 0;
    int damR = 0;
    int bossDamR = 0;
    int normalDamR = 0;
    int ignorePDR = 0;
    int ignoreMDR = 0;
    int critRate = 0;
    int critDam = 0;
    int padR = 0;
    int madR = 0;
    int itemDropProp = 0;
    int mesoDropProp = 0;
    int damageReduce = 0;
    int asrR = 0;
    int buffTimeR = 0;
    int stanceProp = 0;
};

extern CombatPanelStats g_combatPanel;
}
