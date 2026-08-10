# 扩展装备 = 原装备栏同一套逻辑（产品硬要求）

> **Date:** 2026-08-03  
> **Status:** 方向确认 — 停止用迁移/乐观 UI/双轨状态机「补不一致」  
> **Parent:** [`ADDON_083_ADAPT_FROM_095.md`](./ADDON_083_ADAPT_FROM_095.md)

---

## 用户要求（原话对齐）

1. **要真实记录**扩展装备（重登仍是换好的那几件、那个座位）。  
2. **逻辑与原有装备栏一致**：穿 / 卸 / 拖 / 双击 / 背包换格 / 登录解码 / 存盘。  
3. 不一致就会不断加补丁（迁移 swap、SendBusy、clear-only…）— **禁止再靠补丁堆叠**。

---

## 现在其实记在哪？

| 层 | 是否「真实」 | 说明 |
|----|:------------:|------|
| **服务端 DB** `inventoryitems.position` | **是** | −54…−62 等与帽子 −1 一样进表、一样 `equip()`/`unequip()` |
| **服务端内存 EQUIPPED** | **是** | 穿脱只认负槽位 |
| **客户端 CharData `aEquipped[]`** | **对 −56…−62 不是原生** | v083 apply-max≤55；这些座靠 **sidecar 影子** 接登录包 |
| **客户端 Addon UI** | 显示层 | 应只是「看」服务端/CharData，不应另造一套库存 |

所以：不是「没记库」，而是 **客户端没有把扩展座当成和主栏一样的原生数组**，登录/显示用影子 → 再加 migrate swap → 看起来像「装备变了」。

---

## 目标架构（与原栏一致）

```
背包拖放 / 双击 / 卸下
        │
        ▼
SendChangeSlotPosition  (唯一穿脱通道)
        │
        ▼
服务端 InventoryManipulator.equip/unequip/move
        │
        ▼
DB position 真实落库
        │
        ▼
登录 getCharInfo → 客户端 **同一套** GetItem/SetItem/ApplyEquip
        │
        ▼
主栏 or Addon 只负责画自己座位的 GetItem(−bp)
```

**硬规则（与主栏相同）：**

1. 禁止乐观本地 `SetItem` 当真相。  
2. 禁止卸下前 clear-only。  
3. 禁止登录时 cash↔普通 **互换座位**（已禁；仅空目标纠错）。  
4. 禁止并行 busy 状态机替代 `enableActions`。  
5. Addon 只做：**座位 HitTest + 把 dst 指到 −bp**，其余 100% 原生包。

---

## 两条实现路线（按 095）

### 路线 A — 真·原生（最终答案，对齐 095）

- 扩 083 `CharacterData`（095≈1965）使 **Get/Set/Apply 原生容纳 −52…−62**。  
- HitTest/Draw 上限与 CD **同步**加长；Addon 仍可做独立窗（095 Mechanic 哲学）。  
- **删 sidecar 库存角色**；登录不再 jg-allow 旁路。  
- 风险：进图 / `E_POINTER`；必须单独 CD 工程 + 进图矩阵，不能「改个 imm=59」。

### 路线 B — 过渡期「行为等同原生」（当前可执行）

在 A 完成前：

- 服务端继续真实 DB（已具备）。  
- 客户端 sidecar **只做 ApplyEquip/GetItem 镜子**，不发起库存决策。  
- 穿脱 **仅** `SendChange`；登录 **仅** decode 写入镜子。  
- **零 migrate swap**；零乐观 UI。  
- 属性：盲区继续 `STAT_CHANGED`（Occ OFF），与「记录」无关。

B 的验收标准：**重登座位与物品 ID 与下线前逐格一致**（已用 `ADDON_LOGIN_KEEP_SLOTS` 去掉 swap；仍需压测）。

---

## 分阶段（同意后按序做）

| Phase | 内容 | 验收 |
|------:|------|------|
| B0 | 冻结：不再加 migrate/SendBusy/乐观 Set | 文档+代码门禁 |
| B1 | 穿脱拖双击全部 packet-only；Addon=路由 | 与主栏手感一致 |
| B2 | 登录：getCharInfo 前 caves已装好；重登逐格一致 | 你换好的图腾/纹章/辅助不重排 |
| A1 | CD 扩容设计（IDA 083 vs 095） | [`ADDON_NATIVE_CD_GROW.md`](./ADDON_NATIVE_CD_GROW.md) **done 2026-08-03** |
| A2 | 原生 −52…−62，拆除 sidecar 库存 | 代码+EXE 副本 staged；**T1 绿**；**T2–T3 未做** — **仍 blocked**（禁止推 Client_1） |

---

## 和「盾 / 辅助」

产品已要求真正分槽：**109→−10，134/135→−62**。这也符合「类型不同就不同槽」，而不是官服同 Si 再 UI 假装分开。
