# Addon 扩展栏 — 错误操作矩阵（强制防复发）

> 日期：2026-08-04 ~23:40 · 对应用户投诉「问题不要重复出现」  
> 硬规则：[`ADDON_FORBIDDEN_OPS.md`](./ADDON_FORBIDDEN_OPS.md) · 清单：[`ADDON_OCCUPY_GUARD_CHECKLIST.md`](./ADDON_OCCUPY_GUARD_CHECKLIST.md)  
> Mandate：**sidecar = 只存数据；逻辑 = 原版 mode-2**（拖拽 / 双击替换 / tip / 占用 replace）

---

## 1. 每操作一行（期望 vs 禁止）

| 操作 | 期望 vanilla 包 | 客户端 hook | 服务端 | **已知坏模式（禁止）** |
|------|-----------------|-------------|--------|------------------------|
| 双击背包穿扩展座 | mode-2 bag→−bp | PacketSendOnly SendChange；dst=−54…−62 | `equip()` 定座 + **replace** | wear_fn 写原生 aEquipped[54+]；occupy 硬拒；穿后 forceUpdate mode-3+0 |
| 拖背包→扩展座 | 同上 | OnDropped → WearBagToBp send-only | 同上 | PreferSend mega 喷改；空 invent unlock |
| 拖扩展→背包 / 互换 | mode-2 −bp→bag 或 swap | HandleInput unequip / drag_off | `unequip()` wireOld=`toClientWireSlot` | dual −bp/−(bp+100) mode-3；clear-only 不发包 |
| 双击已装备卸下 | mode-2 −bp→bag | pending-ghost-heal；busy 清后仍亮 → ClearSidecar | 有物 mode-2；无物 **仅** enableActions | ghost-sync mode-3 remove（tip 卡死） |
| 登录 CharInfo | 含 −54…−62（omit aux 仅当 `GREEN_ENTER_OMIT_AUX62`） | Apply/Set → arena；LoginClear 换角 | migrate −154→−54（非点装）；点装可留 −154 由 alias 解析 | CharInfo 省略却客户端仍 paint；BP54 写脸/眼 |
| 悬停原装 tip | 原生 GetItem(−1…−11) | **禁止**改 classic tip；hook 仅 sidecar 座 | — | PreferSend/GetItem 把 classic 导进坏 sidecar；equipped mode-3 spam |

抓取（grab）：难做可跳过（本矩阵不要求）。

---

## 2. 2026-08-04 ~23:05–23:08 症状 → 根因（日志）

| # | 用户症状 | out.log / 证据 | 根因 |
|---|----------|----------------|------|
| 1 | 图腾1卸不了 | `unequip reject source-null src=-55` + ghost-sync | 服务端 EQUIPPED 空，客户端 arena 幽灵；旧 jar 发 mode-3 clear → 仍卸不净 / tip 坏 |
| 2/3 | 徽章/副手戴不上 | `equip reject occupy dst=-54/-62 haveId=…` | **occupy 硬拒**（应 replace）；运行中 jar 仍是 23:03 旧码 |
| 4 | 重登：图腾无 / 副手有 | CharInfo + migrate；ghost 与真实 −62 并存 | 幽灵 totem 未落库；−62 真实装备在库 → 重登只剩副手 |
| 5 | 悬停原装卡死 | 同时段 ghost-sync spam | FORBIDDEN #4：equipped mode-3 + addMovement=2 |
| — | 「修了还在」 | class 23:13 已去 occupy；**进程仍 23:03** | **未重启 gms-server**；live DLL 无 `GHOST heal` 字符串 |

---

## 3. 回归守卫（自动化）

```powershell
# Client_1 EXECAVE + APPEND_ONLY lock
powershell -File E:\pro\BeiDou-ijl15\tools\guard_client1_execave.ps1

# 服务端错误操作字符串 + EquipSlot cash dual-band
powershell -File E:\pro\BeiDou-Server_xy\gms-server\tools\guard_addon_error_ops.ps1
```

| 检查 | 通过条件 |
|------|----------|
| InventoryManipulator.class | **无** `equip reject occupy`；**无** `ghost-sync clear`；有 `enableActions only` |
| ExtendedEquipRegistry.class | 有 `VANILLA_REPLACE_NO_OCCUPY_REJECT`；`GREEN_ENTER_OMIT_AUX62=false` 时无 omit 拒穿 |
| EquipSlot.isAllowed | 点装徽章/副手可落 **−54/−62**（exact match，禁止 cash 双减 100） |
| ijl15 live | `60A3E54F…` / 1447424 / stub `53 E8`；源码级 ghost-heal 未进 live 前 **以 jar 为主** |
| 运行中 JVM | 启动时间 **晚于** class mtime（否则旧码继续 occupy） |

---

## 5. 2026-08-05 复发（假卸载 / 整理闪 / 副手空白连穿）

见 [`ADDON_REGRESSION_RECURRENCE_20260805.md`](./ADDON_REGRESSION_RECURRENCE_20260805.md)：

- 服务端已 enableActions-only，但 live DLL **无 GHOST heal** → 幽灵残留  
- 胖链 GHOSTHEAL `F3411A69`/1512960 **禁止**部署  
- 整理：禁止空 `modifyInventory`（已分片/跳过）  
- 副手：DB 有 −62、UI GetItem 空 → replace 表现为「无限穿」— 冷启+登录 Apply，勿恢复 occupy 硬拒
