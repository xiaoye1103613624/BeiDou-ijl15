# 真实装备栏扩展 — Real Equip Expand (2026-08-04)

> **Mandate:** 「做真实的装备栏扩展,保证所有功能复用且不互相影响。」  
> **HARD RULE 2026-08-04 ~20:54:** 「不要改原版地址，自定义的可以改」— **禁止**再改 vanilla `BeiDou.exe` CharacterData 字段偏移 / layout imm；扩栏只在 **ijl15（自定义插件）** 完成。  
> **Status:** Client_1 **FROZEN**（`ACF10F63`/`FA26021C`）；CD64_TEST **rolled back** to same vanilla-layout pair；EXE grow / SAFEIMM / CTOR_FIELDS **suspended**  
> **Parents:** [`ADDON_NATIVE_CD_GROW.md`](./ADDON_NATIVE_CD_GROW.md) · [`ADDON_ALL_OR_NOTHING_20260804.md`](./ADDON_ALL_OR_NOTHING_20260804.md) · [`ADDON_STOP_CASCADE_20260804.md`](./ADDON_STOP_CASCADE_20260804.md) · [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md) · [`CharacterData扩容-进度-20260728.md`](./CharacterData扩容-进度-20260728.md) · [`CD64_IMM_FILTER_DIAG_20260804.md`](./CD64_IMM_FILTER_DIAG_20260804.md)  
> **Test only:** `BeiDou-Client_CD64_TEST` — **never** overwrite `BeiDou-Client_1` until user OK  
> **Live freeze lock:** [`BeiDou-Client_1/CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md`](file:///E:/mxd_soft/2.客户端/083/beidou_client_xiaoye/BeiDou-Client_1/CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md)

---

## 0. Policy pivot / 策略转向（2026-08-04 ~20:54）

| 允许 | 禁止 |
|------|------|
| ijl15 caves / Detours / sidecar / 自定义 hook | 改原版 `BeiDou.exe` CD 字段 imm（skill-cool、cash lea、SAFEIMM、CTOR_FIELDS、APPLY_ABS…） |
| CD64_TEST = `ACF10F63` + `FA26021C`（与 Client_1 同布局） | 继续 mega EXE layout-sync 补丁环 |
| 玩法/协议变更 → **双端审计** | 把「技能冷却」当成新功能去改协议（见下） |

**Skill-cool 说明（答用户）：** 原生 `CharacterData` 自带 skill-cool ZMap；此前改的是 aEquipped 变长后 **字段偏移跟着挪**（layout sync），**不是**新做冷却功能，也未改服务端冷却协议。在硬性规则下，这类 **原版地址偏移补丁已停止**。

**双端规则：** 玩法/协议变更 → 必须审计服务端+客户端；纯客户端 UI/sidecar → 可只改插件。~~纯客户端 CD layout offset sync~~ — **现已禁止**（属改原版地址）。

**MINIMAL/CTOR 冲突：** 已落地的 `3C167A9B` / `6CBFFFE6` 等 = 改了原版 ctor/cash/tail；与硬性规则冲突。CD64_TEST 已回滚为 `ACF10F63`；bak 保留作考古，**禁止再部署** field-spam EXE。

## 0b. One-line verdict / 一句话（更新）

**现行扩栏 = 原版 EXE 保持 52 槽布局；−54…−62 由 ijl15 sidecar/hook 承载（自定义）；Addon paint/HitTest；穿卸走插件拦截的库存路径。**  
历史「CD64 原生长数组」路线（MINIMAL + field sync）**暂停**，除非用户明确撤回「不要改原版地址」。  
（旧文：sidecar 误写进原生 aEquipped[54+] 仍会踩现金脸/眼 — 插件必须 **拦截 Get/Set/Apply**，禁止落到原生 52 槽之后。）

---

## 1. Why sidecar 54–55 fails（为何半 sidecar 必崩）

### 中文

原版 083 `aEquipped` **只有 52 个 ZRef**（`push 34h`）。现金表 `aEquipped2` **紧挨**其后：

| 原生写 BP | 实际落到的现金座 | UI 症状 |
|-----------|------------------|---------|
| BP54 | cash BP2 = **−102 脸饰** | 徽章 ↔ 脸饰串台 |
| BP55 | cash BP3 = **−103 眼饰** | 图腾1 ↔ 眼饰串台 |
| BP62 | 越过现金表 → OOB / 多幽灵 | 辅武鬼影、多穿 |

因此「只扩 sidecar 54–62、不长 CD」无论 PreferSend 多完整，都是在 **52 槽数组后面假装还有格子** —— 不是库存，是别名踩踏。

### Technical English

Vanilla v083 layout (IDA `:13338` / stock `BeiDou.exe`):

| Site | Bytes (vanilla) | Meaning |
|------|-----------------|---------|
| `@0x46F534` ctor | `6A 34` | `push 52` — aEquipped count |
| `@0x42834E` GetItem | `83 F8 CD` | `cmp eax, -51` |
| `@0x47B0BF` SetItem | `83 F8 CD` | same bound |
| `@0x4E5D55` login lea | `… E7 00` | normal ZRef base `+0xE7` |
| `@0x4E5E18` cash lea | `… 87 02` | cash base `+0x287` = `0xE7 + 52×8` |
| `@0x778F02` ZAlloc | `68 40 06 00 00` | slab `0x640` |
| `@0x4E5D0C` apply-max | `83 FE 33` | `cmp esi, 51` |

**Algebra:** index 54 in a 52-slot table ≡ cash index 2 (−102). PreferSend / ZEROGROWTH / TOTEM1 on Client_1 cannot invent array bytes. Avoid #50–#54.

GMS095 proof (separate IDB): `sizeof(CharacterData)=1965`, GetItem allows **−59** native — pattern = **grow CD**, not shadow past 52. BeiDou seats −60…−62 (android/heart/aux) need **≥63** slots (product chooses **64**).

---

## 2. Product HARD（产品硬约束）

1. **Do NOT** break/modify classic Equip display tip/draw for seats already green.  
2. New seats: **type→slot mapping only**; wear/unequip/drag/dblclick/hover-tip/level/job/sort = **vanilla inventory path**.  
3. **All-or-nothing** storage tier: badge / totem×4 / emblem / android / heart / pocket / aux — same tier (native after CD64).  
4. Level + job via vanilla `AbleToWear` + server `canWearEquipment` — no Addon bypass.  
5. **109 → −10 only**; **134/135 → −62**; **zero shield coupling**.  
6. Seats must **not** alias cash (−102/−103): needs **real array growth**, not writing past 52 into cash band.  
7. **STOP_CASCADE:** freeze Client_1 mega sidecar; work on **CD64_TEST** / staged EXE first.

### Seat map (BeiDou product — not blind 095 numbers)

| Seat | Prefix | Slot | Storage after real expand |
|------|--------|-----:|---------------------------|
| Extra rings | 111 | −52/−53 | native `aEquipped` |
| Badge | 118 | −54 | native |
| Totem×4 | 120 | −55…−58 | native |
| Emblem | 119 | −59 | native |
| Android | 166 | −60 | native |
| Heart | 167 | −61 | native |
| Aux weapon | 134/135 | −62 | native |
| Shield | 109 | −10 | already native |
| Pocket | 116 | −33 | already native (UI = Addon row3) |
| Shoulder / pendant2 | 115/−20, 112/−51 | already native |

---

## 3. Target layout（目标结构 — 必须同步）

| Fact | Vanilla 083 | CD64 target | 095 ref |
|------|-------------|-------------|---------|
| `aEquipped` / `aEquipped2` count | **52** (`push 0x34`) | **64** (`push 0x40`) | layout ≠; CD **1965** |
| Get/Set bound | `cmp *, -51` (`CD`) | `cmp *, -63` (`C1`) | allow −59 |
| Normal ZRef base | `+0xE7` | unchanged | — |
| Cash `aEquipped2` base | `+0x287` | `+0x2E7` (+`0x60`) | — |
| Tail after both arrays | `≥ +0x427` | `+0xC0` | — |
| ZAlloc slab `@778F02` | `0x640` | `0x700` | — |
| Stack temp `@46EC8F` | `0x78C` | `0x84C` | — |
| Login apply-max | **51** (`33h`) | **62** (`3Eh`) | 58/59 UI |
| Login clear count | `push 34h` | `push 40h` | — |
| Cash clear lea | `[…+0x28B]` | `[…+0x2EB]` | — |

**Offset rule (52→64):**

```
off <  0x287          → unchanged
0x287 ≤ off < 0x427   → off + 0x60   (old aEquipped2)
off ≥  0x427          → off + 0xC0   (tail)
```

**Synced ceilings (mandatory):**

| Layer | Must match |
|-------|------------|
| `sizeof` / slab / ctor count | 64 ZRefs ×2 tables |
| GetItem / SetItem / walk bounds | allow **−62** (bound −63) |
| Login apply-max + clear | max **62**, clear **64** |
| Cash lea / field imm (SAFEIMM path) | cash base `+0x2E7` |
| HitTest / Draw **main Equip** | do **not** punch skip for pocket; Addon owns paint for −54…−62 icons |
| Plugin `DetectCd64Exe` | `*(u32*)(0x778F03)==0x700` → native Get/Set; sidecar inventory **OFF** |

Addon UI = **paint + HitTest remap only**. Inventory truth = `CharacterData::GetItem(−bp)` native.

---

## 4. Dual-mode / 双模策略

| EXE | Detect | Inventory | Addon |
|-----|--------|-----------|-------|
| Client_1 vanilla (`push 0x640`) | false | Legacy (frozen — no new sidecar mega) | UI only as today; **no** Prefer/ZEROGROWTH/TOTEM1 |
| CD64_TEST (`push 0x700`) | true + `kNativeCd64ExtendedSlots` | **Native ZRef −52…−62** | paint + packet dst remap |

**Forbidden on vanilla:** raise apply-max past 55; native Get/Set −52…−62 without real CD bytes.

---

## 5. Migration from FA / 3642FBDD live

| Item | Value (verified 2026-08-04 ~16:14) |
|------|-------------------------------------|
| Live `ijl15.dll` | **`3642FBDD…`** / 1447424 — `ADDON_AUX62_ONLY_20260804` |
| Live `BeiDou.exe` | **`ACF10F63…`** / 8503296 — **vanilla CD** (not CD64) |
| Rollback FA | `FA26021C…` (`ijl15.dll.bak_before_AUX62_ONLY_*`) |
| Prior cascade forbids | `76C7D7FC` TOTEM1 / `44251097` PREFER / `021C016A` ZEROGROWTH / `EADEF4A8` / `0F244A5D` / … |

**Migration path:**

1. **P0** — Freeze Client_1 (this doc + lock file). No more sidecar 54–62 ZEROGROWTH / PREFER / TOTEM1 on live without gate below.  
2. **P1** — CD64_TEST MINIMAL grow + enter **T1–T3** (classic+cash+relog).  
3. **P2** — Wire −54…−62 as native apply (already in MINIMAL apply-max 62 + bound −63); wear via bag SendChange only.  
4. **P3** — Addon UI remap only (sidecar Get/Set/Apply **OFF** when `DetectCd64`).  
5. **P4** — Promote CD64 EXE + matching NATIVE DLL to Client_1 **only** after T1–T3 green **and** explicit user OK.

While live stays vanilla CD: server may keep CharInfo sending −54…−62 where client sidecar already absorbs; **omit false only with real CD** for any seat that would native-write past 52 without sidecar. Aux: `GREEN_ENTER_OMIT_AUX62=false` already; Client_1 AUX62_ONLY is **one-seat** stopgap — not the real expand.

---

## 6. Phased plan

| Phase | Where | Goal | Gate |
|-------|-------|------|------|
| **P0** | Client_1 | Freeze mega sidecar; document SHA | Lock file present; no enter-red stamps |
| **P1** | CD64_TEST | MINIMAL CD64 + T1–T3 enter | User green matrix |
| **P2** | CD64_TEST | Native −54…−62 wear/relog | Bag drag/dblclick; no PreferSend |
| **P3** | CD64_TEST DLL | Addon = paint/HitTest only | `UseNativeCd64Slots()` true |
| **P4** | Client_1 | Promote EXE+DLL | User OK + bak SHA |

**Imm policy:** prefer **MINIMAL** (ctor/bound/slab/stack/login clear/`0x2EB`/apply-max). FULL×318 = boot-red — **forbidden**. SAFEIMM_DISPL (`7F90405D…`) staged aside if cash/tail fails after T2–T3.

---

## 7. Explicit non-goals（明确不做）

| Non-goal | Why |
|----------|-----|
| PreferSend imm spam (54–62 range hacks) | Collides paint/GetItem; avoid #52–#54 |
| NODUAL tip nop / classic tip surgery | Product HARD #1 |
| Omit/reject aux forever | Server ready; real CD holds −62 |
| Half sidecar (56–61 only, leave 54/55 native) | Cash face/eye alias |
| FULL filtered imm×318 dump | PostGD boot-red |
| Draw-skip punch / BP33 cave | Enter GS family |
| Blind copy 095 seat numbers (119→10, 135→weapon) | Product map differs |
| Overwrite Client_1 with CD64 before T1–T3 + user OK | STOP_CASCADE |

---

## 8. CD64_TEST status (2026-08-04 ~20:55 — PLUGIN_ONLY rollback)

Active pair (**supersedes** MINIMAL / CTOR_FIELDS field-spam EXEs):

| Artifact | SHA8 | Role |
|----------|------|------|
| `BeiDou.exe` | **`ACF10F63`** | **vanilla layout** (= Client_1) — no CD field imm |
| `ijl15.dll` | **`FA26021C`** | PEER_ASLR_V2 — enter-green sidecar family **1447424** |
| bak bad EXE | `BeiDou.exe.bak_before_PLUGIN_ONLY_ROLLBACK_*` | was `6CBFFFE6` CTOR_FIELDS |
| bak NATIVE DLL | `ijl15.dll.bak_before_PLUGIN_ONLY_ROLLBACK_*` | was `8EF3A251` |

**Forbidden redeploy:** `6CBFFFE6` / `1A38CE55` / `EBC8666B` / `3C167A9B` MINIMAL / any SAFEIMM·CTOR·SKILLMAP field batch.  
**Client_1:** untouched `ACF10F63` / `FA26021C`.

Note file: `BeiDou-Client_CD64_TEST\CD64_PLUGIN_ONLY_ROLLBACK_20260804.zh-CN.md`

### Historical (suspended — EXE grow)

| Artifact | SHA8 | Role |
|----------|------|------|
| MINIMAL EXE | `3C167A9B` | aEquipped×64 — **suspended** under HARD RULE |
| NATIVE DLL | `8EF3A251` | DetectCd64 path — bak only |
| SAFEIMM aside | `7F90405D` | displ×241 — **do not** dump on live |

Verify script for MINIMAL: `tools/cd_expand/verify_cd64_minimal.py` (archaeology only).

---

## 9. Server

- Already: `EquipSlot` Aw/−62, Si/−10; `canWearEquipment` level/job; CharInfo can send −54…−62 when omit false.  
- Keep; with real CD, omit stays false for aux and peers.  
- Mirror note: [`gms-server/docs/ADDON_REAL_EQUIP_EXPAND_CLIENT.md`](../../BeiDou-Server_xy/gms-server/docs/ADDON_REAL_EQUIP_EXPAND_CLIENT.md) (link from server docs).

---

## 10. Gate to touch Client_1 again

All must be true:

1. This design + freeze lock still authoritative.  
2. CD64_TEST T1–T3 green (matrix zh-CN).  
3. No FULL×318; MINIMAL or proven SAFEIMM only.  
4. Explicit user OK to promote.  
5. Bak SHA recorded; enter A/B on staged copy before live rename.

Until then: **Client_1 = lock only** (AUX62_ONLY / FA rollback only on enter-red — no new mega sidecar).

---

## 11. IDA appendix — 083 vs 095 CD layout (2026-08-04 ~16:21)

> **Session:** live MCP `:13338` = stock vanilla IDB `BeiDou-Clinet_原版未动\BeiDou.exe.i64` (module `BeiDou.exe`, Hex-Rays ready).  
> **095 MCP `:13339`:** **DOWN** this session — 095 facts below are from prior live Hex-Rays ([`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md) + `095装备槽评估-20260725.md`). Re-open `GMS_v95.0_U_DEVM.i64` on `:13339` to re-verify.

### 11.1 Vanilla 083 — bytes re-read this session (`get_bytes` / `py_eval`)

| Site | Bytes (live IDA) | Meaning |
|------|------------------|---------|
| `@0x46F534` / `@0x46F54C` | `6A 34` | ctor `push 52` — **aEquipped count** |
| `@0x42834E` GetItem | `83 F8 CD` | `cmp eax, -51` |
| `@0x47B0BF` SetItem | `83 F8 CD` | same bound |
| `@0x4E5D55` login lea | `8D 8C F0 E7 00 00 00` | `lea ecx,[eax+esi*8+0E7h]` — **normal ZRef base +0xE7** |
| `@0x4E5E18` cash lea | `8D 8C F0 87 02 00 00` | `lea ecx,[eax+esi*8+287h]` — **cash base +0x287** |
| `@0x778F02` ZAlloc | `68 40 06 00 00` | slab **`0x640`** |
| `@0x46EC8F` stack | `81 EC 8C 07 00 00` | `sub esp, 78Ch` |
| `@0x4E5D0C` / `@0x4E5DCC` | `83 FE 33` + `7F …` | `cmp esi, 51` / `jg skip` — **apply-max 51** |
| `@0x4E5CC0` / `@0x4E5D82` | `6A 34` | login clear **52** |
| `@0x4E5D84` | `8D B0 8B 02 00 00` | cash clear lea **`+0x28B`** |

**Algebra (confirmed):** `0xE7 + 52×8 = 0x287` (cash abut); `0x287 + 52×8 = 0x427` (tail start).  
**Hex-Rays:** `GetItem` `@0x4282F7` — equip TI=1 allows `a4 >= -51 \|\| a4 < -100`; normal ZRef via `this - 8*a4 + 235`; cash via `this - 8*a4 - 149`. `SetItem` `@0x47B05A` same −51 window. **Writing BP54 in a 52-slot table ≡ cash index 2 (−102).**

### 11.2 GMS095 (prior IDA — philosophy only)

| Fact | 095 | Borrow for BeiDou |
|------|-----|-------------------|
| `sizeof(CharacterData)` | **1965** | Proof CD **must grow** before native extended Get/Set |
| GetItem / SetItem | allow **−59** (reject gap `[-100,-60]`) | Pattern = grow array + sync bounds; our product needs **−62** → bound **−63**, count **64** |
| HitTest / Draw ceiling | **58 / 59** expanded | Grow ceilings **with** real seats — not punch skip |
| Mechanic / Dragon | **independent windows** (1100+ / 1000+) | Addon dock = same philosophy (paint/HitTest only) |
| 119→10, 135→weapon, 115→51 | official 095 map | **Do not copy** — BeiDou product seats fixed in §2 |

### 11.3 CD64_TEST vs vanilla (file PE verify — this session)

| | Client_1 (frozen) | CD64_TEST |
|--|-------------------|-----------|
| EXE SHA8 | **`ACF10F63`** | **`EBC8666B`** `LAYOUT_SYNC_ENTER` (parent MINIMAL `3C167A9B`) |
| ijl15 SHA8 | **`FA26021C`** PEER_ASLR_V2 | **`8EF3A251`** NATIVE (unchanged) |
| `@778F02` | `push 0x640` | `push 0x700` |
| apply-max | 51 | **62** (`83 FE 3E`) |
| Get/Set bound | −51 | **−63** (`83 F8 C1`) |
| Ctor cash lea `@46F550` | `+0x287` | **`+0x2E7`** |
| Ctor tail `@46F567` | `+0x427` | **`+0x4E7`** |
| Cash apply lea `@4E5E18` | `+0x287` | **`+0x2E7`** |
| Cash clear `@4E5D84` | `+0x28B`×52 | **`+0x2EB`×64** |
| GetItem cash `@42837F` | `[ecx-95h]` | **`[ecx-35h]`** |
| SetItem cash `@47B0FC` | `sub ecx,99h` | **`sub ecx,39h`** |
| `verify_cd64_minimal.py` | n/a | **RESULT OK** (LAYOUT_SYNC sites) |

**2026-08-04 ~20:07 enter-crash root cause (IDA):** MINIMAL moved cash **clear** to `+0x2EB`×**64** but left ctor cash at `+0x287` and **tail fields at `+0x427`**. Clear walks to `+0x4EB` and `ZRef::Release`s live dragon/map objects → `0xC0000005` write@`0x1C`, stack `BeiDou+0xE5D98` ← `sub_428A50`. CASHBASE_SEP only moved 12×`0x287` (apply/Get/Set) without shifting tail → same family. Fix = **LAYOUT_SYNC_ENTER**: SAFEIMM displ×241 + Get/Set cash + walk `0x1A0→0x200` + EH `A7E5xx` imm×18 + dtor push64 + FLOOR162. **Not** full imm×318.

Plugin dual-mode: `DetectCd64Exe()` (`push 0x700` @`778F02`) + `kNativeCd64ExtendedSlots` → `UseNativeCd64Slots()` / `NativeCd64Inventory()` — **no** shadow Get/Set / sidecar apply on CD64; Paint reads native `GetItem(−bp)` (no cash `−(bp+100)` fallback); wear = PacketSendOnly. Vanilla Client_1 keeps FA sidecar stopgap (face/eye ghost on 54/55 **expected** without CD grow — test expand on CD64_TEST).

### 11.4 「跟095」checklist (borrow philosophy)

| # | 跟095原则 | BeiDou 落地 | Status |
|---|-----------|-------------|--------|
| 1 | Grow CD so extended seats are **native ZRefs** | MINIMAL CD64 aEquipped×64 + slab `0x700` | **Landed** on CD64_TEST |
| 2 | Sync Get/Set + apply-max with real array | bound −63, apply-max 62, clear 64, cash lea `+0x2EB` | **Landed** (MINIMAL) |
| 3 | Wear = vanilla inventory path | bag SendChange / server `InventoryManipulator` only | **Design** — prove on T4+ |
| 4 | Extra UI = separate window (Mechanic/Dragon style) | Addon dock **reused** (paint/HitTest); inventory = native | **Landed** `ADDON_NATIVE_UI_20260804` |
| 5 | Do **not** blind-copy 095 BP numbers | 109→−10; 134/135→−62; 118→−54; 120→−55…−58; 119→−59; 166→−60; 167→−61; pocket −33 Addon | **Product HARD** |
| 6 | No PreferSend / NODUAL tip / draw-skip / FULL×318 | Forbidden on Client_1 + CD64 path | **Locked** |
| 7 | Enter green before promote | T1 **GREEN**; **T2–T3 user-run next** | Open |
| 8 | Cash/tail imm only if evidence | SAFEIMM_DISPL aside (`7F90405D`) — after T2/T3 fail only | Standby |

**Next (user):** run T2 then T3 on `BeiDou-Client_CD64_TEST` only — see `CD64_ENTER_MATRIX.zh-CN.md`. After T2–T3: T4 wear badge/totem/aux. **Do not** promote to Client_1.

### 11.5 CASH_FLOOR162 enter RED (2026-08-04 ~19:44) — superseded by LAYOUT_SYNC

| | |
|--|--|
| Crash pair | EXE `5C748193` CASH_FLOOR162 + DLL `2D3EF80D` CASH_APPEND |
| Evidence | `client_boot.log` 19:43:59 `0xC0000005` write@`0x1C`; stack `sub_428A50` ← cash clear `@0x4E5D98` in `sub_4E592D` (BeiDou.exe, not ijl15) |
| Why red | FLOOR162/CASHBASE still left ctor **tail at `+0x427`**; clear×64 from `+0x2EB` Release()d those objects |
| Client_1 | **untouched** `ACF10F63` / `FA26021C` |

### 11.6 LAYOUT_SYNC_ENTER deployed (2026-08-04 ~20:07) — retest select→enter

| | |
|--|--|
| Stamp | `LAYOUT_SYNC_ENTER_20260804_200749` |
| EXE | **`EBC8666B`** — bak `BeiDou.exe.bak_before_LAYOUT_SYNC_ENTER_20260804_200749` (= MINIMAL `3C167A9B`) |
| DLL | **`8EF3A251`** NATIVE (no change) |
| Builder | `tools/cd_expand/_make_layout_sync_enter.py` → `out/BeiDou.cd64_LAYOUT_SYNC_ENTER_20260804_200749.exe` |
| Ops | 271 = displ×241 + eh_imm×18 + other×12 (Get/Set/walk/floor/Draw/push64) |
| Client_1 | **untouched** |
| User | Close all BeiDou → start **only** `BeiDou-Client_CD64_TEST` → select→enter (classic+cash if possible) |
