# Addon 扩展栏 — 禁止操作清单（强制）

> **Mandate:** sidecar = **只存数据**；穿卸/占用/busy/tip/登录 apply = **原版 parity**  
> 每条都曾在 live 复现为「修 A 坏 B」。**禁止再发明特殊解锁包。**

## Forbidden（代码与评审一律拒绝）

| # | 操作 | 已证实后果 |
|---|------|------------|
| 1 | 空 `modifyInventory([])` 当 unlock | 假死 / AV（同 getInventoryFull） |
| 2 | Addon 座穿后 `forceUpdateItem`（mode-3+0） | 穿 −55 后 ~3s 闪退 |
| 3 | 同一包 dual mode-3（−bp + −(bp+100)） | 闪退（22:47 dual clear） |
| 4 | 装备栏 **mode-3 remove** 做 ghost-sync（`addMovement=2`） | 悬停原装 tip **卡死** → ALL_IDLE（23:08） |
| 5 | clear-only 卸装 / 乐观 SetItem（无真实 mode-2） | 丢物、多一件、背包空洞 |
| 6 | 服务端 **occupy 硬拒**（不 replace）当客户端 GetItem 空 | 「徽章/副手戴不上」+ 栏空白（23:05 occupy 风暴） |
| 7 | 把 BP54+ 写入原生 `aEquipped[52]` | 脸/眼串台 |
| 8 | PreferSend / GetItem / SetItem 范围缩回 56–61 | 空白+连穿 |
| 9 | 胖 DLL（>1447424）/ EXE CD grow 盖 Client_1 | 进图红 |

## Allowed（原版同形）

| 动作 | 包形态 |
|------|--------|
| 穿戴成功 | **mode-2** bag→−bp（落库 normal −54…−62） |
| 卸下成功 | **mode-2** −bp→bag（wire oldPos = −bp） |
| 拒穿/拒卸/幽灵 | **仅** `enableActions`（ItemMoveHandler finally 可再发） |
| 占用（客户端） | GetItem 非空时 UX 提示；**服务端 replace** 同经典座 |
| 幽灵（客户端） | 卸装发包后 busy 清且座仍亮 → **本地** ClearSidecarBp |

## 回归门禁（声称完成前必须过）

1. 冷启客户端 + **重启 gms-server**（新 jar）
2. 徽章穿一次 → 显示；再穿替换或先卸再穿；**无**「请先卸下」硬拒死锁
3. 图腾1–4 穿；卸图腾1 → **回背包一次**；栏清空
4. 副手 134/135 → −62 穿卸正常；盾 109 → −10
5. 下线再上：扩展座与下线前一致（有则全有，无则全无；无「图腾无/副手幽灵」）
6. 悬停 **原装栏 + 扩展栏** tip：**不卡死**
7. 无闪退；整理不假死
8. Live：`ijl15` `60A3E54F…` / 1447424；EXE `ACF10F63…`

脚本：`tools/guard_client1_execave.ps1` + `gms-server/tools/guard_addon_error_ops.ps1` + [`ADDON_ERROR_OPS_MATRIX.md`](./ADDON_ERROR_OPS_MATRIX.md)。
