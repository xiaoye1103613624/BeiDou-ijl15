# Client_1 — 原生扩展装备修改计划（2026-08-07）

> **Status:** 计划文档（本回合 **不换** Gr2D / EXE / 长大 DLL）  
> **Policy update:** **直接在 Client_1 改** Route A；`BeiDou-Client_CD64_TEST` **仅作参考**（曾改过、小问题可接受），**不再**要求「先 stage CD64_TEST → 绿 → promote」。  
> **Live:** EXE `ACF10F63` · ijl15 `EF4A246A` · Gr2D_DX8 proxy `27BB86C0`  
> **Intent:** 扩展装备走 **原生扩展（Route A / CD64）**，不走 Addon `IWzGr2DLayer` overlay  
> **Hard:** Addon overlay **保持关** · EXE **IDA-first** · **无 Data restore** · stub `@AB30C` **完整不动**

---

## 0. 阻塞（先于一切 CD 扩容验证）

| 阻塞 | 说明 | 本回合 |
|------|------|--------|
| **开装备栏 CreateLayer 闪退（DX9 proxy）** | `CreateLayer` / `dword_BF14EC` → `*5EB0`；**无法验证** −52/−53 显示、CD 扩座穿戴 | **需用户做 Gr2D A/B**（见 §3 P0） |
| 本回合动作 | — | **禁止**换 Gr2D / 禁换 EXE / 禁部署长大 DLL；只更新计划 |

**请用户：** 确认后跑 Gr2D A/B（冷启进图绿 + 开 E 不闪退），再谈 Client_1 上 Route A 补丁。

---

## 1. 「原生扩展」在本仓库指什么

| 含义 | 路径 / 符号 | 说明 |
|------|-------------|------|
| **Route A（真原生）** | `ADDON_NATIVE_CD_GROW.md` · `Shoulder_UseNativeCd64Slots` · `NativeCd64Inventory()` | EXE `aEquipped` 52→64（`push 0x700` @`778F02`），Get/Set bound −51→−63，apply-max 62；−52…−62 存原生 ZRef |
| **PLUGIN_ONLY（旧冻结）** | `ADDON_REAL_EQUIP_EXPAND` §0 | 曾禁改原版 CD；**现用户改口：可在 Client_1 直改**，但仍须 bak + ExpectBytes + enter A/B |
| **shoulders ExtraRing** | `kBindExtraRingInventory` / −52/−53 | 主装备栏 HT 扩座（非 Addon Layer） |
| **NATIVE_CD64_ADDON_UI_REUSE** | `kNativeUiMarker` | CD64 时若仍要 dock：**只画/HitTest**，库存走原生 |

服务端座表：`ExtendedEquipRegistry.java`（与 `ExtEquip::kBodyParts` 对齐）。

---

## 2. Avoid-list — 影子槽 → 原生（禁止再犯）

历史把 −52…−62 当「影子/sidecar」再硬切原生时踩过的坑。**下列模式一律禁止复用：**

| # | 禁止 | 后果 / 教训 |
|---|------|-------------|
| S1 | **Vanilla CD（`push 0x640`）上抬 apply-max / 原生写 −54…−62** | 踩现金脸/眼、TSec、进图 AV（avoid #45/#50/#51 族） |
| S2 | **CD64 EXE 已长，但插件仍 Get/Set→shadow**（`kNativeCd64ExtendedSlots=false` / Detect 漏检） | 双真相：登录包进影子、UI/战斗读原生 →「装备变了 / 空了」 |
| S3 | **半截 sidecar**：只迁 56–61，BP54/55 仍写 52 格 `aEquipped` | 同 #51：现金脸/眼鬼影 |
| S4 | **影子→原生时仍跑 migrate swap / 乐观 SetItem / PreferSend mega** | 座位漂移、进图红、经典 tip 坏 |
| S5 | **漏补 login clear / cash lea `0x28B→0x2EB` / apply-max 62**（只改 ctor+bound） | 旧 CD64 `E_POINTER` 进图红 |
| S6 | **FULL imm×318 盲灌** | boot-red `PostGD/9F7034` — **永不 dump 再上** |
| S7 | **`DetectCd64` 假阴**（第二处堆仍 `0x640` 未扫） | 插件继续影子路径 |
| S8 | **GetItem 现金别名 −156 与 −56 同 ZRef** | 点装悬停 AV（avoid #34） |
| S9 | **为「修影子」NOP 原生 tip / 砸 stub `@AB30C` / ForceDrawLoop / DllMain 早 Park+GetSet** | 进图红家族；已绿功能不可当试验场 |
| S10 | **无 IDA ExpectBytes 写洞 / 十进制栈偏移当 hex / 无 bak 盖 live** | SOP 硬禁；出红立刻回滚 |

**正向原则：** 扩座真相只有一条——CD64 后 **−52…−62 = 原生 ZRef**；sidecar 库存角色 **整段拆除**（不是半关）；Addon overlay **保持关**。

---

## 3. Client_1-direct — Route A 流程（本机 live）

> 目标树：`E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1`  
> CD64_TEST **可对照脚本/SHA**，**不是**必经 stage 闸门。

### P0 — Gr2D A/B（**用户执行**；未绿则 **不**开 CD 补丁）

1. **A：** 回滚 `Gr2D_DX8.dll.bak_before_DX9proxy_*`（native DX8）→ 旁置 `Gr2D_DX9.dll` → 冷启；主机仍 E_FAIL 则记现象并回 proxy。  
2. **B：** 保留 proxy，IDA 查 `dword_BF14EC` / CreateLayer 坏 COM（**禁止**再 NOP Addon / tip / stub）。  
3. 验收：冷启进图绿 + **开 E 不闪退** + 经典装 tip/卸正常。

### P1 — 插件门闩（扩 EXE 前保持）

- Addon Layer / OnTick / Park / UiHooks / GetSet / Apply caves **保持 false**。  
- 保留 ExtraRing / 肩坠等主栏路径；**不开** overlay。  
- `kNativeCd64ExtendedSlots=true`：仅 `DetectCd64Exe()` 真时切原生库存。  
- 禁止 PreferSend mega、ForceDrawLoop、tip-nop、砸 `@AB30C`、胖 DLL>1447424 盲盖。

### P2 — Client_1 上 EXE Route A（IDA → bak → ExpectBytes → 补丁 → enter A/B）

1. **IDA**：`server_health`；对照 `ADDON_NATIVE_CD_GROW` §3 全表（ctor×4、bound×5、slab `778F02`、stack、login clear×2、cash lea、apply-max×2；**禁止** FULL×318）。  
2. **Bak：** `BeiDou.exe.bak_<stamp>` + SHA；必要时 `ijl15.dll.bak_<stamp>`。  
3. **ExpectBytes** 逐点核对后再写；优先 phase2 MINIMAL / 已证位点集，**勿**再灌 imm×318。  
4. **配对 DLL：** `DetectCd64` → sidecar inventory **整段 OFF**；Addon 若要 UI 仅 paint/HitTest。  
5. **Enter A/B（仍强制，在 Client_1）：** select→enter 绿 → 经典+点装 → 重登 → 再验 −54…−62 穿卸。出红：**立刻回 bak**，记 avoid。  
6. **不**要求先在 CD64_TEST promote；TEST 仅参考。

### P3 — 服务端

- `ExtendedEquipRegistry` / omit-aux：CD64 持有座稳定后再全开 wire；**P0 不必改服务端**。

---

## 4. CD64_TEST — 仅「勿再引入」（非阻塞 stage）

曾在 TEST 上改过、小问题不大、**不再挡 Client_1 直改**。对照时 **不要重新引入**：

| 参考项 | 勿再引入 |
|--------|----------|
| FULL imm×318 / 盲 CTOR_FIELDS 大灌 | boot-red |
| login clear / `0x28B` / apply-max 漏补 | `E_POINTER` |
| CD64 EXE + 插件仍影子 Get/Set | 双真相 |
| TOTEM2 客户端现金 −156 硬路径 | 进图红（服务端 alias 已 ENTERSAFE） |
| OccShadow ON / draw-skip punch / BP33 cave | 历史红 |
| 半截 sidecar 54–55 留原生、56+ 影子 | 现金鬼影 |

TEST 上 T2–T3 / SAFEIMM 未完成 **不**再作为「禁止动 Client_1」的闸门；Client_1 自己的 bak + ExpectBytes + enter A/B 即闸门。

---

## 5. 与现状的差距

| 层 | 现状 | 关系 |
|----|------|------|
| 开装备栏 | DX9 proxy 下 CreateLayer 闪退 | **P0 阻塞**；先于 CD 验证 |
| EXE | `ACF10F63` · `push 0x640` | `DetectCd64==false`；Route A 未开 |
| ijl15 | `EF4A246A` Addon 全关 | 符合「关 overlay」；扩栏后仍勿开 Layer |
| −54…−62 | 真原生需 CD64 EXE | 直改 Client_1，勿 vanilla 硬写 |

---

## 6. 本回合已做 / 未做

| 做了 | 未做（等用户） |
|------|----------------|
| 计划改为 **Client_1-direct** + 影子→原生 avoid-list | **Gr2D A/B**（请用户跑） |
| 明确 CD64_TEST 非阻塞 stage | 换 Gr2D / EXE CD grow / 重编部署 ijl15 |
| 记录 bak：`Gr2D_DX8.dll.bak_before_DX9proxy_20260807_110843` | — |

---

## 7. 一页清单（给执行者）

1. 用户完成 Gr2D A/B → 开 E 绿。  
2. IDA 对照 Route A 位点 → Client_1 `BeiDou.exe` bak + ExpectBytes 补丁（禁×318）。  
3. 配对 DLL：DetectCd64 → 原生库存，**无**影子双写；Addon overlay **关**。  
4. Client_1 select→enter A/B；红则回 bak。  
5. 勿重复 §2 avoid-list；勿动 `@AB30C`；勿 Data restore。
