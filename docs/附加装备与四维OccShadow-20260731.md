# 附加装备 UI + 护肩 HitTest + 主四维 OccShadow — SHOULDER_HT_ADDON_TIP_20260731

## 0. 本轮（护肩 tip / 双击卸 + Addon tip）

| 项 | 说明 |
|----|------|
| 症状 | 护肩红 8 有图标可从背包穿；悬停 **无 tip**；已装备双击 **不能卸** |
| 根因 | `GetBodyPartFromPoint` 已改走 DLL `g_hitTestExt`；原生 BP20 HitTest 为 **(71,233)**（栏外）。pendant2 只把 PE `BE2260[19]` / GetSlotXY 写成红 8 **(137,101)**，**未同步 DLL 表** → 图标对、HitTest 空 → 无 tip / 双击找不到 BP20 |
| 修复 | DllMain 建表时强制 `g_hitTestExt[19]=(137,101)`；pendant2 `PatchTableSlot(HitTest)` 同步 `Shoulder_SetExtHitTestSlot` |
| Addon | 徽章/图腾座位补 `OnMouseMove` → `ShowItemToolTip`；双击卸装仍走 `SendChangeSlotPosition` |
| 戳记 | `SHOULDER_HT_ADDON_TIP_20260731`（含既有 MAINSTAT_OCCSHADOW_EBPFIX） |
| 约束 | `kNativeCd64ExtendedSlots=false`；护肩仍在 **主栏** 红 8，不进 Addon |

## 1. 主四维根因（已保留）

| 项 | 说明 |
|----|------|
| 症状 | 详情战斗面板随红 3/4 变；**S 主能力四维不变** |
| 根因 | MainStat OccShadow ExpectBytes 按错 EBP；live PE 为 `[ebp+10h]` / `[ebp-4]` |
| 修复 | ExpectBytes + cave fallback 用 live PE；BP52–55 → `g_ring52/53` / badge / totem |
| 前戳记 | `MAINSTAT_OCCSHADOW_EBPFIX_20260731` |

## 2. 附加装备栏

| 项 | 说明 |
|----|------|
| 资源 | `UIWindow.img/Equip/Addon/backgrnd`（141×168 ARGB4444） |
| 运行时 | `equipaddon/`：`CUIEquipAddon` 随 `CUIEquip`（`dword_BED650`）**同开同关** |
| 吸附 | 每 tick `SyncDockToEquip`：Addon 右缘贴 Equip 左缘，顶对齐 |
| 徽章/图腾 | 主栏红 9/10 HitTest/GetSlotXY/BE27 **停靠 (−128,−128)**；Addon 上排绘 −54/−55；悬停 tip；双击 `SendChangeSlotPosition` 卸装 |
| 穿戴 | 背包双击仍走 bodypart→−54/−55；拖到 Addon 座位的完整 OnDragDrop 可后续补 |

## 3. 验证步骤

1. **完全退出**客户端；确认 `ijl15_VERSION.txt` / `ijl15_shoulders_VERSION.txt` = `SHOULDER_HT_ADDON_TIP_20260731`，`pendant2_ui=true`。
2. 穿有四维的戒指到红 3/4 → 开 **S 主能力**：力量等应随穿卸变化（与详情战斗一致）。
3. 开 **E**：左侧「扩展装备栏」；关 E 一并消失；拖装备栏时 Addon 跟左上角。
4. **护肩红 8**：悬停有 tip；双击可卸回背包；背包双击仍可穿。
5. 徽章/图腾：主栏红 9/10 不可点；Addon 上排有图标 + tip；双击可卸。
6. 勿部署未完成 CD64 EXE。

## 4. 相关文件

- `ezorsia/shoulders/shoulders.cpp` — MainStat EBP fix + DLL HT 含 BP20 红 8 + `Shoulder_SetExtHitTestSlot`
- `ezorsia/pendant2/pendant2.cpp` — HitTest remap 同步 DLL 表；badge/totem park
- `ezorsia/equipaddon/*` — Addon UI + tip
- `ezorsia/compat/LazyCompatInit.cpp` — tick / EnsureHooks
