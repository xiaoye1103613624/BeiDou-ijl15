# ADDON_UI_LOGOUT_CLOSE — IDA postmortem (2026-08-03 ~23:37)

**Rule:** 改客户端必须对照 IDA。本文件是 live 上线前必填的 Hex-Rays / xref 记录。

## Crash evidence

| Item | Value |
|------|--------|
| Stamp | `ADDON_UI_LOGOUT_CLOSE_20260803` |
| SHA / size | `1DCCEA52…` / **1509888** (+62464 vs HOVER) |
| Aside | `ijl15.dll.aside_CRASHED_UI_LOGOUT_CLOSE_ENTER_RED_20260803_*.bak` |
| WER | 23:34:42 / 23:35:27 — BEX `0xC0000409` @ `ucrtbase.dll+0x9d132` |
| Dumps | `BeiDou.exe.3632.dmp` / `BeiDou.exe.33244.dmp` embed stamp |
| Boot | quiet ON @23:35:08 → die (select→enter flash); **no** logout-path VEH line |
| EXE | unchanged `B37F0269` NODUAL — not fault module |
| Live after rollback | `B42BC601…` / **1447424** HOVER |

Markers in crashed DLL (absent on HOVER): `EquipAddonLoginPersist`, `DestroyGameUI_A041FF`, `ADDON_UI_LOGOUT_CLOSE`.

## Root cause (ranked)

1. **Primary — polluted frozen link + size jump (ENTER-RED GS family)**  
   Build claimed `Release_enter_frozen` green objs, but `dllmain.obj` still embeds `EquipAddonLoginPersist` / `InstallLoginPersistEarly`. Same class as avoid #35 `LOGIN_HOVER_SAFE`: +~60KB relink → enter `0xC0000409` @ ucrtbase+9d132.  
   **Not** proven as logout/DestroyGameUI AV — crash timing is enter-after-boot.

2. **Secondary — design violates IDA lifecycle (not this dump’s EIP)**  
   `EnsureLogoutUiTornDown` from `OnTick` can **force-call** `A041FF` when CharData already null. Vanilla never does that (see below). Forbidden under「对照 IDA」rule even if enter-red masked it.

## IDA — official control flow (v083 BeiDou.exe)

### `set_stage` = `sub_777347` (`__cdecl(a1, a2)`)

LABEL_16 (`0x777462`) when stage is null **or** not CField/CInterStage/CashShop-family RTTI:

```text
v15 = (*(_DWORD *)(sub_425D0B(CWvsContext) + 4) != 0);  // CharData ZRef.p
if (v12) ZRef_Release(...);
if (v15)
  sub_A041FF(dword_BE7918);   // @0x777494  ecx = CWvsContext*
```

- Skip DestroyGameUI: `jz` @`0x77748c` when CharData already null.
- CharData getter: `sub_425D0B` copies `CWvsContext+8376` (`*(this+2094)` as DWORD*).

### `CWvsContext::DestroyGameUI` = `sub_A041FF`

- Conv: **`__thiscall`**, `ecx` = `dword_BE7918` (CWvsContext singleton).
- Xrefs (only 2):
  1. `set_stage` @`0x777494` (LABEL_16, CharData gate)
  2. `sub_A06735` @`0xA0680F` — after `set_stage(logo,…)` on certain exit UI path; **also** calls A041FF (may double with set_stage — still official).
- Body: walks ZRef UI slots on context + global singletons (`BED788`, `BEC208`, …) via `sub_9E00AF` + typed destroy. **No Addon Gr2D / SideToolbar** — plugin orphans by design.

### Hook calling convention

MSVC Detours `__thiscall` target → hook as `int __fastcall(void* ecx, void* edx)` then call orig(ecx) is **ABI-correct**. Wrong-cc was **not** the enter-red cause.

## IDA-based next design (NO live deploy until enter A/B)

| Step | Do | Do not |
|------|-----|--------|
| 1 | Stay live on HOVER `B42BC601` / 1447424 | Redeploy any +60KB rebuild |
| 2 | Rebuild/replace `Release_enter_frozen\dllmain.obj` from HOVER-matching source (**no** LoginPersist Early string) before any link | Blind `robocopy` polluted frozen |
| 3 | Prefer **binary patch on HOVER DLL** (size stay 1447424) | Full MSBuild/link “hope” |
| 4 | Teardown Addon **only** inside A041FF hook: `DestroyLayer` + `SideToolbar::DestroyForLogout` **then** `orig(ecx)` — same stack frame as official | `OnTick` force `A041FF` |
| 5 | CharData-null skip (`77748c`): Addon-only destroy if stage→login and `g_layer` live — **never** call vanilla DestroyGameUI | Guess hooks / Early Get-Set |
| 6 | Stage golden → user 先确认能进图 → then logout UI matrix | Live without IDA note + enter proof |

## Static gates for next attempt

- SHA size == 1447424 (±0) **or** documented byte-patch only
- FORBID strings: `EquipAddonLoginPersist`, `InstallLoginPersistEarly` (active call), `EquipGrowth`, `SENDBUSY`
- ALLOW after IDA: `DestroyGameUI_A041FF` only if dllmain frozen clean
- Boot must: `ShoulderSlots` → `LazyCompat` → quiet ON — **no** LoginPersist line

## Deployed follow-up — ADDON_UI_LOGOUT_IDA (2026-08-03 ~23:55)

| Item | Value |
|------|--------|
| Stamp | `ADDON_UI_LOGOUT_IDA_20260803` |
| SHA / size | `E88CEBBE…` / **1521664** (+74240 vs HOVER) |
| vs ENTER-RED | No `EquipAddonLoginPersist`; fresh `dllmain.cpp`; no OnTick force `A041FF` |
| Bak | `ijl15.dll.bak_ADDON_UI_LOGOUT_IDA_20260803` = HOVER `B42BC601…` / 1447424 |
| EXE | `B37F0269` NODUAL kept |
| Status | **LIVE — await user enter A/B + logout UI matrix**; rollback bak on `0xC0000409` |

### IDA citations (re-verified MCP)

- `sub_777347` LABEL_16 `@777462`: `425D0B` CharData; `jz` `@77748c` skip DestroyGameUI; `call sub_A041FF` `@777494` `ecx=dword_BE7918`
- `sub_A041FF` `__thiscall`: walks context ZRef UI + globals (`BED788`, `BEC208`, …); **no** Addon Gr2D / SideToolbar
- Xrefs to `A041FF`: only `777494` (`set_stage`) + `A0680F` (`sub_A06735`)

### Implementation

1. Hook `DestroyGameUi_hook` `@0xA041FF`: `TeardownPluginUiForLogout` (DestroyLayer + `SideToolbar::DestroyForLogout`) **then** `orig(ecx)`
2. `EnsureLogoutUiTornDown` from OnTick when `!ShouldKeepGameUi`: Addon-only teardown once — **never** call vanilla `A041FF`
3. `dllmain`: `InstallLoginPersistEarly` remains **commented** (boot = ShoulderSlots → LazyCompat)

## Addendum — ADDON_UI_LOGOUT_IDA ENTER-RED (2026-08-03 ~23:59)

| Item | Value |
|------|--------|
| Verdict | **ENTER-RED** — rolled back to HOVER |
| Live now | `B42BC601…` / **1447424** |
| Aside | `ijl15.dll.aside_CRASHED_UI_LOGOUT_IDA_ENTER_RED_20260803_*.bak` (`E88CEBBE…` / 1521664) |
| WER | 23:58:50 AppError / 23:58:53 BEX `0xC0000409` @ `ucrtbase.dll+0x9d132` |
| Dump | `BeiDou.exe.16132.dmp` embeds `ADDON_UI_LOGOUT_IDA` |
| Timing | Boot quiet ON @23:58:34 → die @23:58:50 (~16s) = **select→enter flash**, not logout |
| Boot | `ShoulderSlots OK` → `LazyCompat bootstrap OK` → quiet ON — **no** `EquipAddonLoginPersist` |
| EXE | `B37F0269` NODUAL **unchanged** (not fault module) |

### Why still red without LoginPersist

1. **Primary — size-jump full relink (same GS family as #35/#38 tip rebuilds)**  
   +74240 vs HOVER: `.text` +58368, `.rdata` +11776, `.reloc` +3584. Exports still 11; `.detourc` RS same (4608) — Detours *section capacity* unchanged, but PE body is a different link. Lost `ADDON_HOVER` binary-patch marker; gained config `enableEquipGrowthTip` / growth companion strings absent on HOVER. Clean dllmain FORBID passed; **FORBID≠enter-safe** when MSBuild rewrites 58KB of code.

2. **A041FF Detours — unproven, install-at-enter risk**  
   Hook installs via `EquipAddon::EnsureHooks` → `InstallUiHooksOnce` → `ATTACH_HOOK(A041FF)` on LazyCompat / first-field path — **not** DllMain, but still during enter. Dump EIP is GS/`ucrtbase`, not a clean A041FF AV, so cannot pin sole blame on Detours; honest rule: **treat Detours-on-A041FF as enter-forbidden until proven on a zero-growth binary**. Prefer late install only after first stable field / CharData, or avoid Detours entirely.

3. **LoginPersist was necessary but not sufficient** — removing it fixed boot string path; enter GS remained.

### Safer next — shipped as EXECAVE (2026-08-04)

| Prefer | Detail |
|--------|--------|
| **Zero size growth** | **Done:** HOVER size kept 1447424. EXE `@77748c` → cave `@AEF602` (AddonDestroy via ijl15 stub RVA `0xAB30C`) then `A041FF` iff CharData≠0; resume `@777499`. CharData-null: Addon-only (no A041FF). NODUAL kept. |
| Tool / stamp | `tools/patch_addon_ui_logout_execave.py` / `ADDON_UI_LOGOUT_EXECAVE_20260804` |
| Bak | `*.bak_ADDON_UI_LOGOUT_EXECAVE_20260804` (= `B42BC601` + `B37F0269`) |
| Forbidden | Full relink “hope”, OnTick force `A041FF`, redeploy `E88CEBBE` / `1DCCEA52` |

中文：已上 live 零涨体积 EXECAVE — **请先确认能进图**；闪退 `0xC0000409` 立刻双回滚 bak。
