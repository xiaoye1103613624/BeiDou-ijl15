# ADDON_LOGIN_HOVER_SAFE — IDA postmortem (2026-08-03 ~22:32)

Enter-red stamp rolled back. This note answers: **有没有参考 IDA？** and proposes the next minimal patch.

## Verdict table (honest)

| Change | IDA 依据 | Evidence |
|--------|----------|----------|
| GetItem normal-only (`SidecarBpFromSlotNormal`) — no cash −156→same ZRef | **有** | Live Hex-Rays `sub_7FE9BB`: `GetItem(−bp)` then `GetItem(−100−bp)`; both non-null → `sub_8EBA5B(v27,v29)`. Prior hover AV @0x500. Vanilla `GetItem@4282F7` does **not** cover −56..−61 (bound ≥−51 / cash −151..−100). |
| Apply / login jg-allow caves @`4E5D0C` / `4E5DCC` / lea @`4E5D55`/`4E5E18` | **部分** | Addresses from prior 083 IDA of `sub_4E592D` (getCharInfo equip decode): `if (v38 <= 51)` drops BP56–61. Cave design is IDA-grounded; **DllMain timing** of install is empirical. |
| `InstallLoginPersistEarly` = quiet caves **+ Get/Set ATTACH_HOOK** in DllMain | **无** (timing) | Avoid #23 forbids Park/Dbg; Early still hooks Get/Set before CharData. Green `6D612F01` has **no** `EquipAddonLoginPersist` boot line. HOVER_SAFE boot **did** log it then enter-red — timing not IDA-proven safe. |
| Tip-clean / FusionAnvil full MSBuild (+70KB) | **无** | Size≠green; lost `ADDON_ROW3_UNEQUIP_UI` string; same `0xC0000409` @ucrtbase+9d132 family as tip stamps. |

## IDA facts (v083 `BeiDou.exe` MCP `:13338`, 2026-08-03)

### CUIEquip tip dual GetItem — `sub_7FE9BB`

```c
v27 = GetItem(..., 1, -v24);      // normal −bp
v29 = GetItem(..., 1, -100 - v24); // cash −(100+bp)
if (v27 && v29)
  sub_8EBA5B(..., v27, v29, ...); // dual tip — same ZRef → AV
```

### CharacterData::GetItem — `sub_4282F7`

Equip seats: allow if `(a4 >= -51 || a4 < -100)` and `a4 >= -151`.  
→ **−56..−61 are outside native array**; sidecar must own them for Addon paint.  
→ **−156..−161 are native cash**; must **not** alias sidecar ZRef for GetItem.

### Login apply — `sub_4E592D` @`4E5D0C`

```c
if (v38 <= 51) { /* ZRef_Assign into aEquipped */ }
```

Slots 56–61 need jg-allow **before** decode walk; installing **only** that cave (no Get/Set yet) is the IDA-minimal first-login fix.

## Crash evidence (HOVER_SAFE)

- SHA `05B67BD9…` / 1518080; aside `…aside_CRASHED_LOGIN_HOVER_SAFE_20260803_223018.bak`
- WER `0xc0000409` BEX `ucrtbase+0x9d132`; dump `BeiDou.exe.10344.dmp` embeds stamp
- Boot: `EquipAddonLoginPersist OK` @22:28:22 → quiet @22:28:34 → die @22:28:44

## Next patch policy (do not redeploy HOVER_SAFE)

1. Keep Client_1 on **green** `6D612F01` until enter-proven.
2. Prefer **one** of:
   - **A.** Binary-patch green DLL: GetItem hook body only → `SidecarBpFromSlotNormal` (hover), hooks already installed by EnsureHooks on green.
   - **B.** DllMain Early = **jg-allow + apply lea caves only** (no Get/Set, no Park, no Dbg); Get/Set stay at EnsureHooks.
3. Never A+B+full relink in one stamp. Stage golden → enter A/B → then live.
4. 悬停/首次空栏: accept green known issues until next stamp.

## Phase1 shipped (2026-08-03 ~22:38) — await user enter+hover

| Item | Value |
|------|-------|
| Stamp | `ADDON_HOVER_GETITEM_ONLY_20260803` |
| SHA / size | `B42BC601…` / **1447424** (same as green) |
| Bak | `ijl15.dll.bak_ADDON_HOVER_GETITEM_ONLY_20260803` = `6D612F01…` |
| Golden | `golden\ijl15.ADDON_HOVER_GETITEM_ONLY_20260803.dll` |
| Patch | GetItem `@VA 10035445` `0F 46 F1`→`90 90 90` (NOP cash `cmovbe`); SehGetItem `@file 36C68` `ja 06`→`1A` (skip cash→xor); stamp string replaces GetItem OK msg |
| Kept | `ADDON_ROW3_UNEQUIP_UI`; SetItem cash −156..−161 alias; no Early; no tip relink |
| IDA | `sub_7FE9BB` dual GetItem (−bp / −100−bp); `GetItem@4282F7` bound excludes −56..−61 |
| Boot smoke | ShoulderSlots→LazyCompat→quiet ON; **no** EquipAddonLoginPersist |
| Phase2 | **staged NOT live** — fixed golden `A0BE14C9…` / 1447424 tramp `@0xAB340` (preserves EXECAVE stub `@0xAB30C`). Broken `07435AFC` clobbered stub — do not redeploy. Live remains EXECAVE `8DD59B06` + EXE `7E240917` (user logout-UI green). Patch: `tools/patch_login_jg_only.ps1`. IDA: `sub_4E592D` `cmp esi,33h`/`jg` @`4E5D0C` |

## User message

先确认能进图；悬停/首次空栏暂用绿端已知问题，等下一版。

**Phase1 用户测：** 进图 → 悬停点装。勿测首次空栏（等 Phase2）。进图红立即回滚 bak。

## Post-relog hover (2026-08-03 ~22:46 / fix ~23:20)

| Hyp | Verdict |
|-----|---------|
| EnsureHooks re-patches GetItem / restores cash alias | **Rejected** — live `0x34845=909090`, `0x36C68=1A` until unrelated GROWTH overwrite |
| Second GetItem site still aliases | **Rejected** for cmovbe (only one); SehGetItem also skip-cash |
| After 小退 sidecar fill → dual tip both non-null | **Plausible** for body 点装 dual path; addon −156 null via NOP so same-ZRef via GetItem cash alias unlikely |
| Crash sig | **Confirmed** `0xC0000005` `@0x8EBC3C` read `@0x500` |

**Fix staged:** EXE `ADDON_HOVER_RELOG_NODUAL` nop `@7FEA9A` dual jnz + restore ijl `B42BC601`. Next refine: equality-only (`v27==v29` → null cash) via EnsureHooks cave, not full dual-off.
