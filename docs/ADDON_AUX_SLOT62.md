# 辅助武器独立槽 −62（与盾 −10 真正分槽）

> Stamp: `ADDON_AUX_SLOT62_20260803`

## 问题

此前「UI 拆分」仍让 **109 / 134 / 135 共用存储 −10**：

- 穿盾：扩展栏辅助座不变（正确）
- 穿辅助：主栏盾位显示辅助，并卸掉盾（因为同槽互斥）

类型/ID 段虽不同，**槽位没分开**。

## 方案

| 前缀 | 槽 | UI |
|------|----|----|
| 109 | −10 | 主栏原生盾 |
| 134 / 135 | **−62**（cash −162） | Addon 第三行「辅助武器」 |

可同时穿；双手武器仍只卸 −10。登录迁移：若 −10 上是 134/135 → 挪到 −62。

## 改动面

- Server: `EquipSlot.AUX_WEAPON`, `ExtendedEquipRegistry`, `InventoryManipulator` + `migrateAuxWeaponOffShieldSlot`
- Client: `kSubWeaponBp=62`, sidecar 56–62, AbleToWear / Get-Set / login jg-allow
- **2026-08-04 live:** binary patch `ADDON_AUX_BP62` on JG_ONLY → ijl `0F244A5D…` / 1447424; server `isGreenEnterWireOmit` cleared (restart required)
- **2026-08-04 ~10:47 ENTER-RED:** select→enter `0xC0000005`; rolled back to `A0BE14C9`; omit −62 **restored**; avoid #45 — do not redeploy BP62 on vanilla CD
