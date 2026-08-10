# CharacterData 扩容 Phase0+1 进度 (2026-07-28)

## 已完成

### Phase0 干跑
- 工具: `tools/cd_expand/phase0_dry_run.py`
- 报告: `tools/cd_expand/out/phase0_report.md`、`docs/CharacterData扩容-Phase0-20260728.md`
- IDA `:13337` 已核对 GetItem/Ctor（52 槽、bound −51）

### Phase1 指令命中
- 全量扫描: `out/phase1_imm_sites.json` — **18383** 处（`[0x287,0x800)` 内所有 displ/imm，含大量非 CD 误报）
- 可动手补丁集: `out/phase1_imm_sites_filtered.json` — **仅 ctor 锚点字段**
- bound `cmp *-51`: **5** 处  
  `0x42834E` GetItem · `0x47B0BF` SetItem · `0x4E5AFA` walk · `0x72367F` · `0x77F879` combat
- ctor `push 52`: **4** 处 @ `0x46F534` / `0x46F54C` / `0x46F8E3` / `0x46F8FC`

## 方案锁定

| 项 | 值 |
|----|-----|
| 槽数 | 52 → **64** |
| 每表 | +`0x60` |
| 尾部位移 | +`0xC0` |
| bound | −51 → **−63** (`CD`→`C1`) |

## Phase2 进度（副本可测；**禁止覆盖 Client_1 正式端**）

> 2026-07-28：曾覆盖 Client_1 → 进图 `E_POINTER`。已回退 vanilla EXE。  
> ijl15 `kNativeCd64ExtendedSlots=false`：即使误放 cd64 也走影子槽。  
> 正式端：**vanilla BeiDou.exe + STABLE_SHADOW_RINGS_***。

最新副本：`tools/cd_expand/out/BeiDou.cd64_20260728_003149.exe`

| 补丁 | 结果 |
|------|------|
| ctor `push 52→64` ×4 | OK |
| bound `−51→−63` ×5 | OK |
| filtered 偏移 ×318 | OK |
| 堆 slab `push 0x640→0x700` @`0x778F02` | OK |
| 栈 `sub esp,0x78C→0x84C` @`0x46EC8F` | OK |

### 请你本地烟雾（不要直接覆盖 Client_1）

1. 备份当前 `BeiDou-Client\BeiDou.exe`
2. 用副本替换到**测试目录**（或临时改名启动）
3. 进游戏：经典装 / 现金装 / 进图 / 重登  
4. **先不测** −52…−55；全绿后再谈拆影子

### 仍可能缺（2026-08-03 已补工具侧）

- 其它函数若也在栈上嵌整颗 `CharacterData`，还需同样加栈
- `push 0x640` 若还有第二处池，需一并改
- ~~ijl15 影子与 apply-max 仍按旧逻辑~~ → 见 [`ADDON_NATIVE_CD_GROW.md`](./ADDON_NATIVE_CD_GROW.md)：phase2 补 login clear / `0x28B` / apply-max 62；插件 `kNativeCd64ExtendedSlots=true` 在 DetectCd64 时拆影子

### 2026-08-03 Route A

| 项 | 状态 |
|----|------|
| 设计冻结 | `docs/ADDON_NATIVE_CD_GROW.md` |
| phase2 关键洞 | login clear×2、cash lea `28B`、apply-max 62 |
| 插件 | `ADDON_NATIVE_CD_20260803`（vanilla 仍影子；CD64 EXE 原生） |
| Client_1 部署 | **禁止**直至进图 A/B 绿 |

## 命令

```bat
cd /d E:\pro\BeiDou-ijl15\tools\cd_expand
python phase0_dry_run.py --ida-port 13337
python phase1_build_imm_site_list.py --ida-port 13337
```
