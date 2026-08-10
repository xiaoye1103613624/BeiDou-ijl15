# Client Patch SOP / 客户端改动 SOP

**Canonical:** `E:\pro\BeiDou-ijl15\docs\CLIENT_PATCH_SOP.md`  
**Crash history / avoid-list:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)  
**Client_1 mirror (pointer):** `BeiDou-Client_1\CLIENT_PATCH_SOP.md`

Last confirmed green (2026-08-04): **EXECAVE** logout UI — prior ijl15 `A0BE14C9…` / 1447424. **Live now (~15:55):** `FA26021C…` PEER_ASLR_V2 / 1447424 — **rolled back from TOTEM1 `76C7D7FC` REGRESSION** (totem blank/multi + aux blank). Forbid `76C7D7FC` / `44251097` / `021C016A` / `EADEF4A8` / `DF3AF2F1` / `73559AC5` / `0F244A5D` / `32C55435`. EXE **RESTORE_DUALTIP** `ACF10F63…` / 8503296. Guard: `tools/guard_client1_execave.ps1` + Client_1 `IJL15_DO_NOT_OVERWRITE.txt`. See [`ADDON_STOP_CASCADE_20260804.md`](./ADDON_STOP_CASCADE_20260804.md).

---
## Product rules — Equip display / Addon (HARD 2026-08-04)

1. **Do NOT modify original CUIEquip tip/draw/hover** for classic seats — no nop of dual-tip `jnz` `@7FEA9A` (`75 4C`), no rewrite of `sub_7FE9BB` / `sub_8EBA5B` / `CUIToolTip__SetToolTip_Equip` for vanilla bodyparts.
2. **Extended Addon seats are new UI**, but must **reuse** the same wear / unequip / drag / dblclick / hover-tip / enhance behavior as native slots (packet + GetItem paths) — do not invent a parallel tip pipeline for classic seats.
3. If Addon truly mirrors native inventory seats, tip/draw bugs should be rare — **stop compensating by patching vanilla tip**. Cash dual-tip crash fix = GetItem sidecar **normal-only** (−56…−61; cash −156 not aliased), **not** deleting dual tip UI.
4. **Forbidden:** `ADDON_HOVER_RELOG_NODUAL` (`90 90` @`7FEA9A`). Prefer equality-only skip (`GetItem(−bp)==GetItem(−100−bp)` same pointer → skip second tip) only if dual tip still AVs after normal-only GetItem — never blanket nop.

---
## 出问题 SOP（强制） / Incident SOP (mandatory)

| # | 中文 | English |
|---|------|---------|
| 1 | **立刻回滚**到上一已知可进图 bak（保持可玩） | **Rollback immediately** to last enter-proven bak (keep playable) |
| 2 | **分析日志 / dump / WER**（对照 IDA，禁止盲改） | **Analyze** `client_boot` / dump / WER — **IDA-backed**; no blind edits |
| 3 | **修改**（优先零涨体积 / 晚装钩；禁止污染全量重链上 live） | **Patch** — prefer **zero size growth** / **late hooks**; never promote polluted full-relink to live |
| 4 | **再测试**（进图 A/B 通过才留 live） | **Retest** — keep on live only after select→enter A/B green |

Do **not** leave a red DLL/EXE on Client_1 while investigating.

---

## Standing rules / 常驻规则

0. **禁止改原版地址（HARD 2026-08-04 ~20:54）** — 不要改 vanilla `BeiDou.exe` CharacterData 字段偏移 / layout imm（skill-cool、cash lea、SAFEIMM、CTOR_FIELDS…）。扩栏 −54…−62 **只在 ijl15（自定义）**。见 avoid **#56** / [`ADDON_REAL_EQUIP_EXPAND_20260804.md`](./ADDON_REAL_EQUIP_EXPAND_20260804.md) §0。玩法/协议变更 → 双端审计。
1. **对照 IDA（强制）** — every ijl15 / EXE change: Hex-Rays + xrefs (addr, cc, callers, lifecycle) via `user-ida-pro-mcp` **before** patch/deploy. See avoid **#38–#40** (and #41–#44).
2. **Bak first** — copy live → `*.bak_<stamp>` + record SHA/size **before** any overwrite.
3. **Stage ≠ live** — build to `golden\` / test copy; promote only after enter A/B.
4. **Zero growth preferred** — binary patch on proven HOVER size (1447424) or EXE cave; avoid MSBuild full-relink that jumps +60–70KB (GS enter-red family).
5. **Late hooks** — install Detours / ATTACH after CharData + field proven; not DllMain / bootstrap-only.
6. **No auto-deploy** of new stamp to Client_1 without user enter proof (or restore of already-proven bak).
7. **LOGOUT UI / tip / growth** — staged only until zero-growth or late-hook path exist; do not redeploy avoid #38 / #42 stamps.
8. **Vanilla CUIEquip tip intact** — never nop `@7FEA9A`; Addon must not alter classic-seat tip/draw (product rule above; avoid **#44**).

---

## Quick paths / 路径

| Item | Path |
|------|------|
| Live client | `E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1` |
| ijl15 source | `E:\pro\BeiDou-ijl15` |
| Avoid / live SHA | [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md) |
| UI logout IDA note | [`ADDON_UI_LOGOUT_CLOSE_IDA_POSTMORTEM.md`](./ADDON_UI_LOGOUT_CLOSE_IDA_POSTMORTEM.md) |

---

## Enter A/B (new stamp only)

Ask user: select→enter (any char). On flash / `0xC0000409` → **step 1** (rollback), then 2–4.  
Restoring a previously enter-green bak: **no** retest nag.
