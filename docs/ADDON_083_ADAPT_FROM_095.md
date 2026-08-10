# 083 扩展装备 — 对照 095 原生模型的适配方案

> **Date:** 2026-08-02  
> **Status:** **产品确认 — 必须与原装备栏逻辑一致（真实记录）**  
> **Trigger:** 用户 2026-08-03：扩展装备要真实记录、逻辑与主栏一致，禁止再靠迁移/补丁弥补不一致。详见 [`ADDON_NATIVE_PARITY.md`](./ADDON_NATIVE_PARITY.md)。  
> **Earlier:** `ADDON_SENDBUSY_FIX` 开机闪退后，停止堆 SendBusy/sidecar 补丁，改走 095 适配。  
> **Parent:** [`ADDON_EXTEND_EQUIP_ARCHITECTURE.md`](./ADDON_EXTEND_EQUIP_ARCHITECTURE.md)  
> **095 IDA:** [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md)  
> **进图禁区:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)  
> **属性基线:** [`ADDON_STATS_BASELINE_RECOVERY.md`](./ADDON_STATS_BASELINE_RECOVERY.md)  
> **口袋/副手 UI:** [`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md)

---

## 0. Live 现状（本设计起点）

| 项 | 值 |
|----|-----|
| Client_1 `ijl15.dll` SHA256 | `6D612F01AA63D217…` |
| Size | `1447424` |
| 含义 | **SENDBUSY 之前** 的 live（= `bak_ADDON_SENDBUSY_FIX` 所保，内容属 ROW3_UNEQUIP_UI 系） |
| `VERSION` 文件 | 可能仍写着旧 stamp（如 `ADDON_SI_WTYPE_CAT_FIX2`）— **以 SHA 为准** |
| 崩溃勿再部署 | `ADDON_SENDBUSY_FIX` `3BFB289D…` / `888C5530…` |
| IDA 默认 MCP `:13338` | **v083** `BeiDou.exe` |
| 095 调研口 `:13339` | `GMS_v95.0_U_DEVM.exe`（需单独开 IDA） |
| 本阶段 | **不** 向 Client_1 部署新 DLL；**不** 要求用户为回滚再测进图 |

---

## 1. 当前 083 痛点 — 自定义钩子造成了什么

> 产品目标（Addon 栏、−54…−61、护肩 −20、第二项链 −51、口袋/副手第三行）**保留**；下面列的是 **错误实现路径** 及其症状。

| 钩子 / 模式 | 意图 | 实际伤害 | 证据 / stamp |
|-------------|------|----------|-------------|
| **乐观穿戴** sidecar `SetItem`（与背包同指针） | 立刻显示 Addon 图标 | `INVENTORY_OPERATION` mode-2 是 **swap**：背包幽灵 + Addon 双份 | `ADDON_ROW3_WEAR_UNEQUIP` |
| **卸下 clear-only**（发包前清 sidecar） | 本地先空座 | 双方变 null → 背包不进货；再卸卡住 / 满格感 | `ADDON_ROW3_FIXES` |
| **全局 SendBusy unstick**（OnTick / DllMain 周边） | 修「卸下后全栏冻」 | 误清 busy → 状态机乱；**开机** 在无 CharData 时碰 `ctx[2089]` / `GetUpdateTime` → `0xC0000409` 闪退 | `SENDBUSY_FIX` |
| **DllMain 内 Park + Get/Set + Dbg fopen**（`InstallLoginPersistEarly` 过重） | 登录尽早装 persist | Loader lock 下文件 I/O + 过早写洞 → boot 死在 `ShoulderSlots OK` 之后 | avoid #23 |
| **登录 allow / decode 装太晚**（等 CField） | 安全 | 重登 Addon 位被 strip / 不进 sidecar | persist 类反馈 |
| **主栏 Draw skip 打洞**（`48→32`）/ BP33 cave | 让口袋进主栏绘制 | 选角→进图 `0xC0000409`（ucrtbase GS）；**095 同样跳过 BP33** | WIREOFF / P12* / FULL_FIX |
| **主栏 red9/10 Wire**（STATS 时代） | 口袋/副手坐标 | 与 skip 带冲突；已弃 → Addon row3 | `ADDON_ROW3_POCKET_SI` |
| **raise apply-max → 59** / 盲扩 Get-Set −59 | 抄 095 原生槽 | TSec / ZRef 踩踏；083 CD 未长大 | avoid #2/#22 |
| **OccShadow / Occ 扫盲区 ZRef** | 客户端自己叠属性 | AV `@535351` / `@77F99E` | STATS 路线已禁 |
| **ExtraRing 影子 + −152 现金 remap** | 六戒 | 进图 GS | FULL_FIX |
| **并行穿卸状态机**（DLL 自管 busy / 乐观 Set） | 「更快 UI」 | 与原版 `SendChangeSlotPosition` 双轨 → 冻栏、幻影、崩溃 | SENDBUSY 系列 |

**一句话：** 083 痛的不是「没 sidecar」，而是 **sidecar 越权扮演了库存引擎**（乐观 SetItem、busy 代管、DllMain 重活），而 095 是靠 **长大的 CharacterData + 原生 Get/Set −59 + 独立 UI 窗** 消化扩展座。

---

## 2. 095 模型（IDA + 已有笔记摘要）

来源：[`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md)（2026-08-02 live Hex-Rays）。

### 2.1 CharacterData / GetItem / SetItem

| 事实 | 095 |
|------|-----|
| `sizeof(CharacterData)` | **1965** |
| Equip `GetItem` / `SetItem` | 允许 **`nPos ∈ [-59,-1]`**（另有 cash / Dragon 1000+ / Mechanic 1100+） |
| 含义 | −52…−59 可以是 **原生 aEquipped ZRef**，不是 DLL 影子 |

083 对照（已有 CD 扩容调研）：vanilla 约 **52 槽 / bound −51**；apply-max **≤55**；曾做 CD64 副本进图 `E_POINTER` 后回退 — **正式端禁止盲升 apply-max**。详见 `docs/CharacterData扩容-进度-20260728.md`。

### 2.2 HitTest / Draw

| API | 095 | 启示 |
|-----|-----|------|
| `CUIEquip::GetBodyPartFromPoint` | max = expanded? **59 : 58** | 主栏座位随 CD **一起**加长 |
| `CUIEquip::Draw` | 同 58/59；skip = **BP14 + BP21–48** | **BP33 在 095 也被跳过** — 无原生口袋绘制 |
| Mechanic / Dragon | **独立窗口**（1100+ / 1000+） | 不塞进 `g_aEquipSlotPos[50]` |

### 2.3 `get_bodypart_from_item` / Si / 护肩等

| 类 | 095 BP | BeiDou 产品（勿盲对齐数字） |
|----|-------:|------------------------------|
| 109 / 119 / 134 | **10** | 盾 −10；纹章 **−59**；134/135 **−10 互斥** |
| 135 | **11 武器** | **保持** BeiDou Si−10，不抄 135→武器 |
| 115 护肩 | **51** | 已交付 **−20 / BP20** |
| 112 第二项链 | **59** | 已交付 **−51**；−59 给纹章 |
| 116 口袋 | **无** | Addon row3 −33 |
| 环 | **仅 4** | 六戒为自定义，排最后 |

### 2.4 095 哲学（要抄的是这个）

1. **扩展座 = 真 bodypart + CD 能放下 + Draw/HitTest 上限同步长大**（在 skip 带外才能主栏画）。  
2. **职业套件 / 额外 UI = 独立窗**，不砸主表。  
3. **穿卸走原生 inventory op**；客户端不维护第二套「已装备」真相。  
4. skip 带仍然 skip — 口袋类 **永远** 不靠打洞 skip。

---

## 3. 083 适配策略（推荐）

### 3.1 总原则

| 优先 | 策略 |
|------|------|
| **A（推荐方向）** | 能进 **原生 apply 范围 / 真 BP（且在 skip 外）** 的，按 095 方式「长大座位」；不能的用 **独立 Addon 窗**（已有 dock） |
| **B（过渡）** | −54…−61 / 口袋 −33 继续 **shadow + sidecar UI**，但 **只做显示与路由**，库存真相 **100% 服务端封包** |
| **C（禁止）** | 再堆一层 SendBusy 全局解冻、乐观 SetItem、DllMain 重初始化、draw-skip 打洞 |

**产品承诺（保留，只改实现）：**

- Addon dock：徽章/图腾×4/安卓/心脏/纹章（−54…−61）  
- 护肩 −20、第二项链 −51（主栏已交付绑定）  
- 口袋/副手 = **Addon 第三行**（095 无口袋；独立窗哲学一致）  
- Si：**109 → −10 盾**；**134|135 → −62 Addon 辅助**（真正分槽，可同穿）

### 3.2 穿 / 卸 / 拖 — 硬规则

| 规则 | 说明 |
|------|------|
| **只走原生** | 最终必须是 `SendChangeSlotPosition` / 服务端 `InventoryManipulator`；DLL 只 remap dst / 过滤座位 |
| **禁止乐观 SetItem** | 穿戴不得本地填 sidecar「预览真相」 |
| **禁止 clear-only 卸下** | 卸下不得发包前先清；等 mode-2 / modifyInventory |
| **SendBusy** | 仅在 **已有 CharData + 装备栏交互中** 且 **enableActions 漏掉** 时做 **窄** 解冻；**禁止** DllMain / 选角前 / 全局 OnTick 乱清 |
| **Login persist** | decode/allow 安装须在 **getCharInfo 解码路径之前**，且 **只装登录相关 cave**（jg-allow / lea redirect）；Park/Get-Set/Dbg **推迟**到安全阶段（见 avoid #23） |

### 3.3 登录持久化 — 「095 方式」在 083 上的落地

095：CD 已含 −59 → 登录 decode **自然**写满。  
083 目标等价物：

1. **短期（P2）：** 登录包 allow 范围与 **服务端 encode 的 −54…−61 / −33** **对齐**；sidecar lea 在 **CharData 分配后、字段 apply 前** 接住 BP56–61；**不要** 等进图后 CField 再补。  
2. **中长期（P3，高风险独立项）：** 研究 CD / apply-max 增长（对照 095 1965 与既有 `cd_expand`），使更多座变 **NativeApplyMax** — **单独工程**，不与穿卸修混做。

### 3.4 属性

- Occ 不安全的座位（Addon / ExtraRing 影子）：**继续服务端 `STAT_CHANGED` / `isClientBlindEquipSlot`**。  
- 客户端 Occ OFF，直到对应座 Occ 路径进图绿。

### 3.5 与「三层存储」的关系（不推翻架构，收紧纪律）

仍用架构文档的 A/B/C 三层，但强调：

```text
A Native (≤55)     ← 095 主航道；护肩/项链2 已在此
B Shadow Get/Set   ← 仅指针代理；真相仍来自封包
C Sidecar ZRef     ← 仅 UI + login absorb；禁止成为第二库存引擎
```

目标演进：**能从 B/C 升到 A 的座，才算「095 化」**；升不了的留在 Addon 窗（095 的 Mechanic/Dragon 哲学）。

---

## 4. 分阶段计划（带进图安全门）

| 阶段 | 目标 | 进图风险 | 退出标准 | 部署？ |
|------|------|----------|----------|--------|
| **P0** | Live 只保留 **已证明可启动** 的 bak；文档/VERSION 与 SHA 对齐 | 无（恢复已做过） | Client_1 = `6D612F01…`；崩溃 DLL aside；**不要求用户再测进图** | 已完成态核对 |
| **P1** | Addon（含 row3）穿卸 = **100% 封包原生**；去掉乐观 Set / clear-only；**无** 全局 SendBusy 滥用 | 低（逻辑删减为主） | 穿：背包格消失且仅 Addon 一份；卸：背包一份、无冻栏；静态脚本绿；**staged 不自动部署** | 否，直至用户主动要测新 stamp |
| **P2** | 登录 persist = **早 allow + 匹配封包范围**（095 语义）；非晚 CField 补丁；DllMain **仅** 轻量 login cave | 中 | 重登 −54…−61/−33 不丢；boot log 过 `EquipAddonLoginPersist`；无 SENDBUSY 式闪退 | 否，先 golden |
| **P3** | **可选** CD / apply-max 增长调研（083 IDA vs 095）；对照 `cd_expand` | **高** | 仅测试目录 EXE；Client_1 vanilla EXE 不动；报告消费者清单 | **永不**与 P1/P2 同包 |
| **P4** | ExtraRing −52/−53 **最后** bisect（影子 ON、−152 OFF、Occ OFF） | **高** | 单角色 −52/−53 进图绿后再谈 UI | 独立 stamp |

### 各阶段门禁（必须同时满足）

- SHA ≠ 任何 avoid-list「Enter-unsafe / CRASHED」前缀  
- `client_boot.log` 能越过 ShoulderSlots **且** 出现预期 persist OK（P2+）  
- 无 draw-skip / BP33 cave / apply-max>55 / OccShadow  
- 新 stamp **才** 请用户 select→enter；**回滚 bak 不请**

---

## 5. 明确非目标 / 禁止项

完整列表见 [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)。本适配方案 **额外钉死**：

| # | 禁止 | 原因 |
|---|------|------|
| F1 | 再部署 `ADDON_SENDBUSY_FIX` / 任何 boot 死于 ShoulderSlots 后的 DLL | `0xC0000409` 闪退 |
| F2 | DllMain / 选角前全局 SendBusy unstick / 裸调 `GetUpdateTime` | 无 CharData |
| F3 | 乐观 sidecar SetItem / 卸下 clear-only | 背包幽灵、卸不下 |
| F4 | 打洞 draw-skip 或 BP33 cave | 进图 GS；095 也无口袋绘制 |
| F5 | 盲升 apply-max / Get-Set −59（无 CD 长大） | TSec / E_POINTER 历史 |
| F6 | 把 095 的 115→51、112→59、119→10、135→武器 套到 BeiDou 产品号 | 双编号混乱 |
| F7 | 并行 DLL 库存状态机 | 与原生 ItemMove 双轨 |
| F8 | ExtraRing 与 P1/P2 同包上线 | 历史最高风险座 |
| F9 | 用 BOOTSAFE「再打补丁」代替本方案纪律 | 仍是 SendBusy 中心思路 |

---

## 6. 具体下一步（编码，不部署）

**只做工程侧，不复制到 Client_1：**

1. **冻结真相源**  
   - 记录 live SHA `6D612F01…` 为 P0 锚点；必要时把 `VERSION` 改成与 SHA 一致的 `RESTORED_…`（文档/脚本，非必须进游戏）。  
   - 崩溃体保持 aside：`CRASHED_SENDBUSY_FIX_3BFB289D_*`。

2. **P1 源码手术（ staged / golden only）**  
   - 在 `equipaddon.cpp`（及 wear 路径）审计：删除/禁用一切 **乐观 SetItem**、**发包前 clear-only**。  
   - **拆除或严格门控** SENDBUSY 全局 watchdog：允许的仅是「Equip UI 打开 ∧ CharData 有效 ∧ 刚发过 Addon ItemMove ∧ enableActions 超时」的窄路径；**删除** boot OnTick / DllMain 解冻。  
   - 构建脚本：`_build_*.bat` **禁止** 调 `_deploy_ijl15.ps1`；产物进 `golden\`。  
   - 跑现有 `_static_enter_risk_*.ps1`；字符串指纹不得出现 SENDBUSY boot 危险路径特征。

3. **对照表（文档已完成，编码时贴注释）**  
   - 095：CD1965 / −59 / Draw58–59 / 独立 Mechanic·Dragon 窗 / skip 仍跳 BP33。  
   - 083：Addon 窗 = 独立窗；row3 = 口袋+副手；主栏真 BP 仅用 skip 外已交付座。

4. **P2 设计备忘（下一编码波）**  
   - 拆 `InstallLoginPersistEarly`：DllMain = **仅** jg-allow / 必要 jmp；Park + Get/Set + 文件日志 → `LazyCompat` / CharData ready。  
   - 用 IDA `:13338` 标出 083 getCharInfo / equip decode 调用序，保证 allow **早于** apply。

5. **P3 不启动**，除非 P1+P2 进图绿且单独开「CD 扩容」任务；复用 `tools/cd_expand`，**禁止**覆盖 Client_1 `BeiDou.exe`。

---

## 7. 决策摘要（给评审）

| 问题 | 决定 |
|------|------|
| 为何对照 095？ | 095 证明扩展装备靠 **原生 CD+槽位长大 + 独立 UI**，不是 busy/乐观补丁 |
| 083 立刻抄 −59？ | **否** — 先封包原生穿卸 + 安全早 login；CD 长大单独 P3 |
| Addon / 口袋？ | **保留** 独立窗 + row3（095 无口袋，哲学一致） |
| 护肩/项链2？ | **保留** 现产品号（−20/−51），不改成 095 的 51/59 |
| SendBusy？ | **窄场景急救** 最多；**禁止** 全局 / boot |
| 下一步？ | P1 源码收敛 → golden；**不部署、不催进图** |

---

## 8. 文档链接图

```text
ADDON_EXTEND_EQUIP_ARCHITECTURE.md  ← 总架构
        │
        ├── ADDON_083_ADAPT_FROM_095.md     ← 本文件（回滚后主路线）
        ├── ADDON_095_IDA_BODY_PART_NOTES.md
        ├── ADDON_ENTER_CRASH_AVOID.md
        ├── ADDON_STATS_BASELINE_RECOVERY.md
        ├── ADDON_ROW3_POCKET_SI.md
        └── ADDON_ROW3_WEAR_UNEQUIP.md      ← P1 已验证方向（packet-only）
```

---

*本文件不授权任何 Client_1 部署。*
