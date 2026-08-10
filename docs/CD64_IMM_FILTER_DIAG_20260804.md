# CD64 imm filter diagnosis — 2026-08-04

> **Scope:** `BeiDou-Client_CD64_TEST` only. **Do not** promote to Client_1.  
> **Daily pair:** MINIMAL `3C167A9B…` + NATIVE `8EF3A251…` — **T1 GREEN (user)**.  
> **Next user:** T2–T3 (classic+cash / relog). SAFEIMM_DISPL staged aside for later.

## Verdict

- FULL imm (`40EBF7F1…`, filtered ×318) → boot-red `PostGD/9F7034` → `Init SEH 0xE06D7363` (even with green DLL).
- MINIMAL (ctor/bound/slab/stack/login clear/`0x2EB`/apply-max **62**) → logo + **T1 empty Addon enter OK**.
- Filtered set is **not** classic and/imul false-friends (0 heuristic hits). It is mostly `displ` lea/mov (241) + `imm` push/add (77).
- Crash is **indirect**: **0** filtered sites within ±32K of `PostGD@9F7034` — GD init later trips on bad state from earlier rebased offsets.
- Likely over-patch: `imm` push/add tables (dense block `@A7E538…`) and high-frequency tail offsets (`0x447`×69, `0x5FF`×55).

## Numbers

| Metric | Value |
|--------|------:|
| Full scan sites | 18383 |
| Filtered sites | 318 (`displ` 241 / `imm` 77) |
| MINIMAL↔FULL differing bytes | 582 |
| Near PostGD `9F7034` (±32K) | **0** |
| Login window `4E5xxx` in filtered | 38 |
| Page `00A7xxxx` (offset-add table) | 36 (all `kind=imm`) |

## Imm fix plan (agent, CD64_TEST only)

| Step | Build | Sites | Goal | Status |
|------|-------|------:|------|--------|
| T0 | `SAFEIMM_DISPL` | 241 lea/mov displ only (drop all `kind=imm`) | Logo smoke | **Staged** `7F90405D…` aside — **not** active daily EXE |
| T0b | drop `A7xxxx` from FULL | 318−36 | Isolate offset-table | Superseded by T0 (A7 all imm) |
| T1fix | grow login `4E5xxx` displ if T0 green | +selective | Cash/tail login | After T0 logo |
| Never | dump full ×318 again | 318 | Known boot-red | **Forbidden** |

### Stage artifacts

| Path | Note |
|------|------|
| `tools/cd_expand/out/phase1_imm_sites_safeimm_displ.json` | displ×241 |
| `tools/cd_expand/_make_safeimm_displ.py` | MINIMAL → SAFEIMM copy |
| `tools/cd_expand/out/BeiDou.cd64_SAFEIMM_DISPL_20260804_113801.exe` | SHA `7F90405D…` |
| `CD64_TEST\BeiDou.cd64_SAFEIMM_DISPL_20260804_113801.exe` | Same aside; do **not** rename over `BeiDou.exe` until logo OK |

```bat
cd E:\pro\BeiDou-ijl15\tools\cd_expand
python _make_safeimm_displ.py
:: or: phase2_patch_copy.py --kind displ --sites out\phase1_imm_sites_safeimm_displ.json
```

Output only under `tools/cd_expand/out\` + CD64_TEST aside. Never Client_1 until T2–T3 green **and** imm path proven.

## User now (T2 / T3 on MINIMAL)

Keep active `BeiDou.exe` = MINIMAL. Exact steps: `CD64_ENTER_MATRIX.zh-CN.md`.

1. Close all `BeiDou.exe`.
2. Start only `BeiDou-Client_CD64_TEST\BeiDou.exe` (`3C167A9B` + `8EF3A251`).
3. **T2:** classic hat/weapon + one cash equip; stay ≥30s.
4. **T3:** relog; classic+cash still equipped.
5. On red: stay MINIMAL; keep log+dump; then consider SAFEIMM logo — **no** FULL×318, **no** Client_1.

## Status

| Item | Status |
|------|--------|
| Imm diagnosis brief | **Done** |
| SAFEIMM_DISPL stage | **Done** (`7F90405D…`, 241/241) |
| SAFEIMM logo smoke | Open — after T2/T3 or on cash-path fail |
| Native A2 sidecar removal | Design only — blocked on T2–T3 |
| Promote Client_1 | **Blocked** |
