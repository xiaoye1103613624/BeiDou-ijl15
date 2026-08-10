# Incident 2026-08-04 ~07:45 — Equip/Addon on login again

## Why
1. Live ijl15 overwritten by `GROWTH_TIP_REEN` `D6D01354…` / **1552896** (`ijl15_VERSION` lock @07:47) — **no** EXECAVE stub @RVA `0xAB30C`.
2. EXE still `7E240917` (cave @77748c→AEF602 intact) but `GetModuleHandleA(ijl15)+0xAB30C` hit unrelated GROWTH code → teardown failed → vanilla Equip stayed.
3. Prior Phase2 `07435AFC` also **clobbered** stub: `patch_login_jg_only.ps1` placed tramp at `VA+VSz=0xAB30C` (same slack as EXECAVE stub).

## Restored then promoted

| When | ijl15 | Note |
|------|-------|------|
| ~08:xx restore | `8DD59B06…` EXECAVE | Logout UI green |
| ~09:55 Phase2 | `A0BE14C9…` JG_ONLY | Deployed from PENDING (lock FREE) |

| File | SHA | Size |
|------|-----|------|
| ijl15.dll **live** | `A0BE14C9…` JG_ONLY | 1447424 |
| BeiDou.exe | `ACF10F63…` **RESTORE_DUALTIP**+EXECAVE (`@7FEA9A`=`75 4C`) | 8503296 |
| BeiDou.exe bak NODUAL | `7E240917…` `bak_before_RESTORE_DUALTIP_20260804` | 8503296 |

Aside GROWTH: `ijl15.dll.aside_GROWTH_REEN_broke_EXECAVE_20260804_075028.bak`  
Bak before Phase2: `ijl15.dll.bak_EXECAVE_8DD59B06_before_A0BE14C9_*`

## Phase2 (now live — retest pending)
- Fixed `tools/patch_login_jg_only.ps1`: tramp @RVA `0xAB340` (after 50-byte stub); post-check stub `53 E8`.
- Golden: `E:\pro\BeiDou-ijl15\golden\ijl15.ADDON_LOGIN_JG_ONLY_20260804.dll` SHA `A0BE14C9…` / 1447424
- Broken golden aside: `*.BROKEN_stub_clobber_07435AFC.bak`
- Prior deploy blocked by BeiDou PID; resumed when process gone (no force-kill).

## IDA (re-verified)
- `set_stage@777347` LABEL_16: CharData gate `jz@77748c`; `A041FF` @777494 `ecx=BE7918`
- Xrefs `A041FF`: `777494` + `A0680F` only
- EXECAVE cave: **always** Addon stub; `A041FF` iff CharData≠0 (flags). If Equip still leaks with CharData already null → need official second path / safe `A041FF` with valid ecx — **not** OnTick force.

## Retest (中文) — **请用户跑一遍**
1. 冷启动 → 选角进图（A/B）
2. 打开装备栏/Addon → 小退到登录
3. **登录界面不得残留装备栏/Addon**
4. 再进图悬停点装仍正常
5. Phase2：首次登录空 Addon

## Guard (防止再被 GROWTH 覆盖)
- Client_1 `IJL15_DO_NOT_OVERWRITE.txt` → 锁定 **JG_ONLY `A0BE14C9`**（**禁止** Required=`GROWTH_TIP_REEN`）
- `tools/guard_client1_execave.ps1` — expect `A0BE14C9` + stub `53 E8` @`0xAB30C`；forbid D6D01354 / 07435AFC / E88CEBBE / 1DCCEA52 / 05B67BD9

## Closed
- **点装 tip restore** — EXE `ACF10F63…` `@7FEA9A`=`75 4C`; user confirmed OK; EXECAVE intact. Never re-nop (avoid #44).

## Remaining backlog
- CD64 MINIMAL+NATIVE：**T1 绿（用户）**；下一关 **T2–T3**（经典+点装 / 重登）；未绿禁止推 Client_1
- Imm：SAFEIMM_DISPL staged (`7F90405D…` displ×241 aside) — ([`CD64_IMM_FILTER_DIAG_20260804.md`](./CD64_IMM_FILTER_DIAG_20260804.md)); do not dump ×318 again
- Native A2：仍 blocked（进图 A/B 未做）
- tip/growth：仅 golden；零涨体积/晚装钩前不上 live

## Timeline
- ~08:55: stall recovered; PENDING staged; deploy blocked (DLL in use).
- ~09:55: BeiDou not locking → bak EXECAVE → deploy A0BE14C9 → VERSION/lock/guard updated.
