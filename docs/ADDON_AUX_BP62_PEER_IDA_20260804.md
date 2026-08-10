# ADDON_AUX_BP62_PEER_ASLR / V2 — IDA Hex-Rays brief (2026-08-04)

> **ENTER-RED:** `73559AC5` (abs) · `DF3AF2F1` (PIC but jmp-back −1) — **do not redeploy.**  
> **Live:** `FA26021C…` PEER_ASLR_V2 / 1447424 — omit ON until enter A/B green. Bak=`A0BE14C9`.  
> EXE `ACF10F63` untouched.

| Build | SHA256 (prefix) | Size | Status |
|-------|-----------------|------|--------|
| JG_ONLY baseline | `A0BE14C9…` | 1447424 | rollback bak only |
| PEER (abs caves) | `73559AC5…` | 1447424 | **ENTER-RED** — forbid |
| PEER imm-only | `0F244A5D…` | 1447424 | **ENTER-RED** — forbid |
| PEER_ASLR | `DF3AF2F1…` | 1447424 | **ENTER-RED** — forbid (#49) |
| **PEER_ASLR_V2** | **`FA26021C…`** | **1447424** | **live** — await enter A/B |

**Golden V2:** `E:\pro\BeiDou-ijl15\golden\ijl15.ADDON_AUX_BP62_PEER_ASLR_V2_20260804.dll`  
**Script:** `tools/patch_aux_bp62_peer_aslr_v2.ps1`  
**Full SHA256 V2:** `FA26021CA7C4209FBC1CEC41E60D5AFD62CE345B4B1BD2812C880F57976D66C4`

## ENTER-RED evidence — DF3AF2F1 (select→enter ~13:44, omit ON)

| Field | Value |
|-------|--------|
| VEH | `0xC0000005` @ `ijl15+0x344E8` abs `0x563A44E8` |
| Module | ASLR base `0x56370000` |
| Regs | `EAX=ECX=0x564B4430` = **correct** `base+0x144430` (PIC OK) |
| Read target | `0xC7645C29` (corrupted insn stream at pad) |
| Stack | `BeiDou+0xE5937` ∈ `sub_4E592D` |

**Root cause:** `EmitCaveJmpBack` did `Add(0xE9)` then used `$code.Count` in rel32 → displacement short by 1 → Clear cave returned to RVA `0x344E8` (INT3/NOP over ClearSidecar abs) instead of `0x344E9` (`test ecx,ecx`). Live HIGHLOW `@0x344E5` also patched the pad under ASLR. **Not** AbleToWear/jg-62 alone; **not** PIC delta wrong.

## V2 fix map

| Item | Change |
|------|--------|
| `EmitCaveJmpBack` | `from = caveVa+Count` **before** `Add(E9)` |
| ClearSidecar pad | NOP; neutralize HIGHLOW `@RVA 0x344E5` |
| Verify | jmp-back → `0x344E9` (`85 C9`) |
| PIC / bp62 `.data` `@0x14B930` / AbleToWear·dock 134→62 | same as V1 |

## Server gate

- `GREEN_ENTER_OMIT_AUX62=true` until V2 enter A/B green
- After green: omit=false + restart; retest dblclick/swap/sort/shield −10

## Retest 中文

1. **Live 已回滚 `A0BE14C9`** — 先确认能进图
2. **勿清 omit**
3. 若试 V2：从 golden 拷到测试目录（或明确同意后再上 Client_1），bak=`A0BE14C9`
4. 选角进图 A/B；红 → 立刻回滚，报 VEH
5. 绿 → 再清 omit 穿辅武
