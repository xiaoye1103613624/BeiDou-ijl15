# Addon / select→enter crash — DO NOT reintroduce

Mirror of Client_1 note. Canonical live path:

`E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1\ADDON_ENTER_CRASH_AVOID.md`

**SOP (出问题强制流程):** [`CLIENT_PATCH_SOP.md`](./CLIENT_PATCH_SOP.md) — rollback → IDA/log/dump → zero-growth/late-hook patch → enter A/B before live.  
**Architecture (design):** [`ADDON_EXTEND_EQUIP_ARCHITECTURE.md`](./ADDON_EXTEND_EQUIP_ARCHITECTURE.md)  
**083←095 adapt plan:** [`ADDON_083_ADAPT_FROM_095.md`](./ADDON_083_ADAPT_FROM_095.md)  
**095 IDA notes:** [`ADDON_095_IDA_BODY_PART_NOTES.md`](./ADDON_095_IDA_BODY_PART_NOTES.md)  
**STATS source recovery:** [`ADDON_STATS_BASELINE_RECOVERY.md`](./ADDON_STATS_BASELINE_RECOVERY.md)  
**Addon row3 pocket/Si:** [`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md)  
**ROW3 wear/unequip (packet-only):** [`ADDON_ROW3_WEAR_UNEQUIP.md`](./ADDON_ROW3_WEAR_UNEQUIP.md)  
**Prior FIXES:** [`ADDON_ROW3_FIXES.md`](./ADDON_ROW3_FIXES.md) — bak `60E3E02F…`

Last updated: 2026-08-07 (Asia/Shanghai) — **HARD RULE: 改客户端必须对照 IDA**. **HARD RULE ~20:54: 禁止改原版地址**（CD field/layout imm）— 扩栏只在 ijl15。 Avoid #38–#62. SOP: [`CLIENT_PATCH_SOP.md`](./CLIENT_PATCH_SOP.md).

## avoid #62 — EnsureLayer wrong RVA 0x34C80 (2026-08-07 ~21:00)

- **Symptom (FIX2 `322A5AD5`):** Addon `LAYER created` / tip/unequip still live; 扩展栏旧问题复发；新戒 −52/−53 悬停无 tip；以为 EnsureLayer 已关。
- **Evidence:** OnTick `E8` → **`0x34CE0`**（SEH `55 8B EC 6A FF`）；`Addon LAYER created` 在该函数内。**`0x34C80`** = `LogInitOnce`（`EquipAddon Init` 串），BOOTSAFE/FIX2 误打 `33 C0 C3`。
- **Bad:** 零增长「关 Layer」却打错 RVA；再叠 Origin NOP / `hooks@599A0=C3` 当 Layer 已关。
- **Good live now:** `514D9D68` `ENSURELAYER_FIX3` — 恢复 `34C80=55 8B EC`；真 EnsureLayer `@34CE0=33 C0 C3`；保留 Origin je NOP `@5911A`；恢复 `hooks@599A0=80`；stub `53 E8`；size 1447424；vs FA 仅 9 字节。脚本：`tools/patch_client1_bootsafe_ensurelayer_fix3.ps1`。
- **Forbidden:** 再把 `0x34C80` 当 EnsureLayer；无 call-xref 就打 `xor eax,eax; ret`。

## avoid #61 — Gr2D `E_FAIL` primary; null-Input AV secondary (2026-08-06)

- **IDA:** `BEC33C` written only in `InitializeInput` ctor `@0x9F821F`. Order in `CWvsApp::Init@9F5239`: ResMan → **Gr2D `@9F7A3B`** → **Input `@9F7CE1`**. Run `@9F5C50` (WinMain after Init) calls `@59B2D2` with `ecx=[BEC33C]` — no null check.
- **Live proof (softfail `184D47F0`):** `Gr2D pre BF0CC0≠0 BF14EC=0` → `throw_hr A5FDF2 hr=0x80004005` (IWzGr2D::Initialize / DX8 device) → Init SEH `E06D7363` → without ExitProcess, Run AV `@59B2D9` read `0x8`.
- **Timeline:** last `Gr2D END` **07:00:32**; in-game ntdll AV **07:05:38**; thereafter machine-wide Gr2D fail (incl. Client_1). **Not** SETITEM/`38AF6891`.
- **Host recovery:** full reboot; keep GameViewer Virtual Display disabled; optional `d3d8to9` (`d3d8.dll.d3d8to9_v1.15.1_aside`); sheet `CD64_TEST\BOOT_GR2D_E_FAIL_RECOVERY.txt`.
- **Plugin hygiene (source, size-safe deploy later):** PcCreateObject hook must **return HRESULT**; LoadTrace soft-fail ExitProcess on Init SEH; null-guard `@59B2D2`. Fat softfail golden OK for diag only — live stay **1447424**.
- **Forbidden:** treat BEC33C AV as equip regression; EXE null-guard caves; leave Init SEH swallow → zombie Run.

## avoid #60 — EXE TSec null-guard cave forbidden; Gr2D flash ≠ equip (2026-08-06)

- **User rule:** 「不要改原版地址」— TSec `@0x42873D` JMP→`@0xAEF640` cave **rolled back**; CD64_TEST EXE back **`ACF10F63`**.
- **Flash symptom:** `InitializeGr2D BEGIN` AV `@0x0059B2D9` `read 0x8` `ecx=0` — IDA `mov ecx,[BEC33C]; call 59B2D2` (CInputSystem NULL).
- **Bisect:** FA/`ACF`、`15602DFF`/`ACF`、`38AF`/`ACF`、**Client_1** 同崩 → **非** SETITEM/APPEND 门闩；上一段好机 `Gr2D END` 07:00:32。
- **TSec NULL (#57):** 只许 ijl15 Detour/Seh 早退；禁止再写 EXE cave。
- **Good pair now:** DLL **`38AF6891`** + EXE **`ACF10F63`**（等 DX/会话恢复后再验穿戴）.

## avoid #59 — SetItem sidecar first-gate left at −61..−56 (2026-08-06)

- **Symptom (CD64_TEST after APPEND_TSEC `15602DFF`):** 徽章/−54、图腾1/−55、副手/−62 **不显示、可连穿**；机器人/−60、心脏/−61、纹章/−59、图腾2–4 **正常**。
- **Evidence:** `equipaddon_debug.log` — wear send-only −54/−55 OK but **no** `Sidecar SetItem`; −60/−61 have SetItem. Server `login addon seats` already holds −54/−55/−62. Binary SetItem hook `@file 0x3733E`: `lea eax,[ebx+61]; cmp eax,5` ⇒ only **−61…−56**.
- **Bad:** 只扩 PreferSend/GetItem/第二道 `CA/08` 而漏 SetItem **第一道** 门闩；再喷 PreferSend。
- **Good CD64_TEST now:** ijl15 **`38AF6891`** `ADDON_SETITEM_54_62` — first gate `+62/≤8` (−62…−54)；EXE **`3BC0E37C`** TSec 保留。
- **Forbidden:** 再部署 `15602DFF` 当「完整 APPEND」；无日志对照 android SetItem 就改占用 UX。

## avoid #58 — APPEND cash LoginAllow/CashApply must not admit BP54–62 (2026-08-06)

- **IDA:** cash decode `slot = -(100+esi)` @`0x4E5DE4`; normal lea `+0E7h` @`0x4E5D55`; cash lea `+287h` @`0x4E5E18`; **0xE7+54×8 ≡ 0x287+2×8** (badge≡face), **55≡eye**.
- **Bad:** APPEND `60A3E54F` LoginAllow_Cash + CashApply both treated 54–62 like normal → cash path can decode/assign addon seats / feed NULL into TSec (avoid #57 family).
- **Good CD64_TEST now:** ijl15 **`15602DFF`** `ADDON_APPEND_TSEC_GUARD` — LoginAllow_Cash **≤51 only**; CashApply **never sidecar** (`cmp esi,7Fh` always-jb); EXE **`3BC0E37C`** TSec null-guard `@0xAEF640`.
- **Forbidden:** re-widen LoginAllowB / CashApply to 54–62; deploy unguarded `60A3E54F` without TSec cave; promote to Client_1 before enter A/B green.

## avoid #57 — APPEND_ONLY TSecType NULL → 启动后未响应 (2026-08-05)

- **Symptom:** Init/Logo 常绿 → 随后窗口 **未响应**；VEH `0xC0000005` @ **`0x00428743`** `read target=0x00000014`（`stage=set_stage END`）；进程不退。
- **Root:** `TSecType_long_GetData` @`0x0042873D`：`this=pItem+0x0C` 且 **`pItem==NULL`** ⇒ `this=0x0C` ⇒ 读 `[0x14]`。与 APPEND_ONLY `60A3E54F` 扩展座 sidecar/GetItem 占用不同步同族。
- **Bad:** 继续盖 APPEND 不修空指针；胖链 `F3411A69`/`GHOSTHEAL` 1512960 盖 live（#55）。
- **Good CD64_TEST (2026-08-06):** EXE **`3BC0E37C`** (TSec `this<0x100` → 0 @cave `0xAEF640`) + DLL **`15602DFF`** APPEND_TSEC_GUARD；bak `*_APPEND_TSEC_GUARD_*` ← FA/`ACF10F63`.
- **Prior rollback:** FA `FA26021C` + `ACF10F63`；APPEND bak `*_TSEC_NULL_ROLLBACK_*`。
- **Forbidden:** 无 IDA 再喷 PreferSend/sidecar；把未响应当「GameData 慢」忽略 VEH；盖 Client_1 未进图绿戳记。

## avoid #56 — EXE CD field-imm / layout-sync mega loop (2026-08-04 ~20:07–20:54)

- **User rule:** 「不要改原版地址，自定义的可以改」.
- **Bad family:** MINIMAL→LAYOUT_SYNC→APPLY_ABS→SKILLMAP_5FF→BATCH_KNOWN→CTOR_FIELDS (`6CBFFFE6`) — skill-cool/`+0x4CB`/`+0x45B` 等均为 **原生字段偏移同步**，不是新冷却功能；进图后 AV `@46D2F9` 读 `CD+0x45B` null。
- **Good CD64_TEST now:** EXE **`ACF10F63`** + DLL **`FA26021C`**（与 Client_1 同）；bak `*_PLUGIN_ONLY_ROLLBACK_*`.
- **Forbidden:** 再跑 `tools/cd_expand/_make_*` 往 live EXE 写 field displ；FULL×318 / SAFEIMM daily；把 skill-cool 当新协议去改服务端。
- **Next:** 扩栏只改 ijl15 sidecar/hook（FA 系 1447424）；need IDA on **plugin** caves，不是改原版 CD imm。

## avoid #55 — VS Clean+Rebuild / growth DLL destroys EXECAVE stub (2026-08-04 ~17:37→18:46)

- **Symptom:** Logo OK → `set_stage` `0xC0000005` @ `ijl15+0xAB30C` → Init 256 → **黑屏**（非未响应）.
- **Bad:** `B464E1BC` / 1541120 (VS Clean+Rebuild); also `007D59FD` WEAR_FIX growth — stub ≠ `53 E8`.
- **Good Client_1:** `3642FBDD` AUX62_ONLY or `FA26021C` PEER_ASLR_V2 — size **1447424**, stub `@AB30C`=`53 E8`.
- **WEAR_FIX growth:** stage only as `golden\ijl15.ADDON_WEAR_FIX_20260804_CD64_TEST_ONLY.dll` (`2B65633B`) — **CD64_TEST only**; pair EXE `3C167A9B`. **Never** Client_1.
- **Forbidden:** VS Clean+Rebuild onto EXECAVE Client_1; calling growth DLL “Client_1-safe” without stub verify.

## Live — FROZEN ADDON_AUX62_ONLY 3642FBDD (2026-08-04 ~16:14) — real expand gate

- **ijl15:** `3642FBDD…` / **1447424** — stamp `ADDON_AUX62_ONLY_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **vanilla CD** (not CD64)
- **Lock:** [`CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md`](./CLIENT_1_REAL_EQUIP_EXPAND_FREEZE.lock.md) + Client_1 folder copy
- **Forbidden without design gate:** sidecar ZEROGROWTH / PREFER / TOTEM1 mega; CD64 overwrite live; enter-red stamps
- **Bak FA:** `ijl15.dll.bak_before_AUX62_ONLY_20260804_*` (`FA26021C`)
- **If enter red:** restore bak FA immediately. Do not expand PreferSend.
- **Real expand work:** `BeiDou-Client_CD64_TEST` only (`3C167A9B` + `8EF3A251`)

## Prior — DEPLOYED ADDON_AUX62_ONLY 3642FBDD (2026-08-04 ~16:05) — await enter A/B

- **ijl15:** `3642FBDD…` / **1447424** — stamp `ADDON_AUX62_ONLY_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Bak FA:** `ijl15.dll.bak_before_AUX62_ONLY_20260804_*` (`FA26021C`)
- **Change (一座一改):** ExpectedBp `mov eax,10→62` @file `0x345E3` + 24 aux `cmp …,0Ah→3Eh` (WearBag/IsMainPocketSub/PreferSendChangeOnly). PreferSend **left 56–61**. Totem/badge/sidecar-min **untouched**.
- **Already on FA:** GetItem −62, PacketSlot ≤62, ForBp PIC bp62, AbleToWear/dock 62, paint seat 62
- **Verify:** cold select→enter A/B; aux 134/135 → −62 only; unequip; shield −10 OK. Totem1/badge = next 继续.
- **If red:** restore bak FA immediately. Do not expand PreferSend.

## Prior — ROLLED BACK to FA26021C (2026-08-04 ~15:55) — TOTEM1 REGRESSION avoid #54

- **ijl15:** `FA26021C…` / **1447424** — stamp `ADDON_AUX_BP62_PEER_ASLR_V2_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **DLL swapped?** **No** — live was still `76C7D7FC` until this rollback (mtime ~15:42).
- **Restored from:** `ijl15.dll.bak_before_TOTEM1_BP55_ZG_20260804_154307` (`FA26021C`)
- **Aside REGRESSION:** `ijl15.dll.aside_REGRESSION_TOTEM1_BP55_ZG_76C7D7FC_20260804_155454.bak` — **FORBIDDEN redeploy**
- **User bugs on 76C7:** 图腾1–4 不显示、可多穿（全打到 bp55）、辅武不显示
- **Log evidence (`equipaddon_debug.log` ~15:51):** multi totem dblclick all `-> bp=55` WEAR send-only; **no** `Sidecar SetItem slot=-55`; aux later `bag_dbl_main … bp=10`; PreferSend 55–61 broke totem2–4 + aux paint path
- **Do not redeploy `76C7D7FC`.** Freeze 一座一改 — no PreferSend range expand without isolated A/B.
- **Next:** 冷启选角进图 A/B on FA；预期 totem2–4/辅武 回到 FA 基线显示

## Prior — DEPLOYED then REGRESSION TOTEM1_BP55_ZG 76C7D7FC (2026-08-04 ~15:43→15:55) — avoid #54

- **ijl15:** `76C7D7FC…` / **1447424** — stamp `ADDON_TOTEM1_BP55_ZG_20260804` — **REGRESSION blank/multi**
- **Base:** FA26021C; PreferSend/sidecar **55–61** (intended one-seat totem1)
- **Tool:** `tools/patch_sidecar_totem1_bp55_zerogrowth.ps1`
- **Do not redeploy `76C7D7FC`.**

## Prior — ROLLED BACK to FA26021C (2026-08-04 ~15:30) — STOP CASCADE

- **ijl15:** `FA26021C…` / **1447424** — stamp `ADDON_AUX_BP62_PEER_ASLR_V2_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Restored from:** `ijl15.dll.bak_before_SIDECAR_54_62_ZEROGROWTH_*` (`FA26021C`)
- **Aside REGRESSION:** `ijl15.dll.aside_REGRESSION_PREFER_AUX62_44251097_*.bak` — **FORBIDDEN redeploy**
- **Also forbid:** `021C016A` ZEROGROWTH (incomplete PreferSend cascade)
- **Why:** User live bugs on `44251097` — all totem blank, endless wear, badge blank, aux show/unequip ghost. PreferSend 54–62 + incomplete paint/GetItem; ExpectedBp 62 without matching unequip PacketSlot. **Original logic was not wrong** — cascade broke display/sync.
- **Freeze:** totem2–4 and other seats that painted on FA — do not retouch without isolated bak+A/B. No mega-patch without user OK.
- **Next:** 冷启选角进图 A/B on FA；预期 totem1/badge 可能撞点装，但应有显示；再 **一座一改**

## Prior — REGRESSION ADDON_SIDECAR_PREFER_AUX62 (2026-08-04 ~15:21→15:30) — avoid #53

- **ijl15:** `44251097…` / **1447424** — stamp `ADDON_SIDECAR_PREFER_AUX62_20260804` — **REGRESSION blank UI**
- **Bak at deploy:** `ijl15.dll.bak_before_SIDECAR_PREFER_AUX62_*` (`021C016A…`)
- **User bugs:** (1) 图腾全不显示 (2) 可无限穿 (3) 徽章不显示 (4) 辅武显示但卸不掉/换装旧图标
- **Do not redeploy `44251097`.**

## Prior — DEPLOYED ADDON_SIDECAR_54_62_ZEROGROWTH (2026-08-04 ~15:05) — incomplete PreferSend — avoid #52

- **ijl15:** `021C016A…` / **1447424** — stamp `ADDON_SIDECAR_54_62_ZEROGROWTH_20260804`
- **Bak:** `ijl15.dll.bak_before_SIDECAR_54_62_ZEROGROWTH_*` (`FA26021C…`)
- **Gap:** Get/Set/Apply 54–62 OK, but PreferSend inline + ExpectedBp aux=10 left → badge/totem ghosts + aux bp=10
- **Also forbid redeploy** after STOP CASCADE (led to PreferSend mega-patch).
- **Growth golden (not live):** `32C55435…` / 1520640 — PreferSendChangeOnly full

## Prior — ROLLED BACK to FA26021C after PEER_COMPLETE cascade (2026-08-04 ~14:44) — avoid #50

- **ijl15:** `FA26021C…` / **1447424** — stamp `ADDON_AUX_BP62_PEER_ASLR_V2_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Aside:** `ijl15.dll.aside_PEER_COMPLETE_CASCADE_*` (`EADEF4A8…`) — **FORBIDDEN redeploy**
- **Root cause:** COMPLETE remapped aux→62 but ApplyEquip still 56–62 + wear_fn; native `aEquipped[54/55]` ≡ cash face/eye (−102/−103); aux wear_fn OOB multi-ghost. See [`ADDON_ALL_OR_NOTHING_20260804.md`](./ADDON_ALL_OR_NOTHING_20260804.md)
- **Golden fix (growth):** `golden\ijl15.ADDON_SIDECAR_54_62_20260804.dll` `32C55435…` / **1520640** — sidecar 54–62 + PreferSendChangeOnly; superseded as live candidate by ZEROGROWTH `021C016A`
- **Server:** omit=`false` kept; aux −10 stray strip in InventoryManipulator (rebuild server)

## Prior — DEPLOYED ADDON_AUX_BP62_PEER_ASLR_V2 FA26021C (2026-08-04 ~13:59) — await enter A/B

- **ijl15:** `FA26021C…` / **1447424** — stamp `ADDON_AUX_BP62_PEER_ASLR_V2_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Bak:** `ijl15.dll.bak_before_AUX_BP62_PEER_ASLR_V2_20260804_20260804_135927` (`A0BE14C9…`)
- **Pre-deploy verify:** jmp-back → `0x344E9` (`85 C9`); cave no bare `0x10xxxxxx`; stub `@0xAB30C` `53 E8`; reloc `@0x344E5` type=0
- **Server:** `GREEN_ENTER_OMIT_AUX62=true` **KEEP** — do **not** clear omit; do **not** restart server for omit
- **Guard/lock:** allow `FA26021C` (+ `A0BE14C9` rollback only); forbid `DF3AF2F1`/`73559AC5`/`0F244A5D`/`3B864717`
- **Next:** user 冷启选角进图 A/B；**红** → rollback bak `A0BE14C9`；**绿** → 再清 omit 穿辅武

## Prior — ROLLED BACK to A0BE14C9 after PEER_ASLR DF3AF2F1 ENTER-RED (2026-08-04 ~13:45)

- **ijl15:** `A0BE14C9…` / **1447424** — restored from `bak_before_AUX_BP62_PEER_ASLR_20260804_20260804_134139`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Aside crash:** `ijl15.dll.aside_CRASHED_AUX_BP62_PEER_ASLR_DF3AF2F1_ENTER_RED_20260804_134526.bak` (`DF3AF2F1…` / 1447424)
- **Boot (~13:44:14):** quiet ON → select→enter `0xC0000005` @ `ijl15+0x344E8` abs=`0x563A44E8`; `EAX=ECX=0x564B4430` (= **ASLR-correct** `base+0x144430` sidecar[56]); read target=`0xC7645C29`; stack CharData `sub_4E592D`
- **Root cause (honest):** **not PIC failure**. `EmitCaveJmpBack` computed rel32 **after** `Add(E9)` → jmp short by 1 → landed on RVA `0x344E8` (INT3/NOP pad over ClearSidecar abs, HIGHLOW `@0x344E5` still live) instead of `0x344E9` (`test ecx`). Same EIP family as PEER `73559AC5` (that build also had unrebased ForBp). Omit still ON.
- **Server:** `GREEN_ENTER_OMIT_AUX62=true` **KEEP**
- **Forbidden:** redeploy `DF3AF2F1` / `73559AC5` / `0F244A5D` / `3B864717`; clear omit; `EmitCaveJmpBack` Count-after-E9 pattern

## Prior — STAGED ADDON_AUX_BP62_PEER_ASLR_V2 (2026-08-04 ~13:50) — golden only

- **Golden:** `golden\ijl15.ADDON_AUX_BP62_PEER_ASLR_V2_20260804.dll` — `FA26021C…` / 1447424
- **Script:** `tools/patch_aux_bp62_peer_aslr_v2.ps1`
- **Fix:** `EmitCaveJmpBack` from=`caveVa+Count` **before** E9; ClearSidecar abs HIGHLOW `@0x344E5` neutralized; pad NOP; PIC + `.data` bp62 retained
- **Verify:** Clear jmp-back → RVA `0x344E9` (`test ecx`); cave scan no bare `0x10xxxxxx`
- **Promoted to live ~13:59** (see Live)

## Prior — DEPLOYED then ENTER-RED ADDON_AUX_BP62_PEER_ASLR DF3AF2F1 (2026-08-04 ~13:41→13:44)

- **ijl15:** `DF3AF2F1…` / **1447424** — stamp `ADDON_AUX_BP62_PEER_ASLR_20260804` — **ENTER-RED** (avoid #49)
- Rolled back → `A0BE14C9` (see Live)

## Prior — RESTORED A0BE14C9 after POTENTIAL_GRADE_UI black screen (2026-08-04 ~13:33)

- **ijl15:** `A0BE14C9…` / **1447424** — restored from `bak_before_AUX_BP62_PEER_ASLR_20260804_*`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Aside black:** `ijl15.dll.aside_BLACK_POTENTIAL_GRADE_UI_3B864717_*.bak` (`3B864717…` / **1508352**, stamp `POTENTIAL_GRADE_UI_20260804`)
- **Boot (~13:27):** quiet ON; Logo ctor OK → `set_stage` SEH **`0x80000003`** (STATUS_BREAKPOINT) → `CWvsApp::Init returned 256` → **black hang** (process alive); **no** new CrashDump/WER today
- **Verdict:** **NOT** PEER_ASLR — live at black was Grade UI overwrite, not `DF3AF2F1`
- **Forbidden:** redeploy `3B864717` / growth Grade UI to Client_1; redeploy `73559AC5` / `0F244A5D`; clear omit

## Prior — STAGED ADDON_AUX_BP62_PEER_ASLR (2026-08-04 ~12:35) — golden only; enter A/B pending

- **Golden:** `golden\ijl15.ADDON_AUX_BP62_PEER_ASLR_20260804.dll` — `DF3AF2F1…` / 1447424 (PIC caves; bp62 ZRef `.data` `@0x14B930`)
- **Now live** as of ~13:41 (see Live section)
- **If enter red:** rollback to `A0BE14C9`; do **not** clear omit; do **not** redeploy `73559AC5`

## Prior — ROLLED BACK after ADDON_AUX_BP62_PEER ENTER-RED (2026-08-04 ~12:11)

- **ijl15:** `A0BE14C9…` / **1447424** — restored from `bak_before_AUX_BP62_PEER_20260804_114908`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged**
- **Aside crash:** `ijl15.dll.aside_CRASHED_AUX_BP62_PEER_ENTER_RED_73559AC5_*.bak` (`73559AC5…` / 1447424)
- **Boot:** quiet ON → select→enter `0xC0000005` @ `ijl15+0x344E8` (ClearSidecar after ForBp cave); `EAX=0x10144430` preferred sidecar[56] while module ASLR base `0x7BFA0000`; stack CharData `sub_4E592D`; WER AppHang 12:06:43
- **Root cause:** cave absolutes `0x10144270` / `0x100AB350` **no `.reloc`** → unrebased ZRef ptr under ASLR (omit still ON — not wire −62)
- **Server:** `GREEN_ENTER_OMIT_AUX62=true` **KEEP** — do not clear
- **Forbidden:** redeploy `73559AC5` / `0F244A5D`; clear omit; put mutable ZRef / abs VA caves in `.text` slack without reloc or RIP-relative/`dll_base+RVA`

## Prior — ROLLED BACK after ADDON_AUX_BP62 ENTER-RED (2026-08-04 ~10:48)

- **ijl15:** `A0BE14C9…` / **1447424** (JG_ONLY + EXECAVE) — restored from `bak_before_ADDON_AUX_BP62_20260804`
- **EXE:** `ACF10F63…` / 8503296 — **unchanged** (no dump blame on EXE)
- **Aside crash:** `ijl15.dll.aside_CRASHED_ADDON_AUX_BP62_ENTER_RED_20260804_*.bak` (`0F244A5D…` / 1447424)
- **Boot:** quiet ON @10:47:18 → select→enter flash @10:47:30 `0xC0000005` write `@0x7AE09404` (EAX=`FFFFFFFF`); **not** `0xC0000409`
- **Server:** `isGreenEnterWireOmit` restored for `−62/−162` (must rebuild+restart). Do not clear omit on vanilla CD again
- **User:** **先确认能进图** before any aux display rework

## CRASHED — ADDON_AUX_BP62 (2026-08-04 ~10:47) — do not redeploy — ENTER-RED

- **Stamp:** `ADDON_AUX_BP62_20260804`
- **SHA256:** `0F244A5D…` **Size:** `1447424` (zero-growth binary patch on JG_ONLY — size alone ≠ safe)
- **Change:** jg-allow/IsSidecar/shoulders range 61→62; server omit cleared so CharInfo sends −62
- **Root cause:** raised sidecar jg-allow to BP62 **without** CD64 / apply-max; CharInfo −62 + incomplete apply → ZRef release AV (`sub_428A50` / CharData decode `sub_4E592D` stack). Vanilla decode only `SetItem` when slot≤51; sidecar path for 62 unsafe on stock CD
- **Forbidden:** redeploy `0F244A5D`; clear `isGreenEnterWireOmit` for −62 on Client_1 without enter A/B on staged copy; bundle BP62 jg with omit-clear on non-CD64 EXE

## Prior — RESTORE_DUALTIP + EXECAVE (**CLOSED** 2026-08-04; user: 点装 tip OK)

- **Stamp:** `RESTORE_DUALTIP_20260804` (EXE only; ijl unchanged) — **tip restore CLOSED**
- **ijl15:** `A0BE14C9…` / **1447424** (JG_ONLY + EXECAVE stub RVA `0xAB30C` = `53 E8…`, tramp `@0xAB340`; GetItem `cmovbe`→NOP `@0x34845` normal-only) — **zero growth**
- **EXE:** `ACF10F63…` / 8503296 — **`@7FEA9A`=`75 4C`**; EXECAVE `@77748c`→`@AEF602` **re-verified intact** after dual-tip restore; slab still `push 0x640`
- **Root cause of missing 点装 tip:** prior `ADDON_HOVER_RELOG_NODUAL` nopped dual-tip (`75 4C`→`90 90`) — **forbidden** (product: do not alter vanilla CUIEquip tip)
- **Bak:** `BeiDou.exe.bak_before_RESTORE_DUALTIP_20260804` (=`7E240917` NODUAL); golden `golden\BeiDou.RESTORE_DUALTIP_20260804.exe`
- **Next focus:** CD64_TEST **T2–T3** (classic+cash / relog) after **T1 GREEN**; SAFEIMM_DISPL staged aside; not Client_1 promote; do not fight Track A aux BP62 on Client_1
- **Forbidden:** re-nop `@7FEA9A`; LoginPersist Early Get/Set; +70KB tip relink; `GROWTH_TIP_REEN`

## Prior — ADDON_UI_LOGOUT_EXECAVE (user OK 2026-08-04)

- **Stamp:** `ADDON_UI_LOGOUT_EXECAVE_20260804`
- **ijl15:** `A0BE14C9…` / **1447424** (JG_ONLY + EXECAVE stub RVA `0xAB30C` = `53 E8…`, tramp `@0xAB340`) — **zero growth**
- **EXE:** `7E240917…` / 8503296 — NODUAL `@7FEA9A` was kept then (**superseded** by RESTORE_DUALTIP); `@77748c` → cave `@AEF602` (`add eax,0xAB30C`) → stub then `A041FF` iff CharData≠0 → `777499`
- **User:** 进图 + 小退登录装备栏残留 **已绿**（「可以了继续」）
- **Guard:** `tools/guard_client1_execave.ps1` + Client_1 `IJL15_DO_NOT_OVERWRITE.txt` locks EXECAVE — **never** `GROWTH_TIP_REEN` `D6D01354`
- **Bak / restore:** golden `ijl15.ADDON_UI_LOGOUT_EXECAVE_20260804.dll` + live EXE bak `BeiDou.exe.bak_ADDON_LOGIN_JG_ONLY_20260804` (=`7E240917`); aside GROWTH `ijl15.dll.aside_GROWTH_REEN_broke_EXECAVE_*`
- **Phase2 live (2026-08-04 ~09:55):** `A0BE14C9…` tramp `@0xAB340` (broken `07435AFC` clobbered stub — do not redeploy)
- **Forbidden:** `GROWTH_TIP_REEN` / `E88CEBBE` / `1DCCEA52` / `05B67BD9` / TOTEM2 / TIP / SENDBUSY / +70KB relink / CD64 promote without T2–T3 / FULL imm×318 / ENTER-RED `0F244A5D` / **NODUAL re-apply**

## Prior — HOVER enter OK (user confirmed 2026-08-04)

- **ijl15:** `ADDON_HOVER_GETITEM_ONLY_20260803` — SHA `B42BC601…` / **1447424** (from `bak_ADDON_UI_LOGOUT_IDA_20260803`)
- **EXE:** `ADDON_HOVER_RELOG_NODUAL` `B37F0269…` — kept
- **User:** **进图 OK** on HOVER; then zero-growth EXECAVE deployed (above)
- **Aside (prior):** `ijl15.dll.aside_CRASHED_UI_LOGOUT_IDA_ENTER_RED_20260803_*.bak` (`E88CEBBE…` / 1521664) — do not redeploy

## Prior — RESTORED HOVER after UI_LOGOUT_IDA enter-red (2026-08-03 ~23:59)

- **ijl15:** `ADDON_HOVER_GETITEM_ONLY_20260803` — SHA `B42BC601…` / **1447424** (from `bak_ADDON_UI_LOGOUT_IDA_20260803`)
- **EXE:** `ADDON_HOVER_RELOG_NODUAL` `B37F0269…` — kept (dump/WER blame `ucrtbase`, not EXE)
- **Aside crash:** `ijl15.dll.aside_CRASHED_UI_LOGOUT_IDA_ENTER_RED_20260803_*.bak` (`E88CEBBE…` / 1521664)
- **WER:** 23:58:50 / 23:58:53 BEX `0xC0000409` @ `ucrtbase+0x9d132`; dump `BeiDou.exe.16132.dmp` embeds stamp
- **Boot:** `ShoulderSlots` → `LazyCompat` → quiet ON @23:58:34 — **no** LoginPersist — then enter flash (~16s)
- **User:** 先确认能进图；退登录 UI 暂搁置直到不涨体积 / 晚装钩方案
- **Policy:** no live UI-teardown until zero-growth (EXE `@777494` cave or HOVER binary trampoline) + enter proof on test copy

## CRASHED — ADDON_UI_LOGOUT_IDA (2026-08-03 ~23:58) — do not redeploy — ENTER-RED

- **Stamp:** `ADDON_UI_LOGOUT_IDA_20260803`
- **SHA256:** `E88CEBBE…` **Size:** `1521664` (+74240 vs HOVER)
- **Markers:** `DestroyGameUI_A041FF` + stamp present; **no** `EquipAddonLoginPersist`; **lost** `ADDON_HOVER`; has `enableEquipGrowthTip` config strings (HOVER lacks)
- **PE delta:** `.text` +58368 / `.rdata` +11776 / `.reloc` +3584; exports=11 same; `.detourc` RS unchanged
- **Root cause:** (1) full MSBuild size-jump GS family — clean dllmain insufficient; (2) A041FF `ATTACH_HOOK` at EnsureHooks/first-field unproven enter-safe — prefer EXE jmp `@777494` cave or late zero-growth install
- **Bak that was pre-crash HOVER:** `ijl15.dll.bak_ADDON_UI_LOGOUT_IDA_20260803` (`B42BC601…`)

## Prior live — HOVER after UI_LOGOUT_CLOSE enter-red (2026-08-03 ~23:36)

- **ijl15:** `ADDON_HOVER_GETITEM_ONLY_20260803` — SHA `B42BC601…` / **1447424**
- **EXE:** `ADDON_HOVER_RELOG_NODUAL` `B37F0269…` — kept (not fault module)
- **Aside crash:** `ijl15.dll.aside_CRASHED_UI_LOGOUT_CLOSE_ENTER_RED_20260803_*.bak` (`1DCCEA52…` / 1509888)
- **User:** 先确认能进图 before any further UI-teardown deploy — **confirmed enter OK**, then IDA deploy above
- **Policy:** no live UI fix until IDA postmortem exists + clean dllmain (prefer binary on HOVER size 1447424; rebuild only after LoginPersist-proof)

## CRASHED — ADDON_UI_LOGOUT_CLOSE (2026-08-03 ~23:34) — do not redeploy — ENTER-RED

- **Stamp:** `ADDON_UI_LOGOUT_CLOSE_20260803`
- **SHA256:** `1DCCEA52…` **Size:** `1509888` (+62464 vs HOVER)
- **WER:** 23:34:42 / 23:35:27 BEX `0xC0000409` @ `ucrtbase+0x9d132`; dumps `BeiDou.exe.3632.dmp` / `33244.dmp` embed stamp
- **Boot:** quiet ON → enter flash (not logout VEH)
- **Markers:** `EquipAddonLoginPersist` + `DestroyGameUI_A041FF` present (HOVER has neither LoginPersist)
- **Root cause:** (1) `Release_enter_frozen\dllmain.obj` polluted with LoginPersist Early — same GS enter-red family as #35; (2) OnTick force-`A041FF` violates IDA lifecycle (secondary; not this EIP)
- **IDA:** `set_stage@777347` LABEL_16 calls `A041FF@777494` **only if** CharData ZRef.p≠0 via `425D0B`; `A041FF` is `__thiscall(ecx=BE7918)`; xrefs only `777494` + `A0680F`. See postmortem.
- **Next (staged only):** Addon-only destroy inside A041FF hook same ecx; CharData-null → Addon DestroyLayer only; **never** OnTick force A041FF; fix frozen dllmain before any relink; prefer binary patch keep 1447424
- **Bak that was pre-crash HOVER:** `ijl15.dll.bak_ADDON_UI_LOGOUT_CLOSE_20260803` (`B42BC601…`)

## Prior — HOVER + RELLOG NODUAL (2026-08-03 ~23:20)

- **ijl15:** `ADDON_HOVER_GETITEM_ONLY_20260803` — SHA `B42BC601…` / **1447424** (restored after `GROWTH_TIP_REEN` `D6D01354` overwrite @23:18; aside `ijl15.dll.aside_GROWTH_REEN_overwrote_HOVER_*`)
- **EXE:** `ADDON_HOVER_RELOG_NODUAL_20260803` — SHA `B37F0269…`; bak `BeiDou.exe.bak_ADDON_HOVER_RELOG_NODUAL_20260803` (`4934A0CF…`)
- **EXE patch:** `sub_7FE9BB` `@0x7FEA9A` `75 4C`→`90 90` — skip `sub_8EBA5B` when both normal+cash non-null (single tip = normal only)
- **ijl15 patches:** GetItem `cmovbe`→NOP `@0x34845`; SehGetItem skip-cash `ja 0x1A` `@0x36C68`. SetItem cash alias KEPT. No Early / no tip-clean relink
- **Post-relog evidence:** `client_boot` 22:46:08 `0xC0000005` abs=`0x8EBC3C` read `@0x500`. EnsureHooks does **not** restore GetItem cash alias
- **Retest:** 进图 → 悬停点装 → 小退 → 再进 → 再悬停点装
- **Phase2:** not live
- **Rollback:** EXE bak `…NODUAL…` → `BeiDou.exe`; ijl bak/golden HOVER → `ijl15.dll` on enter-red

## Prior — RESTORED green after LOGIN_HOVER_SAFE enter-red (2026-08-03 ~22:30)

- **Stamp:** `RESTORED_ADDON_ROW3_UNEQUIP_UI_20260802` (from `ijl15.dll.bak_ADDON_LOGIN_HOVER_SAFE_20260803`)
- **SHA256:** `6D612F01AA63D217…` **Size:** `1447424`
- **Aside crash:** `ijl15.dll.aside_CRASHED_LOGIN_HOVER_SAFE_20260803_223018.bak` (`05B67BD9…` / 1518080)
- **WER:** 22:28:44 AppError / 22:28:48 BEX `0xc0000409` @ `ucrtbase.dll+0x9d132`; dump `BeiDou.exe.10344.dmp` embeds `ADDON_LOGIN_HOVER_SAFE_20260803`
- **Boot:** `ShoulderSlots OK` → **`EquipAddonLoginPersist OK`** → `LazyCompat` → quiet ON @22:28:34 → enter flash ~22:28:44 (green path has **no** EquipAddonLoginPersist line)
- **SIZE:** +70656 vs green (~+70KB) — tip-clean claim still full relink; same GS family as tip enter-reds
- **User policy:** stay on green until next patch enter-proven; 悬停/首次空栏 = known green issues

## CRASHED — ADDON_LOGIN_HOVER_SAFE (2026-08-03 ~22:28) — do not redeploy

- **Stamp:** `ADDON_LOGIN_HOVER_SAFE_20260803`
- **SHA256:** `05B67BD9522D8397…` **Size:** `1518080`
- **Markers:** EquipAddonLoginPersist + SidecarBpFromSlotNormal **present**; EquipGrowth/EQUIP_TIP/TOTEM2/SENDBUSY **absent**; **lost** `ADDON_ROW3_UNEQUIP_UI` string vs green
- **Root cause class:** DllMain `InstallLoginPersistEarly` (quiet caves + **Get/Set ATTACH_HOOK**) + full rebuild size≠green — not proven enter-safe. Hover GetItem normal-only is IDA-sound for 点装 tip but was bundled with Early hooks + polluted relink
- **IDA (2026-08-03 ~22:31 re-check v083):** see avoid #35; next = split patches

## Live — RESTORED green after TIP_TOOLTIP_ONLY enter-red (2026-08-03 ~19:31) — history

- **SHA256:** `6D612F01…` **Size:** `1447424`
- **Aside:** `ijl15.dll.aside_CRASHED_TIP_TOOLTIP_ONLY_20260803.bak` (`0FBEE29A…` / 1506304)
- **WER:** 19:30:14 / 19:30:18 `0xc0000409` @ `ucrtbase+0x9d132`; meso Decode @19:30:13 → beauty attach → die; server 保存 萧曳_Xy
- **Lesson:** 去掉 growth/invresize 仍不够；当前源树任意含 tip 的 MSBuild 全量产物相对绿基线仍进图红。tip 须另路（真正二进制级只补丁绿 DLL / 或从绿源快照复现后再只改 tooltip），**在找到前 tip 不上 live**

## CRASHED — EQUIP_TIP_TOOLTIP_ONLY (2026-08-03 ~19:30) — do not redeploy

- See restore section above.

## Live — EQUIP_TIP_TOOLTIP_ONLY pending enter A/B (2026-08-03 ~19:27) — SUPERSEDED / CRASHED

- **Stamp:** `RESTORED_ADDON_ROW3_UNEQUIP_UI_20260802` (from `ijl15.dll.20260803_184832.bak`)
- **SHA256:** `6D612F01AA63D217…` **Size:** `1447424`
- **Aside crash:** `ijl15.dll.aside_CRASHED_DRAWONLY_20260803_190000.bak` (`C3B3946C…` / 1527808)
- **WER:** 18:58:35 AppError / 18:58:39 BEX `0xc0000409` @ `ucrtbase+0x9d132`; dump `BeiDou.exe.41980.dmp`; meso Decode8 @18:58:34 → beauty attach @18:58:35 → die; server「开始保存 萧曳_Xy」@18:58:40
- **Root cause class (旧错再犯):** `_build_equip_tip_drawonly.bat` 虽删 growth/invresize 字符串，但仍 `link Release\*.obj` 全量重链，并重编 tooltip/fusionanvil/equipaddon/ModRegistry/Client/dllmain/… + 残留 `PacketDispatcher.obj`（含 `DiagGrowth`/`InstallHook`）。静态门禁「无 EquipGrowth 串」≠ 进图安全。**禁止**用污染 Release 全链冒充 tip-only。
- **Next tip attempt must:** 冻结绿基线 obj 集，**只**替换 `tooltip.obj`（或等价最小集合），禁止 `Release\*.obj` 盲链；未进图验证不得上 live

## CRASHED — EQUIP_TIP_DRAWONLY (2026-08-03 ~18:58) — do not redeploy

- See Live restore section above. Static gate passed; enter still `0xC0000409` any-path (same GS family).

## Prior live — RESTORED green after ENTEROK enter-red (2026-08-03 ~18:30)

- **Stamp:** `RESTORED_ADDON_ROW3_UNEQUIP_UI_20260802` (from `ijl15.dll.20260803_181313.bak`)
- **SHA256:** `6D612F01AA63D217…` **Size:** `1447424`
- **Note:** no enter retest required for this restore; superseded by DRAWONLY deploy @18:48 (bak `184832`)

## CRASHED — EQUIP_TIP_ENTEROK (2026-08-03 ~18:27) — do not redeploy

- **Stamp:** `EQUIP_TIP_ENTEROK_20260803` (+ `EQUIP_TIP_COLOR_20260803`)
- **SHA256:** `42BAFBBD17DF5312…` **Size:** `1524224`
- **Failure:** select→enter flash `0xC0000409` BEX `ucrtbase.dll+0x9d132`; dump `BeiDou.exe.5108.dmp` @18:27:35 embeds stamp; boot to quiet ON then die; server loads `萧曳_Xy` (also prior LoginTest @17:50 under tip-on-green) — **any-char**
- **Aside:** `_ijl15_backup\ijl15.dll.aside_CRASHED_EQUIP_TIP_ENTEROK_20260803_20260803_183048.bak`
- **Root cause class (旧错):** same enter GS family as TIP_ON_GREEN — live still embedded `EquipGrowth` + `sidebar_debug` (stubs ≠ excised); size +76KB vs green; tip stamp promoted without enter proof
- **Rollback:** live → `6D612F01` from `ijl15.dll.20260803_181313.bak` @~18:30
- **Lesson:** stubbing growth/invresize is **not** enough if Release tree still links growth symbols / sidebar fopen / polluted objs; forbid `EquipGrowth`/`sidebar_debug` strings and do not live-deploy until enter A/B

## CRASHED — EQUIP_TIP_ON_GREEN (2026-08-03 ~17:54) — do not redeploy

- **Stamp:** `EQUIP_TIP_ON_GREEN_20260803` (+ `EQUIP_TIP_COLOR_20260803` / `GROWTH_TIP_OFF`)
- **SHA256:** `C1A3A14BA79BA9BB…` **Size:** `1545216`
- **Failure:** any-char select→enter flash `0xC0000409` (~1s after meso Decode8 → ModRegistry attach incl EquipGrowth)
- **Aside:** `ijl15.dll.aside_TIP_ON_GREEN_enter_crash_20260803_175431`
- **Rollback:** restored green `6D612F01` from `ijl15.dll.20260803_172441.bak`

## CRASHED — EQUIP_TIP_ENTERSAFE (2026-08-03 ~15:44) — do not redeploy

- **Stamp:** `EQUIP_TIP_ENTERSAFE_20260803` (+ tip color `EQUIP_TIP_COLOR_20260803`)
- **SHA256:** `20E6BAB4138EA62B…` **Size:** `1553408` (from `out\Release\ijl15.dll` @15:39)
- **Failure:** **select→enter flash** — `client_boot.log` to quiet ON; WER `0xC0000409` BEX `ucrtbase.dll+0x9d132`; dump `BeiDou.exe.24228.dmp` @15:44:02 embeds stamp; server `out.log` loads `萧曳_Xy` @15:44:02 then client dies
- **Aside:** `_ijl15_backup\ijl15.dll.aside_CRASHED_EQUIP_TIP_ENTERSAFE_20260803_154857.bak`
- **Not WZ/DB:** live `01202193.img` cash=0 (orange-wz OK, size 1391, Totem⇄Accessory hardlink); DB `萧曳_Xy` dual 1202193 at **−55/−56** (not −155/−156); EXE vanilla `4934A0CF…`
- **Rollback:** restored green `6D612F01…` from `_ijl15_backup\ijl15.dll.20260803_153956.bak` @~15:48
- **Lesson:** tip/color-only rebuilds still need enter proof — do **not** promote tip stamps to Client_1 until A/B green

## CRASHED — ADDON_TOTEM2_ENTERSAFE (2026-08-03 ~15:22) — do not redeploy

- **Stamp:** `ADDON_TOTEM2_ENTERSAFE_20260803` (+ `ADDON_SENDBUSY_BOOTSAFE` + `ADDON_UNEQUIP_FALLTHROUGH` + `GROWTH_TIP_SETLIKE`)
- **SHA256:** `52F19C8BE4C97960…` **Size:** `1548800` (bak `_ijl15_backup\ijl15.dll.20260803_151800.bak`)
- **Failure:** same enter `0xC0000409`; dumps `BeiDou.exe.712.dmp` @15:22:45 / `BeiDou.exe.41012.dmp` @15:23:33 embed `ADDON_TOTEM2_ENTERSAFE_20260803`
- **Note:** distinct from earlier `ADDON_NATIVE_TOTEM2` (`552212BD…` / 1549312 @14:05–14:07). Server-first PacketSlot stamp still enter-red when rebuilt with SENDBUSY tree — **source only**

## Process — when to ask for in-game enter test

| Action | Ask user select→enter? |
|--------|------------------------|
| Restore live from STATS bak `4AD0AA7A` / `RESTORED_ADDON_STATS_UNEQUIP` | **No** — enter already proven |
| Restore live from `bak_ADDON_SENDBUSY_FIX` / ROW3_UNEQUIP_UI (`6D612F01…`) | **No** — pre-SENDBUSY working |
| Any other bak restore that was previously enter-green on this char set | **No** |
| Deploy a **new feature / new stamp** DLL (even “flags-only” / “WIREOFF” / BOOTSAFE) | **Yes** — only then |
| Engineer-only rebuild staged to `golden\`, not copied to Client_1 | **No** |

Do **not** nag for enter A/B after every STATS rollback.

## Live — RESTORED pre-SENDBUSY (boot-safe)

- **Stamp:** `RESTORED_ADDON_ROW3_UNEQUIP_UI_20260802` (from `ijl15.dll.bak_ADDON_SENDBUSY_FIX_20260802`)
- **SHA256:** `6D612F01AA63D217…` **Size:** `1447424`
- **Note:** bak embeds `ADDON_ROW3_UNEQUIP_UI` / `ADDON_ROW3_WEAR_UNEQUIP` (not SI_WTYPE_CAT_FIX2 despite prior note)
- **Aside crash lives:** `ijl15.dll.CRASHED_SENDBUSY_FIX_3BFB289D_20260802`, `ijl15.dll.aside_crash_SENDBUSY_*` (`888C5530…` also SENDBUSY)
- **2026-08-03 ~14:23:** re-restored after TOTEM2 enter crash (see below); Client_1 `ENTER_CRASH_ROLLBACK_20260803_142306.md`
- **2026-08-03 ~15:48:** re-restored after `EQUIP_TIP_ENTERSAFE` enter crash (dump `24228`); also covers prior `TOTEM2_ENTERSAFE` enter-red @15:22

## CRASHED — ADDON_NATIVE_TOTEM2 (do not redeploy)

- **Stamp:** `ADDON_NATIVE_TOTEM2_20260803` (live crash build was **post-golden** rebuild)
- **Crash SHA256:** `552212BD50C13D87…` **Size:** `1549312` (from `ezorsia\out\Release\ijl15.dll` @13:58)
- **Golden (also session, not enter-proven):** `38506238D4072699…` **Size:** `1484800` — `golden\ijl15.ADDON_NATIVE_TOTEM2_20260803.dll` / `_ijl15_backup\ijl15.dll.20260803_135846.bak`
- **Failure:** **select→enter flash** — `client_boot.log` completes to `CWvsApp::Init` / quiet ON; WER `0xC0000409` BEX faulting `ucrtbase.dll+0x9d132`; dumps `BeiDou.exe.37788.dmp` @14:05:45 / `BeiDou.exe.37664.dmp` @14:07:40 embed TOTEM2 init + `totem2=-156`
- **Server:** `logs\out.log` loads `萧曳_Xy` then client dies (no server NPE); EXE stayed **vanilla** (`4934A0CF…`, not CD64)
- **Aside:** `ijl15.dll.aside_TOTEM2_enter_crash_20260803_142306`
- **Root cause (2026-08-03 ~14:45):** not “−156 Get/Set alone”. Crash + golden both embed `ADDON_SENDBUSY_BOOTSAFE` + `ADDON_UNEQUIP_FALLTHROUGH` + TOTEM2 WZ `PacketSlotForBp` (cash→−(bp+100)). Live green `6D612F01` has **none** of those stamps. Client GetItem already aliases −156→−56 (one ZRef) so packet −156 required WZ/COM cash lookup — rebuilt from polluted SENDBUSY tree → enter `0xC0000409`. **Do not** redeploy golden TOTEM2 or any new DLL from that tree until enter-proven on a copy.
- **ENTERSAFE fix:** keep Client_1 on `6D612F01` / `RESTORED_ADDON_ROW3_UNEQUIP_UI`; server `InventoryManipulator.unequip` resolves −54…−62 ↔ −154…−162; cash totem `equip` remaps −55…−58 → −155…−158. Source stamp `ADDON_TOTEM2_ENTERSAFE` = server-first / PacketSlot always −bp — **source only, do not auto-deploy**.

## Staged (NOT live) — ADDON_NATIVE_CD (Route A)

- **Stamp:** `ADDON_NATIVE_CD_20260803`
- **SHA256:** `8EF3A2510BB46940…` **Size:** `1485824`
- **Path:** `golden\ijl15.ADDON_NATIVE_CD_20260803.dll`
- **Test EXE (boot-OK):** `tools\cd_expand\out\BeiDou.cd64_MINIMAL_noimm_20260803.exe` (SHA `3C167A9B…`; from Client_1 `4934A0CF`; **no** filtered imm)
- **Test EXE (boot-red):** `BeiDou.cd64_20260803_212848.exe` / `*_FULL_imm_*` (SHA `40EBF7F1…`) — imm×318 → PostGD SEH; also prior `122656` wrong V16 base
- **Test copy:** `E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_CD64_TEST` + `CD64_ENTER_MATRIX.md`
- **Design:** [`ADDON_NATIVE_CD_GROW.md`](./ADDON_NATIVE_CD_GROW.md)
- **Promote:** blocked until user T2–T3 on MINIMAL (T1 green) or fixed-imm + ask; `_build_addon_native_cd.bat` STAGE ONLY
- **Vanilla + NATIVE DLL:** logo OK (DetectCd64 false → sidecar)
- **Risk:** golden still has `EquipGrowth`/`sidebar_debug` strings (unlike live green)

## Staged (NOT live) — ADDON_SENDBUSY_BOOTSAFE

- **Stamp:** `ADDON_SENDBUSY_BOOTSAFE_20260802`
- **SHA256:** `365386A2D293605A…` **Size:** `1451520`
- **Path:** `golden\ijl15.ADDON_SENDBUSY_BOOTSAFE_20260802.dll`
- **Fix:** DllMain early = login caves only; Park/Get-Set/Dbg deferred; OnTick/SendBusy gated on CharData; no `GetUpdateTime` pre-field
- **Promote:** only after user confirms boot + select→enter — `_build_addon_sendbusy_bootsafe.bat` never auto-deploys

## CRASHED — ADDON_SENDBUSY_FIX (do not redeploy)

- **Stamp:** `ADDON_SENDBUSY_FIX_20260802`
- **SHA256:** `3BFB289D2155252E…` **Size:** `1452032`
- **Failure:** **boot flash** — `client_boot.log` dies after `ShoulderSlots OK`, never `EquipAddonLoginPersist OK`; WER `0xC0000409`; dumps `BeiDou.exe.*.dmp` @22:53–22:55 contain `ADDON_SENDBUSY` / `SendBusy`
- **Cause:** `InstallLoginPersistEarly` did Park + Get/Set + `LogInitOnce`/`Dbg` fopen under DllMain; OnTick SendBusy/`GetUpdateTime` ungated

## Prior — ADDON_ROW3_WEAR_UNEQUIP

- **Stamp:** `ADDON_ROW3_WEAR_UNEQUIP_20260802`
- **Internal:** packet-only wear/unequip; ExtraRing Get/Set+UI ON; Occ/−152 OFF; ROW3 pocket+aux
- **FIXES bak:** `ijl15.dll.bak_ADDON_ROW3_FIXES_20260802` (`60E3E02F…`)
- **ROW3 bak:** `ijl15.dll.bak_ADDON_ROW3_POCKET_SI_20260802` (`D2C538A9…`)

## Prior — ADDON_ROW3_FIXES (rollback if WEAR_UNEQUIP enter-red)

- **Stamp:** `ADDON_ROW3_FIXES_20260802`
- **SHA256:** `60E3E02FFF100CE99C25D8404B1826B9CDC80BECEB6C1AF7D44AC1A75297717B`
- **Size:** `1444864`
- **Note:** clear-only + optimistic wear broke bag sync / dblclick unequip

## Prior proven baseline (ROW3 — deeper rollback)

- **Stamp:** `ADDON_ROW3_POCKET_SI_20260802`
- **SHA256:** `D2C538A931259114EA036D3AB843144C2BD1460E3C904DAC5C13138CA57BCDAB`
- **Size:** `1443840`

## Older STATS baseline

- **Stamp:** `RESTORED_ADDON_STATS_UNEQUIP_20260802` / `ADDON_UNEQUIP_SYNC_20260802`
- **SHA256:** `4AD0AA7A93947D797A636F243A104436CA7870495B9B2849E5F7D4A5DEFB4B4E`
- **Size:** `1438208`

## Enter-unsafe stamps (do not redeploy)

| Stamp | SHA (prefix) | Size | Failure |
|-------|--------------|-----:|---------|
| `ADDON_ENTER_STATS_WIREOFF_20260802` | `A6308A19…` | 1445888 | `0xC0000409` @ucrtbase+9D132 |
| `ADDON_P12_NOPAINT_20260802` | `E8368D37…` | 1446912 | same GS |
| `ADDON_P12_POCKET_PAINT_20260802` | `3BDAD59C…` | 1451008 | same GS |
| `ADDON_FULL_FIX_20260802` | `3E88F0F6…` | 1447936 | same GS |
| `ADDON_ENTER_STRICT_WIRE_20260802` | staged | 1446912 | **not** enter-proven; stop until STATS source restored |
| `ADDON_REGISTRY_P12_20260802` | `BF3F8B1F…` | 1446400 | enter unproven / polluted tree |
| `ADDON_SENDBUSY_FIX_20260802` | `3BFB289D…` | 1452032 | **boot** `0xC0000409` after ShoulderSlots; DllMain Park/Get-Set/Dbg |
| `ADDON_SENDBUSY_FIX` rebuild `888C5530…` | `888C5530…` | 1452032 | same boot flash (aside 22:55) |

**Flags-only ≠ STATS-identical.** WIREOFF was rebuilt from REGISTRY/P12-polluted sources.

### Binary deltas vs STATS (WIREOFF example)

- STATS Init: `main red9/10=BP33+BP10 park54/55 every tick`
- WIREOFF Init: flags-off / NOPAINT banner; no every-tick park string
- STATS has `WireMainPocketSubSlots: red9…` log; WIREOFF lacks it
- STATS close: `destroy Addon layer` vs WIREOFF `destroy Addon + pocket paint layers`
- STATS UNEQUIP `bag+clear` vs WIREOFF `clear-only`
- STATS build: no `ExtendedBodyPart.h`; WIREOFF/REGISTRY/P12: yes

## Avoid-list (enter path)

1. OccShadow → `@77F99E` / `@535351`
2. apply-max past 55
3. Equip UIToggle/UIOpen/ctor/OnDraw detours
4. draw-skip 48→32 @ `0x007FEE89`
5. BP33 draw cave
6. Si/pocket wire before live Equip + CharData
7. Early-return ExtraRing bind
8. Trust bak name without SHA
9. Native −52/−53 Get/Set on vanilla EXE
10. Fake DEPLOY_OK — verify SHA
11. ExtraRing shadows ON **with** −152 remap or Occ ON (Get/Set+UI alone is ROW3_FIXES)
12. Pocket overlay paint
13. `GetEquipUi()` raw non-vtable return
14. P12_NOPAINT registry wire / pocket scaffolding / SetItem REMAP
15. Shipping P12 wire/paint before enter green
16. **WIREOFF** — not STATS-identical; enter `0xC0000409`
17. Flag-toggle “enter-safe” claims when size/hooks ≠ STATS
18. Relink `Release\*.obj` after REGISTRY/P12 without STATS objs/sources
19. **STRICT_WIRE** / REMAP / paint until true STATS source baseline restored
20. Any “new” build that is not literally STATS bak or proven near-identical rebuild
21. **Assuming official 095 paints pocket BP33** — `CUIEquip::Draw@0x7AA560` uses the **same** `bp==14 \|\| (21…48)` skip; no 095 pocket paint path to copy
22. Blind-raising 083 Get/Set / apply to **−59** because 095 `CharacterData` is 1965 bytes — stock 083 apply-max ≤55; needs EXE CD grow, not a flag flip
23. **SENDBUSY_FIX boot mode** — `InstallLoginPersistEarly` doing Park + Get/Set + file `Dbg` under DllMain; OnTick SendBusy / `GetUpdateTime@987257` before CharData
24. **Packet unequip swallow** — `NeedsPacketUnequipBp` covering classic rings/pendant/pet (12–17,21–22) + `return 0` when SendChange/wear fail → **all unequip dead**. Packet path only for 20/51/52–55; **always fall through to native** on failure. Live fix: `ADDON_UNEQUIP_FALLTHROUGH_20260803`
25. Redeploying `3BFB289D` / any DLL whose boot log stops at `ShoulderSlots OK`
26. **TOTEM2 / WZ PacketSlot cash→−(bp+100)** rebuilds (`552212BD` / golden `38506238`) — select→enter `0xC0000409`; also embeds SENDBUSY_BOOTSAFE+FALLTHROUGH. Prefer **server** −56↔−156 alias with live `6D612F01`
27. Redeploying unchanged golden TOTEM2 or any post-golden rebuild claiming “only PacketSlot” without enter A/B
28. Calling `CItemInfo` / WZ cash lookup on enter/login/apply paths to decide equipped packet slots
29. **EQUIP_TIP_ENTEROK** — stubs still left `EquipGrowth`/`sidebar_debug` in DLL; size≢green; enter `0xC0000409` @ucrtbase+9d132 (dump 5108). Do not redeploy `42BAFBBD`
30. Claiming tip-only fixed without binary FORBID on `EquipGrowth` / `sidebar_debug` / `InvResize` strings
31. Auto-deploying tip stamps to Client_1 before user select→enter proof
32. Linking `EquipGrowthStub`/`InvResizeStub` “to satisfy symbols” — prefer decoupling callers so growth objs are absent entirely
33. **Green `6D612F01` has no early jg/lea** — boot `ShoulderSlots OK` → `LazyCompat bootstrap OK` (no `EquipAddonLoginPersist OK`). jg-allow/lea install only at `EnsureHooks` (after first getCharInfo) → **first login Addon empty**. Phase2 fix = quiet `InstallSidecarApplyCave` before getCharInfo — **binary trampoline**, not Get/Set Early. Do **not** require boot line `EquipAddonLoginPersist OK` (that string = ENTER-RED #35).
34. **GetItem cash alias −156→same ZRef as −56** — CUIEquip `@7FE9BB` fetches both −bp and −(100+bp); dual tip `sub_8EBA5B` → AV `0xC0000005` read `@0x500` / EIP `BeiDou.exe+0x4EBC3C` on 点装 hover (Client_1 `client_boot` 22:02:39 / 22:04:02). Not `EQUIP_TIP_*` stamp (absent on green). Fix: GetItem sidecar **normal seats only**; SetItem may still accept cash −156..−161. Do not “fix” with tip rewrite on polluted tree.
35. **ADDON_LOGIN_HOVER_SAFE** (`05B67BD9…` / 1518080) — select→enter `0xC0000409` same GS family; dump `BeiDou.exe.10344.dmp` @22:28:48. Bundled (a) DllMain Early Get/Set+caves (boot shows `EquipAddonLoginPersist OK`) (b) GetItem `SidecarBpFromSlotNormal` (c) +70KB tip-clean relink. **Do not redeploy as-is.** Split: green-obj binary patch **only** GetItem normal-only OR **only** jg-allow caves without DllMain SetItem; never both+relink until enter A/B.
36. **Do not bundle Phase1+Phase2 via MSBuild** — Phase1 `ADDON_HOVER_GETITEM_ONLY`; Phase2 `ADDON_LOGIN_JG_ONLY` = binary trampoline on EXECAVE (no Get/Set). **GROWTH_TIP_REEN** (`D6D01354` / 1552896) must not overwrite Client_1 — restore EXECAVE/JG_ONLY if seen.
37. **Post-小退 hover still AV with Phase1** — EnsureHooks does **not** restore cash `cmovbe` (NOP sticks). Crash `0xC0000005` `@0x8EBC3C` read `@0x500` at 22:46. **Historical mitigate** EXE `ADDON_HOVER_RELOG_NODUAL` nop dual-tip jnz `@7FEA9A` — **now FORBIDDEN** (kills 点装 tip; see #44). Real fix = GetItem normal-only (Phase1). If dual tip still AVs: equality-only skip (`v27==v29`), never blanket `90 90`. **GROWTH_TIP_REEN** (`D6D01354`) overwrote HOVER @23:18 — do not leave growth rebuild on Client_1 without re-applying HOVER bytes.
38. **ADDON_UI_LOGOUT_CLOSE** (`1DCCEA52…` / 1509888) — enter `0xC0000409` same GS family; frozen `dllmain.obj` still has `EquipAddonLoginPersist`. **Do not redeploy.** OnTick force-`A041FF` forbidden (IDA: only `777494`/`A0680F` with CharData gate).
39. **Client change without IDA = forbidden** (user HARD RULE 2026-08-03 ~23:37) — every ijl15/EXE patch must document Hex-Rays + xrefs (addrs, calling convention, callers, lifecycle) via `user-ida-pro-mcp` before patch/deploy. No blind OnTick force-calls, no guessed hooks, no full-relink “hope”.
40. **Do not trust `Release_enter_frozen` by name** — verify `dllmain.obj` has **no** `EquipAddonLoginPersist` / active Early before linking; else enter-red repeats.
41. **Always recompile `dllmain.cpp` into link dir** when frozen embeds LoginPersist — do not robocopy polluted `dllmain.obj`. Gate: `_check_dllmain_no_loginpersist.ps1` + FORBID `EquipAddonLoginPersist` in final DLL.
42. **ADDON_UI_LOGOUT_IDA** (`E88CEBBE…` / 1521664) — enter `0xC0000409` same GS family despite **no** LoginPersist / clean dllmain / no OnTick force-A041FF. Dump `BeiDou.exe.16132.dmp` @23:58:53. Boot ShoulderSlots→LazyCompat only. **Do not redeploy.** Size +74KB full relink lost HOVER patches; A041FF Detours at EnsureHooks/enter = unproven. Next: **zero size growth** — EXE 5-byte jmp `@777494`→cave→A041FF, or late binary trampoline on HOVER; no live until test-copy enter green.
43. **`IJL15_DO_NOT_OVERWRITE.txt` must lock EXECAVE** — wrong lock to `GROWTH_TIP_REEN` `D6D01354` (2026-08-04 ~07:47) caused agents to re-deploy growth over stub `@0xAB30C` → login Equip leak. Guard: `tools/guard_client1_execave.ps1`. Broken Phase2 `07435AFC` also clobbered same slack — tramp must be `@0xAB340` only (`A0BE14C9` golden).
44. **Never nop vanilla dual-tip `@7FEA9A`** (`ADDON_HOVER_RELOG_NODUAL` `75 4C`→`90 90`) — modifies original CUIEquip tip path (`sub_7FE9BB` → skips `sub_8EBA5B`); **点装悬停无 tip**. Product HARD: do not alter classic-seat tip/draw; Addon reuses native tip. Crash fix stays GetItem sidecar normal-only (−56…−61). Restored 2026-08-04: EXE `ACF10F63…` (`75 4C`); bak NODUAL `7E240917…`.
45. **ADDON_AUX_BP62** (`0F244A5D…` / 1447424) — select→enter `0xC0000005` write `@0x7AE09404` (~10:47:30). Mechanical imm 61→62 on JG_ONLY caves + cleared CharInfo omit −62 on **non-CD64** EXE. **Not a fresh IDA-SOP deploy** (sites inherited from prior `sub_4E592D` jg-allow work; no Hex-Rays brief / enter A/B before live). IDA: `@4E5D0C`/`@4E5DCC` vanilla `cmp esi,33h`(51); `@4E5D55` lea→`ZRef_Assign`; stack ZRef `sub_428A50`. Aux must be **peer of BP56–61 sidecar**, never shield −10. Keep omit −62 until enter-green. Rolled back → `A0BE14C9`. **Do not redeploy `0F244A5D`.**
46. **ADDON_AUX_BP62_PEER** (`73559AC5…` / 1447424) — select→enter `0xC0000005` @ `ijl15+0x344E8` (~12:08). Abs caves `0x10144270` / `0x100AB350` **no `.reloc`** → unrebased ForBp `EAX=0x10144430` under ASLR. Also shared `EmitCaveJmpBack` off-by-one (see #49). Omit ON. Rolled back → `A0BE14C9`. **Do not redeploy `73559AC5`.**
47. **Hardcoded `0x10xxxxxx` in ijl15 caves without `.reloc` = forbidden** — ASLR rebases the module; unrebased immediates → ZRef/ClearSidecar AV on enter. Required: PIC (`call/pop`→`dll_base+RVA`, same pattern as EXECAVE stub `@AB30C`) **or** proper HIGHLOW reloc for every absolute imm. Never put writable ZRef arrays in `.text` slack. Also: **never INT3-pad over a live HIGHLOW site** without neutralizing the reloc entry.
48. **POTENTIAL_GRADE_UI** (`3B864717…` / **1508352**) — boot black ~13:27 before login. Overwrote Client_1 after PEER_ASLR stage; `set_stage` SEH `0x80000003` → Init=256 hang; no dump. Growth MSBuild (+60KB) on live — same size class as ENTER-RED family. **Do not redeploy `3B864717` to Client_1.** Build log claimed `LIVE_DEPLOY=SKIPPED` but file still landed live — guard must block size≠1447424.
49. **ADDON_AUX_BP62_PEER_ASLR** (`DF3AF2F1…` / 1447424) — select→enter `0xC0000005` @ `ijl15+0x344E8` (~13:44). PIC **worked** (`EAX=0x564B4430`=ASLR `base+0x144430`). True bug: `EmitCaveJmpBack` used `Count` **after** `Add(E9)` → jmp to `0x344E8` (pad) not `0x344E9` (`test ecx`); pad sat on ClearSidecar abs with live HIGHLOW `@0x344E5`. **Do not redeploy `DF3AF2F1`.** Fix = V2 `FA26021C…` (jmp-back + reloc neutralize).

50. **PEER_COMPLETE** (`EADEF4A8…` / growth) — remapped aux→62 + cleared omit but left Apply 56–62 + wear_fn → badge/totem native `aEquipped[54/55]` ≡ cash face/eye; aux multi-ghost. **Do not redeploy.** Fix = ZEROGROWTH `021C016A` (or growth `32C55435` after enter A/B).
51. **Incomplete sidecar 56–61 only** (FA `PEER_ASLR_V2`) — BP54/55 still native → same cash face/eye ghosts as #50. Always extend IsSidecar/Apply/Get·Set/LoginAllow to **54–62** together; never leave 54/55 on 52-slot `aEquipped`.
52. **ZEROGROWTH without PreferSend** (`021C016A`) — Get/Set/Apply 54–62 OK, but WearBag PreferSend still 56–61 + ExpectedBp aux=10 → badge/totem **sidecar ghosts** (bag still holds item; unequip `source-null` −54/−55); aux `bag_dbl_main bp=10`. **Do not redeploy `021C016A`.** Incomplete half-fix led to PreferSend mega-patch cascade.
53. **PREFER_AUX62** (`44251097…` / 1447424) — PreferSend 54–62 + ExpectedBp aux=62 on ZEROGROWTH. User live: **all totem blank**, endless wear, badge blank, aux shows but unequip/swap ghost (old icon / bag holds old). PreferSend range + incomplete paint/GetItem; ExpectedBp 62 without matching unequip PacketSlot. **Do not redeploy `44251097`.** Rolled back → `FA26021C` (~15:30). See [`ADDON_STOP_CASCADE_20260804.md`](./ADDON_STOP_CASCADE_20260804.md). Next = **one seat one change** only.
54. **TOTEM1_BP55_ZG** (`76C7D7FC…` / 1447424) — PreferSend/sidecar **55–61** on FA (intended one-seat totem1). User live (~15:51): 图腾1–4 **blank** + **multi-wear all → bp=55**, 辅武 blank/`bp=10`. Log: dblclick 120xxxx all `-> bp=55` WEAR send-only; **no** `Sidecar SetItem -55`; freeze seats totem2–4/aux collateral. **Do not redeploy `76C7D7FC`.** Rolled back → `FA26021C` (~15:55). Aside `ijl15.dll.aside_REGRESSION_TOTEM1_BP55_ZG_76C7D7FC_*`.
55. **VS Clean+Rebuild / growth DLL** (`B464E1BC` / 1541120; also `007D59FD`/`2B65633B` WEAR_FIX) — destroys EXECAVE stub `@AB30C` (`53 E8`). Logo → `set_stage` `0xC0000005` @`ijl15+0xAB30C` → Init 256 → **黑屏**. Client_1 must stay **1447424** + stub OK (`3642FBDD`/`FA26021C`). Growth WEAR_FIX = **CD64_TEST only** (EXE `3C167A9B` has no EXECAVE jmp). **Never** Clean+Rebuild onto Client_1; never call growth “Client_1-safe” without stub verify.

## Diagnose note — 2026-08-03 ~22:05 (萧曳_Xy / Client_1)

- **Live SHA:** `6D612F01…` / 1447424 — tip stamps `EQUIP_TIP*` **absent**; stamp string `ADDON_ROW3_UNEQUIP_UI_20260802`
- **CD64_TEST:** different ijl15 `8EF3A251…` (NATIVE_CD); WER/crash tonight path = **Client_1** (`AppPath` + boot VEH)
- **DB:** equipped `inventorytype=-1` has −52/−53/−56…−62 filled; 1202193 in bag 52/53; −54/−55 empty (unequipped ~21:52)
- **Server packets:** no first/second difference observed in out.log (save-on-crash only); empty UI = client decode timing, not server omit
- **Fixed on live?** **No** — source patched only; deploy only after green-baseline enter-safe golden

## Rebuild plan

**A (now):** live = restored `6D612F01` (ROW3_UNEQUIP_UI-era bak). No enter retest for this restore. Cash totem2 unequip/wear = **server alias** (already in `InventoryManipulator`; restart if Java changed).

**B (only if Test A unequip still fails):** narrow client send-path on a **ROW3-green baseline** (not SENDBUSY tree); never WZ on enter; stage to `golden\` first.

**C (staged):** `ADDON_SENDBUSY_BOOTSAFE` in `golden\` — ask boot + enter before promote.

**D (longer):** STATS bak `4AD0AA7A` still deepest proven baseline if ROW3 restore misbehaves.

**E (Phase1 live 2026-08-03 ~22:38):** `ADDON_HOVER_GETITEM_ONLY` binary patch on green — user enter OK.
**F (logout IDA live 2026-08-03 ~23:55 → ENTER-RED ~23:58):** `ADDON_UI_LOGOUT_IDA` `E88CEBBE` — rolled back.
**G (EXECAVE 2026-08-04):** zero-growth EXE cave `@AEF602` + ijl stub `@0xAB30C` — logout UI OK (`8DD59B06` / `7E240917`); bak before Phase2.
**H (Phase2 live 2026-08-04 ~09:55):** `A0BE14C9…` tramp `@0xAB340` on Client_1; stub preserved. **User retest** logout UI + first-login empty Addon.
**I (RESTORE_DUALTIP 2026-08-04 ~10:05 → CLOSED):** EXE `@7FEA9A` `90 90`→`75 4C`; SHA `7E240917`→`ACF10F63`; ijl unchanged `A0BE14C9`; EXECAVE intact. **User:** 点装 tip OK. Next = CD64_TEST T1 + imm SAFEIMM bisect ([`CD64_IMM_FILTER_DIAG_20260804.md`](./CD64_IMM_FILTER_DIAG_20260804.md)).

**J (ADDON_AUX_BP62 2026-08-04 ~10:40 → ENTER-RED ~10:47):** Zero-growth jg→62 + omit cleared → `0F244A5D` select→enter `0xC0000005`. **Rolled back** to `A0BE14C9`; aside ENTER-RED bak; server omit −62 **restored**. Avoid #45. Next = enter proof on JG_ONLY; aux display via bag-side / CD64_TEST only — **not** Client_1 BP62 without CD.

**K (PEER 2026-08-04 ~11:49 → ENTER-RED ~12:08):** `73559AC5` abs caves no reloc → ASLR ZRef AV. Avoid #46/#47.
**L (PEER_ASLR ~12:35 stage → ~13:41 live → ENTER-RED ~13:44):** `DF3AF2F1` PIC OK but `EmitCaveJmpBack` −1 → `+0x344E8`. Avoid #49. Rolled back `A0BE14C9`.
**M (PEER_ASLR_V2 ~13:50 staged):** `FA26021C…` jmp-back fix + reloc neutralize; golden only; omit ON; live stays `A0BE14C9` until enter A/B green.

55. **VS Clean+Rebuild / WEAR_FIX full-relink** (B464E1BC… / **1541120**, also staged  07D59FD… / **1558528**) — Client_1 boot black (not AppHang) ~17:40. Logo/CLogo_ctor OK → set_stage AV  xC0000005 **write** @ijl15+0xAB30C (EIP=EAX=stub); SEH → Init=256 → quiet ON. Root: growth/tip MSBuild relocates .text, **clobbers EXECAVE stub** (need 53 E8…); EXE still ACF10F63 EXECAVE. **Do not redeploy B464E1BC /  07D59FD to Client_1.** Restore 3642FBDD / FA26021C / A0BE14C9. Forbid VS Clean+Rebuild onto live EXECAVE clients; wear fixes must be **zero-growth binary patch** on allowlisted base, never link Release\*.obj after growth rebuild.
