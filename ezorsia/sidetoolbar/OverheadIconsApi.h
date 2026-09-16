#pragma once

// 场上头顶入口迁侧边栏：屏蔽 NPC QuestIcon + 角色 QuestAlert 灯泡。
// IDA 证据见 docs/features/overhead-icons-to-sidebar/evidence/

namespace OverheadIcons {
/** 默认 true，与服务端 game_config replace_overhead_icons 缺省一致。 */
void SetSuppressFieldIcons(bool suppress);
bool IsSuppressFieldIcons();

void AttachHooks();
}
