# 实现笔记

- 2026-09-15：功能在 `setitem.cpp` 落地；配置 `optional.showItemTipId` 默认 true。
- 对比 tip：`equipcompare` 经 live entry 调用 ShowItemToolTip，以便 SetItem 设 pending。
- 构建：`build_once.bat` Release；PostBuild 部署 `BeiDou-Client_S9`。
- 无临时 DBG 日志；EnsureUiHooks 一次打印 `showItemTipId=on|off`。

## 重叠 / 漏显修复（同日续作）

### 根因

1. **重叠/间距**：初版在 `Hook_MakeLayer` 里对装备 `AddInfoEx`。`SetToolTip_Equip`（`0x8E8252`）在 MakeLayer **之前**已用 `sub_8F4535` 量测橙色脚注并抬高 `m_nHeight`（另有条件 `+=30`）；再追加 ID 行会落入该预留带，`PrintLines` 把 ID 画在脚注带起点，橙色 `DrawTextA` 仍按 `Y ≈ height - measured` 绘制 → 重叠。无脚注时则只见空白忽大忽小。
2. **漏显**：IDA xref 确认 `CUIEquip::OnMouseMove` / `CUIPetEquip::OnMouseMove` 直调 `SetToolTip_Equip`，不经 `ShowItemToolTip`，pending 无效。扩展槽 / 背包走 ShowItemToolTip 故原先可见。

### 改法

1. `Hook_SetToolTipEquipBasic`（`0x008ECA0C`）：Basic 返回后、footer 预留前 `AppendItemIdLine`，并 `MarkPendingItemTipIdConsumed`。
2. `Hook_MakeLayer`：仅当 pending 未消费且 `TipInfoLineCount==0`（Pet/Bundle）时 `m_nHeight += 17`，bake 后画布绘制。
3. **硬化（23:12）**：禁止在 `TipInfoLineCount>0` 时晚 `AddInfoEx`（会再次踩进 footer 带）。
4. LazyCompatInit：FusionAnvilUI → EquipCompare → SetItemUI；FA Tip 当前 SKIP，SetItem Basic 在 EquipCompare 之后挂载为最外层。

### 验证点

- 有脚注装备：金锤子 → ID → 橙色文案，行距均匀。
- 普通装备槽、宠物装备槽有 ID。
- 背包宠物 tip 有 ID。
- 须使用 SHA256 `31124A5F…` 的 dll；客户端若仍为 `1CF4A91E…`（22:57）则仍含危险 MakeLayer 回退。
