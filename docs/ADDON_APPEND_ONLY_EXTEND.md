# Append-only 扩展装备存储（2026-08-04）

> **Mandate:** 「不要改原版地址，自定义的可以改」  
> **Status (2026-08-06):** CD64_TEST — ijl15 **`15602DFF…`** `ADDON_APPEND_TSEC_GUARD` / 1447424；EXE **`3BC0E37C…`** (TSec null-guard)；bak FA/`ACF10F63`  
> Client_1 still **`60A3E54F`** unguarded APPEND — **do not promote** TEST pair until enter A/B green (avoid #57/#58).  
> vanilla EXE 保持 52 槽；−54…−62 仅在 **ijl15 插件 arena**  
> **Parents:** [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md) · [`ADDON_STOP_CASCADE_20260804.md`](./ADDON_STOP_CASCADE_20260804.md) · [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)

---

## 0. 一句话

**sidecar = 只存数据；逻辑 = 原版装备 parity。**  
不挪 cash `+0x287` / skillcool / bag 等原版 CD 字段；插件 arena 承载 BP54–62；Get/Set/Apply/LoginAllow **只对扩展座** 重定向；穿卸走 SendChange + 服务端 **mode-2**（与 −1…−11 同形），禁止另搞一套 ghost-sync-only / 空 invent / clear-only 捷径。

---

## 1. 为何不能「假装加长」原生 aEquipped

IDA 083（`:13338` / stock `BeiDou.exe`，2026-08-04 复核）：

| Site | Bytes | Meaning |
|------|-------|---------|
| `@0x46F534` | `6A 34` | ctor `push 52` |
| `@0x42834E` | `83 F8 CD` | GetItem bound `−51` |
| `@0x4E5D55` | `lea …+0E7h` | normal ZRef base |
| `@0x4E5E18` | `lea …+287h` | cash base = normal + 52×8 |
| `@0x778F02` | `push 0x640` | ZAlloc slab |
| `@0x4E5D0C` | `cmp esi,33h` / `jg` | apply-max **51** |

**代数：** 在 52 槽表写 BP54 ≡ cash BP2（−102 脸饰）；BP55 ≡ cash BP3（−103 眼饰）。  
半 sidecar（只 56–61）必出徽章↔脸、图腾1↔眼。

**禁止：** SAFEIMM / CTOR_FIELDS / skillmap / cash lea reloc / PreferSend mega 喷改 / VS Clean+Rebuild 胖 DLL 盖 Client_1 / 中段挪 CD 字段涨体积。

---

## 2. 存储拓扑（锁定）

```text
CharacterData (vanilla, offsets UNCHANGED)
├── aEquipped[52]     @ +0xE7     ← −1…−51 等原版
├── aEquipped2[52]    @ +0x287    ← 现金（脸/眼等）— 永不被 −54/−55 写入
├── skillcool / bag / …           ← 原偏移
└── (no mid-CD insert)

ijl15 plugin arena (append-only / sidecar)
└── ZRef[bp] for bp ∈ {54…62}     ← 徽章/图腾×4/纹章/安卓/心脏/辅武
    (Set/Apply 可收 −154…−162 → 同座；GetItem tip/invent 线仍 normal −bp)
```

| 层 | 职责 |
|----|------|
| GetItem / SetItem hook | BP54–62 → arena；永不 `aEquipped[54+]` |
| ApplyEquip lea cave | login/decode `esi=54…62` → `SidecarZRefForBp` |
| LoginAllow jg-cave | 允许 1…53 原生/戒影 + 54…62 arena（**不**抬 apply-max imm） |
| Login clear | `push 34h` 前清 arena（换角） |
| Wear | **PacketSendOnly**（SendChange）；ack **mode-2** → SetItem→arena |
| PreferSend | **仅**扩展座 54–62 走发包；禁止 wear_fn 写原生 |
| Addon UI | paint / HitTest / tip 读 GetItem(arena)；占用早退 |

### 座位表

| Seat | Prefix | Slot | Store |
|------|--------|-----:|-------|
| Badge | 118 | −54 | arena |
| Totem×4 | 120 | −55…−58 | arena |
| Emblem | 119 | −59 | arena |
| Android | 166 | −60 | arena |
| Heart | 167 | −61 | arena |
| Aux | 134/135 | −62 | arena |
| Shield | 109 | −10 | **native only** |
| Pocket | 116 | −33 | native + Addon row3 UI |

---

## 3. 原版 parity 规则（服务端 + 客户端）

| 动作 | 必须像原版 | 禁止（见 [`ADDON_FORBIDDEN_OPS.md`](./ADDON_FORBIDDEN_OPS.md)） |
|------|------------|------|
| 穿戴 | mode-2 bag→−bp；落库 **−54…−62**；已占用则 **replace** | forceUpdate；空 invent；occupy 硬拒死锁 |
| 卸下 | mode-2 −bp→bag；wire oldPos=−bp | clear-only；装备 mode-3 ghost（addMovement=2→tip 卡死） |
| 幽灵 | enableActions only；客户端本地 ClearSidecar | dual/ghost mode-3 |
| 显示 | mode-2 SetItem→arena | 穿后 forceUpdate「补显示」 |

Live GetItem hook **只认 normal −bp**。现金扩展装勿落 −154。

---

## 4. 与历史失败戳记的区别

| Stamp | 问题 | 本方案 |
|-------|------|--------|
| FA `FA26021C` PEER_ASLR_V2 | sidecar **56–61 only**；54/55→脸/眼 | 扩到 **54–62** |
| `021C016A` ZEROGROWTH | Apply/Get 扩了，PreferSend 仍 56–61 → wear_fn 鬼影 | PreferSend/PacketSend **同步** 54–62 |
| `44251097` PREFER | Prefer + **大量** aux `0A→3E` 喷改 → 栏空白 | PreferSend **单点** + 精简 aux |
| `76C7D7FC` TOTEM1 | 叠补丁破坏图腾 | 不单独 TOTEM1 hack |
| fat `1B0E80AA` CD64_LAYOUT 1543168 | avoid #55 进图红 | **不上** Client_1 |
| crash 20260804 | 空 invent / 双 mode-3 / forceUpdate | 见 §3 禁止列 |

---

## 5. 交付物

| 项 | 路径 |
|----|------|
| 本文 | `docs/ADDON_APPEND_ONLY_EXTEND.md` |
| 占用清单 | [`ADDON_OCCUPY_GUARD_CHECKLIST.md`](./ADDON_OCCUPY_GUARD_CHECKLIST.md) |
| 源码意图 | `ezorsia/equipaddon/` — `kSidecarBpMin=54`…`62`；PacketSendOnly |
| 零增长补丁 | `tools/patch_append_only_ext54_62.ps1` |
| Golden | `golden/ijl15.ADDON_APPEND_ONLY_EXTEND_20260804.dll`（1447424） |
| 部署 | Client_1：bak FA → replace → VERSION；**勿**胖 DLL |
| 回滚 | `ijl15.dll.bak_before_APPEND_ONLY_*` → `FA26021C` |

### 验证门禁

1. size **1447424**；stub `@AB30C` = `53 E8`；JG tramp `@AB340` = `6A 01`
2. EXE 仍 `ACF10F63`；`@778F02` = `push 0x640`
3. Cold select→enter **A/B 绿**
4. 徽章/−54、图腾1/−55 **不**串脸/眼；辅武→−62；盾→−10
5. 穿+卸徽章/图腾1–4/辅武：**无闪退**、栏显示、占座拒二穿、无假死

---

## 6. 源码 vs 零增长

- **Live 路径：** FA 上 binary patch — 与已绿 EXECAVE/PEER 体积族兼容。  
- **Workspace 源码：** 54–62 + PacketSendOnly；MSBuild 胖 DLL（>1447424）**禁止**盖 Client_1。  
- **Arena 键：** 进程内 `g_sidecarZRef[bp]`；换角靠 LoginClear。
