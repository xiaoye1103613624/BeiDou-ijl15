# Open items — 2026-08-04 (PLUGIN_ONLY pivot ~20:54)

## HARD RULE ~20:54

「不要改原版地址」→ EXE CD field/layout imm **stopped** (avoid #56). Expand = **ijl15 sidecar only**.  
Skill-cool patches were layout sync, **not** a new feature — stopped.

## Live now (~21:20 APPEND_ONLY)

| Item | Value |
|------|-------|
| Design | [`ADDON_APPEND_ONLY_EXTEND.md`](./ADDON_APPEND_ONLY_EXTEND.md) |
| ijl15 | **`60A3E54F…`** / 1447424 — `ADDON_APPEND_ONLY_EXTEND_20260804` |
| EXE | **`ACF10F63`** vanilla CD (unchanged) |
| Clients | **Client_1 + CD64_TEST** (both) |
| Bak FA | `ijl15.dll.bak_before_APPEND_ONLY_*` → `FA26021C` |
| Stub | `@AB30C` = `53 E8` |
| Next | **冷启** select→enter A/B；红则回滚 FA |

Prior FA pair (`ACF10F63` + `FA26021C`) kept as rollback only.

## Parallel tracks (do not collide)

| Track | Scope | Rule |
|-------|-------|------|
| **A — Client_1** | **FROZEN** `ACF10F63` / `FA26021C` | Lock: [`CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md`](./CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md) |
| **B — CD64_TEST** | Same vanilla EXE + plugin sidecar extend | [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md) §0; **no** EXE field batches |

## User mandate (updated)

- 扩栏在 **自定义插件**；禁止再跑 `tools/cd_expand/_make_*` 写 live EXE。
- STOP_CASCADE PreferSend / ZEROGROWTH / TOTEM1；fat DLL >1447424 不上 live（avoid #55）。
- 玩法/协议 → 双端审计；纯 sidecar UI → 插件 only。

## 「跟095」checklist

| # | Item | Status |
|---|------|--------|
| 1 | Grow CD → native ZRefs −54…−62 | CD64_TEST MINIMAL **landed** |
| 2 | Sync Get/Set + apply-max 62 | **verified** `verify_cd64_minimal.py` OK |
| 3 | Wear = vanilla SendChange only | design; prove T4+ |
| 4 | Addon = paint/HitTest (Mechanic/Dragon style) | NATIVE DLL DetectCd64 dual-mode **landed** |
| 5 | Product seats ≠ blind 095 BP map | HARD locked in REAL_EQUIP_EXPAND §2 |
| 6 | No PreferSend / tip-nop / draw-skip / FULL×318 | frozen |
| 7 | T1 enter empty Addon | **GREEN (user)** historically on MINIMAL |
| 8 | Enter crash fix (cash clear / tail) | **DEPLOYED** `LAYOUT_SYNC_ENTER` EXE `EBC8666B` + DLL `8EF3A251` — **retest select→enter** |
| 9 | T2 classic+cash ≥30s | **user run next** (after enter green) |
| 10 | T3 relog classic+cash | **user run next** |
| 11 | Promote Client_1 | **blocked** |

IDA live pass 2026-08-04 ~16:21: 083 `:13338` re-confirmed ctor52 / bound−51 / lea+0xE7/+0x287 / slab0x640 / apply-max51 — appendix §11. 095 `:13339` **down** — use prior notes (CD1965 / allow−59).

## Closed (user confirmed)

| Item | Stamp / SHA | Note |
|------|-------------|------|
| Dual-tip restore | EXE `ACF10F63…` `@7FEA9A`=`75 4C` | **CLOSED** — do not re-nop (avoid #44) |
| EXECAVE intact | `@77748c`→`@AEF602`; stub `@AB30C`=`53 E8` | Client_1 DLL must be 1447424 + `53 E8` (avoid #55) |
| CD64_TEST **T1** | MINIMAL `3C167A9B…` + NATIVE `8EF3A251…` | **GREEN (user)** — empty Addon enter |
| CD64_TEST **NATIVE_UI** | ijl `E7BB3766…` `ADDON_NATIVE_UI_20260804` | 原生+沿用Addon UI；Client_1 untouched |
| Wear gates (source) | stage `2B65633B` `ADDON_WEAR_FIX` **CD64_TEST only** | aux−62 / drag-replace / bodypart / no PE HT≥50; **not** Client_1 |

## Live Client_1 (FROZEN — real expand on CD64_TEST only)

| Item | Value | Status |
|------|-------|--------|
| Live ijl15 | **`3642FBDD`** AUX62_ONLY | **Frozen** — lock file; **untouched this session** |
| Live EXE | **`ACF10F63`** vanilla CD | **Do not** CD64 overwrite; **untouched this session** |
| Aside ENTER-RED | `0F244A5D` etc. | **Forbidden redeploy** |
| Server omit −62 | `GREEN_ENTER_OMIT_AUX62=false` | Keep; real CD holds seats |
| CD64 / NATIVE on live | — | **Blocked** until P4 gate |

## Was AUX_BP62 IDA-grounded? — **NO (partial inherit only)**

| Layer | Verdict |
|-------|---------|
| Cave **sites** in EXE | Prior IDA of `sub_4E592D`: `@4E5D0C`/`@4E5DCC` = `cmp esi, 33h` (51) + `jg skip`; `@4E5D55` = `lea ecx,[eax+esi*8+0E7h]` → `ZRef_Assign`. BP56–61 jg-allow/lea design = IDA-grounded. |
| AUX_BP62 **deploy** | Mechanical **29-byte** imm patch on live + stamp + **cleared omit** — **no** fresh Hex-Rays writeup / enter A/B. |
| Honest label | **Blind/mechanical extension** — avoid #45. |

## Shield decoupling audit

| Path | Status |
|------|--------|
| Server `EquipSlot` SHIELD vs `AUX_WEAPON` | OK — 109→−10; 134/135→−62 |
| `InventoryManipulator` / migrate / AvatarLook | OK |
| **`getEquipmentSlot` 134/135→`Si`** | **FIXED → `Aw`** |
| Client AbleToWear / dock BP62 | OK |
| Vanilla Equip tip | **Do not modify** |

## Track B — CD64_TEST real expand (next user steps)

| Item | Status |
|------|--------|
| Design + IDA appendix | [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md) §11 |
| Pair | MINIMAL `3C167A9B…` + **NATIVE_UI `E7BB3766…`**（「原生+沿用Addon UI」） |
| Verify sites | `tools/cd_expand/verify_cd64_minimal.py` → **OK** (apply-max 62, bound −63, slab 0x700) |
| Dual-mode plugin | DetectCd64 → native GetItem Paint + PacketSendOnly; vanilla Client_1 sidecar frozen |
| T1 | **GREEN (user on prior 8EF3)** — smoke again after DLL bump |
| **T2** | Classic hat/weapon + **one cash**; map ≥30s — **user run** |
| **T3** | Relog; classic+cash still on — **user run** |
| SAFEIMM_DISPL | Staged `7F90405D…` aside — after T2/T3 fail only |
| FULL×318 | Boot-red — **never dump again** |
| Promote Client_1 | **Blocked** until T2–T3 + user OK |

Exact steps: `BeiDou-Client_CD64_TEST\CD64_ENTER_MATRIX.zh-CN.md`  
Imm: [`CD64_IMM_FILTER_DIAG_20260804.md`](./CD64_IMM_FILTER_DIAG_20260804.md)  
Builder: `tools/cd_expand/_make_safeimm_displ.py`

## Track A — Client_1 (frozen — do not cascade)

Client_1 stays **`3642FBDD` / `ACF10F63`**. No PEER/TOTEM1/ZEROGROWTH/ENTER-RED redeploy. Aux stopgap only until Track B promotes.

**ENTER-RED forbid:** `DF3AF2F1` · `73559AC5` · `0F244A5D` · `3B864717` · Prefer/ZEROGROWTH/TOTEM1 mega stamps
