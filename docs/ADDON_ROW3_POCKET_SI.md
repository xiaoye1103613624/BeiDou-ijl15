# Addon 第三行 — 口袋 + 辅助武器

> **Date:** 2026-08-02  
> **Status:** **IMPLEMENTED / deployed** — stamp `ADDON_ROW3_POCKET_SI_20260802`  
> **Parent:** [`ADDON_EXTEND_EQUIP_ARCHITECTURE.md`](./ADDON_EXTEND_EQUIP_ARCHITECTURE.md)  
> **095 facts:** [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md)  
> **Avoid:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)

---

## Live

| Field | Value |
|-------|-------|
| Stamp | `ADDON_ROW3_POCKET_SI_20260802` |
| SHA256 | `D2C538A931259114EA036D3AB843144C2BD1460E3C904DAC5C13138CA57BCDAB` |
| Size | `1443840` |
| WZ | `UI/UIWindow.img/Equip/Addon/backgrnd` **141×204 ARGB4444**（标签：口袋 / 辅助武器） |
| STATS bak (rollback DLL) | `ijl15.dll.bak_ADDON_STATS_UNEQUIP_20260802` (`4AD0AA7A…`, 1438208) |
| WZ aside | `UIWindow.img.aside_before_row3_promote` + `golden/UIWindow.img.bak_pre_row3_20260802` |

### User verify (this stamp only)

1. 选角 → **进图**（新功能版必测）  
2. 开装备栏 → 开扩展栏 → 见 **第三行**：口袋 | 辅助武器  
3. （可选）拖 116→口袋座、134/135→辅助座；109 仍主栏盾，拖到辅助座应拒绝  

**不要**为纯 STATS 回滚再测进图。

### Rollback

```text
DLL:  copy bak_ADDON_STATS_UNEQUIP_20260802 → ijl15.dll ; VERSION=RESTORED_ADDON_STATS_UNEQUIP_20260802
WZ:   MCP save_as / promote from UIWindow.img.aside_before_row3_promote (or golden bak_pre_row3)
```

---

## 1. 结论

**可以（YES）** — 口袋（116 / −33）与辅助武器（134|135 / −10）做成 Addon **第三行 sidecar 座位**，走与现有图腾/徽章/安卓/心脏/徽章 **同一套** GetItem → `DrawItemIconForSlot` → Addon HitTest/拖放路径。

| 条件 | 要求 | 实现 |
|------|------|------|
| 绘制 | **仅** Addon `Paint()` | ✅ `kSeats` + Paint loop |
| 主栏红 9/10 | **废弃** `WireMainPocketSubSlots` | ✅ no-op；Init 串无 red9/10 wire |
| 盾牌 109 | 主栏原版；Addon 辅助拒 109 | ✅ `ItemMatchesSeat` / wear reject |
| 存储 | −33 / −10 不变 | ✅ 服务端 AbleToWear 未改路由 |
| 进图 | Occ OFF；ExtraRing OFF；无 −152；无 draw-skip/BP33 cave | ✅ 静态 GREEN |

---

## 2. 座位表

`kPanelH` **204**；第三行 `y=147`：

| 座 | (sx,sy) | BP | 槽 | 前缀 |
|----|---------|---:|----|------|
| 口袋 | (6,147) | 33 | −33 | 116 |
| 辅助武器 | (39,147) | 10 | −10 | 134\|135 only |

右两格空。`IsSidecarBp` 仍仅 56–61。

---

## 3. 构建

- `_build_addon_row3_pocket_si.bat` + `_static_enter_risk_row3.ps1`  
- WZ：`_wz_addon_row3_backgrnd_v3.py`（MCP set_png→save→`.bak`）+ unload 后 promote MCP stage（Java 曾锁 live）
